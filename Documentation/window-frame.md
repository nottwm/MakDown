# Custom window frame (Windows)

On Windows MakDown replaces the standard title bar and window border with its own, themed ones:

* a **title bar** row that holds the app icon, the menu bar, a draggable area and minimise / maximise / close buttons
  (the close button turns red on hover, like the native one);
* a **window border** painted in the theme's colours - accent colour while the window is focused, a quieter colour
  while another window has focus;
* **rounded corners** on Windows 11 (the border follows the OS corner radius); square corners on Windows 10.

Everything that is hard to get right stays native: edge / corner resizing, Aero Snap (drag to an edge, `Win+Arrow`),
double-click the title bar to maximise, the drop shadow, `Alt+Space` / right-click system menu, multi-monitor DPI and
auto-hide taskbar handling. Linux and macOS are unaffected (the window manager keeps drawing the frame).

## Turning it on / off

* **Settings > Appearance > Custom window frame** (default: on). The switch is applied when you close Settings.
* `MakDown --native-frame` forces the native Windows frame for one run, whatever the setting says. Use it if a
  graphics driver or remote-desktop setup misbehaves with frameless windows.
* In `settings.jsonc`: `"customFrame": false`.

## Theming

Border settings are part of every preset (`.jsonc`):

```jsonc
"colors": {
  "frameBorder":         "#0067c0",   // focused window       (default: the accent colour)
  "frameBorderInactive": "#bdbdbd"    // unfocused window     (default: border blended 25% towards the text colour)
},
"shape":  { "windowBorderWidth": 1 }  // 0-6 px, 0 = no border (default 1)
```

Both colours appear in Settings > Appearance > Customize > Colours; the width is the **Window border** row.
Presets that predate this feature keep working: missing keys are derived automatically. The border is hidden while the
window is maximised or full screen.

## How it works

| Piece | Where |
|---|---|
| Title bar, caption buttons, border overlay | `ui/titlebar.cpp`, `include/makdown/ui/titlebar.h` |
| Wiring, runtime switch, hit-test callback | `MainWindow::applyFrame / setCustomFrame / updateFrameState / isCaptionAt / nativeEvent` |
| Win32 / DWM glue | `arch/win/platform.cpp` (declared in `include/makdown/arch/platform.h`) |

The window is made frameless with `Qt::FramelessWindowHint`, then the standard frame styles (`WS_THICKFRAME`,
`WS_CAPTION`, ...) are switched back on so Windows still offers resize, snap and animations. `WM_NCCALCSIZE` makes the
client area cover the whole window (so Qt paints the title bar and border) and compensates for the off-screen
frame of a maximised window. `WM_NCHITTEST` reports resize edges and tells Windows which part of the title bar is
"caption": the icon and the empty stretch are; menus and buttons are ordinary clicks. `DwmExtendFrameIntoClientArea`
brings the shadow back and, on Windows 11, `DWMWA_WINDOW_CORNER_PREFERENCE` rounds the corners. The DWM border is
disabled in favour of MakDown's own so the colour and width follow the theme on Windows 10 and 11 alike.

## Known limitations

* The Windows 11 *snap layouts* flyout (hovering the maximise button) is not shown; snapping by dragging or `Win+Arrow` works.
* The border is an overlay drawn by Qt, so it is 1-6 logical px *inside* the window rather than around it.
* Switching frames re-creates the native window, so the window flickers once.
