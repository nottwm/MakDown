// mainwindow.cpp - wires the editor, preview, menus, settings and the themed "chrome" together.
#include <makdown/ui/mainwindow.h>
#include <makdown/ui/editor.h>
#include <makdown/core/markdown.h>
#include <makdown/ui/settingsdialog.h>
#include <makdown/ui/explorer.h>
#include <makdown/ui/browsertab.h>
#include <makdown/ui/console.h>
#include <makdown/ui/findbar.h>
#include <makdown/ui/gittab.h>
#include <makdown/ui/quickopen.h>
#include <makdown/ui/searchtab.h>
#include <makdown/ui/sidebar.h>
#include <makdown/ui/mediatab.h>
#include <makdown/ui/fx.h>
#include <makdown/core/anim.h>
#include <QPainter>
#include <QPixmap>
#include <QVariantAnimation>
#include <makdown/core/buildinfo.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QMimeData>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QToolButton>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QDir>
#include <QMenu>
#include <QTextFrame>
#include <QTextDocument>
#include <QSignalBlocker>

// ---------------------------------------------------------------------------------------------
// Text shown when MakDown starts without a file - doubles as a feature tour.
// ---------------------------------------------------------------------------------------------
static const char* kWelcome = R"MD(# Welcome to MakDown

Markdown, but **fancier**. Edit on the left, watch it render on the right.

## Try these
- [x] Switch themes in *Settings* (Ctrl+,): GitHub, Windows 11, TTY, opencode, Warm ...
- [ ] Turn on **vimkeys** (Ctrl+Alt+V) and press `Esc`, then `dd`, `yy`, `p`, `u`
- [ ] Toggle paragraph marks (Ctrl+Shift+L) for Word-style ¶ marks
- [ ] Toggle *reveal markers* (Ctrl+Shift+M) to see the hidden `*`, `-`, `#` in the preview

> Tip: clone a preset in Settings, give it a name and make it your own.

### A table
| Key            | Action            |
|----------------|:-----------------:|
| Ctrl+B / Ctrl+I| bold / italic     |
| Ctrl+K         | insert link       |
| Alt+Up/Down    | move line (vim)   |

### Code
```cpp
int main() { return 0; }   // fenced blocks get their own styling
```
)MD";

// ---------------------------------------------------------------------------------------------
// file-type helpers: what opens where
// ---------------------------------------------------------------------------------------------
static bool isWebPagePath(const QString& p) {
    const QString s = QFileInfo(p).suffix().toLower();
    return s == "html" || s == "htm" || s == "xhtml" || s == "svg";
}
static bool isMarkdownPath(const QString& p) {            // an untitled buffer counts as markdown
    if (p.isEmpty()) return true;
    const QString s = QFileInfo(p).suffix().toLower();
    return s.isEmpty() || s == "md" || s == "markdown" || s == "mdown" || s == "mkd" || s == "txt";
}
// binary = a NUL byte, or a lot of control characters, in the first 8 KB
static bool isBinaryFile(const QString& p) {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(8192);
    if (head.contains('\0')) return true;
    int ctl = 0;
    for (const char c : head) { const uchar u = uchar(c); if (u < 0x20 && u != '\t' && u != '\n' && u != '\r' && u != '\f' && u != 0x1b) ++ctl; }
    return !head.isEmpty() && ctl * 100 / head.size() > 10;
}
static QString indexHtmlIn(const QString& dir) {
    for (const char* n : {"index.html", "index.htm"}) {
        const QString p = dir + "/" + n;
        if (QFileInfo::exists(p)) return p;
    }
    return QString();
}

// ---------------------------------------------------------------------------------------------
// PreviewBrowser
// ---------------------------------------------------------------------------------------------
void PreviewBrowser::applyMargins() {
    QTextFrame* root = document()->rootFrame();
    QTextFrameFormat f = root->frameFormat();
    const int side = qMax(24, (viewport()->width() - pageW_) / 2);
    if (f.leftMargin() == side && f.rightMargin() == side) return;      // avoid relayout loops
    f.setLeftMargin(side); f.setRightMargin(side); f.setTopMargin(24); f.setBottomMargin(48);
    root->setFrameFormat(f);
}

// ---------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------
MainWindow::MainWindow(const QString& fileToOpen) {
    s_.load();
    ThemeStore::ensureStock();
    themes_ = ThemeStore::loadAll();
    SyntaxStore::ensureStock();
    syntax_ = SyntaxStore::loadAll();
    mdLog(QString("syntax folder: %1 (%2 packs loaded)").arg(SyntaxStore::syntaxDir()).arg(syntax_.size()));
    mdLog(QString("presets folder: %1 (%2 themes loaded)").arg(ThemeStore::presetsDir()).arg(themes_.size()));

    setAcceptDrops(true);
    resize(1280, 800);
    buildUi();
    buildMenus();
    if (!s_.geometry.isEmpty()) restoreGeometry(QByteArray::fromBase64(s_.geometry.toLatin1()));
    applyAll();

    QString root = s_.folder;
    if (root.isEmpty() || !QFileInfo(root).isDir())
        root = fileToOpen.isEmpty() ? QDir::currentPath() : QFileInfo(fileToOpen).absolutePath();
    explorer_->setRoot(root);
    s_.folder = explorer_->root();

    if (!fileToOpen.isEmpty() && isPreviewable(fileToOpen)) {            // an image / sound on the command line: preview it
        syncExplorerFor(fileToOpen);
        editor_->setPlainText(QString::fromUtf8(kWelcome)); editor_->document()->setModified(false);
        openMedia(fileToOpen);
    }
    else if (!fileToOpen.isEmpty() && isBinaryFile(fileToOpen)) {           // ask before dumping a binary into the editor
        syncExplorerFor(fileToOpen);
        editor_->setPlainText(QString::fromUtf8(kWelcome)); editor_->document()->setModified(false);
        openAnyFile(fileToOpen);
    }
    else if (!fileToOpen.isEmpty()) { syncExplorerFor(fileToOpen); openFile(fileToOpen); }
    else if (!s_.lastFile.isEmpty() && QFile::exists(s_.lastFile)) openFile(s_.lastFile);
    else { editor_->setPlainText(QString::fromUtf8(kWelcome)); editor_->document()->setModified(false); }
    renderPreview();
    updateTitle();
    console_->setWorkingDirectory(explorer_->root());
    if (s_.console) showConsole(true);
}

void MainWindow::buildUi() {
    auto* central = new QWidget; central->setObjectName("root");
    auto* v = new QVBoxLayout(central); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);

    // top chrome bar (content depends on the theme, see buildChrome)
    chromeBar_ = new QFrame; chromeBar_->setObjectName("chromeBar");
    new QHBoxLayout(chromeBar_);
    v->addWidget(chromeBar_);

    // editor pane = header + editor + (hidden) vim command line
    editorPane_ = new QWidget;
    auto* ev = new QVBoxLayout(editorPane_); ev->setContentsMargins(0, 0, 0, 0); ev->setSpacing(0);
    editorHeader_ = new QLabel; editorHeader_->setObjectName("paneHeader");
    editor_ = new MarkdownEditor; editor_->setObjectName("editor");
    cmdLine_ = new QLineEdit; cmdLine_->setObjectName("cmdline"); cmdLine_->hide();
    findBar_ = new FindBar(editor_);
    ev->addWidget(editorHeader_); ev->addWidget(findBar_); ev->addWidget(editor_, 1); ev->addWidget(cmdLine_);

    // preview pane = header + browser
    previewPane_ = new QWidget;
    auto* pv = new QVBoxLayout(previewPane_); pv->setContentsMargins(0, 0, 0, 0); pv->setSpacing(0);
    previewHeader_ = new QLabel; previewHeader_->setObjectName("paneHeader");
    preview_ = new PreviewBrowser; preview_->setObjectName("preview");
    preview_->setOpenLinks(false);                  // we route clicks ourselves (anchors, local files, web)
    connect(preview_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        auto jump = [this](const QString& frag) {        // #heading-slug -> scroll the preview there
            preview_->scrollToAnchor(QUrl::fromPercentEncoding(frag.toUtf8()).toLower());
        };
        const QString scheme = url.scheme();
        if (scheme.isEmpty() && url.path().isEmpty()) { if (url.hasFragment()) jump(url.fragment()); return; }   // "#section"
        if (scheme.isEmpty() || scheme == "file") {      // relative or absolute local file, optionally with #section
            const QString raw = scheme == "file" ? url.toLocalFile() : QUrl::fromPercentEncoding(url.path().toUtf8());
            const QString abs = QFileInfo(raw).isAbsolute() ? raw : QFileInfo(file_).absoluteDir().filePath(raw);
            if (QFileInfo::exists(abs)) {
                openAnyFile(abs);
                if (url.hasFragment()) { const QString f = url.fragment(); QTimer::singleShot(0, this, [jump, f] { jump(f); }); }
            } else statusBar()->showMessage("File not found: " + abs, 4000);
            return;
        }
        QDesktopServices::openUrl(url);                  // http(s), mailto ...
    });
    preview_->setFrameShape(QFrame::NoFrame);
    pv->addWidget(previewHeader_); pv->addWidget(preview_, 1);

    // right side = tabs: the markdown preview (tab 0, not closable) plus any number of browser tabs.
    // The tab bar stays hidden while only the preview exists, so the default look is unchanged.
    rightTabs_ = new QTabWidget;
    rightTabs_->setDocumentMode(true);
    rightTabs_->setTabsClosable(true);
    rightTabs_->setTabBarAutoHide(true);
    rightTabs_->addTab(previewPane_, "Preview");
    if (QTabBar* bar = rightTabs_->findChild<QTabBar*>()) bar->setTabButton(0, QTabBar::RightSide, nullptr);
    connect(rightTabs_, &QTabWidget::tabCloseRequested, this, [this](int i) {
        if (i <= 0) return;
        QWidget* w = rightTabs_->widget(i);
        rightTabs_->removeTab(i);
        w->deleteLater();
    });

    connect(rightTabs_, &QTabWidget::currentChanged, this, [this](int) {
        QWidget* w = rightTabs_->currentWidget();
        if (shown_ && w && !qobject_cast<BrowserTab*>(w)) fx::fadeIn(w, 170);     // not on web views: effects break their painting
    });

    split_ = new QSplitter(Qt::Horizontal);
    split_->addWidget(editorPane_); split_->addWidget(rightTabs_);
    split_->setChildrenCollapsible(false);
    split_->setSizes({640, 640});

    // explorer on the left of everything
    sidebar_ = new Sidebar([this] { return explorer_ ? explorer_->root() : QString(); });
    explorer_ = sidebar_->explorer();
    sidebar_->setMinimumWidth(240);
    connect(sidebar_->search(), &SearchTab::openRequested, this, [this](const QString& p, int l, int c, int n) { openFileAt(p, l, c, n); });
    connect(sidebar_->quick(), &QuickOpen::fileChosen, this, [this](const QString& path, int line) {
        openAnyFile(path);
        if (line > 0 && QFileInfo(path).absoluteFilePath() == file_) editor_->gotoLine(line);
        if (QFileInfo(path).absoluteFilePath() == file_) editor_->setFocus();
    });
    connect(sidebar_->git(), &GitTab::openFolderRequested, this, [this](const QString& d) { openFolder(d); });
    connect(explorer_, &FileExplorer::fileActivated, this, [this](const QString& p) { openAnyFile(p); });
    connect(explorer_, &FileExplorer::previewRequested, this, [this](const QString& p) { openMedia(p); });
    connect(explorer_, &FileExplorer::browserRequested, this, [this](const QString& p) { openInBrowser(p); });
    connect(explorer_, &FileExplorer::openFolderRequested, this, [this] { chooseFolder(); });
    connect(explorer_, &FileExplorer::pathRenamed, this, [this](const QString& a, const QString& b) { onPathRenamed(a, b); });
    connect(explorer_, &FileExplorer::pathRemoved, this, [this](const QString& p) { onPathRemoved(p); });
    outer_ = new QSplitter(Qt::Horizontal);
    outer_->addWidget(sidebar_); outer_->addWidget(split_);
    outer_->setChildrenCollapsible(false);
    outer_->setStretchFactor(0, 0); outer_->setStretchFactor(1, 1);
    outer_->setSizes({300, 980});
    console_ = new ConsolePanel; console_->setMinimumHeight(80); console_->hide();
    connect(console_, &ConsolePanel::closeRequested, this, [this] { s_.console = false; applyAll(); s_.save(); });
    vsplit_ = new QSplitter(Qt::Vertical);
    vsplit_->addWidget(outer_); vsplit_->addWidget(console_);
    vsplit_->setChildrenCollapsible(false);
    vsplit_->setStretchFactor(0, 1); vsplit_->setStretchFactor(1, 0);
    v->addWidget(vsplit_, 1);
    setCentralWidget(central);
    fx::smoothScroll(editor_); fx::smoothScroll(preview_);       // glide on the wheel at "Entertain me"

    // status bar: [vim mode]  ...........  Ln/Col   words   theme
    modeLabel_ = new QLabel; modeLabel_->setObjectName("modeLabel");
    posLabel_ = new QLabel; themeLabel_ = new QLabel;
    statusBar()->addWidget(modeLabel_);
    statusBar()->addPermanentWidget(posLabel_);
    statusBar()->addPermanentWidget(themeLabel_);
    statusBar()->setSizeGripEnabled(false);

    // ---- signal wiring
    renderTimer_.setSingleShot(true); renderTimer_.setInterval(130);       // debounce live preview
    connect(&renderTimer_, &QTimer::timeout, this, [this] { renderPreview(); });
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] { if (s_.livePreview) renderTimer_.start(); });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateStatus(); });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, [this](bool) { updateTitle(); });
    connect(editor_, &MarkdownEditor::vimModeChanged, this, [this](const QString& t) {
        modeLabel_->setText(t); modeLabel_->setVisible(!t.isEmpty());
    });
    connect(editor_, &MarkdownEditor::escapePressed, this, [this] { if (findBar_->isVisible()) findBar_->close(); });
    connect(findBar_, &FindBar::message, this, [this](const QString& t) { statusBar()->showMessage(t, 3000); });
    connect(editor_, &MarkdownEditor::commandRequested, this, [this](const QString& p) { showCommandLine(p); });
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (!s_.syncScroll || s_.viewMode == 0) return;
        QScrollBar* es = editor_->verticalScrollBar(); QScrollBar* ps = preview_->verticalScrollBar();
        if (es->maximum() > 0) ps->setValue(int(double(v) / es->maximum() * ps->maximum()));
    });
    connect(cmdLine_, &QLineEdit::returnPressed, this, [this] {
        const QString text = cmdLine_->text(); cmdLine_->hide(); editor_->setFocus(); runCommand(text);
    });
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), cmdLine_);      // Esc closes the command line
    esc->setContext(Qt::WidgetShortcut);
    connect(esc, &QShortcut::activated, this, [this] { cmdLine_->hide(); editor_->setFocus(); });
}

void MainWindow::buildMenus() {
    // tiny helper: add an action with optional shortcut and handler
    auto add = [this](QMenu* m, const QString& text, const QKeySequence& key, auto fn) {
        QAction* a = m->addAction(text); if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn); return a;
    };
    auto check = [this](QMenu* m, const QString& text, const QKeySequence& key, bool* target, auto after) {
        QAction* a = m->addAction(text); a->setCheckable(true); a->setChecked(*target);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::toggled, this, [this, target, after](bool on) { *target = on; applyAll(); s_.save(); after(); });
        return a;
    };

    QMenu* file = menuBar()->addMenu("&File");
    add(file, "&New", QKeySequence::New, [this] { newFile(); });
    add(file, "&Open...", QKeySequence::Open, [this] {
        const QString p = QFileDialog::getOpenFileName(this, "Open file", QString(), "Markdown (*.md *.markdown *.txt);;Images and sound (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.ico *.mp3 *.wav *.ogg *.opus *.flac *.m4a *.aac);;Web files (*.html *.htm *.css *.js);;Source code (*.c *.h *.cpp *.hpp *.rs *.sh *.bat *.cmd *.ld *.mk Makefile);;All files (*)");
        if (!p.isEmpty()) openAnyFile(p);
    });
    add(file, "Open &Folder...", QKeySequence("Ctrl+Shift+O"), [this] { chooseFolder(); });
    file->addSeparator();
    add(file, "&Save", QKeySequence::Save, [this] { saveFile(); });
    add(file, "Save &As...", QKeySequence::SaveAs, [this] { saveFileAs(); });
    add(file, "&Export HTML...", QKeySequence(), [this] { exportHtml(); });
    file->addSeparator();
    add(file, "&Quit", QKeySequence::Quit, [this] { close(); });

    QMenu* edit = menuBar()->addMenu("&Edit");
    add(edit, "&Undo", QKeySequence::Undo, [this] { editor_->undo(); });
    add(edit, "&Redo", QKeySequence::Redo, [this] { editor_->redo(); });
    edit->addSeparator();
    add(edit, "&Bold", QKeySequence("Ctrl+B"), [this] { editor_->wrapSelection("**", "**"); });
    add(edit, "&Italic", QKeySequence("Ctrl+I"), [this] { editor_->wrapSelection("*", "*"); });
    add(edit, "Stri&kethrough", QKeySequence("Ctrl+Shift+X"), [this] { editor_->wrapSelection("~~", "~~"); });
    add(edit, "Inline &code", QKeySequence("Ctrl+E"), [this] { editor_->wrapSelection("`", "`"); });
    add(edit, "Insert &link", QKeySequence("Ctrl+K"), [this] { editor_->insertLink(); });
    edit->addSeparator();
    add(edit, "Heading level up", QKeySequence("Ctrl+Alt+Up"), [this] { editor_->changeHeading(+1); });
    add(edit, "Heading level down", QKeySequence("Ctrl+Alt+Down"), [this] { editor_->changeHeading(-1); });
    add(edit, "Bullet list", QKeySequence("Ctrl+Shift+8"), [this] { editor_->toggleLinePrefix("- "); });
    add(edit, "Task list", QKeySequence("Ctrl+Shift+T"), [this] { editor_->toggleLinePrefix("- [ ] "); });
    add(edit, "Quote", QKeySequence("Ctrl+Shift+9"), [this] { editor_->toggleLinePrefix("> "); });

    // Search: with vim keys off Ctrl+F / Ctrl+H / F3 drive the VS Code style find bar; with vim keys on Ctrl+F opens the "/" line
    QMenu* srch = menuBar()->addMenu("&Search");
    add(srch, "&Find", QKeySequence::Find, [this] {
        if (!editorPane_->isVisible()) return;
        if (s_.vim) showCommandLine("/"); else findBar_->open(false);
    });
    add(srch, "&Replace", QKeySequence("Ctrl+H"), [this] { if (!s_.vim && editorPane_->isVisible()) findBar_->open(true); });
    add(srch, "Find &next", QKeySequence::FindNext, [this] { if (s_.vim) editor_->findNext(editor_->lastSearch()); else findBar_->step(false); });
    add(srch, "Find &previous", QKeySequence::FindPrevious, [this] { if (s_.vim) editor_->findNext(editor_->lastSearch(), true); else findBar_->step(true); });
    srch->addSeparator();
    add(srch, "Go to &file...", QKeySequence("Ctrl+P"), [this] { quickOpen(); });
    add(srch, "Find in f&iles...", QKeySequence("Ctrl+Shift+F"), [this] {
        const QTextCursor c = editor_->textCursor();
        const QString sel = c.hasSelection() && !c.selectedText().contains(QChar::ParagraphSeparator) ? c.selectedText() : QString();
        openSearchTab(sel);
    });

    QMenu* view = menuBar()->addMenu("&View");
    auto* group = new QActionGroup(this);
    auto mkView = [&](const QString& text, const QString& key, int mode) {
        QAction* a = view->addAction(text); a->setCheckable(true); a->setShortcut(QKeySequence(key)); group->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode] { setViewMode(mode); });
        return a;
    };
    actEditor_ = mkView("Editor only", "Ctrl+1", 0);
    actSplit_ = mkView("Split view", "Ctrl+2", 1);
    actPreview_ = mkView("Preview only", "Ctrl+3", 2);
    actExplorer_ = check(view, "Side bar (files, search, git)", QKeySequence("Ctrl+Shift+E"), &s_.explorer, [] {});
    actConsole_  = check(view, "Console", QKeySequence("Ctrl+`"), &s_.console, [] {});
    view->addSeparator();
    actMarks_   = check(view, "Paragraph marks (¶)", QKeySequence("Ctrl+Shift+L"), &s_.paragraphMarks, [] {});
    actSpaces_  = check(view, "Show spaces && tabs", QKeySequence(), &s_.showSpaces, [] {});
    actReveal_  = check(view, "Reveal markdown markers in preview", QKeySequence("Ctrl+Shift+M"), &s_.revealMarkers, [this] { renderPreview(); });
    actNumbers_ = check(view, "Line numbers", QKeySequence(), &s_.lineNumbers, [] {});
    view->addSeparator();
    actVim_     = check(view, "Vim keys", QKeySequence("Ctrl+Alt+V"), &s_.vim, [] {});
    add(view, "Refresh preview", QKeySequence(Qt::Key_F5), [this] { renderPreview(); });
    add(view, "Full screen", QKeySequence(Qt::Key_F11), [this] { isFullScreen() ? showNormal() : showFullScreen(); });

    QMenu* web = menuBar()->addMenu("&Browser");
    add(web, "Open page in &browser tab", QKeySequence("Ctrl+Shift+B"), [this] { openInBrowser(); });
    add(web, "&New browser tab", QKeySequence("Ctrl+T"), [this] { newBrowserTab(); });
    web->addSeparator();
    actLive_ = check(web, "&Live reload (watches .html / .js / .css)", QKeySequence(), &s_.liveReload, [] {});

    QMenu* git = menuBar()->addMenu("&Git");
    add(git, "&Clone repository...", QKeySequence("Ctrl+Shift+G"), [this] { openGitTab(); });

    QMenu* set = menuBar()->addMenu("&Settings");
    add(set, "&Preferences...", QKeySequence("Ctrl+,"), [this] { openSettings(); });

    QMenu* help = menuBar()->addMenu("&Help");
    add(help, "&About MakDown", QKeySequence(), [this] { showAbout(); });
    add(help, "&Build info", QKeySequence(), [this] { QMessageBox::information(this, "Build info", makdownBuildInfo()); });
}

// ---------------------------------------------------------------------------------------------
// Chrome: the part of the UI that gives each theme its own personality
// ---------------------------------------------------------------------------------------------
void MainWindow::buildChrome() {
    auto* hl = static_cast<QHBoxLayout*>(chromeBar_->layout());
    while (QLayoutItem* it = hl->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }

    const QString ch = theme_.chrome;
    const QString name = (file_.isEmpty() ? QString("untitled.md") : QFileInfo(file_).fileName()).toHtmlEscaped();
    const QString accent = theme_.color("accent"), muted = theme_.color("muted"), link = theme_.color("link");
    hl->setContentsMargins(16, ch == "warm" ? 14 : (ch == "terminal" ? 4 : 8), 16, ch == "warm" ? 14 : (ch == "terminal" ? 4 : 8));
    hl->setSpacing(8);

    auto label = [&](const QString& html) {
        auto* l = new QLabel(html); l->setObjectName("chromeTitle"); l->setTextFormat(Qt::RichText); hl->addWidget(l); return l;
    };
    if (ch == "github") {                    // breadcrumb like a repository file path
        label(QString("<span style='color:%1'>MakDown</span> <span style='color:%2'>/</span> <b>%3</b>").arg(link, muted, name));
        QString kind = "Markdown";                       // language badge next to the file name
        if (const SyntaxPtr pack = SyntaxStore::forFile(syntax_, file_)) kind = pack->name;
        else if (!isMarkdownDoc(file_)) kind = QFileInfo(file_).suffix().isEmpty() ? QString("Text") : QFileInfo(file_).suffix().toUpper();
        auto* pill = new QLabel(kind); pill->setObjectName("pill"); hl->addWidget(pill);
    } else if (ch == "win11") {              // title-bar style: glyph + file + app name
        label(QString("<span style='color:%1'>&#9998;</span>&nbsp; <b>%2</b> <span style='color:%3'>&mdash; MakDown</span>").arg(accent, name, muted));
    } else if (ch == "vscode") {             // editor-tab style: file icon + name, like the tab above a VS Code editor
        label(QString("<span style='color:%1'>&#9632;</span>&nbsp; %2 <span style='color:%3'>&mdash; MakDown</span>").arg(accent, name, muted));
    } else if (ch == "glass") {              // a floating capsule title
        label(QString("<span style='color:%1; font-size:13pt'>&#9673;</span>&nbsp; <b>%2</b> <span style='color:%3'>&mdash; MakDown</span>").arg(accent, name, muted));
    } else if (ch == "terminal") {           // a shell prompt
        label(QString("<span style='color:%1'><b>$</b></span> makdown <span style='color:%2'>--open</span> %3").arg(accent, muted, name));
    } else if (ch == "warm") {             // warm, spacious masthead
        label(QString("<span style=\"font-family:%1; font-size:16pt\">MakDown</span>&nbsp;&nbsp;"
                      "<span style='color:%2'>%3</span>").arg(ThemeStore::cssFamily(theme_.previewFont, '\''), muted, name));
    } else {
        label(QString("<b>MakDown</b> <span style='color:%1'>%2</span>").arg(muted, name));
    }
    hl->addStretch(1);

    // compact icon-only controls: [editor | split | preview]  [console]  (settings live in the Settings menu, Ctrl+,)
    const QColor off(theme_.color(ch == "terminal" ? "fg" : "muted")),
                 on(theme_.color(ch == "terminal" ? "bg" : (ch == "win11" || ch == "github" || ch == "glass" ? "accent" : "fg")));
    auto icon = [&](int kind) {                  // 0 editor, 1 split, 2 preview, 3 console - drawn so they follow the theme
        QIcon ic;
        for (int state = 0; state < 2; ++state) {
            QPixmap pm(36, 28); pm.setDevicePixelRatio(2.0); pm.fill(Qt::transparent);
            QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
            QColor c = state ? on : off;
            QPen pen(c, 1.3); pen.setJoinStyle(Qt::RoundJoin); pen.setCapStyle(Qt::RoundCap); p.setPen(pen);
            const QRectF r(1.5, 1.5, 15, 11);
            if (kind == 3) {                     // ">_"
                p.drawPolyline(QPolygonF({QPointF(3.5, 4), QPointF(7.5, 7), QPointF(3.5, 10)}));
                p.drawLine(QPointF(9.5, 10.2), QPointF(14, 10.2));
            } else {
                p.drawRoundedRect(r, 2, 2);
                if (kind == 0) { for (int y : {4, 7, 10}) p.drawLine(QPointF(4, y), QPointF(y == 10 ? 9 : 14, y)); }
                else if (kind == 1) {
                    p.drawLine(QPointF(9, 1.5), QPointF(9, 12.5));
                    p.drawLine(QPointF(3.8, 5), QPointF(7.2, 5)); p.drawLine(QPointF(3.8, 8), QPointF(7.2, 8));
                    p.setBrush(c); p.setPen(Qt::NoPen); p.drawRect(QRectF(10.6, 3.6, 4.2, 6.6));
                } else { p.setBrush(c); p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(4, 4, 10, 6), 1, 1); }
            }
            ic.addPixmap(pm, QIcon::Normal, state ? QIcon::On : QIcon::Off);
        }
        return ic;
    };
    auto button = [&](QAction* a, int kind) {
        a->setIcon(icon(kind));
        auto* b = new QToolButton; b->setDefaultAction(a); b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(18, 14)); b->setStyleSheet("QToolButton { padding: 4px 7px; }");
        QString tip = a->text(); tip.remove('&');
        if (!a->shortcut().isEmpty()) tip += "  (" + a->shortcut().toString(QKeySequence::NativeText) + ")";
        b->setToolTip(tip);
        return b;
    };
    auto* seg = new QWidget; auto* sl = new QHBoxLayout(seg); sl->setContentsMargins(0, 0, 0, 0); sl->setSpacing(1);
    int k = 0;
    for (QAction* a : {actEditor_, actSplit_, actPreview_}) sl->addWidget(button(a, k++));
    hl->addWidget(seg);
    hl->addSpacing(6);
    hl->addWidget(button(actConsole_, 3));
}

void MainWindow::updatePaneHeaders() {
    const QString ch = theme_.chrome;
    const QString name = file_.isEmpty() ? "untitled.md" : QFileInfo(file_).fileName();
    QString e, p;
    if (ch == "terminal")      { e = "┌─[ editor: " + name + " ]";  p = "┌─[ preview ]"; }
    else if (ch == "warm")   { e = "\u2726  Draft";               p = "\u2726  Preview"; }
    else if (ch == "github")   { e = "Edit  \u00B7  " + name;       p = "\u2630  Preview"; }
    else if (ch == "vscode")   { e = name;                          p = "Preview " + name; }
    else                       { e = "Editor";                      p = "Preview"; }
    editorHeader_->setText(e); previewHeader_->setText(p);
}

// ---------------------------------------------------------------------------------------------
// applying theme + settings
// ---------------------------------------------------------------------------------------------
Theme MainWindow::currentTheme() const {
    for (const Theme& t : themes_) if (t.name == s_.theme) return t;
    return themes_.isEmpty() ? ThemeStore::stockThemes().first() : themes_.first();
}

void MainWindow::applyAll() {
    Anim::setLevel(s_.animations);
    if (shown_ && !appliedTheme_.isEmpty() && appliedTheme_ != s_.theme) fx::crossfade(centralWidget());   // old look fades into the new one
    appliedTheme_ = s_.theme;
    theme_ = currentTheme();
    mdLog(QString("apply theme '%1' (chrome=%2, radius=%3)").arg(theme_.name, theme_.chrome).arg(theme_.radius));

    // application-wide palette (so native dialogs / message boxes follow the theme) + QSS + font
    QPalette pal;
    const auto C = [this](const char* k) { return QColor(theme_.color(k)); };
    pal.setColor(QPalette::Window, C("bg"));            pal.setColor(QPalette::WindowText, C("fg"));
    pal.setColor(QPalette::Base, C("editorBg"));        pal.setColor(QPalette::AlternateBase, C("panel"));
    pal.setColor(QPalette::Text, C("fg"));              pal.setColor(QPalette::Button, C("panel"));
    pal.setColor(QPalette::ButtonText, C("fg"));        pal.setColor(QPalette::Highlight, C("accent"));
    pal.setColor(QPalette::HighlightedText, C("accentFg")); pal.setColor(QPalette::ToolTipBase, C("panel"));
    pal.setColor(QPalette::ToolTipText, C("fg"));       pal.setColor(QPalette::PlaceholderText, C("muted"));
    pal.setColor(QPalette::Link, C("link"));
    qApp->setPalette(pal);
    qApp->setFont(ThemeStore::makeFont(theme_.uiFont, theme_.uiSize));
    qApp->setStyleSheet(ThemeStore::styleSheet(theme_));

    // Liquid Glass floats its panels as separate tiles with gaps; every other theme is edge to edge
    if (centralWidget() && centralWidget()->layout()) {
        const bool glass = theme_.chrome == "glass";
        auto* lay = centralWidget()->layout();
        lay->setContentsMargins(glass ? 12 : 0, glass ? 8 : 0, glass ? 12 : 0, glass ? 6 : 0);
        lay->setSpacing(glass ? 8 : 0);
    }
    update();

    // editor
    editor_->applyTheme(theme_);
    editor_->setVimEnabled(s_.vim);
    if (s_.vim && findBar_->isVisible()) findBar_->close();      // vim has its own "/" search
    editor_->setLineNumbers(s_.lineNumbers);
    editor_->setShowParagraphMarks(s_.paragraphMarks);
    editor_->setShowSpaces(s_.showSpaces);
    editor_->setWrap(s_.wrap);
    editor_->setHighlightLine(s_.highlightLine);
    editor_->setTabWidthChars(s_.tabWidth);
    modeLabel_->setVisible(s_.vim);

    // preview
    preview_->setFont(ThemeStore::makeFont(theme_.previewFont, theme_.previewSize));
    preview_->setPageWidth(theme_.pageWidth);
    preview_->document()->setDefaultStyleSheet(previewCss(theme_));

    themeLabel_->setText(theme_.name + "  ");
    explorer_->setAnimated(Anim::on());
    sidebar_->applyTheme(theme_);           // folders expand / collapse smoothly
    showExplorer(s_.explorer);
    console_->applyTheme(theme_);
    showConsole(s_.console);
    for (BrowserTab* t : rightTabs_->findChildren<BrowserTab*>()) t->setLiveReload(s_.liveReload);
    buildChrome();
    updatePaneHeaders();
    syncActions();
    setViewMode(s_.viewMode);
    renderPreview();
    updateStatus();
}

void MainWindow::syncActions() {            // menu check marks follow AppSettings (without re-triggering)
    struct P { QAction* a; bool v; };
    for (const P& p : {P{actMarks_, s_.paragraphMarks}, P{actSpaces_, s_.showSpaces}, P{actReveal_, s_.revealMarkers},
                       P{actNumbers_, s_.lineNumbers}, P{actVim_, s_.vim},
                       P{actExplorer_, s_.explorer}, P{actLive_, s_.liveReload}, P{actConsole_, s_.console}}) {
        QSignalBlocker b(p.a); p.a->setChecked(p.v);
    }
}

void MainWindow::setViewMode(int mode) {
    s_.viewMode = qBound(0, mode, 2);
    const bool hadEditor = !editorPane_->isHidden(), hadRight = !rightTabs_->isHidden();
    editorPane_->setVisible(s_.viewMode != 2);
    rightTabs_->setVisible(s_.viewMode != 0);
    if (shown_) {                                // the pane that just appeared fades in
        if (!hadEditor && !editorPane_->isHidden()) fx::fadeIn(editorPane_, 200);
        if (!hadRight && !rightTabs_->isHidden() && !qobject_cast<BrowserTab*>(rightTabs_->currentWidget())) fx::fadeIn(rightTabs_, 200);
    }
    QAction* a = s_.viewMode == 0 ? actEditor_ : (s_.viewMode == 1 ? actSplit_ : actPreview_);
    QSignalBlocker b(a); a->setChecked(true);
    s_.save();
}

// ---------------------------------------------------------------------------------------------
// preview
// ---------------------------------------------------------------------------------------------
void MainWindow::renderPreview() {
    const QString text = editor_->toPlainText();
    const QString base = file_.isEmpty() ? QDir::currentPath() : QFileInfo(file_).absolutePath();
    const int scroll = preview_->verticalScrollBar()->value();            // keep the reading position
    if (isMarkdownDoc(file_))
        preview_->setHtml(renderMarkdownToHtml(text, theme_, s_.revealMarkers, base));
    else
        preview_->setHtml(QString("<html><body style=\"color:%1\"><p>Not a markdown file, so there is nothing to render here.</p>"
                                  "<p>For web pages use <b>Browser &gt; Open page in browser tab</b> (Ctrl+Shift+B): it reloads "
                                  "whenever you save the .html, .js or .css.</p></body></html>").arg(theme_.color("muted")));
    preview_->applyMargins();
    preview_->verticalScrollBar()->setValue(scroll);
    wordCount_ = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
    updateStatus();
}

void MainWindow::updateStatus() {
    const QTextCursor c = editor_->textCursor();
    posLabel_->setText(QString("Ln %1, Col %2   \u00B7   %3 words   ").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1).arg(wordCount_));
}

// ---------------------------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------------------------
void MainWindow::updateTitle() {
    const QString n = file_.isEmpty() ? "untitled.md" : QFileInfo(file_).fileName();
    setWindowTitle(n + (editor_->document()->isModified() ? " *" : "") + " \u2014 MakDown");
}

bool MainWindow::maybeSave() {
    if (!editor_->document()->isModified()) return true;
    const auto r = QMessageBox::question(this, "Unsaved changes", "Save changes before continuing?",
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return saveFile();
    return r == QMessageBox::Discard;
}

bool MainWindow::writeTo(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save failed", "Could not write " + path); return false;
    }
    f.write(editor_->toPlainText().toUtf8());
    file_ = path; s_.lastFile = path; s_.save();
    applyHighlighting(path);
    editor_->document()->setModified(false);
    statusBar()->showMessage("Saved " + path, 3000);
    buildChrome(); updatePaneHeaders(); updateTitle(); renderPreview();
    return true;
}
bool MainWindow::saveFile() { return file_.isEmpty() ? saveFileAs() : writeTo(file_); }
bool MainWindow::saveFileAs() {
    const QString p = QFileDialog::getSaveFileName(this, "Save markdown", file_.isEmpty() ? "untitled.md" : file_, "Markdown (*.md);;All files (*)");
    if (p.isEmpty() || !writeTo(p)) return false;
    explorer_->reveal(file_);
    return true;
}

void MainWindow::openFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Open failed", "Could not read " + path); return; }
    const QString abs = QFileInfo(path).absoluteFilePath();
    applyHighlighting(abs);
    editor_->setPlainText(QString::fromUtf8(f.readAll()));
    editor_->document()->setModified(false);
    file_ = abs; s_.lastFile = abs;
    buildChrome(); updatePaneHeaders(); updateTitle(); renderPreview();
    explorer_->reveal(abs);
}

// Which colouring a file gets: a syntax pack wins (so "Makefile" - no extension - is not mistaken for
// markdown), then markdown, else none (.html / .js / .css ...).
bool MainWindow::isMarkdownDoc(const QString& path) const {
    return path.isEmpty() || (!SyntaxStore::forFile(syntax_, path) && isMarkdownPath(path));
}

void MainWindow::applyHighlighting(const QString& path) {
    if (const SyntaxPtr pack = SyntaxStore::forFile(syntax_, path)) editor_->setSyntaxPack(pack);
    else editor_->setMarkdownHighlighting(isMarkdownPath(path));
}

// The explorer renamed a file or folder: if the open file is (inside) it, follow it. The buffer stays as is.
void MainWindow::onPathRenamed(const QString& from, const QString& to) {
    if (file_.isEmpty() || !FileExplorer::contains(from, file_)) return;
    file_ = QDir::cleanPath(to) + QDir::cleanPath(file_).mid(QDir::cleanPath(from).size());
    s_.lastFile = file_; s_.save();
    applyHighlighting(file_);                    // the extension may have changed
    buildChrome(); updatePaneHeaders(); updateTitle(); renderPreview();
    statusBar()->showMessage("Renamed to " + QFileInfo(file_).fileName(), 3000);
}

// The explorer deleted a file or folder: if the open file was in it, keep the text but make it an unsaved buffer.
void MainWindow::onPathRemoved(const QString& path) {
    if (file_.isEmpty() || !FileExplorer::contains(path, file_)) return;
    file_.clear(); s_.lastFile.clear(); s_.save();
    if (!editor_->document()->isEmpty()) editor_->document()->setModified(true);
    applyHighlighting(QString());
    buildChrome(); updatePaneHeaders(); updateTitle(); renderPreview();
    statusBar()->showMessage("The open file was deleted - its text is still here, Save to keep it", 6000);
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    if (shown_) return;
    shown_ = true;
    if (Anim::level() >= Anim::Normal) {         // the window fades in
        setWindowOpacity(0.0);
        fx::tween(this, 0, 100, 260, [this](int v) { setWindowOpacity(v / 100.0); }, {}, false, Anim::Normal);
    }
}

void MainWindow::showExplorer(bool on) {
    if (on == explorerTarget_ && (!sidebar_->isHidden()) == on) return;      // already (going) there
    explorerTarget_ = on;
    if (explorerAnim_) { explorerAnim_->stop(); explorerAnim_ = nullptr; }
    if (!shown_ || !Anim::on()) { sidebar_->setVisible(on); sidebar_->setMinimumWidth(240); return; }
    const int total = outer_->width();
    sidebar_->setMinimumWidth(0);               // so the splitter can squeeze it while it slides
    auto setW = [this, total](int w) { outer_->setSizes({w, qMax(1, total - w)}); };
    if (on) {
        sidebar_->setVisible(true); setW(0);
        explorerAnim_ = fx::tween(this, 0, explorerW_, 220, setW, [this] { sidebar_->setMinimumWidth(240); explorerAnim_ = nullptr; }, true);
    } else {
        explorerW_ = qMax(240, sidebar_->width());
        explorerAnim_ = fx::tween(this, sidebar_->width(), 0, 200, setW,
                                  [this] { sidebar_->setVisible(false); sidebar_->setMinimumWidth(240); explorerAnim_ = nullptr; });
    }
}

void MainWindow::showConsole(bool on) {
    if (on == consoleTarget_ && (!console_->isHidden()) == on) return;
    consoleTarget_ = on;
    if (consoleAnim_) { consoleAnim_->stop(); consoleAnim_ = nullptr; }
    if (on && !explorer_->root().isEmpty()) { console_->setWorkingDirectory(explorer_->root()); console_->ensureStarted(); }
    if (!shown_ || !Anim::on()) {
        console_->setVisible(on);
        if (shown_) { if (on) console_->focusInput(); else editor_->setFocus(); }
        return;
    }
    const int total = vsplit_->height();
    console_->setMinimumHeight(0);
    auto setH = [this, total](int h) { vsplit_->setSizes({qMax(1, total - h), h}); };
    if (on) {
        console_->setVisible(true); setH(0);
        consoleAnim_ = fx::tween(this, 0, consoleH_, 240, setH, [this] { console_->setMinimumHeight(80); consoleAnim_ = nullptr; console_->focusInput(); }, true);
    } else {
        consoleH_ = qMax(120, console_->height());
        consoleAnim_ = fx::tween(this, console_->height(), 0, 200, setH,
                                 [this] { console_->setVisible(false); console_->setMinimumHeight(80); consoleAnim_ = nullptr; editor_->setFocus(); });
    }
}

bool MainWindow::isPreviewable(const QString& path) {
    return MediaTab::kindOf(path) != MediaTab::None && !QFileInfo(path).suffix().toLower().startsWith("svg");
}

void MainWindow::openMedia(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < rightTabs_->count(); ++i)
        if (auto* m = qobject_cast<MediaTab*>(rightTabs_->widget(i)); m && m->path() == abs) { rightTabs_->setCurrentIndex(i); return; }
    auto* t = new MediaTab(abs);
    if (t->kind() == MediaTab::None) { delete t; QDesktopServices::openUrl(QUrl::fromLocalFile(abs)); return; }
    const int idx = rightTabs_->addTab(t, QFileInfo(abs).fileName().replace("&", "&&"));
    rightTabs_->setTabToolTip(idx, abs);
    rightTabs_->setCurrentIndex(idx);
    if (s_.viewMode == 0) setViewMode(1);        // the tabs live on the right: make sure they are shown
    t->setFocus();
}

void MainWindow::newFile() {
    if (!maybeSave()) return;
    applyHighlighting(QString());
    editor_->clear(); editor_->document()->setModified(false);
    file_.clear(); s_.lastFile.clear();
    buildChrome(); updatePaneHeaders(); updateTitle(); renderPreview();
}

void MainWindow::exportHtml() {
    const QString p = QFileDialog::getSaveFileName(this, "Export HTML", "export.html", "HTML (*.html)");
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    const QString base = file_.isEmpty() ? QDir::currentPath() : QFileInfo(file_).absolutePath();
    const QString body = renderMarkdownToHtml(editor_->toPlainText(), theme_, false, base);
    f.write(("<!doctype html><html><head><meta charset=\"utf-8\"><title>MakDown export</title><style>" + previewCss(theme_) +
             "body{background:" + theme_.color("editorBg") + ";max-width:" + QString::number(theme_.pageWidth) + "px;margin:2em auto;}</style></head>" +
             body.mid(body.indexOf("<body>")) + "</html>").toUtf8());
    statusBar()->showMessage("Exported " + p, 3000);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) { e->ignore(); return; }
    s_.geometry = QString::fromLatin1(saveGeometry().toBase64());
    s_.save();
    e->accept();
}
void MainWindow::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) return;
    const QString p = urls.first().toLocalFile();
    if (QFileInfo(p).isDir()) openFolder(p);                 // dropping a folder opens it in the explorer
    else openAnyFile(p);
}

// ---------------------------------------------------------------------------------------------
// dialogs + vim-style command line
// ---------------------------------------------------------------------------------------------
void MainWindow::openSettings() {
    SettingsDialog dlg(s_, themes_, this);
    connect(&dlg, &SettingsDialog::applied, this, [this] { applyAll(); s_.save(); });   // live preview of every change
    dlg.exec();
    applyAll();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About MakDown",
        "<h2>MakDown</h2><p>Markdown editor &amp; previewer, but fancier.</p>"
        "<p>MIT License &copy; 2026 nottwm</p>"
        "<pre>" + makdownBuildInfo().toHtmlEscaped() + "</pre>");
}

void MainWindow::showCommandLine(const QString& prefix) {
    if (!editorPane_->isVisible()) return;
    cmdLine_->setText(prefix); cmdLine_->show(); cmdLine_->setFocus();
}

void MainWindow::runCommand(const QString& text) {
    if (text.size() < 1) return;
    const QChar p = text[0];
    const QString arg = text.mid(1).trimmed();
    if (p == '/') {
        if (!editor_->findNext(arg)) statusBar()->showMessage("Pattern not found: " + arg, 3000);
        return;
    }
    // ---- ":" ex commands
    bool ok = false; const int line = arg.toInt(&ok);
    if (ok)                                   editor_->gotoLine(line);                       // :42
    else if (arg == "w" || arg == "write")    saveFile();
    else if (arg == "wq" || arg == "x")       { if (saveFile()) close(); }
    else if (arg == "q!" )                    { editor_->document()->setModified(false); close(); }
    else if (arg == "q" || arg == "quit") {
        if (editor_->document()->isModified()) statusBar()->showMessage("No write since last change (add ! to override)", 4000);
        else close();
    }
    else if (arg == "set vim" || arg == "set novim") { s_.vim = arg == "set vim"; applyAll(); }
    else if (arg == "set nu"  || arg == "set nonu")  { s_.lineNumbers = arg == "set nu"; applyAll(); }
    else if (arg == "set list" || arg == "set nolist") { s_.paragraphMarks = arg == "set list"; applyAll(); }
    else if (arg.startsWith("theme ")) {
        const QString n = arg.mid(6).trimmed();
        for (const Theme& t : themes_) if (t.name.compare(n, Qt::CaseInsensitive) == 0) { s_.theme = t.name; applyAll(); return; }
        statusBar()->showMessage("Unknown theme: " + n, 3000);
    }
    else statusBar()->showMessage("Not an editor command: " + arg, 3000);
}

// ---------------------------------------------------------------------------------------------
// explorer + browser tabs
// ---------------------------------------------------------------------------------------------
void MainWindow::chooseFolder() {
    const QString d = QFileDialog::getExistingDirectory(this, "Open folder", explorer_->root());
    if (!d.isEmpty()) openFolder(d);
}

void MainWindow::openFolder(const QString& dir) {
    explorer_->setRoot(dir);
    sidebar_->rootChanged();
    s_.folder = explorer_->root();
    s_.explorer = true;                          // opening a folder implies wanting to see it
    applyAll();
    s_.save();
}

// A file named on the command line should be visible in the tree: if it lies outside the saved root,
// move the root to its folder.
void MainWindow::syncExplorerFor(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (FileExplorer::contains(explorer_->root(), abs)) return;
    explorer_->setRoot(QFileInfo(abs).absolutePath());
    sidebar_->rootChanged();
    s_.folder = explorer_->root();
}

// Explorer double-click. Text goes to the editor (html / js / css included; use "Open in browser tab"
// to view a page); anything else (images, pdf, archives...) is handed to the system.
void MainWindow::openAnyFile(const QString& path) {
    if (isPreviewable(path)) { openMedia(path); return; }                 // images / sound: preview tab, like VS Code
    if (QFileInfo(path).size() > 20 * 1024 * 1024) {
        QMessageBox::information(this, "MakDown", "That file is larger than 20 MB; not opening it in the editor.");
        return;
    }
    if (isBinaryFile(path)) {                    // warn: the text editor would show garbage and saving would corrupt the file
        QMessageBox box(QMessageBox::Warning, "Binary file", "\"" + QFileInfo(path).fileName() + "\" looks like a binary file.", QMessageBox::NoButton, this);
        box.setInformativeText("Opening it in the text editor shows unreadable characters, and saving it from here would damage it.");
        QPushButton* sys = box.addButton("Open with default app", QMessageBox::AcceptRole);
        QPushButton* ed = box.addButton("Open in editor anyway", QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(sys);
        box.exec();
        if (box.clickedButton() == sys) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        if (box.clickedButton() != ed) return;
    }
    if (!maybeSave()) return;
    openFile(path);
}

BrowserTab* MainWindow::addBrowserTab() {
    auto* tab = new BrowserTab;
    tab->setLiveReload(s_.liveReload);
    const int idx = rightTabs_->addTab(tab, "Browser");
    connect(tab, &BrowserTab::titleChanged, this, [this, tab](const QString& title) {
        const int i = rightTabs_->indexOf(tab);
        if (i < 0) return;
        QString t = title.size() > 22 ? title.left(21) + "\u2026" : title;
        rightTabs_->setTabText(i, t.isEmpty() ? QString("Browser") : t.replace("&", "&&"));
    });
    connect(tab, &BrowserTab::liveToggled, this, [this](bool on) {
        s_.liveReload = on;                      // the Live button is global: applyAll() syncs the other tabs
        applyAll();
        s_.save();
    });
    rightTabs_->setCurrentIndex(idx);
    if (s_.viewMode == 0) setViewMode(1);        // the tabs live on the right: make sure they are shown
    return tab;
}

void MainWindow::revealSidebar() {
    if (!s_.explorer) { s_.explorer = true; applyAll(); s_.save(); }
}

void MainWindow::openSearchTab(const QString& seed) { revealSidebar(); sidebar_->showTextSearch(seed); }
void MainWindow::quickOpen() { revealSidebar(); sidebar_->showFileSearch(); }

// Opens a text file (unless it is the current one) and selects the match at line / col / len (line 0: just open it).
void MainWindow::openFileAt(const QString& path, int line, int col, int len) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (abs != file_) {
        if (!maybeSave()) return;
        openFile(abs);
        if (file_ != abs) return;                // could not be read (a warning was shown)
    }
    if (editorPane_->isHidden()) setViewMode(1);
    if (line > 0) {
        const QTextBlock b = editor_->document()->findBlockByNumber(line - 1);
        if (b.isValid()) {
            QTextCursor c(b);
            const int start = b.position() + qMin(col, qMax(0, b.length() - 1));
            c.setPosition(start); c.setPosition(qMin(start + len, b.position() + b.length() - 1), QTextCursor::KeepAnchor);
            editor_->setTextCursor(c);
            editor_->centerCursor();
        }
    }
    editor_->setFocus();
}

void MainWindow::openGitTab() { revealSidebar(); sidebar_->showPage(Sidebar::Git); }

void MainWindow::newBrowserTab() {
    if (!BrowserTab::available()) {
        statusBar()->showMessage("This build has no QtWebEngine - see Documentation/building.md", 6000);
        return;
    }
    addBrowserTab()->focusAddressBar();
}

// path empty: the page open in the editor if it is html, else the explorer folder's index.html, else ask.
void MainWindow::openInBrowser(const QString& pathIn) {
    QString path = pathIn;
    if (path.isEmpty()) {
        const QString index = indexHtmlIn(explorer_->root());
        if (!file_.isEmpty() && isWebPagePath(file_)) {
            path = file_;
        } else if (!index.isEmpty()) {
            path = index;
        } else {
            path = QFileDialog::getOpenFileName(this, "Open page", explorer_->root(),
                                                "Web pages (*.html *.htm *.xhtml *.svg);;All files (*)");
            if (path.isEmpty()) return;
        }
    }
    if (!BrowserTab::available()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        statusBar()->showMessage("This build has no QtWebEngine - opened in the system browser "
                                 "(see Documentation/building.md)", 6000);
        return;
    }
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (BrowserTab* t : rightTabs_->findChildren<BrowserTab*>()) {      // already open? reuse that tab
        if (t->target() == abs) {
            rightTabs_->setCurrentWidget(t);
            t->reloadNow();
            if (s_.viewMode == 0) setViewMode(1);
            return;
        }
    }
    addBrowserTab()->openTarget(abs);
}
