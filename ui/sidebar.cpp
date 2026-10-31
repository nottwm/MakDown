// sidebar.cpp - see include/makdown/ui/sidebar.h
#include <makdown/ui/sidebar.h>
#include <makdown/core/lang.h>
#include <makdown/ui/explorer.h>
#include <makdown/ui/gittab.h>
#include <makdown/ui/quickopen.h>
#include <makdown/ui/searchtab.h>

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// 22x22 line icons drawn in the theme colours (folder, magnifier, git branch)
QIcon railIcon(int kind, const QColor& off, const QColor& on) {
    QIcon ic;
    for (int state = 0; state < 2; ++state) {
        QPixmap pm(44, 44); pm.setDevicePixelRatio(2.0); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
        QPen pen(state ? on : off, 1.5); pen.setJoinStyle(Qt::RoundJoin); pen.setCapStyle(Qt::RoundCap); p.setPen(pen);
        if (kind == 0) {                                   // folder
            QPainterPath f;
            f.moveTo(3, 17); f.lineTo(3, 5.5); f.lineTo(8.5, 5.5); f.lineTo(10.5, 7.5); f.lineTo(19, 7.5); f.lineTo(19, 17); f.closeSubpath();
            p.drawPath(f);
        } else if (kind == 1) {                            // magnifier
            p.drawEllipse(QPointF(9.5, 9.5), 5.5, 5.5);
            p.drawLine(QPointF(13.6, 13.6), QPointF(18.5, 18.5));
        } else {                                           // branch
            p.drawEllipse(QPointF(7, 4.8), 2.2, 2.2); p.drawEllipse(QPointF(7, 17.2), 2.2, 2.2); p.drawEllipse(QPointF(15.5, 8.6), 2.2, 2.2);
            p.drawLine(QPointF(7, 7), QPointF(7, 15));
            QPainterPath b; b.moveTo(15.5, 10.8); b.quadTo(15.5, 13.6, 7, 14.2);
            p.drawPath(b);
        }
        ic.addPixmap(pm, QIcon::Normal, state ? QIcon::On : QIcon::Off);
    }
    return ic;
}
} // namespace

Sidebar::Sidebar(std::function<QString()> rootProvider, QWidget* parent) : QWidget(parent), root_(std::move(rootProvider)) {
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);

    // ---- the rail: tabs stacked top to bottom
    auto* rail = new QFrame; rail->setObjectName("sideRail"); rail->setFixedWidth(40);
    auto* rv = new QVBoxLayout(rail); rv->setContentsMargins(0, 6, 0, 6); rv->setSpacing(2);
    railGroup_ = new QButtonGroup(this); railGroup_->setExclusive(true);
    const QString tips[3] = {L("side.explorer"), L("side.search"), L("side.git")};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton; b->setCheckable(true); b->setToolTip(tips[i]); b->setAutoRaise(true);
        b->setIconSize(QSize(22, 22)); b->setFocusPolicy(Qt::NoFocus); b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        railGroup_->addButton(b, i); rv->addWidget(b); rail_[i] = b;
    }
    rv->addStretch(1);
    rail_[0]->setChecked(true);
    h->addWidget(rail);

    // ---- pages
    pages_ = new QStackedWidget;
    explorer_ = new FileExplorer;
    pages_->addWidget(explorer_);

    auto* sp = new QWidget;                                // Search page: mode switch + the two search panels
    auto* sv = new QVBoxLayout(sp); sv->setContentsMargins(0, 0, 0, 0); sv->setSpacing(0);
    auto* modes = new QHBoxLayout; modes->setContentsMargins(12, 8, 12, 0); modes->setSpacing(4);
    modeText_ = new QToolButton; modeText_->setText(L("side.modeText")); modeText_->setToolTip(L("side.modeTextTip"));
    modeFiles_ = new QToolButton; modeFiles_->setText(L("side.modeFiles")); modeFiles_->setToolTip(L("side.modeFilesTip"));
    modeGroup_ = new QButtonGroup(this); modeGroup_->setExclusive(true);
    for (QToolButton* b : {modeText_, modeFiles_}) { b->setCheckable(true); b->setFocusPolicy(Qt::NoFocus); modeGroup_->addButton(b); modes->addWidget(b); }
    modeText_->setChecked(true);
    modes->addStretch(1);
    sv->addLayout(modes);
    searchStack_ = new QStackedWidget;
    search_ = new SearchTab(root_);
    quick_ = new QuickOpen;
    searchStack_->addWidget(search_); searchStack_->addWidget(quick_);
    sv->addWidget(searchStack_, 1);
    pages_->addWidget(sp);

    git_ = new GitTab;
    pages_->addWidget(git_);
    h->addWidget(pages_, 1);

    connect(railGroup_, &QButtonGroup::idClicked, this, [this](int id) { showPage(Page(id)); });
    connect(modeText_, &QToolButton::clicked, this, [this] { setSearchMode(false); focusPage(); });
    connect(modeFiles_, &QToolButton::clicked, this, [this] { setSearchMode(true); focusPage(); });
}

void Sidebar::applyTheme(const Theme& t) {
    const QColor off(t.color("muted")), on(t.color("fg"));
    for (int i = 0; i < 3; ++i) rail_[i]->setIcon(railIcon(i, off, on));
}

void Sidebar::indexFiles() {
    if (!indexDirty_) return;
    indexDirty_ = false;
    quick_->start(root_());
}

void Sidebar::setSearchMode(bool fileNames) {
    fileMode_ = fileNames;
    (fileNames ? modeFiles_ : modeText_)->setChecked(true);
    searchStack_->setCurrentWidget(fileNames ? static_cast<QWidget*>(quick_) : static_cast<QWidget*>(search_));
    if (fileNames) { indexDirty_ = true; indexFiles(); }       // fresh index every time the panel is opened
}

void Sidebar::rootChanged() {
    indexDirty_ = true;
    if (pageIdx_ == Search && fileMode_) indexFiles();
}

void Sidebar::focusPage() {
    switch (pageIdx_) {
        case Files:  explorer_->setFocus(); break;
        case Search: if (fileMode_) quick_->focusInput(); else search_->focusQuery(QString()); break;
        case Git:    git_->focusUrl(); break;
    }
}

void Sidebar::showPage(Page p) {
    pageIdx_ = p;
    pages_->setCurrentIndex(p);
    rail_[p]->setChecked(true);
    if (p == Git) git_->setDefaultParent(root_());
    if (p == Search && fileMode_) { indexDirty_ = true; indexFiles(); }
    focusPage();
}

void Sidebar::showFileSearch() {
    pageIdx_ = Search; pages_->setCurrentIndex(Search); rail_[Search]->setChecked(true);
    setSearchMode(true);
    focusPage();
}

void Sidebar::showTextSearch(const QString& seed) {
    pageIdx_ = Search; pages_->setCurrentIndex(Search); rail_[Search]->setChecked(true);
    setSearchMode(false);
    search_->focusQuery(seed);
}
