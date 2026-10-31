// findbar.cpp - see include/makdown/ui/findbar.h
#include <makdown/ui/findbar.h>
#include <makdown/core/lang.h>
#include <makdown/core/search.h>
#include <makdown/ui/editor.h>

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace { constexpr int kMaxMatches = 20000, kMaxHighlighted = 3000; }

FindBar::FindBar(MarkdownEditor* editor, QWidget* parent) : QWidget(parent), ed_(editor) {
    setObjectName("findBar");
    setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 6, 8, 6); v->setSpacing(4);

    auto tb = [](const QString& text, const QString& tip, bool checkable) {
        auto* b = new QToolButton; b->setText(text); b->setToolTip(tip); b->setCheckable(checkable);
        b->setFocusPolicy(Qt::NoFocus); b->setAutoRaise(true);
        b->setStyleSheet("QToolButton { padding: 4px 6px; }"); return b;
    };

    auto* r1 = new QHBoxLayout; r1->setSpacing(4);
    toggle_ = tb("\u25B8", L("find.toggleReplace"), true);
    find_ = new QLineEdit; find_->setPlaceholderText(L("find.placeholder"));
    case_ = tb("Aa", L("opt.matchCase"), true);
    word_ = tb("ab", L("opt.wholeWord"), true);
    re_   = tb(".*", L("opt.regex"), true);
    count_ = new QLabel; count_->setMinimumWidth(78); count_->setAlignment(Qt::AlignCenter);
    prev_ = tb("\u2191", L("find.prev"), false);
    next_ = tb("\u2193", L("find.next"), false);
    close_ = tb("\u2715", L("find.close"), false);
    for (QWidget* w : std::initializer_list<QWidget*>{toggle_, find_, case_, word_, re_, count_, prev_, next_, close_}) r1->addWidget(w, w == find_ ? 1 : 0);
    v->addLayout(r1);

    replRow_ = new QWidget;
    auto* r2 = new QHBoxLayout(replRow_); r2->setContentsMargins(0, 0, 0, 0); r2->setSpacing(4);
    auto* pad = new QWidget; pad->setFixedWidth(toggle_->sizeHint().width());
    repl_ = new QLineEdit; repl_->setPlaceholderText(L("find.replacePlaceholder"));
    replOne_ = tb(L("find.replace"), L("find.replaceTip"), false);
    replAll_ = tb(L("find.replaceAll"), L("find.replaceAllTip"), false);
    r2->addWidget(pad); r2->addWidget(repl_, 1); r2->addWidget(replOne_); r2->addWidget(replAll_);
    v->addWidget(replRow_);
    replRow_->hide();
    hide();

    docTimer_.setSingleShot(true); docTimer_.setInterval(150);
    connect(&docTimer_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(false); });
    connect(ed_->document(), &QTextDocument::contentsChanged, this, [this] { if (isVisible() && !selfEdit_) docTimer_.start(); });
    connect(find_, &QLineEdit::textChanged, this, [this] { refresh(true); });
    for (QToolButton* b : {case_, word_, re_}) connect(b, &QToolButton::toggled, this, [this] { refresh(true); });
    connect(prev_, &QToolButton::clicked, this, [this] { step(true); });
    connect(next_, &QToolButton::clicked, this, [this] { step(false); });
    connect(close_, &QToolButton::clicked, this, [this] { close(); });
    connect(toggle_, &QToolButton::toggled, this, [this](bool on) { replRow_->setVisible(on); toggle_->setText(on ? "\u25BE" : "\u25B8"); });
    connect(replOne_, &QToolButton::clicked, this, [this] { replaceOne(); });
    connect(replAll_, &QToolButton::clicked, this, [this] { replaceAll(); });
    find_->installEventFilter(this); repl_->installEventFilter(this);

    auto sc = [this](const QKeySequence& k, QToolButton* b) {
        auto* s = new QShortcut(k, this); s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, b, &QToolButton::toggle);
    };
    sc(QKeySequence("Alt+C"), case_); sc(QKeySequence("Alt+W"), word_); sc(QKeySequence("Alt+R"), re_);
}

bool FindBar::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        const bool enter = k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter;
        if (k->key() == Qt::Key_Escape) { close(); return true; }
        if (o == find_ && enter) { step(k->modifiers() & Qt::ShiftModifier); return true; }
        if (o == repl_ && enter) { (k->modifiers() & Qt::ControlModifier) && (k->modifiers() & Qt::AltModifier) ? replaceAll() : replaceOne(); return true; }
        if (enter && (k->modifiers() & Qt::ControlModifier) && (k->modifiers() & Qt::AltModifier)) { replaceAll(); return true; }
        if (k->key() == Qt::Key_F3) { step(k->modifiers() & Qt::ShiftModifier); return true; }
        if (k->key() == Qt::Key_Down && o == find_) { step(false); return true; }
        if (k->key() == Qt::Key_Up && o == find_) { step(true); return true; }
    }
    return QWidget::eventFilter(o, e);
}

void FindBar::open(bool withReplace) {
    QString seed;
    const QTextCursor c = ed_->textCursor();
    if (c.hasSelection() && !c.selectedText().contains(QChar::ParagraphSeparator)) seed = c.selectedText();
    else { QTextCursor w = c; w.select(QTextCursor::WordUnderCursor); seed = w.selectedText(); }
    const bool wasHidden = isHidden();
    show();
    if (withReplace) toggle_->setChecked(true);
    if (!seed.isEmpty() && seed != find_->text()) { QSignalBlocker b(find_); find_->setText(seed); }
    (withReplace && !find_->text().isEmpty() ? repl_ : find_)->setFocus();
    if (withReplace && !find_->text().isEmpty()) repl_->selectAll(); else find_->selectAll();
    refresh(wasHidden || !seed.isEmpty());
}

void FindBar::close() {
    hide();
    matches_.clear(); cur_ = -1;
    ed_->setFindMatches({});
    ed_->setFocus();
}

void FindBar::refresh(bool jump) {
    matches_.clear(); cur_ = -1;
    Search::Options o; o.query = find_->text(); o.matchCase = case_->isChecked(); o.wholeWord = word_->isChecked(); o.regex = re_->isChecked();
    QString err;
    const QRegularExpression rx = Search::makeRegex(o, &err);
    find_->setProperty("bad", !err.isEmpty());
    if (!rx.isValid()) {
        count_->setText(err.isEmpty() ? QString() : L("find.invalid"));
        count_->setToolTip(err);
        ed_->setFindMatches({});
        return;
    }
    count_->setToolTip(QString());
    const QString text = ed_->toPlainText();
    QRegularExpression mrx(rx.pattern(), rx.patternOptions() | QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = mrx.globalMatch(text);
    while (it.hasNext() && matches_.size() < kMaxMatches) {
        const auto m = it.next();
        if (m.capturedLength() == 0) continue;
        matches_.append({int(m.capturedStart()), int(m.capturedEnd())});
    }
    ed_->setFindMatches(matches_.size() > kMaxHighlighted ? QVector<QPair<int, int>>() : matches_);
    if (matches_.isEmpty()) { count_->setText(L("find.noResults")); return; }

    const int pos = ed_->textCursor().selectionStart();           // current match = first one at / after the caret
    int idx = 0;
    while (idx < matches_.size() && matches_[idx].first < pos) ++idx;
    if (idx == matches_.size()) idx = 0;
    if (jump) select(idx);
    else {                                                        // typing in the document: keep the counter honest
        cur_ = -1;
        const int sel = ed_->textCursor().selectionStart(), se = ed_->textCursor().selectionEnd();
        for (int i = 0; i < matches_.size(); ++i) if (matches_[i].first == sel && matches_[i].second == se) { cur_ = i; break; }
        count_->setText(cur_ >= 0 ? L("find.counter").arg(cur_ + 1).arg(matches_.size()).arg(QString()) : L("find.found").arg(matches_.size()));
    }
}

void FindBar::select(int idx) {
    if (idx < 0 || idx >= matches_.size()) return;
    cur_ = idx;
    QTextCursor c(ed_->document());
    c.setPosition(matches_[idx].first); c.setPosition(matches_[idx].second, QTextCursor::KeepAnchor);
    ed_->setTextCursor(c);
    ed_->centerCursor();
    count_->setText(L("find.counter").arg(idx + 1).arg(matches_.size()).arg(matches_.size() >= kMaxMatches ? "+" : ""));
}

int FindBar::nextIndex(bool backwards) const {
    if (matches_.isEmpty()) return -1;
    const QTextCursor c = ed_->textCursor();
    if (!backwards) {
        const int from = c.selectionEnd();
        for (int i = 0; i < matches_.size(); ++i) if (matches_[i].first >= from) return i;
        return 0;                                                 // wrap
    }
    const int from = c.selectionStart();
    for (int i = matches_.size() - 1; i >= 0; --i) if (matches_[i].first < from) return i;
    return matches_.size() - 1;
}

void FindBar::step(bool backwards) {
    if (isHidden()) { open(false); if (find_->text().isEmpty()) return; }
    else if (matches_.isEmpty()) refresh(false);
    const int i = nextIndex(backwards);
    if (i >= 0) select(i);
}

// $0 / $& whole match, $1..$9 groups, $$ a dollar sign (regex mode only; plain mode inserts the text as is)
QString FindBar::expand(const QString& repl, const QString& docText, int start) const {
    if (!re_->isChecked()) return repl;
    Search::Options o; o.query = find_->text(); o.matchCase = case_->isChecked(); o.wholeWord = word_->isChecked(); o.regex = true;
    const QRegularExpression rx = Search::makeRegex(o);
    const auto m = QRegularExpression(rx.pattern(), rx.patternOptions() | QRegularExpression::MultilineOption)
                       .match(docText, start, QRegularExpression::NormalMatch);
    QString out;
    for (int i = 0; i < repl.size(); ++i) {
        if (repl[i] == '$' && i + 1 < repl.size()) {
            const QChar n = repl[i + 1];
            if (n == '$') { out += '$'; ++i; continue; }
            if (n == '&') { out += m.captured(0); ++i; continue; }
            if (n.isDigit()) { out += m.captured(n.digitValue()); ++i; continue; }
        }
        out += repl[i];
    }
    return out;
}

void FindBar::replaceOne() {
    if (find_->text().isEmpty()) return;
    if (matches_.isEmpty()) refresh(false);
    const QTextCursor c = ed_->textCursor();
    int hit = -1;
    for (int i = 0; i < matches_.size(); ++i)
        if (matches_[i].first == c.selectionStart() && matches_[i].second == c.selectionEnd()) { hit = i; break; }
    if (hit < 0) { const int i = nextIndex(false); if (i >= 0) select(i); return; }    // first Enter just selects the match
    const QString repl = expand(repl_->text(), ed_->toPlainText(), matches_[hit].first);
    selfEdit_ = true;
    QTextCursor w = ed_->textCursor();
    w.insertText(repl);
    selfEdit_ = false;
    refresh(false);
    const int i = nextIndex(false);                               // caret is now just after the replacement
    if (i >= 0) select(i);
}

void FindBar::replaceAll() {
    if (find_->text().isEmpty()) return;
    refresh(false);
    if (matches_.isEmpty()) return;
    const QString text = ed_->toPlainText();
    const QString repl = repl_->text();
    QVector<QPair<int, int>> ms = matches_;
    selfEdit_ = true;
    QTextCursor c(ed_->document());
    c.beginEditBlock();
    for (int i = ms.size() - 1; i >= 0; --i) {                    // back to front: earlier positions stay valid
        c.setPosition(ms[i].first); c.setPosition(ms[i].second, QTextCursor::KeepAnchor);
        c.insertText(expand(repl, text, ms[i].first));
    }
    c.endEditBlock();
    selfEdit_ = false;
    emit message(L("find.replaced").arg(ms.size()));
    refresh(false);
}
