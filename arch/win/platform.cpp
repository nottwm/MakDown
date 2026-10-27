// arch/win/platform.cpp - Windows implementation of the platform layer.
//
// Custom window frame
// -------------------
// Qt's Qt::FramelessWindowHint creates a WS_POPUP window: no resize border, no Aero Snap, no shadow,
// no animations. We keep the OS doing all of that by turning the standard frame styles back on
// (WS_THICKFRAME | WS_CAPTION ...) and then removing the *visible* frame ourselves:
//   * WM_NCCALCSIZE  -> the client area is the whole window (so Qt paints the border and title bar)
//   * WM_NCHITTEST   -> resize edges / corners and the title-bar area are reported to the OS as such
//   * DWM            -> 1px frame extension gives the drop shadow; rounded corners on Windows 11
// The technique is the well-known "borderless window with Aero shadow" recipe.
#include <makdown/arch/platform.h>
#include <QDir>
#include <QHash>
#include <QWidget>
#include <cstdio>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace platform {
QString appHome() { return QDir::homePath() + "/AppData/Local/MakDown"; }   // $HOME\AppData\Local\MakDown

// The exe is linked with -mwindows (no console). For --debug, reuse the parent console or open a new one.
void attachDebugConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
}

namespace {
// DWM attribute ids (older MinGW headers lack them; our own names avoid clashing with newer headers).
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmBorderColor = 34;
constexpr DWORD kDwmColorNone = 0xFFFFFFFE;      // "draw no DWM border" - the app paints its own
constexpr int   kCornerRound = 2, kCornerDoNotRound = 1;
constexpr int   kResizeBorder = 6;               // logical px at 96 dpi: grab area inside each edge
constexpr DWORD kFrameStyle = WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

struct FrameState { std::function<bool(const QPoint&)> isCaption; };
QHash<QWidget*, FrameState>& states() { static QHash<QWidget*, FrameState> s; return s; }

HWND hwndOf(QWidget* w) { return reinterpret_cast<HWND>(w->winId()); }   // winId() creates the native window if needed

// Functions that only exist on newer Windows 10 builds are resolved at run time.
template <class Fn> Fn user32Fn(const char* name) {
    static HMODULE m = GetModuleHandleW(L"user32.dll");
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, name)));
}
UINT dpiOf(HWND h) {
    using Fn = UINT (WINAPI*)(HWND);
    static Fn f = user32Fn<Fn>("GetDpiForWindow");
    return f ? f(h) : 96;
}
int metric(int index, UINT dpi) {
    using Fn = int (WINAPI*)(int, UINT);
    static Fn f = user32Fn<Fn>("GetSystemMetricsForDpi");
    return f ? f(index, dpi) : GetSystemMetrics(index);
}
DWORD windowsBuild() {
    using Fn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    static DWORD build = [] {
        Fn f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
        RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof(vi);
        return (f && f(&vi) == 0) ? vi.dwBuildNumber : 0;
    }();
    return build;
}
bool isWindows11() { return windowsBuild() >= 22000; }

// Applies / repairs everything that has to be done on the native window itself.
void applyNativeFrame(QWidget* w) {
    HWND h = hwndOf(w);
    const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    if ((style & kFrameStyle) != kFrameStyle) {          // only touch the frame when something is missing
        SetWindowLongPtrW(h, GWL_STYLE, style | kFrameStyle);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    const MARGINS shadow = {1, 1, 1, 1};                 // a 1px glass margin is what makes DWM draw the shadow
    DwmExtendFrameIntoClientArea(h, &shadow);
    if (isWindows11()) {
        const int corner = kCornerRound;
        DwmSetWindowAttribute(h, kDwmWindowCornerPreference, &corner, sizeof(corner));
    }
    const DWORD none = kDwmColorNone;                    // the themed border is painted by MakDown (ui/titlebar.cpp)
    DwmSetWindowAttribute(h, kDwmBorderColor, &none, sizeof(none));    // ignored before Windows 11
}

// WM_NCCALCSIZE with a maximised window: Windows positions it (frame) px beyond the monitor, so pull
// the client area back in - and leave one pixel at an auto-hide taskbar's edge so it can still pop up.
void shrinkForMaximized(HWND h, RECT& r) {
    const UINT dpi = dpiOf(h);
    const int fx = metric(SM_CXSIZEFRAME, dpi) + metric(SM_CXPADDEDBORDER, dpi);
    const int fy = metric(SM_CYSIZEFRAME, dpi) + metric(SM_CXPADDEDBORDER, dpi);
    r.left += fx; r.right -= fx; r.top += fy; r.bottom -= fy;
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return;
    APPBARDATA abd{}; abd.cbSize = sizeof(abd); abd.rc = mi.rcMonitor;
    for (UINT edge : {ABE_LEFT, ABE_TOP, ABE_RIGHT, ABE_BOTTOM}) {
        abd.uEdge = edge;
        if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &abd)) continue;
        if (edge == ABE_LEFT) r.left += 1; else if (edge == ABE_TOP) r.top += 1;
        else if (edge == ABE_RIGHT) r.right -= 1; else r.bottom -= 1;
    }
}

LRESULT hitTest(QWidget* w, HWND h, const FrameState& st, LPARAM lp) {
    const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};      // screen coordinates, physical px
    RECT wr; GetWindowRect(h, &wr);
    const bool maximized = IsZoomed(h) || w->isFullScreen();
    const bool fixedSize = w->minimumSize() == w->maximumSize();
    if (!maximized && !fixedSize) {                             // resize edges and corners
        const int bw = qMax(2, kResizeBorder * int(dpiOf(h)) / 96);
        const bool l = pt.x < wr.left + bw, r = pt.x >= wr.right - bw;
        const bool t = pt.y < wr.top + bw,  b = pt.y >= wr.bottom - bw;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
    }
    POINT origin = {0, 0}; ClientToScreen(h, &origin);          // -> the widget's logical coordinates
    const qreal dpr = w->devicePixelRatioF();
    const QPoint local(qRound((pt.x - origin.x) / dpr), qRound((pt.y - origin.y) / dpr));
    if (st.isCaption && st.isCaption(local)) return HTCAPTION;
    return HTCLIENT;
}
} // namespace

bool customFrameSupported() { return true; }

void installCustomFrame(QWidget* w, std::function<bool(const QPoint&)> isCaptionAt) {
    states()[w] = FrameState{std::move(isCaptionAt)};
    w->setWindowFlag(Qt::FramelessWindowHint, true);     // hides a visible window: the caller shows it again
    applyNativeFrame(w);
}

void removeCustomFrame(QWidget* w) {
    states().remove(w);
    w->setWindowFlag(Qt::FramelessWindowHint, false);
}

void syncFrame(QWidget* w) { if (states().contains(w)) applyNativeFrame(w); }

void setFrameAppearance(QWidget* w, bool dark) {
    if (!states().contains(w)) return;
    const BOOL on = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwndOf(w), kDwmUseImmersiveDarkMode, &on, sizeof(on));
}

int frameCornerRadius() { return isWindows11() ? 8 : 0; }

bool frameNativeEvent(QWidget* w, const QByteArray& eventType, void* message, NativeResult* result) {
    const auto it = states().constFind(w);
    if (it == states().constEnd() || eventType != "windows_generic_MSG") return false;
    MSG* msg = static_cast<MSG*>(message);
    HWND h = msg->hwnd;
    switch (msg->message) {
    case WM_NCCALCSIZE:
        if (msg->wParam) {                               // wParam == FALSE: the client area is simply the window
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            if (IsZoomed(h)) shrinkForMaximized(h, p->rgrc[0]);
        }
        *result = 0;                                     // no non-client area at all: Qt paints the whole window
        return true;
    case WM_NCHITTEST:
        *result = hitTest(w, h, it.value(), msg->lParam);
        return true;
    default:
        return false;
    }
}
}
