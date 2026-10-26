// mainwindow.h - the application window: chrome bar, editor + preview panes, menus, command line.
#pragma once
#include <QMainWindow>
#include <QTextBrowser>
#include <QTimer>
#include <makdown/core/syntax.h>
#include <makdown/core/theme.h>

class MarkdownEditor;
class FileExplorer;
class BrowserTab;
class ConsolePanel;
class FindBar;
class Sidebar;
class QVariantAnimation;
class QLabel; class QLineEdit; class QSplitter; class QFrame; class QAction; class QActionGroup; class QWidget; class QTabWidget;

// Preview pane: a QTextBrowser that keeps the document centred at the theme's page width.
class PreviewBrowser : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
    void setPageWidth(int w) { pageW_ = w; applyMargins(); }
    void applyMargins();                         // side margins = (viewport - pageWidth) / 2
protected:
    void resizeEvent(QResizeEvent* e) override { QTextBrowser::resizeEvent(e); applyMargins(); }
private:
    int pageW_ = 860;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& fileToOpen = QString());

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    // ---- construction
    void buildUi();
    void buildMenus();
    void buildChrome();               // rebuilds the top bar for the active theme's "chrome" flavour
    void updatePaneHeaders();
    // ---- applying state
    void applyAll();                  // theme + settings -> every widget
    void syncActions();               // menu check marks <- AppSettings
    void setViewMode(int mode);
    Theme currentTheme() const;
    // ---- preview
    void renderPreview();
    // ---- files
    bool maybeSave();
    bool saveFile();
    bool saveFileAs();
    bool writeTo(const QString& path);
    void openFile(const QString& path);
    void openAnyFile(const QString& path);       // explorer double-click: text -> editor, anything else -> system handler
    void newFile();
    void openMedia(const QString& path);         // image / audio preview in a right-hand tab
    static bool isPreviewable(const QString& path);   // image or audio file that opens in a preview tab (svg stays text)
    void showExplorer(bool on);                  // slides the explorer in / out (see core/anim.h)
    void showConsole(bool on);
    void applyHighlighting(const QString& path); // markdown colours, a syntax pack (syntax/*.json) or none, by file name
    bool isMarkdownDoc(const QString& path) const;
    void onPathRenamed(const QString& from, const QString& to);   // explorer: keep the open file in sync
    void onPathRemoved(const QString& path);
    void exportHtml();
    void updateTitle();
    // ---- explorer + browser tabs
    void chooseFolder();
    void openFolder(const QString& dir);
    void syncExplorerFor(const QString& path);  // keep the explorer root above a file given on the command line
    void openInBrowser(const QString& path = QString());   // empty: current page / index.html / ask
    void newBrowserTab();
    BrowserTab* addBrowserTab();
    void openSearchTab(const QString& seed = QString());   // sidebar > Search > Text (find in files)
    void quickOpen();                            // Ctrl+P: sidebar > Search > File names
    void revealSidebar();                    // make sure the left panel is visible
    void openFileAt(const QString& path, int line, int col, int len);   // open a text file and select a match
    void openGitTab();                           // sidebar > Git (clone a repository)
    // ---- misc
    void openSettings();
    void showAbout();
    void showCommandLine(const QString& prefix);
    void runCommand(const QString& text);       // vim-style ":w", ":q", ":set vim", "/search" ...
    void updateStatus();

    // ---- state
    AppSettings s_;
    Theme theme_;
    QList<Theme> themes_;
    QList<SyntaxPtr> syntax_;                    // packs loaded from <home>/syntax at launch
    QString file_;
    int wordCount_ = 0;

    // ---- widgets
    MarkdownEditor* editor_ = nullptr;
    PreviewBrowser* preview_ = nullptr;
    QSplitter* split_ = nullptr;                 // editor | right tabs
    QSplitter* outer_ = nullptr;                 // sidebar | split_
    Sidebar* sidebar_ = nullptr;                 // rail (Files / Search / Git) + the selected page
    FileExplorer* explorer_ = nullptr;           // the Files page
    QTabWidget* rightTabs_ = nullptr;            // tab 0 = markdown preview, the rest = browser tabs
    QWidget *editorPane_ = nullptr, *previewPane_ = nullptr;
    QFrame* chromeBar_ = nullptr;
    QLabel *editorHeader_ = nullptr, *previewHeader_ = nullptr;
    QLabel *modeLabel_ = nullptr, *posLabel_ = nullptr, *themeLabel_ = nullptr;
    QLineEdit* cmdLine_ = nullptr;
    QTimer renderTimer_;
    QAction *actEditor_ = nullptr, *actSplit_ = nullptr, *actPreview_ = nullptr;
    QAction *actMarks_ = nullptr, *actSpaces_ = nullptr, *actReveal_ = nullptr, *actNumbers_ = nullptr, *actVim_ = nullptr;
    QAction *actExplorer_ = nullptr, *actLive_ = nullptr, *actConsole_ = nullptr;
    FindBar* findBar_ = nullptr;                 // VS Code style find / replace (vim keys off)
    ConsolePanel* console_ = nullptr;            // shell panel under editor + preview
    QSplitter* vsplit_ = nullptr;                // (explorer | split_) over console_
    bool shown_ = false;                         // first show happened: animate from now on
    bool explorerTarget_ = true, consoleTarget_ = false;
    int explorerW_ = 300, consoleH_ = 240;
    QVariantAnimation *explorerAnim_ = nullptr, *consoleAnim_ = nullptr;
    QString appliedTheme_;
};
