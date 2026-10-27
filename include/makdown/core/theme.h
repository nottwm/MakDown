// theme.h - everything about THEMES (presets) and persisted APP SETTINGS.
//
//  * A Theme describes colours, fonts, corner radius, code-block styling and
//    which "chrome" (app layout flavour: GitHub / Win11 / Terminal / Warm / VS Code / Liquid Glass / plain) to use.
//  * Themes live on disk as .jsonc files (JSON with // comments) inside the presets folder:
//        Windows   : %USERPROFILE%\AppData\Local\MakDown\presets
//        Linux/mac : ~/.local/share/MakDown/presets
//    Stock presets are (re)written there on first launch, so you can open and tweak them.
#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QFont>

struct Theme {
    QString name;                       // display name AND file name (sanitised)
    int     revision = 0;               // stock preset revision (see kStockRevision in theme.cpp)
    bool    builtin = false;            // stock presets are read-only in the UI (clone them to edit)
    bool    dark    = false;            // informational only
    QString chrome  = "plain";          // layout flavour: github | win11 | terminal | warm | vscode | glass | plain

    // Named colours, "#rrggbb". See ThemeStore::colorKeys() for the full list.
    QMap<QString, QString> colors;

    // Font specs are comma separated fallback lists, e.g. "JetBrains Mono, Consolas, monospace".
    QString uiFont, editorFont, previewFont, codeFont;
    int uiSize = 10, editorSize = 11, previewSize = 11;

    int  radius        = 8;      // "UI corners" in px (buttons, menus, inputs, scrollbars ...)
    bool codeBorder    = true;   // draw a border around code blocks in the preview
    bool codeLangLabel = true;   // show the language name above fenced code blocks
    int  codeFontScale = 92;     // code font size relative to preview font, in percent
    bool headingRules  = true;   // draw a thin rule under H1/H2 (GitHub style)
    int  pageWidth     = 860;    // max width of the rendered document in px
    int  frameWidth    = 1;      // custom window border in px, 0-6 (0 = none); colours: frameBorder / frameBorderInactive

    QString color(const QString& key) const;   // colour lookup with a loud fallback
    void    fillDefaults();                    // fills every missing colour/font so a theme is always complete
    QJsonObject toJson() const;
    static Theme fromJson(const QJsonObject& o);
};

namespace ThemeStore {
    QString appHome();                          // MakDown's home folder (see top of file)
    QString presetsDir();                       // <home>/presets
    QString stripJsonComments(const QString&);  // turns JSONC into plain JSON (also drops trailing commas)
    QList<Theme> stockThemes();                 // the built-in presets
    void ensureStock();                         // writes missing stock presets to disk
    QList<Theme> loadAll();                     // reads every *.jsonc preset from disk
    bool save(const Theme& t);                  // writes <presets>/<name>.jsonc
    bool remove(const Theme& t);                // deletes a (custom) preset file
    QString sanitizeName(const QString& n);     // strips characters that are illegal in file names

    QStringList colorKeys();                    // ordered colour keys (settings UI uses this)
    QString     colorLabel(const QString& key); // human-readable label for a key

    QFont   makeFont(const QString& spec, int pt, bool mono = false);  // spec "A, B, C" -> QFont with fallbacks
    QString cssFamily(const QString& spec, char quote = '"');          // spec -> CSS font-family value
    QString styleSheet(const Theme& t);         // the global Qt style sheet (QSS) for a theme
}

// Persisted user settings (not part of a theme). Stored as <home>/settings.jsonc
struct AppSettings {
    QString theme = "GitHub";
    bool vim = false;              // "vimkeys": vim modal editing + extra hotkeys
    bool lineNumbers = true;
    bool paragraphMarks = false;   // Word-style pilcrows (¶) at the end of every paragraph
    bool showSpaces = false;       // dots for spaces / arrows for tabs in the editor
    bool wrap = true;
    bool highlightLine = true;
    bool revealMarkers = false;    // preview shows the normally hidden markdown marks (*, -, #, >)
    bool livePreview = true;
    bool syncScroll = true;
    int  tabWidth = 4;
    int  viewMode = 1;             // 0 = editor only, 1 = split, 2 = preview only
    QString geometry;              // base64 window geometry
    QString lastFile;
    bool explorer = true;          // folder tree on the left
    bool liveReload = true;        // browser tabs reload when the page's .html/.js/.css change
    QString folder;                // explorer root
    int  animations = 2;           // 0 none, 1 little, 2 normal, 3 full (core/anim.h)
    bool console = false;          // the console panel under the editor is shown
    bool customFrame = true;       // MakDown draws its own title bar + themed window border (Windows only; see ui/titlebar.h)
    void load();
    void save() const;
};
