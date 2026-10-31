// quickopen.cpp - see include/makdown/ui/quickopen.h
#include <makdown/ui/quickopen.h>
#include <makdown/core/lang.h>
#include <makdown/core/search.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>

namespace { constexpr int kMaxFiles = 200000, kShown = 200; }

struct QuickOpen::Job {
    std::atomic<bool> cancel{false}, done{false}, limited{false};
    QMutex m;
    QStringList pending;
};

QuickOpen::QuickOpen(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 8, 12, 8); v->setSpacing(6);
    edit_ = new QLineEdit; edit_->setPlaceholderText(L("quick.placeholder"));
    list_ = new QListWidget; list_->setObjectName("quickList"); list_->setUniformItemSizes(true);
    list_->setFocusPolicy(Qt::NoFocus);
    status_ = new QLabel; status_->setObjectName("quickStatus");
    v->addWidget(edit_); v->addWidget(list_, 1); v->addWidget(status_);
    poll_.setInterval(60);
    connect(&poll_, &QTimer::timeout, this, [this] { drain(); });
    connect(edit_, &QLineEdit::textChanged, this, [this] { refilter(); });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { choose(it); });
    edit_->installEventFilter(this);
}

QuickOpen::~QuickOpen() { stop(); }

void QuickOpen::stop() {
    poll_.stop();
    if (job_) job_->cancel = true;
    if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
    job_.reset();
}

void QuickOpen::focusInput() { edit_->setFocus(); edit_->selectAll(); }

void QuickOpen::start(const QString& rootIn) {
    stop();
    root_ = QDir::cleanPath(QFileInfo(rootIn.isEmpty() ? QDir::currentPath() : rootIn).absoluteFilePath());
    files_.clear(); list_->clear(); edit_->clear();
    indexing_ = true;
    status_->setText(L("quick.indexing"));

    job_ = std::make_shared<Job>();
    std::shared_ptr<Job> job = job_;
    const QString root = root_;
    const int rootLen = root.endsWith('/') ? root.size() : root.size() + 1;
    thread_ = QThread::create([job, root, rootLen] {
        QStringList batch;
        Search::walk(root, job->cancel, kMaxFiles, [&](const QString& abs, qint64) {
            batch << abs.mid(rootLen);
            if (batch.size() >= 500) { QMutexLocker l(&job->m); job->pending += batch; batch.clear(); }
            return !job->cancel;
        });
        { QMutexLocker l(&job->m); job->pending += batch; }
        job->done = true;
    });
    thread_->start();
    poll_.start();

}

void QuickOpen::drain() {
    if (!job_) return;
    QStringList batch;
    { QMutexLocker l(&job_->m); batch.swap(job_->pending); }
    const bool changed = !batch.isEmpty();
    files_ += batch;
    if (job_->done) {
        indexing_ = false; poll_.stop();
        if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
    }
    if (changed || !indexing_) refilter();
}

void QuickOpen::refilter() {
    QString q = edit_->text().trimmed();
    static const QRegularExpression lineRx(R"(:(\d+)$)");
    if (const auto m = lineRx.match(q); m.hasMatch()) q.chop(m.capturedLength());
    struct Row { int score; int idx; };
    QVector<Row> rows;
    for (int i = 0; i < files_.size(); ++i) {
        const int s = q.isEmpty() ? 0 : Search::fuzzyScore(files_[i], q);
        if (s >= 0 || q.isEmpty()) rows.append({s, i});
    }
    const int shown = qMin<int>(kShown, rows.size());
    if (!q.isEmpty())
        std::partial_sort(rows.begin(), rows.begin() + shown, rows.end(), [this](const Row& a, const Row& b) {
            return a.score != b.score ? a.score > b.score : files_[a.idx].size() < files_[b.idx].size();
        });
    list_->setUpdatesEnabled(false);
    list_->clear();
    for (int i = 0; i < shown; ++i) {
        const QString& rel = files_[rows[i].idx];
        const QFileInfo fi(rel);
        const QString dir = fi.path() == "." ? QString() : fi.path();
        auto* it = new QListWidgetItem(fi.fileName() + (dir.isEmpty() ? QString() : "    " + dir));
        it->setData(Qt::UserRole, rel);
        it->setToolTip(rel);
        list_->addItem(it);
    }
    list_->setUpdatesEnabled(true);
    if (list_->count()) list_->setCurrentRow(0);
    status_->setText(L("quick.status").arg(rows.size()).arg(files_.size()).arg(indexing_ ? L("quick.indexingSuffix") : QString()));
}

void QuickOpen::choose(QListWidgetItem* it) {
    if (!it) return;
    static const QRegularExpression lineRx(R"(:(\d+)$)");
    const auto m = lineRx.match(edit_->text().trimmed());
    const int line = m.hasMatch() ? m.captured(1).toInt() : 0;
    const QString abs = QDir(root_).filePath(it->data(Qt::UserRole).toString());
    emit fileChosen(abs, line);
}

bool QuickOpen::eventFilter(QObject* o, QEvent* e) {
    if (o == edit_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        switch (k->key()) {
            case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown:
                QApplication::sendEvent(list_, k); return true;
            case Qt::Key_Return: case Qt::Key_Enter: choose(list_->currentItem()); return true;
            case Qt::Key_Escape: edit_->clear(); return true;
            default: break;
        }
    }
    return QWidget::eventFilter(o, e);
}
