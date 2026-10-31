// searchtab.cpp - see include/makdown/ui/searchtab.h
#include <makdown/ui/searchtab.h>
#include <makdown/core/lang.h>
#include <makdown/core/search.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kMaxHits = 5000, kMaxFileHits = 300, kMaxFiles = 200000;
constexpr qint64 kMaxBytes = 2 * 1024 * 1024;
enum Role { RolePath = Qt::UserRole + 1, RoleLine, RoleCol, RoleLen, RolePre, RoleMatch, RolePost };

struct FileResult { QString abs, rel; QVector<Search::Hit> hits; };

// Draws a match row as "12  text before [match] text after" with the match highlighted; file rows use the default painter.
class HitDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override {
        if (!i.data(RoleLine).isValid() || i.data(RoleLine).toInt() == 0) { QStyledItemDelegate::paint(p, o, i); return; }
        QStyleOptionViewItem opt(o);
        initStyleOption(&opt, i);
        opt.text.clear();
        const QWidget* w = opt.widget;
        (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
        const bool sel = opt.state & QStyle::State_Selected;
        const QColor fg = opt.palette.color(sel ? QPalette::HighlightedText : QPalette::Text);
        const QColor muted = opt.palette.color(sel ? QPalette::HighlightedText : QPalette::PlaceholderText);
        QFontMetrics fm(opt.font);
        QRect r = opt.rect.adjusted(2, 0, -2, 0);
        p->save();
        p->setClipRect(opt.rect);
        p->setFont(opt.font);
        const QString num = QString::number(i.data(RoleLine).toInt()) + "  ";
        p->setPen(muted); p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, num);
        r.setLeft(r.left() + fm.horizontalAdvance(num));
        const QString pre = i.data(RolePre).toString(), m = i.data(RoleMatch).toString(), post = i.data(RolePost).toString();
        p->setPen(fg); p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, pre);
        r.setLeft(r.left() + fm.horizontalAdvance(pre));
        QColor bg = opt.palette.color(QPalette::Highlight); bg = sel ? QColor(255, 255, 255, 70) : QColor(bg.red(), bg.green(), bg.blue(), 90);
        const QRect mr(r.left(), r.top() + 2, fm.horizontalAdvance(m), r.height() - 4);
        p->fillRect(mr, bg);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, m);
        r.setLeft(r.left() + fm.horizontalAdvance(m));
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, post);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override {
        QSize s = QStyledItemDelegate::sizeHint(o, i);
        return QSize(s.width(), qMax(s.height(), QFontMetrics(o.font).height() + 6));
    }
};
} // namespace

struct SearchTab::Job {
    std::atomic<bool> cancel{false}, done{false}, limited{false};
    std::atomic<int> scanned{0};
    QMutex m;
    QVector<FileResult> pending;
};

SearchTab::SearchTab(std::function<QString()> rootProvider, QWidget* parent) : QWidget(parent), root_(std::move(rootProvider)) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 10, 12, 8); v->setSpacing(6);

    auto tb = [](const QString& text, const QString& tip) {
        auto* b = new QToolButton; b->setText(text); b->setToolTip(tip); b->setCheckable(true); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet("QToolButton { padding: 4px 5px; }"); return b;
    };
    auto* r1 = new QHBoxLayout; r1->setSpacing(4);
    query_ = new QLineEdit; query_->setPlaceholderText(L("search.placeholder"));
    case_ = tb("Aa", L("opt.matchCase")); word_ = tb("ab", L("opt.wholeWord")); re_ = tb(".*", L("opt.regex"));
    r1->addWidget(query_, 1); r1->addWidget(case_); r1->addWidget(word_); r1->addWidget(re_);
    v->addLayout(r1);
    include_ = new QLineEdit; include_->setPlaceholderText(L("search.include"));
    exclude_ = new QLineEdit; exclude_->setPlaceholderText(L("search.exclude"));
    v->addWidget(include_); v->addWidget(exclude_);
    summary_ = new QLabel; summary_->setWordWrap(true);
    v->addWidget(summary_);

    tree_ = new QTreeWidget; tree_->setObjectName("searchTree");
    tree_->setHeaderHidden(true); tree_->setUniformRowHeights(true); tree_->setRootIsDecorated(true);
    tree_->setItemDelegate(new HitDelegate(tree_));
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    v->addWidget(tree_, 1);

    debounce_.setSingleShot(true); debounce_.setInterval(350);
    poll_.setInterval(70);
    connect(&debounce_, &QTimer::timeout, this, [this] { startSearch(); });
    connect(&poll_, &QTimer::timeout, this, [this] { drain(); });
    for (QLineEdit* e : {query_, include_, exclude_}) connect(e, &QLineEdit::textChanged, this, [this] { debounce_.start(); });
    for (QToolButton* b : {case_, word_, re_}) connect(b, &QToolButton::toggled, this, [this] { debounce_.start(); });
    connect(query_, &QLineEdit::returnPressed, this, [this] { debounce_.stop(); startSearch(); });
    connect(include_, &QLineEdit::returnPressed, this, [this] { debounce_.stop(); startSearch(); });
    connect(exclude_, &QLineEdit::returnPressed, this, [this] { debounce_.stop(); startSearch(); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) { activate(it); });
    auto sc = [this](const QKeySequence& k, QToolButton* b) {
        auto* s = new QShortcut(k, this); s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, b, &QToolButton::toggle);
    };
    sc(QKeySequence("Alt+C"), case_); sc(QKeySequence("Alt+W"), word_); sc(QKeySequence("Alt+R"), re_);
    setFocusProxy(query_);
}

SearchTab::~SearchTab() { cancel(); }

void SearchTab::focusQuery(const QString& seed) {
    query_->setFocus();
    if (!seed.isEmpty() && seed != query_->text()) { query_->setText(seed); debounce_.stop(); startSearch(); }
    query_->selectAll();
}

void SearchTab::cancel() {
    poll_.stop();
    if (job_) job_->cancel = true;
    if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
    job_.reset();
}

void SearchTab::startSearch() {
    cancel();
    tree_->clear(); files_ = hits_ = 0;
    Search::Options o; o.query = query_->text(); o.matchCase = case_->isChecked(); o.wholeWord = word_->isChecked(); o.regex = re_->isChecked();
    if (o.query.isEmpty()) { summary_->clear(); return; }
    QString err;
    const QRegularExpression rx = Search::makeRegex(o, &err);
    if (!rx.isValid()) { summary_->setText(L("search.invalidRegex").arg(err)); return; }
    QString root = root_();
    if (root.isEmpty()) root = QDir::currentPath();
    root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    searchedRoot_ = root;
    const QStringList inc = Search::splitGlobs(include_->text()), exc = Search::splitGlobs(exclude_->text());

    job_ = std::make_shared<Job>();
    std::shared_ptr<Job> job = job_;
    const int rootLen = root.endsWith('/') ? root.size() : root.size() + 1;
    thread_ = QThread::create([job, root, rx, inc, exc, rootLen] {
        int total = 0;
        Search::walk(root, job->cancel, kMaxFiles, [&](const QString& abs, qint64 size) {
            if (job->cancel) return false;
            ++job->scanned;
            const QString rel = abs.mid(rootLen);
            if (!inc.isEmpty() && !Search::globMatch(inc, rel)) return true;
            if (!exc.isEmpty() && Search::globMatch(exc, rel)) return true;
            if (size <= 0 || size > kMaxBytes) return true;
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) return true;
            const QByteArray data = f.readAll();
            if (Search::looksBinary(data.left(8192))) return true;
            const int room = qMin(kMaxFileHits, kMaxHits - total);
            QVector<Search::Hit> hits = Search::searchText(QString::fromUtf8(data), rx, room);
            if (hits.isEmpty()) return true;
            total += hits.size();
            { QMutexLocker l(&job->m); job->pending.append({abs, rel, hits}); }
            if (total >= kMaxHits) { job->limited = true; return false; }
            return true;
        });
        job->done = true;
    });
    summary_->setText(L("search.searching"));
    thread_->start();
    poll_.start();
}

void SearchTab::drain() {
    if (!job_) return;
    QVector<FileResult> batch;
    { QMutexLocker l(&job_->m); batch.swap(job_->pending); }
    if (!batch.isEmpty()) {
        tree_->setUpdatesEnabled(false);
        for (const FileResult& fr : batch) {
            auto* fi = new QTreeWidgetItem(tree_);
            fi->setText(0, fr.rel + "   (" + QString::number(fr.hits.size()) + ")");
            fi->setData(0, RolePath, fr.abs);
            fi->setToolTip(0, fr.abs);
            for (const Search::Hit& h : fr.hits) {
                auto* it = new QTreeWidgetItem(fi);
                it->setData(0, RolePath, fr.abs); it->setData(0, RoleLine, h.line); it->setData(0, RoleCol, h.col); it->setData(0, RoleLen, h.len);
                it->setData(0, RolePre, h.preview.left(h.pcol)); it->setData(0, RoleMatch, h.preview.mid(h.pcol, h.plen));
                it->setData(0, RolePost, h.preview.mid(h.pcol + h.plen));
                it->setText(0, QString::number(h.line) + "  " + h.preview);            // used for the row width + accessibility
                it->setToolTip(0, fr.rel + ":" + QString::number(h.line));
            }
            fi->setExpanded(true);
            ++files_; hits_ += fr.hits.size();
        }
        tree_->setUpdatesEnabled(true);
        summary_->setText(L("search.progress").arg(Ln("search.nResults", hits_), Ln("search.inNFiles", files_)));
    }
    if (job_->done) finish();
}

void SearchTab::finish() {
    poll_.stop();
    if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
    QString s = L("search.summary").arg(Ln("search.nResults", hits_), Ln("search.inNFiles", files_));
    if (job_ && job_->limited) s += L("search.limited").arg(kMaxHits);
    s += L("search.scanned").arg(job_ ? job_->scanned.load() : 0).arg(QDir::toNativeSeparators(searchedRoot_));
    summary_->setText(s);
    job_.reset();
}

void SearchTab::activate(QTreeWidgetItem* it) {
    if (!it) return;
    const QString path = it->data(0, RolePath).toString();
    if (path.isEmpty()) return;
    emit openRequested(path, it->data(0, RoleLine).toInt(), it->data(0, RoleCol).toInt(), it->data(0, RoleLen).toInt());
}
