// explorer.h - folder tree shown on the left (QFileSystemModel + QTreeView).
#pragma once
#include <QWidget>

class QTreeView;
class QFileSystemModel;
class QLabel;
class QModelIndex;
class QPoint;

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    void setAnimated(bool on);                   // folders expand / collapse with an animation
    explicit FileExplorer(QWidget* parent = nullptr);

    void setRoot(const QString& dir);            // no-op when dir is not a directory
    QString root() const { return root_; }
    void reveal(const QString& path);            // expand to + select path (only if it is inside the root)

    static bool contains(const QString& root, const QString& path);   // path == root or inside it

signals:
    void fileActivated(const QString& path);     // double-click / Enter on a file: open it in the editor
    void browserRequested(const QString& path);  // context menu: open in a browser tab
    void previewRequested(const QString& path);  // context menu: preview an image / sound in a tab
    void openFolderRequested();                  // context menu: choose another root
    void pathRenamed(const QString& oldPath, const QString& newPath);   // a file / folder was renamed (F2 / context menu)
    void pathRemoved(const QString& path);       // a file / folder was deleted (Del / context menu)

private:
    void contextMenu(const QPoint& pos);
    void createEntry(const QString& dir, bool folder);
    void renameEntry(const QString& path);       // asks for a new name, renames on disk
    void deleteEntry(const QString& path);       // asks for confirmation, moves to the trash
    QString currentPath() const;                 // selected entry ("" when nothing / the root is selected)
    bool validName(const QString& name);         // shows a warning and returns false for bad names

    QTreeView* tree_ = nullptr;
    QFileSystemModel* model_ = nullptr;
    QLabel* header_ = nullptr;
    QString root_;
};
