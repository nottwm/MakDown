// theme.cpp - presets on disk, stock palettes, QSS generation, app settings.
#include <makdown/core/theme.h>
#include <makdown/arch/platform.h>
#include <makdown/core/lang.h>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
namespace {
// Blend two "#rrggbb" colours: t=0 -> a, t=1 -> b.
QString mixColors(const QString& a, const QString& b, double t) {
    QColor x(a), y(b);
    return QColor::fromRgbF(x.redF() + (y.redF() - x.redF()) * t,
                            x.greenF() + (y.greenF() - x.greenF()) * t,
                            x.blueF() + (y.blueF() - x.blueF()) * t).name();
}

QString readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}
bool writeFile(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(text.toUtf8());
    return true;
}

// Font stacks reused by the stock presets.
#define SANS  "Segoe UI, Inter, Noto Sans, Helvetica Neue, Arial, sans-serif"
#define SERIF "Tiempos Text, Georgia, Noto Serif, Times New Roman, serif"
// Anthropic's brand serif. It is NOT bundled with MakDown: install "Anthropic Serif" on your system to get it;
// otherwise Qt falls through the list to the next installed family.
#define ANTHROPIC_SERIF "Anthropic Serif, Tiempos Text, Georgia, Noto Serif, Times New Roman, serif"
// Bump when stock palettes change: untouched stock presets on disk with an older revision are rewritten at launch.
static const int kStockRevision = 3;
#define VSSANS  "Segoe UI, system-ui, Ubuntu, Noto Sans, Helvetica Neue, Arial, sans-serif"
#define VSMONO  "Consolas, Menlo, Droid Sans Mono, DejaVu Sans Mono, monospace"
#define GLASSSANS "SF Pro Display, SF Pro Text, -apple-system, Segoe UI Variable, Segoe UI, Inter, Noto Sans, Helvetica Neue, Arial, sans-serif"
#define MONO  "JetBrains Mono, Cascadia Code, Consolas, Menlo, DejaVu Sans Mono, monospace"

// One row of the stock-palette table.
struct Pal {
    const char *name, *chrome; bool dark;
    const char *bg, *panel, *fg, *muted, *border, *accent, *accentFg,
               *editorBg, *codeBg, *codeFg, *link, *quote, *heading, *selection;
    const char *ui, *editor, *preview;
    int radius; bool codeBorder, rules; int uiSize, pageWidth;
};

const Pal kPalettes[] = {
 // name        chrome      dark   bg         panel      fg         muted      border     accent     accentFg   editorBg   codeBg     codeFg     link       quote      heading    selection   ui     editor  preview  r  border rules ui  width
 {"GitHub",     "github",   false, "#ffffff","#f6f8fa","#1f2328","#59636e","#d1d9e0","#0969da","#ffffff","#ffffff","#f6f8fa","#1f2328","#0969da","#d1d9e0","#1f2328","#b6e3ff", SANS, MONO, SANS,  6, true,  true, 10, 880},
 {"GitHub Dark","github",  true,  "#0d1117","#161b22","#e6edf3","#8b949e","#30363d","#1f6feb","#ffffff","#0d1117","#161b22","#e6edf3","#4493f8","#3d444d","#e6edf3","#1f3a5f", SANS, MONO, SANS,  6, true,  true, 10, 880},
 {"Windows 11", "win11",    false, "#f3f3f3","#fbfbfb","#1a1a1a","#616161","#e5e5e5","#0067c0","#ffffff","#ffffff","#f3f3f3","#1a1a1a","#0067c0","#0067c0","#1a1a1a","#cce4f7", "Segoe UI Variable, " SANS, MONO, "Segoe UI Variable, " SANS, 8, false, false, 10, 860},
 {"Windows 11 Dark","win11",true,  "#202020","#2b2b2b","#ffffff","#a0a0a0","#3a3a3a","#60cdff","#000000","#1c1c1c","#2b2b2b","#e6e6e6","#60cdff","#60cdff","#ffffff","#0b4a6a", "Segoe UI Variable, " SANS, MONO, "Segoe UI Variable, " SANS, 8, false, false, 10, 860},
 {"TTY",        "terminal", true,  "#000000","#000000","#aaaaaa","#666666","#aaaaaa","#aaaaaa","#000000","#000000","#000000","#55ff55","#55ffff","#aa5500","#ffff55","#444444", MONO, MONO, MONO,  0, true,  false, 10, 1000},
 {"opencode",   "terminal", true,  "#0a0a0a","#141414","#eeeeee","#808080","#484848","#fab283","#0a0a0a","#0a0a0a","#141414","#7fd88f","#5c9cf5","#e5c07b","#9d7cd8","#2a2a3a", MONO, MONO, MONO,  0, true,  false, 10, 1000},
 {"Warm Light",  "warm",   false, "#faf9f5","#f0eee6","#141413","#73726c","#e3dacc","#d97757","#ffffff","#faf9f5","#f0eee6","#3d3d3a","#c6613f","#d97757","#141413","#f1d4c8", ANTHROPIC_SERIF, MONO, ANTHROPIC_SERIF, 12, false, false, 10, 760},
 {"Warm Dark","warm",   true,  "#262624","#1f1e1d","#faf9f5","#a8a69e","#3d3d3a","#d97757","#ffffff","#262624","#1f1e1d","#e8e6dc","#e08a6b","#d97757","#faf9f5","#4a3a33", ANTHROPIC_SERIF, MONO, ANTHROPIC_SERIF, 12, false, false, 10, 760},
 {"Gruvbox",    "plain",    true,  "#282828","#3c3836","#ebdbb2","#928374","#504945","#d79921","#282828","#1d2021","#32302f","#b8bb26","#83a598","#fe8019","#fabd2f","#504945", SANS, MONO, SANS,  6, true,  true, 10, 860},
 {"Nord",       "plain",    true,  "#2e3440","#3b4252","#d8dee9","#7b88a1","#434c5e","#88c0d0","#2e3440","#2e3440","#3b4252","#a3be8c","#81a1c1","#ebcb8b","#88c0d0","#434c5e", SANS, MONO, SANS,  8, true,  true, 10, 860},
 {"Catppuccin", "plain",    true,  "#1e1e2e","#181825","#cdd6f4","#6c7086","#313244","#cba6f7","#1e1e2e","#1e1e2e","#11111b","#a6e3a1","#89b4fa","#f9e2af","#f5c2e7","#45475a", SANS, MONO, SANS, 10, true,  true, 10, 860},
 // VS Code: "Light+" / "Dark+" colours, square-ish corners, blue status bar
 {"VS Code Light","vscode", false, "#ffffff","#f3f3f3","#333333","#6e6e6e","#d4d4d4","#007acc","#ffffff","#ffffff","#f0f0f0","#a31515","#006ab1","#007acc","#333333","#add6ff", VSSANS, VSMONO, VSSANS, 4, true,  true, 10, 900},
 {"VS Code Dark","vscode",  true,  "#1e1e1e","#252526","#d4d4d4","#858585","#3c3c3c","#007acc","#ffffff","#1e1e1e","#2d2d2d","#d7ba7d","#3794ff","#007acc","#d4d4d4","#264f78", VSSANS, VSMONO, VSSANS, 4, true,  true, 10, 900},
 // Glass: capsule-shaped translucent tiles with a specular sheen and light rim; no backdrop is painted !EXPERIMENTAL
 {"Glass Light","glass", false, "#eef1f7","#f6f8fc","#1d1d1f","#5b6173","#c4cfe8","#007aff","#ffffff","#f6f8fd","#e8edf9","#c0356f","#0071e3","#8fb4f5","#1d1d1f","#b8d4ff", GLASSSANS, MONO, GLASSSANS, 22, true,  false, 10, 820},
 {"Glass Dark","glass",  true,  "#0b1020","#161b2e","#f5f5f7","#a3aabd","#2d3552","#0a84ff","#ffffff","#0f1424","#1a2036","#ff9fc4","#64b0ff","#4a7bd0","#ffffff","#26467a", GLASSSANS, MONO, GLASSSANS, 22, true,  false, 10, 820},
};

const QStringList kColorKeys = {
    "bg","panel","fg","muted","border","accent","accentFg",
    "editorBg","editorFg","gutterBg","gutterFg","lineHighlight","selection",
    "link","heading","quoteBar","codeBg","codeFg","tableHeader",
    "frameBorder","frameBorderInactive"
};
} // namespace

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
QString Theme::color(const QString& key) const { return colors.value(key, QStringLiteral("#ff00ff")); }

void Theme::fillDefaults() {
    // neutral dark base palette; only fills keys that are missing
    auto def = [this](const QString& k, const QString& v) { if (!colors.contains(k)) colors[k] = v; };
    def("bg", "#1e1f22");   def("panel", "#2b2d30"); def("fg", "#dfe1e5");  def("muted", "#868a91");
    def("border", "#393b40"); def("accent", "#4c8dff"); def("accentFg", "#ffffff");
    def("editorBg", color("bg")); def("editorFg", color("fg"));
    def("link", color("accent"));  def("heading", color("fg")); def("quoteBar", color("border"));
    def("codeBg", color("panel")); def("codeFg", "#a5d6a7");
    def("selection", mixColors(color("bg"), color("accent"), 0.35));
    def("gutterBg", color("editorBg")); def("gutterFg", color("muted"));
    def("lineHighlight", mixColors(color("editorBg"), color("fg"), 0.06));
    def("tableHeader", color("panel"));
    def("frameBorder", color("accent"));                                         // custom window border, focused window
    def("frameBorderInactive", mixColors(color("border"), color("fg"), 0.25));   // ... and when another window has focus
    if (uiFont.isEmpty())      uiFont = SANS;
    if (editorFont.isEmpty())  editorFont = MONO;
    if (previewFont.isEmpty()) previewFont = SANS;
    if (codeFont.isEmpty())    codeFont = MONO;
    if (chrome.isEmpty())      chrome = "plain";
}

QJsonObject Theme::toJson() const {
    QJsonObject c;
    for (const QString& k : kColorKeys) c[k] = color(k);
    QJsonObject f;
    f["ui"] = uiFont; f["uiSize"] = uiSize;
    f["editor"] = editorFont; f["editorSize"] = editorSize;
    f["preview"] = previewFont; f["previewSize"] = previewSize;
    f["code"] = codeFont; f["codeScalePercent"] = codeFontScale;
    QJsonObject shape;
    shape["cornerRadius"] = radius;
    shape["codeBorder"] = codeBorder;
    shape["codeLanguageLabel"] = codeLangLabel;
    shape["headingRules"] = headingRules;
    shape["pageWidth"] = pageWidth;
    shape["windowBorderWidth"] = frameWidth;
    QJsonObject o;
    o["name"] = name; o["revision"] = revision; o["builtin"] = builtin; o["dark"] = dark; o["chrome"] = chrome;
    o["colors"] = c; o["fonts"] = f; o["shape"] = shape;
    return o;
}

Theme Theme::fromJson(const QJsonObject& o) {
    Theme t;
    t.name    = o["name"].toString();
    t.builtin = o["builtin"].toBool(false);
    t.revision = o["revision"].toInt(0);
    t.dark    = o["dark"].toBool(false);
    t.chrome  = o["chrome"].toString("plain");
    const QJsonObject c = o["colors"].toObject();
    for (const QString& k : c.keys()) t.colors[k] = c[k].toString();
    const QJsonObject f = o["fonts"].toObject();
    t.uiFont = f["ui"].toString();           t.uiSize = f["uiSize"].toInt(10);
    t.editorFont = f["editor"].toString();   t.editorSize = f["editorSize"].toInt(11);
    t.previewFont = f["preview"].toString(); t.previewSize = f["previewSize"].toInt(11);
    t.codeFont = f["code"].toString();       t.codeFontScale = f["codeScalePercent"].toInt(92);
    const QJsonObject s = o["shape"].toObject();
    t.radius = s["cornerRadius"].toInt(8);
    t.codeBorder = s["codeBorder"].toBool(true);
    t.codeLangLabel = s["codeLanguageLabel"].toBool(true);
    t.headingRules = s["headingRules"].toBool(true);
    t.pageWidth = s["pageWidth"].toInt(860);
    t.frameWidth = qBound(0, s["windowBorderWidth"].toInt(1), 6);
    t.fillDefaults();       // hand-edited files may omit keys - never leave a theme incomplete
    return t;
}

// ---------------------------------------------------------------------------
// ThemeStore: folders + files
// ---------------------------------------------------------------------------
QString ThemeStore::appHome() { return platform::appHome(); }   // per-OS path lives in arch/<target>/platform.cpp
QString ThemeStore::presetsDir() { return appHome() + "/presets"; }

QString ThemeStore::sanitizeName(const QString& n) {
    QString s = n.trimmed();
    s.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    return s;
}

// Removes // line comments, /* block comments */ and trailing commas, but leaves strings untouched.
QString ThemeStore::stripJsonComments(const QString& in) {
    QString out; out.reserve(in.size());
    bool inStr = false;
    for (int i = 0; i < in.size(); ++i) {
        const QChar c = in[i];
        if (inStr) {                                   // inside "...": copy verbatim, honour \" escapes
            out += c;
            if (c == '\\' && i + 1 < in.size()) out += in[++i];
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; out += c; continue; }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {          // // comment
            while (i < in.size() && in[i] != '\n') ++i;
            out += '\n';
            continue;
        }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {          // /* comment */
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        out += c;
    }
    // trailing commas ("a", ] / "a", }) - second pass that skips string contents, so a regex like "{2,}" survives
    QString res; res.reserve(out.size());
    inStr = false;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out[i];
        if (inStr) {
            res += c;
            if (c == '\\' && i + 1 < out.size()) res += out[++i];
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; res += c; continue; }
        if (c == ',') {
            int j = i + 1;
            while (j < out.size() && out[j].isSpace()) ++j;
            if (j < out.size() && (out[j] == '}' || out[j] == ']')) continue;   // drop the comma
        }
        res += c;
    }
    return res;
}

QList<Theme> ThemeStore::stockThemes() {
    QList<Theme> list;
    for (const Pal& p : kPalettes) {
        Theme t;
        t.name = p.name; t.builtin = true; t.revision = kStockRevision; t.dark = p.dark; t.chrome = p.chrome;
        t.colors = {{"bg", p.bg}, {"panel", p.panel}, {"fg", p.fg}, {"muted", p.muted}, {"border", p.border},
                    {"accent", p.accent}, {"accentFg", p.accentFg}, {"editorBg", p.editorBg}, {"editorFg", p.fg},
                    {"codeBg", p.codeBg}, {"codeFg", p.codeFg}, {"link", p.link}, {"quoteBar", p.quote},
                    {"heading", p.heading}, {"selection", p.selection}};
        t.uiFont = p.ui; t.editorFont = p.editor; t.previewFont = p.preview; t.codeFont = p.editor;
        t.radius = p.radius; t.codeBorder = p.codeBorder; t.headingRules = p.rules;
        t.uiSize = p.uiSize; t.pageWidth = p.pageWidth;
        t.fillDefaults();   // derives gutter / line-highlight / table-header colours
        list << t;
    }
    return list;
}

bool ThemeStore::save(const Theme& t) {
    QDir().mkpath(presetsDir());
    const QString header =
        "// MakDown preset: " + t.name + "\n"
        "// Plain JSON with comments (.jsonc). Edit freely - MakDown re-reads this folder on launch.\n"
        "//   chrome : github | win11 | terminal | warm | vscode | glass | plain   (overall app layout flavour)\n"
        "//   colors : \"#rrggbb\" values;  fonts: comma separated fallback lists\n"
        "//   shape  : cornerRadius (px) for UI corners, code-block + page options, windowBorderWidth (0-6 px, Windows)\n"
        "// Set \"builtin\": false (or clone the preset in Settings) to edit a stock preset in the UI.\n";
    const QString body = QString::fromUtf8(QJsonDocument(t.toJson()).toJson(QJsonDocument::Indented));
    return writeFile(presetsDir() + "/" + sanitizeName(t.name) + ".jsonc", header + body);
}

bool ThemeStore::remove(const Theme& t) {
    return QFile::remove(presetsDir() + "/" + sanitizeName(t.name) + ".jsonc");
}

void ThemeStore::ensureStock() {
    QDir().mkpath(presetsDir());
    for (const Theme& t : stockThemes()) {
        const QString path = presetsDir() + "/" + sanitizeName(t.name) + ".jsonc";
        bool write = !QFile::exists(path);
        if (!write) {   // refresh outdated stock presets - but never touch files the user took over ("builtin": false)
            const QJsonDocument d = QJsonDocument::fromJson(stripJsonComments(readFile(path)).toUtf8());
            if (d.isObject()) { const Theme old = Theme::fromJson(d.object()); write = old.builtin && old.revision < t.revision; }
        }
        if (write) save(t);
    }
}

QList<Theme> ThemeStore::loadAll() {
    QList<Theme> out;
    const QFileInfoList files = QDir(presetsDir()).entryInfoList({"*.jsonc", "*.json"}, QDir::Files);
    for (const QFileInfo& fi : files) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(stripJsonComments(readFile(fi.filePath())).toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;   // skip broken files silently
        Theme t = Theme::fromJson(doc.object());
        if (t.name.isEmpty()) t.name = fi.completeBaseName();
        out << t;
    }
    // stock presets first (in canonical order), then custom ones alphabetically
    QStringList order; for (const Pal& p : kPalettes) order << p.name;
    std::sort(out.begin(), out.end(), [&](const Theme& a, const Theme& b) {
        int ia = a.builtin ? order.indexOf(a.name) : -1, ib = b.builtin ? order.indexOf(b.name) : -1;
        if (ia < 0) ia = 1000;
        if (ib < 0) ib = 1000;
        return ia != ib ? ia < ib : a.name.toLower() < b.name.toLower();
    });
    if (out.isEmpty()) out = stockThemes();
    return out;
}

QStringList ThemeStore::colorKeys() { return kColorKeys; }
QString ThemeStore::colorLabel(const QString& k) {      // names come from the language files ("color.<key>")
    const QString key = "color." + k, s = Lang::text(key);
    return s == key ? k : s;
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------
QFont ThemeStore::makeFont(const QString& spec, int pt, bool mono) {
    QStringList fam;
    for (QString s : spec.split(',')) { s = s.trimmed(); if (!s.isEmpty()) fam << s; }
    QFont f;
    if (!fam.isEmpty()) f.setFamilies(fam);          // Qt walks the list until it finds an installed family
    f.setPointSize(qMax(6, pt));
    if (mono) f.setStyleHint(QFont::Monospace);
    return f;
}

QString ThemeStore::cssFamily(const QString& spec, char quote) {
    static const QStringList generic = {"monospace", "serif", "sans-serif", "cursive", "fantasy"};
    QStringList parts;
    for (QString s : spec.split(',')) {
        s = s.trimmed();
        if (s.isEmpty()) continue;
        parts << (generic.contains(s) ? s : QString(quote) + s + QString(quote));
    }
    return parts.join(", ");
}

// ---------------------------------------------------------------------------
// The global QSS. Tokens like @bg@ are replaced with theme values below.
// (Fonts are NOT set here - they're applied with QWidget::setFont so editor/preview keep their own.)
// ---------------------------------------------------------------------------
QString ThemeStore::styleSheet(const Theme& t) {
    QString q = QStringLiteral(R"QSS(
QMainWindow, QDialog, QWidget#root { background: @bg@; color: @fg@; }
QLabel, QCheckBox, QGroupBox, QRadioButton { color: @fg@; background: transparent; }
QLabel#hint { color: @muted@; margin-left: 24px; margin-bottom: 8px; }
QLabel#pageTitle { font-size: 16pt; font-weight: 600; margin-bottom: 6px; }
QLabel#pill { border: 1px solid @border@; border-radius: @r3@px; padding: 1px 8px; color: @muted@; font-size: 8pt; }
QLabel#modeLabel { background: @accent@; color: @accentFg@; padding: 2px 12px; font-weight: bold; font-family: @codefont@; }
QLabel#paneHeader { background: @panel@; color: @muted@; padding: 6px 14px; border-bottom: 1px solid @border@; }
QLabel#chromeTitle { background: transparent; }

QFrame#chromeBar { background: @panel@; border-bottom: 1px solid @border@; }
QFrame#titleBar { background: @panel@; border-bottom: 1px solid @border@; }
QFrame#titleBar QMenuBar { background: transparent; border: none; }
QFrame#swatches QLabel { border: 1px solid @border@; border-radius: @r3@px; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; }

QMenuBar { background: @panel@; color: @fg@; border-bottom: 1px solid @border@; padding: 2px 4px; }
QMenuBar::item { padding: 4px 10px; border-radius: @r2@px; background: transparent; }
QMenuBar::item:selected { background: @accent@; color: @accentFg@; }
QMenu { background: @panel@; color: @fg@; border: 1px solid @border@; border-radius: @radius@px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 18px; border-radius: @r2@px; }
QMenu::item:selected { background: @accent@; color: @accentFg@; }
QMenu::item:disabled { color: @muted@; }
QMenu::separator { height: 1px; background: @border@; margin: 5px 8px; }
QStatusBar { background: @panel@; color: @muted@; border-top: 1px solid @border@; }
QStatusBar::item { border: none; }
QToolTip { background: @panel@; color: @fg@; border: 1px solid @border@; padding: 4px 6px; }

QPlainTextEdit#editor, QTextBrowser#preview {
    background: @editorBg@; color: @editorFg@; border: none;
    selection-background-color: @selection@; selection-color: @fg@;
}
QLineEdit#cmdline { background: @panel@; color: @fg@; border: none; border-top: 1px solid @border@;
    border-radius: 0px; padding: 5px 10px; font-family: @codefont@; }

QToolButton { background: transparent; color: @fg@; border: 1px solid transparent; border-radius: @r2@px; padding: 4px 11px; }
QToolButton:hover { background: @border@; }
QToolButton:checked { background: @accent@; color: @accentFg@; }

QPushButton { background: @panel@; color: @fg@; border: 1px solid @border@; border-radius: @radius@px; padding: 6px 16px; }
QPushButton:hover { border-color: @accent@; }
QPushButton:disabled { color: @muted@; }
QPushButton:default, QPushButton#primary { background: @accent@; color: @accentFg@; border-color: @accent@; }

QLineEdit, QSpinBox, QComboBox, QFontComboBox {
    background: @editorBg@; color: @fg@; border: 1px solid @border@; border-radius: @r2@px; padding: 5px 8px;
    selection-background-color: @accent@; selection-color: @accentFg@; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QFontComboBox:focus { border: 1px solid @accent@; }
QComboBox::drop-down, QFontComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView, QFontComboBox QAbstractItemView { background: @panel@; color: @fg@; border: 1px solid @border@;
    selection-background-color: @accent@; selection-color: @accentFg@; outline: 0; }
QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; background: transparent; }

QListWidget#nav { background: @panel@; border: none; border-right: 1px solid @border@; outline: 0; padding: 10px; }
QListWidget#nav::item { padding: 9px 14px; border-radius: @r2@px; margin: 2px 0; color: @fg@; }
QListWidget#nav::item:hover { background: @border@; }
QListWidget#nav::item:selected { background: @accent@; color: @accentFg@; }

QGroupBox { border: 1px solid @border@; border-radius: @radius@px; margin-top: 14px; padding: 14px 10px 8px 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: @muted@; }
QScrollArea { border: none; background: transparent; }

QCheckBox { spacing: 8px; padding: 3px 0; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @border@; border-radius: @r3@px; background: @editorBg@; }
QCheckBox::indicator:hover { border-color: @accent@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }

QSlider::groove:horizontal { height: 4px; background: @border@; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
QSlider::handle:horizontal { background: @accent@; width: 14px; height: 14px; margin: -6px 0; border-radius: @r7@px; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: @border@; border-radius: @r3@px; min-height: 30px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @muted@; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: @border@; border-radius: @r3@px; min-width: 30px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: @muted@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: @border@; }
QSplitter::handle:horizontal { width: 1px; }
QTreeView#explorer { background: @panel@; color: @fg@; border: none; outline: 0; padding: 4px; }
QTreeView#explorer::item { padding: 3px 4px; border-radius: @r2@px; }
QTreeView#explorer::item:hover { background: @border@; }
QTreeView#explorer::item:selected { background: @accent@; color: @accentFg@; }
QFrame#browserBar { background: @panel@; border-bottom: 1px solid @border@; }
QFrame#findBar { background: @panel@; border-bottom: 1px solid @border@; }
QLineEdit[bad="true"] { border: 1px solid #e5484d; }
QTreeWidget#searchTree, QListWidget#quickList { background: @editorBg@; color: @fg@; border: none; outline: 0; }
QTreeWidget#searchTree::item { padding: 1px 2px; border-radius: @r2@px; }
QListWidget#quickList::item { padding: 4px 8px; border-radius: @r2@px; }
QTreeWidget#searchTree::item:hover, QListWidget#quickList::item:hover { background: @border@; }
QTreeWidget#searchTree::item:selected, QListWidget#quickList::item:selected { background: @accent@; color: @accentFg@; }
QFrame#sideRail { background: @panel@; border-right: 1px solid @border@; }
QFrame#sideRail QToolButton { background: transparent; border: none; border-left: 2px solid transparent; border-radius: 0px; padding: 9px 6px; }
QFrame#sideRail QToolButton:hover { background: @border@; }
QFrame#sideRail QToolButton:checked { background: transparent; border-left: 2px solid @accent@; }
QLabel#quickStatus { color: @muted@; }
QTabWidget::pane { border: none; }
QTabBar { background: @panel@; }
QTabBar::tab { background: transparent; color: @muted@; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: @fg@; }
QTabBar::tab:selected { color: @fg@; border-bottom: 2px solid @accent@; }
)QSS");

    // chrome-specific extras
    if (t.chrome == "terminal") {
        q += QStringLiteral("QLabel#paneHeader, QLabel#chromeTitle, QMenuBar, QMenu, QToolButton, QStatusBar { font-family: @codefont@; }"
                            "QToolButton:checked { background: @fg@; color: @bg@; }");
    } else if (t.chrome == "github") {
        q += QStringLiteral("QLabel#paneHeader { border-top: 1px solid @border@; font-weight: 600; color: @fg@; }"
                            "QToolButton { border: 1px solid @border@; } QToolButton:checked { background: @panel@; color: @fg@; border-color: @accent@; }");
    } else if (t.chrome == "warm") {
        q += QStringLiteral("QToolButton { border-radius: @radius@px; padding: 5px 14px; }"
                            "QLabel#paneHeader { background: @bg@; border-bottom: none; padding-top: 12px; }"
                            "QFrame#chromeBar { background: @bg@; border-bottom: 1px solid @border@; }");
    } else if (t.chrome == "win11") {
        q += QStringLiteral("QFrame#chromeBar { background: @panel@; } QToolButton:checked { background: @editorBg@; color: @accent@; border: 1px solid @border@; }");
    } else if (t.chrome == "vscode") {         // editor-tab look, blue status bar, flat selection colours
        q += QStringLiteral(
            "QFrame#chromeBar { background: @panel@; border-bottom: 1px solid @border@; }"
            "QLabel#paneHeader { background: @editorBg@; color: @fg@; border-top: 1px solid @accent@; border-bottom: 1px solid @border@; padding: 6px 14px; }"
            "QMenuBar::item:selected { background: @border@; color: @fg@; }"
            "QStatusBar { background: @accent@; color: @accentFg@; border-top: none; } QStatusBar QLabel { color: @accentFg@; background: transparent; }"
            "QToolButton:hover { background: @border@; } QToolButton:checked { background: @border@; color: @fg@; border: 1px solid transparent; }"
            "QTreeView#explorer::item:selected, QTreeWidget#searchTree::item:selected, QListWidget#quickList::item:selected, QListWidget#nav::item:selected"
            " { background: @selection@; color: @fg@; }"
            "QTabBar::tab { background: @panel@; color: @muted@; padding: 8px 14px; border: none; border-right: 1px solid @border@; border-top: 1px solid transparent; }"
            "QTabBar::tab:hover { color: @fg@; }"
            "QTabBar::tab:selected { background: @editorBg@; color: @fg@; border-top: 1px solid @accent@; }");
    } else if (t.chrome == "glass") {        // Glass: translucent tiles over the backdrop painted by MainWindow::paintEvent
        const bool dk = t.dark;
        auto rgba = [](const QString& hex, int a) { const QColor c(hex); return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a); };
        // diagonal sheen: bright at the top-left corner, clear in the middle, a faint catch-light bottom-right (the refraction look)
        auto grad = [](const QString& top, const QString& bot) {
            return QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:0.4 %2, stop:0.75 %2, stop:1 %1)").arg(top, bot);
        };
        const QString white = QStringLiteral("255,255,255");
        const QString gTop   = QStringLiteral("rgba(%1,%2)").arg(white).arg(dk ? 60 : 200);     // specular corner of a glass tile
        const QString gBot   = QStringLiteral("rgba(%1,%2)").arg(white).arg(dk ? 10 : 70);
        const QString gEdge  = dk ? QStringLiteral("rgba(255,255,255,80)") : QStringLiteral("rgba(140,155,190,110)");   // 1px rim (visible on a flat bg)
        const QString gHover = QStringLiteral("rgba(%1,%2)").arg(white).arg(dk ? 40 : 120);
        const QString gPane  = dk ? rgba(t.color("editorBg"), 175) : QStringLiteral("rgba(%1,170)").arg(white);   // reading surfaces
        const QString gHandle = dk ? QStringLiteral("rgba(255,255,255,80)") : QStringLiteral("rgba(60,70,110,90)");
        QString g = QStringLiteral(
            "QWidget#root { background: transparent; }"
            "QFrame#titleBar { background: transparent; border: none; }"
            "QFrame#chromeBar, QFrame#sideRail, QFrame#findBar, QFrame#browserBar { background: @gGrad@; border: 1px solid @gEdge@; border-radius: @radius@px; }"
            "QFrame#sideRail { border-radius: @r2@px; }"
            "QLabel#paneHeader { background: @gGrad@; color: @muted@; border: 1px solid @gEdge@; border-bottom: none; padding: 7px 16px; }"
            "QMenuBar { background: transparent; border-bottom: none; padding: 4px 12px 0 12px; }"
            "QMenuBar::item { padding: 5px 12px; border-radius: @radius@px; }"
            "QMenuBar::item:selected { background: @gHover@; color: @fg@; }"
            "QMenu { border-radius: @r2@px; }"
            "QStatusBar { background: transparent; border-top: none; color: @muted@; padding: 2px 12px; }"
            "QToolButton { border-radius: @radius@px; padding: 5px 9px; }"
            "QToolButton:hover { background: @gHover@; }"
            "QToolButton:checked { background: @gAccentFill@; color: @fg@; border: 1px solid @gAccentEdge@; }"
            "QFrame#sideRail QToolButton { border: none; border-radius: @r2@px; padding: 9px 6px; margin: 2px 3px; }"
            "QFrame#sideRail QToolButton:hover { background: @gHover@; }"
            "QFrame#sideRail QToolButton:checked { background: @gAccentFill@; border: 1px solid @gAccentEdge@; }"
            "QPushButton { background: @gGrad@; border: 1px solid @gEdge@; border-radius: @radius@px; padding: 6px 18px; }"
            "QPushButton:hover { background: @gHover@; border-color: @accent@; }"
            "QPushButton:default, QPushButton#primary { background: @gAccentFill@; color: @accentFg@; border: 1px solid @gAccentEdge@; }"
            "QLineEdit, QSpinBox, QComboBox, QFontComboBox { background: @gPane@; border: 1px solid @gEdge@; border-radius: @r2@px; }"
            "QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QFontComboBox:focus { border: 1px solid @accent@; }"
            "QPlainTextEdit#editor, QTextBrowser#preview, QTreeView#explorer, QTreeWidget#searchTree, QListWidget#quickList"
            " { background: @gPane@; border: 1px solid @gEdge@; }"
            "QTreeView#explorer::item:hover, QTreeWidget#searchTree::item:hover, QListWidget#quickList::item:hover { background: @gHover@; }"
            "QTreeView#explorer::item:selected, QTreeWidget#searchTree::item:selected, QListWidget#quickList::item:selected"
            " { background: @gAccent@; color: @fg@; }"
            "QListWidget#nav { background: @gGrad@; border: 1px solid @gEdge@; border-radius: @r2@px; }"
            "QListWidget#nav::item:hover { background: @gHover@; } QListWidget#nav::item:selected { background: @gAccent@; color: @fg@; }"
            "QTabWidget::pane { background: transparent; border: none; }"
            "QTabBar { background: transparent; }"
            "QTabBar::tab { background: transparent; color: @muted@; padding: 7px 16px; margin: 4px 2px 0 2px; border: 1px solid transparent; border-radius: @r2@px; }"
            "QTabBar::tab:hover { background: @gHover@; color: @fg@; }"
            "QTabBar::tab:selected { background: @gGrad@; color: @fg@; border: 1px solid @gEdge@; }"
            "QSplitter::handle { background: transparent; }"
            "QSplitter::handle:horizontal { width: 8px; } QSplitter::handle:vertical { height: 8px; }"
            "QGroupBox { border: 1px solid @gEdge@; }"
            "QScrollBar:vertical { width: 14px; } QScrollBar:horizontal { height: 14px; }"
            "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: @gHandle@; border-radius: 5px; }"
            "QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: @muted@; }"
            "QSlider::groove:horizontal { background: @gHandle@; }"
            "QCheckBox::indicator { border: 1px solid @gEdge@; border-radius: 5px; background: @gPane@; }"
            "QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }");
        g.replace("@gGrad@", grad(gTop, gBot));
        g.replace("@gEdge@", gEdge).replace("@gHover@", gHover).replace("@gPane@", gPane).replace("@gHandle@", gHandle);
        const QColor ac(t.color("accent"));
        const QString rim = ac.lighter(150).name();            // light, cyan-ish rim like the reference's blue buttons
        g.replace("@gAccent@", rgba(t.color("accent"), dk ? 90 : 55)).replace("@gAccentEdge@", rgba(rim, 220));
        g.replace("@gAccentFill@", QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:0.5 %2, stop:1 %1)")
                                       .arg(rgba(ac.lighter(125).name(), 215), rgba(t.color("accent"), dk ? 170 : 150)));
        q += g;
    }

    const int r = t.radius;
    q.replace("@radius@", QString::number(r));
    q.replace("@r2@", QString::number(r / 2));
    q.replace("@r3@", QString::number(qMin(r, 4)));
    q.replace("@r7@", QString::number(qMin(r, 7)));
    q.replace("@codefont@", cssFamily(t.codeFont, '"'));
    for (auto it = t.colors.constBegin(); it != t.colors.constEnd(); ++it) q.replace("@" + it.key() + "@", it.value());
    return q;
}

// ---------------------------------------------------------------------------
// AppSettings <-> <home>/settings.jsonc
// ---------------------------------------------------------------------------
void AppSettings::load() {
    const QString text = readFile(ThemeStore::appHome() + "/settings.jsonc");
    if (text.isEmpty()) return;
    const QJsonDocument d = QJsonDocument::fromJson(ThemeStore::stripJsonComments(text).toUtf8());
    if (!d.isObject()) return;
    const QJsonObject o = d.object();
    theme = o["theme"].toString(theme);          vim = o["vim"].toBool(vim);
    language = o["language"].toString(language);
    lineNumbers = o["lineNumbers"].toBool(lineNumbers);
    paragraphMarks = o["paragraphMarks"].toBool(paragraphMarks);
    showSpaces = o["showSpaces"].toBool(showSpaces);
    wrap = o["wrap"].toBool(wrap);               highlightLine = o["highlightLine"].toBool(highlightLine);
    revealMarkers = o["revealMarkers"].toBool(revealMarkers);
    livePreview = o["livePreview"].toBool(livePreview);
    syncScroll = o["syncScroll"].toBool(syncScroll);
    tabWidth = o["tabWidth"].toInt(tabWidth);    viewMode = o["viewMode"].toInt(viewMode);
    geometry = o["geometry"].toString();         lastFile = o["lastFile"].toString();
    explorer = o["explorer"].toBool(explorer);   liveReload = o["liveReload"].toBool(liveReload);
    folder = o["folder"].toString();
    animations = qBound(0, o["animations"].toInt(animations), 3);
    console = o["console"].toBool(console);
    customFrame = o["customFrame"].toBool(customFrame);
}

void AppSettings::save() const {
    QJsonObject o;
    o["theme"] = theme; o["language"] = language; o["vim"] = vim; o["lineNumbers"] = lineNumbers; o["paragraphMarks"] = paragraphMarks;
    o["showSpaces"] = showSpaces; o["wrap"] = wrap; o["highlightLine"] = highlightLine;
    o["revealMarkers"] = revealMarkers; o["livePreview"] = livePreview; o["syncScroll"] = syncScroll;
    o["tabWidth"] = tabWidth; o["viewMode"] = viewMode; o["geometry"] = geometry; o["lastFile"] = lastFile;
    o["explorer"] = explorer; o["liveReload"] = liveReload; o["folder"] = folder;
    o["animations"] = animations; o["console"] = console; o["customFrame"] = customFrame;
    QDir().mkpath(ThemeStore::appHome());
    writeFile(ThemeStore::appHome() + "/settings.jsonc",
              "// MakDown settings (JSONC). Changed from the Settings dialog; safe to edit by hand while MakDown is closed.\n" +
              QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented)));
}
