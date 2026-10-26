// searchtab.h - "Find in files": search a string in every file below the working folder (the explorer root).
//   Options: match case, whole word, regex; "files to include" / "exclude" globs (comma separated).
//   The search runs in a worker thread and streams results into a tree (file > matching lines); double-click / Enter
//   opens the file at the match. Binary files, files over 2 MB and hidden / node_modules folders are skipped.
#pragma once
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

class QLabel;
class QLineEdit;
class QThread;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class SearchTab : public QWidget {
    Q_OBJECT
public:
    explicit SearchTab(std::function<QString()> rootProvider, QWidget* parent = nullptr);
    ~SearchTab() override;
    void focusQuery(const QString& seed);          // seed (if not empty) replaces the query; searches right away

signals:
    void openRequested(const QString& path, int line, int col, int len);   // line 0: just open the file

private:
    struct Job;
    void startSearch();
    void cancel();
    void drain();                                  // timer: move worker results into the tree
    void finish();
    void activate(QTreeWidgetItem* it);

    std::function<QString()> root_;
    QLineEdit *query_ = nullptr, *include_ = nullptr, *exclude_ = nullptr;
    QToolButton *case_ = nullptr, *word_ = nullptr, *re_ = nullptr;
    QLabel* summary_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTimer debounce_, poll_;
    std::shared_ptr<Job> job_;
    QThread* thread_ = nullptr;
    QString searchedRoot_;
    int files_ = 0, hits_ = 0;
};
