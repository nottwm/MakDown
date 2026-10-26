// sidebar.h - the left panel: a vertical rail of tabs (top to bottom) and the page of the selected one.
//   Files   the folder tree (FileExplorer)
//   Search  "Text" = find in files (SearchTab), "File names" = go to file (QuickOpen)
//   Git     clone a repository (GitTab)
#pragma once
#include <QWidget>
#include <functional>
#include <makdown/core/theme.h>

class FileExplorer;
class GitTab;
class QButtonGroup;
class QuickOpen;
class QStackedWidget;
class QToolButton;
class SearchTab;

class Sidebar : public QWidget {
    Q_OBJECT
public:
    enum Page { Files = 0, Search = 1, Git = 2 };

    explicit Sidebar(std::function<QString()> rootProvider, QWidget* parent = nullptr);

    FileExplorer* explorer() const { return explorer_; }
    SearchTab* search() const { return search_; }
    QuickOpen* quick() const { return quick_; }
    GitTab* git() const { return git_; }
    Page page() const { return Page(pageIdx_); }

    void showPage(Page p);                         // switch tab and focus its main input
    void showFileSearch();                         // Search tab, "File names" mode (Ctrl+P)
    void showTextSearch(const QString& seed);      // Search tab, "Text" mode (Ctrl+Shift+F); seed = query or empty
    void rootChanged();                            // the working folder changed: refresh the file index
    void applyTheme(const Theme& t);               // rail icons follow the theme

private:
    void setSearchMode(bool fileNames);
    void indexFiles();
    void focusPage();

    std::function<QString()> root_;
    FileExplorer* explorer_ = nullptr;
    SearchTab* search_ = nullptr;
    QuickOpen* quick_ = nullptr;
    GitTab* git_ = nullptr;
    QStackedWidget *pages_ = nullptr, *searchStack_ = nullptr;
    QButtonGroup *railGroup_ = nullptr, *modeGroup_ = nullptr;
    QToolButton* rail_[3] = {nullptr, nullptr, nullptr};
    QToolButton *modeText_ = nullptr, *modeFiles_ = nullptr;
    int pageIdx_ = 0;
    bool fileMode_ = false, indexDirty_ = true;
};
