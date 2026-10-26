// explorer.cpp - folder tree on the left. Double-click opens a file in the editor; the context
// menu adds "open in browser tab", new file / folder, rename (F2), delete (Del) and "show in file manager".
#include <makdown/ui/explorer.h>

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <makdown/ui/mediatab.h>
#include <QShortcut>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

FileExplorer::FileExplorer(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    header_ = new QLabel("EXPLORER");
    header_->setObjectName("paneHeader");
    header_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // long folder names must not widen the pane
    v->addWidget(header_);

    model_ = new QFileSystemModel(this);
    model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);   // hidden files stay hidden

    tree_ = new QTreeView;
    tree_->setObjectName("explorer");
    tree_->setModel(model_);
    for (int c = 1; c < model_->columnCount(); ++c) tree_->hideColumn(c);   // name only
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(14);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setExpandsOnDoubleClick(false);                    // a single click toggles folders (below)
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(tree_, 1);

    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& i) {
        if (model_->isDir(i)) tree_->setExpanded(i, !tree_->isExpanded(i));
    });
    connect(tree_, &QTreeView::activated, this, [this](const QModelIndex& i) {
        if (!model_->isDir(i)) emit fileActivated(model_->filePath(i));
    });
    connect(tree_, &QTreeView::customContextMenuRequested, this, [this](const QPoint& p) { contextMenu(p); });

    // F2 / Del act on the selected entry while the explorer has focus (they must not steal Del from the editor)
    auto* rename = new QShortcut(QKeySequence(Qt::Key_F2), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(rename, &QShortcut::activated, this, [this] { const QString p = currentPath(); if (!p.isEmpty()) renameEntry(p); });
    auto* del = new QShortcut(QKeySequence(Qt::Key_Delete), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(del, &QShortcut::activated, this, [this] { const QString p = currentPath(); if (!p.isEmpty()) deleteEntry(p); });
}

QString FileExplorer::currentPath() const {
    const QModelIndex i = tree_->currentIndex();
    if (!i.isValid() || !tree_->selectionModel()->isSelected(i)) return QString();
    const QString p = model_->filePath(i);
    return QDir::cleanPath(p) == QDir::cleanPath(root_) ? QString() : p;
}

bool FileExplorer::contains(const QString& root, const QString& path) {
    const QString r = QDir::cleanPath(root), p = QDir::cleanPath(path);
    if (r.isEmpty() || p.isEmpty()) return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (p.compare(r, cs) == 0) return true;
    return p.startsWith(r.endsWith('/') ? r : r + '/', cs);
}

void FileExplorer::setRoot(const QString& dir) {
    const QFileInfo fi(dir);
    if (!fi.isDir()) return;
    root_ = fi.absoluteFilePath();
    tree_->setRootIndex(model_->setRootPath(root_));
    header_->setText("EXPLORER  \u00B7  " + (fi.fileName().isEmpty() ? root_ : fi.fileName()));
    header_->setToolTip(QDir::toNativeSeparators(root_));
}

void FileExplorer::reveal(const QString& path) {
    if (path.isEmpty() || !contains(root_, path)) return;
    const QModelIndex idx = model_->index(path);
    if (!idx.isValid()) return;
    const QModelIndex rootIdx = tree_->rootIndex();
    for (QModelIndex p = idx.parent(); p.isValid() && p != rootIdx; p = p.parent()) tree_->expand(p);
    tree_->setCurrentIndex(idx);
    tree_->scrollTo(idx);
}

void FileExplorer::contextMenu(const QPoint& pos) {
    const QModelIndex i = tree_->indexAt(pos);
    const QString path = i.isValid() ? model_->filePath(i) : root_;
    const bool isDir = !i.isValid() || model_->isDir(i);
    const QString dir = isDir ? path : QFileInfo(path).absolutePath();
    const QString index = isDir && QFileInfo::exists(dir + "/index.html") ? dir + "/index.html" : QString();

    QMenu m(this);
    QAction *open = nullptr, *browse = nullptr, *browseIndex = nullptr, *preview = nullptr;
    if (!isDir) {
        if (MediaTab::kindOf(path) != MediaTab::None) preview = m.addAction("Preview");
        open = m.addAction("Open in editor");
        browse = m.addAction("Open in browser tab");
        m.addSeparator();
    } else if (!index.isEmpty()) {
        browseIndex = m.addAction("Open index.html in browser tab");
        m.addSeparator();
    }
    QAction* newFile = m.addAction("New file...");
    QAction* newDir = m.addAction("New folder...");
    QAction *rename = nullptr, *del = nullptr;
    if (i.isValid()) {                                         // not for the empty area / the root itself
        m.addSeparator();
        rename = m.addAction("Rename...");  rename->setShortcut(QKeySequence(Qt::Key_F2));
        del = m.addAction("Delete");        del->setShortcut(QKeySequence(Qt::Key_Delete));
    }
    m.addSeparator();
    QAction* show = m.addAction("Show in file manager");
    QAction* change = m.addAction("Open folder...");

    QAction* a = m.exec(tree_->viewport()->mapToGlobal(pos));
    if (!a) return;
    if (a == preview) emit previewRequested(path);
    else if (a == open) emit fileActivated(path);
    else if (a == browse) emit browserRequested(path);
    else if (a == browseIndex) emit browserRequested(index);
    else if (a == newFile) createEntry(dir, false);
    else if (a == newDir) createEntry(dir, true);
    else if (a == rename) renameEntry(path);
    else if (a == del) deleteEntry(path);
    else if (a == show) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    else if (a == change) emit openFolderRequested();
}

void FileExplorer::createEntry(const QString& dir, bool folder) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, folder ? "New folder" : "New file", "Name:", QLineEdit::Normal,
                                               folder ? QString() : QStringLiteral("untitled.md"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!validName(name)) return;
    const QString p = QDir(dir).filePath(name);
    if (QFileInfo::exists(p)) {
        QMessageBox::warning(this, "MakDown", "\"" + name + "\" already exists.");
        return;
    }
    if (folder) {
        if (!QDir().mkdir(p)) QMessageBox::warning(this, "MakDown", "Could not create the folder.");
        return;
    }
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "MakDown", "Could not create the file:\n" + f.errorString());
        return;
    }
    f.close();
    emit fileActivated(p);
}

bool FileExplorer::validName(const QString& name) {
    static const QString bad = QStringLiteral("<>:\"|?*");     // not allowed on Windows; avoids surprises when sharing folders
    bool ok = !(name == "." || name == ".." || name.contains('/') || name.contains('\\'));
    if (!ok) { QMessageBox::warning(this, "MakDown", "The name must not contain path separators."); return false; }
#ifdef Q_OS_WIN
    for (const QChar c : bad) if (name.contains(c)) {
        QMessageBox::warning(this, "MakDown", QString("The name must not contain any of: ") + bad);
        return false;
    }
#else
    Q_UNUSED(bad);
#endif
    return true;
}

void FileExplorer::renameEntry(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || contains(path, root_)) return;            // never rename the root or one of its parents
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == fi.fileName()) return;
    if (!validName(name)) return;
    const QString target = fi.absoluteDir().filePath(name);
    // a case-only change ("readme.md" -> "README.md") "exists" on case-insensitive file systems: that is fine
    if (QFileInfo::exists(target) && name.compare(fi.fileName(), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, "MakDown", "\"" + name + "\" already exists.");
        return;
    }
    if (!QDir().rename(fi.absoluteFilePath(), target)) {
        QMessageBox::warning(this, "MakDown", "Could not rename \"" + fi.fileName() + "\".");
        return;
    }
    const QString newPath = QDir::cleanPath(target);
    emit pathRenamed(QDir::cleanPath(fi.absoluteFilePath()), newPath);
    QTimer::singleShot(200, this, [this, newPath] { reveal(newPath); });   // the model picks the change up asynchronously
}

void FileExplorer::deleteEntry(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || contains(path, root_)) return;
    const bool dir = fi.isDir() && !fi.isSymLink();
    QMessageBox box(QMessageBox::Question, "Delete",
                    "Delete \"" + fi.fileName() + "\"" + (dir ? " and everything inside it?" : "?"),
                    QMessageBox::Yes | QMessageBox::Cancel, this);
    box.setInformativeText("It will be moved to the trash.");
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) return;

    bool gone = QFile::moveToTrash(fi.absoluteFilePath());
    if (!gone) {                                                  // no trash available (some Linux setups, network drives)
        const auto r = QMessageBox::warning(this, "Delete", "Could not move \"" + fi.fileName() + "\" to the trash.\n"
                                            "Delete it permanently? This cannot be undone.",
                                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Yes) return;
        gone = dir ? QDir(fi.absoluteFilePath()).removeRecursively() : QFile::remove(fi.absoluteFilePath());
        if (!gone) { QMessageBox::warning(this, "MakDown", "Could not delete \"" + fi.fileName() + "\"."); return; }
    }
    emit pathRemoved(QDir::cleanPath(fi.absoluteFilePath()));
}

void FileExplorer::setAnimated(bool on) { tree_->setAnimated(on); }
