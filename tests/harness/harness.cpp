// SPDX-License-Identifier: MIT
// tests/harness/harness.cpp - a tiny JSON-lines bridge between the Python test-suite and MakDown's C++ code.
//
// The Python tests start this program once (offscreen, with a throw-away $HOME), write one JSON object per
// line to its stdin and read one JSON object per line from its stdout:
//
//      {"cmd":"render","md":"# Hi","theme":"GitHub","reveal":false}   ->   {"ok":true,"html":"...","warnings":[]}
//
// It links against the very same object files as the real application (everything except init/main.o),
// so what is tested here is the shipped code, not a re-implementation.  Every reply carries the Qt
// warnings (qWarning/qCritical) emitted while the command ran, so tests can assert "no warnings".
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QDialog>
#include <QSet>
#include <QImage>
#include <QTextStream>
#include <QTimer>
#include <atomic>
#include <cstdio>

#include <makdown/core/anim.h>
#include <makdown/core/buildinfo.h>
#include <makdown/core/lang.h>
#include <makdown/core/markdown.h>
#include <makdown/core/search.h>
#include <makdown/core/syntax.h>
#include <makdown/core/theme.h>
#include <makdown/ui/editor.h>
#include <makdown/ui/findbar.h>
#include <makdown/ui/mainwindow.h>
#include <makdown/ui/settingsdialog.h>
#include <QListWidget>
#include <QStackedWidget>
#include <makdown/ui/synhl.h>

static QStringList g_warnings;
static QStringList g_dialogs;      // titles of modal dialogs that popped up (and were auto-cancelled) during a command
static void msgHandler(QtMsgType t, const QMessageLogContext&, const QString& m) {
    if (t == QtDebugMsg || t == QtInfoMsg) return;
    g_warnings << QString(t == QtWarningMsg ? "warning: " : t == QtCriticalMsg ? "critical: " : "fatal: ") + m;
}

static QJsonArray arr(const QStringList& l) { QJsonArray a; for (const QString& s : l) a << s; return a; }

static Theme themeByName(const QString& name) {
    for (const Theme& t : ThemeStore::stockThemes()) if (t.name == name) return t;
    return ThemeStore::stockThemes().first();
}

// ---- key injection ------------------------------------------------------------------------------
// Tokens: plain text is typed char by char; "<Esc>", "<CR>", "<BS>", "<Tab>", "<Up>" ... or with modifiers
// "<C-r>", "<A-Up>", "<C-S-d>" (C = ctrl, A = alt, S = shift) are special keys.
static void sendKey(QWidget* w, int key, Qt::KeyboardModifiers m, const QString& text) {
    QKeyEvent press(QEvent::KeyPress, key, m, text);
    QApplication::sendEvent(w, &press);
    QKeyEvent rel(QEvent::KeyRelease, key, m, text);
    QApplication::sendEvent(w, &rel);
}
static int keyFor(const QString& n, bool* ok) {
    static const QHash<QString, int> k = {
        {"Esc", Qt::Key_Escape}, {"CR", Qt::Key_Return}, {"Enter", Qt::Key_Return}, {"BS", Qt::Key_Backspace},
        {"Tab", Qt::Key_Tab}, {"S-Tab", Qt::Key_Backtab}, {"Up", Qt::Key_Up}, {"Down", Qt::Key_Down}, {"Left", Qt::Key_Left},
        {"Right", Qt::Key_Right}, {"Home", Qt::Key_Home}, {"End", Qt::Key_End}, {"Del", Qt::Key_Delete}, {"F3", Qt::Key_F3},
        {"Space", Qt::Key_Space}, {"[", Qt::Key_BracketLeft}};
    *ok = k.contains(n);
    return k.value(n);
}
static void typeTokens(QWidget* w, const QStringList& toks) {
    for (const QString& tok : toks) {
        if (tok.size() > 2 && tok.startsWith('<') && tok.endsWith('>')) {
            QString body = tok.mid(1, tok.size() - 2);
            Qt::KeyboardModifiers m;
            while (body.size() > 2 && body[1] == '-' && QString("CAS").contains(body[0]) && !(body == "S-Tab")) {
                if (body[0] == 'C') m |= Qt::ControlModifier;
                if (body[0] == 'A') m |= Qt::AltModifier;
                if (body[0] == 'S') m |= Qt::ShiftModifier;
                body = body.mid(2);
            }
            bool ok = false;
            int key = keyFor(body, &ok);
            if (!ok && body.size() == 1) { key = body[0].toUpper().unicode(); ok = true; }
            if (ok) {
                const QString text = (m & (Qt::ControlModifier | Qt::AltModifier)) || key == Qt::Key_Escape || key == Qt::Key_Backspace || key == Qt::Key_Tab
                                         || key == Qt::Key_Backtab || key == Qt::Key_Delete || key >= Qt::Key_Left ? QString()
                                         : (key == Qt::Key_Return ? QString("\r") : key == Qt::Key_Space ? QString(" ") : QString());
                sendKey(w, key, m, key == Qt::Key_Return ? QString("\r") : text);
                continue;
            }
        }
        for (const char32_t cp : tok.toUcs4()) {              // code points, so emoji arrive as ONE key event like from a real keyboard
            const QString text = QString::fromUcs4(&cp, 1);
            const QChar ch = text[0];
            int key = cp < 0x10000 ? int(ch.toUpper().unicode()) : int(Qt::Key_unknown);
            Qt::KeyboardModifiers m = (cp < 0x10000 && ch.isUpper()) ? Qt::ShiftModifier : Qt::NoModifier;
            if (cp == ' ') key = Qt::Key_Space;
            if (cp == '\n') { sendKey(w, Qt::Key_Return, {}, "\r"); continue; }
            sendKey(w, key, m, text);
        }
    }
}

static QJsonObject cursorInfo(QPlainTextEdit* e) {
    const QTextCursor c = e->textCursor();
    QJsonObject o;
    o["text"] = e->toPlainText();
    o["pos"] = c.position();
    o["line"] = c.blockNumber() + 1;
    o["col"] = c.positionInBlock();
    o["selection"] = c.selectedText();
    o["selStart"] = c.selectionStart();
    o["selEnd"] = c.selectionEnd();
    return o;
}

// One shared editor between commands so tests can drive it step by step.
static MarkdownEditor* g_editor = nullptr;
static QString g_mode;
static MarkdownEditor* editor() {
    if (!g_editor) {
        g_editor = new MarkdownEditor;
        g_editor->resize(700, 500);
        QObject::connect(g_editor, &MarkdownEditor::vimModeChanged, [](const QString& s) { g_mode = s; });
        g_editor->applyTheme(themeByName("GitHub"));
        g_editor->show();
    }
    return g_editor;
}

static MainWindow* g_win = nullptr;

static QJsonObject handle(const QJsonObject& q) {
    const QString cmd = q["cmd"].toString();
    QJsonObject r;
    r["ok"] = true;

    // ------------------------------------------------------------------ markdown
    if (cmd == "render") {
        const Theme t = themeByName(q["theme"].toString("GitHub"));
        r["html"] = renderMarkdownToHtml(q["md"].toString(), t, q["reveal"].toBool(), q["baseDir"].toString());
    } else if (cmd == "render_plain") {              // render + let Qt parse it, return what a user would SEE
        const Theme t = themeByName(q["theme"].toString("GitHub"));
        const QString html = renderMarkdownToHtml(q["md"].toString(), t, q["reveal"].toBool(), q["baseDir"].toString());
        QTextDocument d; d.setDefaultStyleSheet(previewCss(t)); d.setHtml(html);
        r["plain"] = d.toPlainText();
        r["blocks"] = d.blockCount();
        r["html"] = html;
    } else if (cmd == "css") {
        r["css"] = previewCss(themeByName(q["theme"].toString("GitHub")));

    // ------------------------------------------------------------------ search helpers
    } else if (cmd == "regex") {
        Search::Options o; o.query = q["query"].toString(); o.matchCase = q["case"].toBool(); o.wholeWord = q["word"].toBool(); o.regex = q["regex"].toBool();
        QString err; const QRegularExpression rx = Search::makeRegex(o, &err);
        r["valid"] = rx.isValid() && !o.query.isEmpty(); r["error"] = err; r["pattern"] = rx.pattern();
        if (rx.isValid() && q.contains("subject")) {
            QJsonArray m; auto it = rx.globalMatch(q["subject"].toString());
            while (it.hasNext()) m << it.next().captured(0);
            r["matches"] = m;
        }
    } else if (cmd == "search_text") {
        Search::Options o; o.query = q["query"].toString(); o.matchCase = q["case"].toBool(); o.wholeWord = q["word"].toBool(); o.regex = q["regex"].toBool();
        const QRegularExpression rx = Search::makeRegex(o);
        QJsonArray hits;
        if (rx.isValid()) for (const Search::Hit& h : Search::searchText(q["text"].toString(), rx, q["max"].toInt(5000))) {
            QJsonObject j; j["line"] = h.line; j["col"] = h.col; j["len"] = h.len; j["preview"] = h.preview; j["pcol"] = h.pcol; j["plen"] = h.plen; hits << j;
        }
        r["hits"] = hits;
    } else if (cmd == "fuzzy") {
        r["score"] = Search::fuzzyScore(q["path"].toString(), q["query"].toString());
    } else if (cmd == "globs") {
        const QStringList g = Search::splitGlobs(q["spec"].toString());
        r["globs"] = arr(g);
        QJsonArray m; for (const QJsonValue& p : q["paths"].toArray()) m << Search::globMatch(g, p.toString());
        r["match"] = m;
    } else if (cmd == "binary") {
        r["binary"] = Search::looksBinary(QByteArray::fromBase64(q["b64"].toString().toLatin1()));
    } else if (cmd == "skipped_dir") {
        r["skipped"] = Search::skippedDir(q["name"].toString());
    } else if (cmd == "walk") {
        std::atomic<bool> cancel(false); QJsonArray files; const QString root = QDir(q["root"].toString()).absolutePath();
        const int n = Search::walk(root, cancel, q["max"].toInt(100000), [&](const QString& p, qint64 sz) {
            QJsonObject j; j["path"] = QDir(root).relativeFilePath(p); j["size"] = double(sz); files << j; return true; });
        r["count"] = n; r["files"] = files;

    // ------------------------------------------------------------------ syntax packs
    } else if (cmd == "syntax_parse") {
        QString err; const SyntaxPtr p = SyntaxStore::parse(q["json"].toString().toUtf8(), &err);
        r["null"] = !p; r["error"] = err;
        if (p) { r["name"] = p->name; r["rules"] = p->rules.size(); r["regions"] = p->regions.size();
                 r["extensions"] = arr(p->extensions); r["filenames"] = arr(p->filenames); r["revision"] = p->revision; r["builtin"] = p->builtin; }
    } else if (cmd == "syntax_ensure") {
        SyntaxStore::ensureStock(); r["dir"] = SyntaxStore::syntaxDir();
    } else if (cmd == "syntax_load_all") {
        QJsonArray a; for (const SyntaxPtr& p : SyntaxStore::loadAll()) {
            QJsonObject j; j["name"] = p->name; j["extensions"] = arr(p->extensions); j["filenames"] = arr(p->filenames);
            j["rules"] = p->rules.size(); j["regions"] = p->regions.size(); a << j; }
        r["packs"] = a;
    } else if (cmd == "syntax_for_file") {
        const QList<SyntaxPtr> packs = SyntaxStore::loadAll();
        const SyntaxPtr p = SyntaxStore::forFile(packs, q["path"].toString());
        r["pack"] = p ? p->name : QString();
    } else if (cmd == "stock_syntax_json") {
        extern const char* const kStockSyntaxFiles[]; extern const char* const kStockSyntaxJson[];
        QJsonArray a; for (int i = 0; kStockSyntaxFiles[i]; ++i) { QJsonObject j; j["file"] = kStockSyntaxFiles[i]; j["json"] = kStockSyntaxJson[i]; a << j; }
        r["packs"] = a;
    } else if (cmd == "highlight_pack") {            // run a pack over some text, report the colour ranges per line
        QString err; const SyntaxPtr p = SyntaxStore::parse(q["json"].toString().toUtf8(), &err);
        QPlainTextEdit e; e.setPlainText(q["text"].toString());
        SyntaxHighlighter hl;                       // (synhl.h)
        hl.setTheme(themeByName("GitHub")); hl.setPack(p); hl.setDocument(e.document()); hl.rehighlight();
        QJsonArray lines;
        for (QTextBlock b = e.document()->begin(); b.isValid(); b = b.next()) {
            QJsonArray fr; for (const auto& f : b.layout()->formats()) { QJsonObject j; j["start"] = f.start; j["len"] = f.length; j["color"] = f.format.foreground().color().name(); fr << j; }
            lines << fr; }
        r["lines"] = lines;

    // ------------------------------------------------------------------ themes + settings
    } else if (cmd == "strip_comments") {
        r["out"] = ThemeStore::stripJsonComments(q["in"].toString());
    } else if (cmd == "stock_themes") {
        QJsonArray a; for (const Theme& t : ThemeStore::stockThemes()) a << t.toJson(); r["themes"] = a;
        r["colorKeys"] = arr(ThemeStore::colorKeys());
    } else if (cmd == "theme_from_json") {
        const Theme t = Theme::fromJson(q["theme"].toObject()); r["theme"] = t.toJson(); r["frameWidth"] = t.frameWidth;
        r["radius"] = t.radius; r["uiFont"] = t.uiFont; r["chrome"] = t.chrome;
    } else if (cmd == "theme_color") {
        Theme t; t.colors = {}; r["fallback"] = t.color(q["key"].toString());
    } else if (cmd == "ensure_stock") {
        ThemeStore::ensureStock(); r["dir"] = ThemeStore::presetsDir();
    } else if (cmd == "load_themes") {
        QJsonArray a; for (const Theme& t : ThemeStore::loadAll()) { QJsonObject j; j["name"] = t.name; j["builtin"] = t.builtin; j["revision"] = t.revision; j["chrome"] = t.chrome; j["dark"] = t.dark; a << j; }
        r["themes"] = a;
    } else if (cmd == "save_theme") {
        Theme t = Theme::fromJson(q["theme"].toObject()); r["saved"] = ThemeStore::save(t);
    } else if (cmd == "remove_theme") {
        Theme t; t.name = q["name"].toString(); r["removed"] = ThemeStore::remove(t);
    } else if (cmd == "sanitize") {
        r["out"] = ThemeStore::sanitizeName(q["name"].toString());
    } else if (cmd == "css_family") {
        r["out"] = ThemeStore::cssFamily(q["spec"].toString(), q["quote"].toString("\"")[0].toLatin1());
    } else if (cmd == "make_font") {
        const QFont f = ThemeStore::makeFont(q["spec"].toString(), q["pt"].toInt(10), q["mono"].toBool());
        r["families"] = arr(f.families()); r["pt"] = f.pointSize();
    } else if (cmd == "stylesheet") {                // apply the QSS to a real widget tree; Qt warns about parse errors
        const Theme t = themeByName(q["theme"].toString("GitHub"));
        const QString qss = ThemeStore::styleSheet(t);
        r["leftoverTokens"] = qss.contains('@'); r["length"] = qss.size();
        QWidget w; QLineEdit le(&w); w.setStyleSheet(qss); w.ensurePolished(); le.ensurePolished();
        r["qss"] = qss;
    } else if (cmd == "settings_load") {
        AppSettings s; s.load();
        QJsonObject j; j["theme"] = s.theme; j["language"] = s.language; j["vim"] = s.vim; j["lineNumbers"] = s.lineNumbers; j["tabWidth"] = s.tabWidth; j["viewMode"] = s.viewMode;
        j["animations"] = s.animations; j["wrap"] = s.wrap; j["explorer"] = s.explorer; j["folder"] = s.folder; j["customFrame"] = s.customFrame;
        j["liveReload"] = s.liveReload; j["console"] = s.console; j["lastFile"] = s.lastFile; r["settings"] = j;
    } else if (cmd == "settings_save") {
        AppSettings s; const QJsonObject o = q["settings"].toObject();
        s.theme = o["theme"].toString("GitHub"); s.language = o["language"].toString("auto"); s.vim = o["vim"].toBool(); s.tabWidth = o["tabWidth"].toInt(4); s.viewMode = o["viewMode"].toInt(1);
        s.animations = o["animations"].toInt(2); s.wrap = o["wrap"].toBool(true); s.folder = o["folder"].toString(); s.lastFile = o["lastFile"].toString();
        s.lineNumbers = o["lineNumbers"].toBool(true); s.console = o["console"].toBool(); s.save();
    } else if (cmd == "anim") {
        if (q.contains("level")) Anim::setLevel(q["level"].toInt());
        r["level"] = Anim::level(); r["on"] = Anim::on(); r["ms"] = Anim::ms(q["base"].toInt(100));
    } else if (cmd == "buildinfo") {
        r["info"] = makdownBuildInfo();

    // ------------------------------------------------------------------ editor
    } else if (cmd == "ed_reset") {
        delete g_editor; g_editor = nullptr; g_mode.clear();
        MarkdownEditor* e = editor();
        e->setVimEnabled(q["vim"].toBool());
        e->setPlainText(q["text"].toString());
        QTextCursor c = e->textCursor(); c.setPosition(qBound(0, q["pos"].toInt(0), e->toPlainText().size())); e->setTextCursor(c);
        if (q.contains("tab")) e->setTabWidthChars(q["tab"].toInt());
        r["editor"] = cursorInfo(e); r["mode"] = g_mode;
    } else if (cmd == "ed_keys") {
        MarkdownEditor* e = editor();
        QStringList toks; for (const QJsonValue& v : q["keys"].toArray()) toks << v.toString();
        typeTokens(e, toks);
        r["editor"] = cursorInfo(e); r["mode"] = g_mode;
    } else if (cmd == "ed_call") {
        MarkdownEditor* e = editor(); const QString fn = q["fn"].toString();
        if (fn == "wrap") e->wrapSelection(q["a"].toString(), q["b"].toString());
        else if (fn == "prefix") e->toggleLinePrefix(q["a"].toString());
        else if (fn == "heading") e->changeHeading(q["n"].toInt());
        else if (fn == "goto") e->gotoLine(q["n"].toInt());
        else if (fn == "find") r["found"] = e->findNext(q["a"].toString(), q["back"].toBool());
        else if (fn == "link") e->insertLink();
        else if (fn == "select") { QTextCursor c = e->textCursor(); c.setPosition(q["from"].toInt()); c.setPosition(q["to"].toInt(), QTextCursor::KeepAnchor); e->setTextCursor(c); }
        else if (fn == "undo") e->undo();
        else if (fn == "redo") e->redo();
        else if (fn == "vim") e->setVimEnabled(q["on"].toBool());
        else if (fn == "pack") { QString err; e->setSyntaxPack(SyntaxStore::parse(q["json"].toString().toUtf8(), &err)); }
        else if (fn == "mdhl") e->setMarkdownHighlighting(q["on"].toBool());
        else if (fn == "flags") { e->setShowParagraphMarks(q["marks"].toBool()); e->setShowSpaces(q["spaces"].toBool()); e->setLineNumbers(q["numbers"].toBool(true));
                                  e->setHighlightLine(q["hl"].toBool(true)); e->setWrap(q["wrap"].toBool(true)); e->setTabWidthChars(q["tab"].toInt(4));
                                  e->repaint(); r["gutterWidth"] = e->gutterWidth(); }
        else if (fn == "theme") { e->applyTheme(themeByName(q["name"].toString())); e->repaint(); }
        else { r["ok"] = false; r["error"] = "unknown ed_call " + fn; }
        r["editor"] = cursorInfo(e); r["mode"] = g_mode;
    } else if (cmd == "findbar") {                   // drive the real FindBar through its line edits
        MarkdownEditor* e = editor(); FindBar fb(e); fb.show();
        fb.open(q["replace"].toBool());
        const QList<QLineEdit*> edits = fb.findChildren<QLineEdit*>();
        if (!edits.isEmpty()) edits[0]->setText(q["find"].toString());
        if (edits.size() > 1) edits[1]->setText(q["with"].toString());
        QStringList toks; for (const QJsonValue& v : q["keys"].toArray()) toks << v.toString();
        QWidget* target = (q["target"].toString() == "replace" && edits.size() > 1) ? static_cast<QWidget*>(edits[1]) : (edits.isEmpty() ? static_cast<QWidget*>(e) : static_cast<QWidget*>(edits[0]));
        for (const QString& n : q["toggles"].toVariant().toStringList()) typeTokens(target, {n});
        typeTokens(target, toks);
        QCoreApplication::processEvents();
        QElapsedTimer t; t.start(); while (t.elapsed() < 400) QCoreApplication::processEvents();
        r["editor"] = cursorInfo(e);
        QStringList labels; for (QLabel* l : fb.findChildren<QLabel*>()) labels << l->text(); r["labels"] = arr(labels);
        r["editCount"] = edits.size();

    // ------------------------------------------------------------------ languages
    } else if (cmd == "lang_stock") {
        extern const char* const kStockLangFiles[]; extern const char* const kStockLangJson[];
        QJsonArray a; for (int i = 0; kStockLangFiles[i]; ++i) { QJsonObject j; j["file"] = kStockLangFiles[i]; j["json"] = kStockLangJson[i]; a << j; }
        r["files"] = a;
    } else if (cmd == "lang_ensure") {
        Lang::ensureStock(); r["dir"] = Lang::langDir();
    } else if (cmd == "lang_available") {
        QJsonArray a; for (const Lang::Info& i : Lang::available()) { QJsonObject j; j["code"] = i.code; j["name"] = i.name; j["builtin"] = i.builtin; j["revision"] = i.revision; a << j; }
        r["languages"] = a;
    } else if (cmd == "lang_resolve") {
        r["code"] = Lang::resolve(q["setting"].toString("auto"));
    } else if (cmd == "lang_load") {
        r["loaded"] = Lang::load(q["code"].toString()); r["current"] = Lang::current();
    } else if (cmd == "lang_text") {
        const QString key = q["key"].toString();
        r["text"] = q.contains("n") ? Lang::plural(key, q["n"].toInt()) : Lang::text(key);
    } else if (cmd == "lang_plural_index") {
        r["index"] = Lang::pluralIndex(q["rule"].toString(), q["n"].toInt());
    } else if (cmd == "lang_parse") {
        Lang::Info i; QString err; r["valid"] = Lang::parseInfo(q["json"].toString().toUtf8(), &i, &err); r["error"] = err; r["name"] = i.name;
    } else if (cmd == "settings_grab") {              // the real Settings dialog on page `page`; texts of its labels / buttons / nav list
        AppSettings s; s.load(); QList<Theme> themes = ThemeStore::loadAll();
        SettingsDialog dlg(s, themes); dlg.show();
        QListWidget* nav = dlg.findChild<QListWidget*>("nav");
        if (nav && q.contains("page")) nav->setCurrentRow(q["page"].toInt());
        QElapsedTimer t; t.start(); while (t.elapsed() < 250) QCoreApplication::processEvents();
        QStringList items; if (nav) for (int i = 0; i < nav->count(); ++i) items << nav->item(i)->text();
        QStringList labels; for (QLabel* l : dlg.findChildren<QLabel*>()) if (l->isVisibleTo(&dlg) && !l->text().isEmpty()) labels << l->text();
        r["nav"] = arr(items); r["labels"] = arr(labels); r["title"] = dlg.windowTitle();
        if (q.contains("save")) dlg.grab().save(q["save"].toString());
    } else if (cmd == "win_strings") {                // every menu title / menu item / label / status text of the open window
        if (!g_win) { r["ok"] = false; r["error"] = "no window"; }
        else {
            QStringList menus, items, labels;
            for (QMenuBar* mb : g_win->findChildren<QMenuBar*>()) {
                for (QAction* a : mb->actions()) { menus << a->text(); if (a->menu()) for (QAction* x : a->menu()->actions()) if (!x->isSeparator()) items << x->text(); }
            }
            for (QLabel* l : g_win->findChildren<QLabel*>()) if (!l->text().isEmpty()) labels << l->text();
            r["menus"] = arr(menus); r["items"] = arr(items); r["labels"] = arr(labels);
            r["status"] = g_win->statusBar()->currentMessage();
        }
    // ------------------------------------------------------------------ main window (whole app, offscreen)
    } else if (cmd == "win_open") {
        delete g_win; g_win = new MainWindow(q["file"].toString(), q["nativeFrame"].toBool());
        g_win->resize(1100, 700); g_win->show();
        QElapsedTimer t; t.start(); while (t.elapsed() < q["wait"].toInt(700)) QCoreApplication::processEvents();
        r["title"] = g_win->windowTitle();
        MarkdownEditor* e = g_win->findChild<MarkdownEditor*>();
        r["hasEditor"] = e != nullptr; if (e) r["text"] = e->toPlainText();
        QStringList previews;
        for (QTextBrowser* b : g_win->findChildren<QTextBrowser*>()) previews << b->toPlainText();
        r["previews"] = arr(previews);
        r["visible"] = g_win->isVisible();
        r["size"] = QString("%1x%2").arg(g_win->width()).arg(g_win->height());
    } else if (cmd == "win_keys") {
        QStringList toks; for (const QJsonValue& v : q["keys"].toArray()) toks << v.toString();
        QWidget* target = g_win ? static_cast<QWidget*>(g_win->findChild<MarkdownEditor*>()) : nullptr;
        if (!target) { r["ok"] = false; r["error"] = "no window"; }
        else {
            target->setFocus(); typeTokens(target, toks);
            QElapsedTimer t; t.start(); while (t.elapsed() < q["wait"].toInt(500)) QCoreApplication::processEvents();
            r["text"] = g_win->findChild<MarkdownEditor*>()->toPlainText();
            QStringList previews; for (QTextBrowser* b : g_win->findChildren<QTextBrowser*>()) previews << b->toPlainText(); r["previews"] = arr(previews);
            r["title"] = g_win->windowTitle();
        }
    } else if (cmd == "win_type") {                  // replace editor text like typing/pasting, wait for the live preview
        MarkdownEditor* e = g_win ? g_win->findChild<MarkdownEditor*>() : nullptr;
        if (!e) { r["ok"] = false; r["error"] = "no window"; }
        else {
            e->selectAll(); e->insertPlainText(q["text"].toString());   // like typing/pasting: marks the document modified
            QElapsedTimer t; t.start(); while (t.elapsed() < q["wait"].toInt(700)) QCoreApplication::processEvents();
            QStringList previews; for (QTextBrowser* b : g_win->findChildren<QTextBrowser*>()) previews << b->toPlainText(); r["previews"] = arr(previews);
            r["title"] = g_win->windowTitle();
        }
    } else if (cmd == "win_grab") {
        if (!g_win) { r["ok"] = false; }
        else { const QImage img = g_win->grab().toImage(); r["w"] = img.width(); r["h"] = img.height();
               QSet<QRgb> colors; for (int y = 0; y < img.height(); y += 7) for (int x = 0; x < img.width(); x += 7) colors.insert(img.pixel(x, y));
               r["distinctColors"] = colors.size();
               if (q.contains("save")) img.save(q["save"].toString()); }
    } else if (cmd == "win_close") {
        if (g_win) { r["closed"] = g_win->close(); delete g_win; g_win = nullptr; }
    } else if (cmd == "ping") {
        r["pong"] = true;
    } else {
        r["ok"] = false; r["error"] = "unknown cmd: " + cmd;
    }
    return r;
}

int main(int argc, char** argv) {
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);
    QApplication::setApplicationName("MakDown");
    // Modal dialogs (binary-file prompt, "Open failed", ...) would block a headless run forever: record and cancel them.
    QTimer dialogKiller;
    QObject::connect(&dialogKiller, &QTimer::timeout, [] {
        if (QWidget* m = QApplication::activeModalWidget()) { g_dialogs << m->windowTitle(); if (auto* d = qobject_cast<QDialog*>(m)) d->reject(); else m->close(); }
    });
    dialogKiller.start(20);
    QApplication::setStyle("Fusion");
    g_makdownDebug = false;
    QTextStream in(stdin);
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        if (line == "quit") break;
        QJsonParseError pe;
        const QJsonDocument d = QJsonDocument::fromJson(line.toUtf8(), &pe);
        g_warnings.clear();
        g_dialogs.clear();
        QJsonObject r;
        if (pe.error != QJsonParseError::NoError) { r["ok"] = false; r["error"] = "bad request json: " + pe.errorString(); }
        else r = handle(d.object());
        r["warnings"] = arr(g_warnings);
        r["dialogs"] = arr(g_dialogs);
        out << QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact)) << '\n';
        out.flush();
    }
    delete g_win; delete g_editor;
    return 0;
}
