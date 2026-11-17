// aipanel.cpp - see include/makdown/ui/aipanel.h
#include <makdown/ui/aipanel.h>
#include <makdown/arch/platform.h>
#include <makdown/core/lang.h>
#include <makdown/core/markdown.h>

#include <QAbstractTextDocumentLayout>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHostAddress>
#include <QHostInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QSysInfo>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// The part of a rendered document that goes between <body> and </body>.
QString bodyOf(const QString& html) {
    int a = html.indexOf("<body");
    if (a < 0) return html;
    a = html.indexOf('>', a) + 1;
    const int b = html.lastIndexOf("</body>");
    return b > a ? html.mid(a, b - a) : html.mid(a);
}
QString rgba(const QString& hex, double alpha) {
    const QColor c(hex);
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// A text browser as tall as its text (no scroll bars of its own): assistant messages live in the chat's scroll area.
class FitBrowser : public QTextBrowser {
public:
    explicit FitBrowser(QWidget* p = nullptr) : QTextBrowser(p) {
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setOpenLinks(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        document()->setDocumentMargin(0);
        viewport()->setAutoFillBackground(false);
        QObject::connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] { fit(); });
    }
protected:
    void resizeEvent(QResizeEvent* e) override { QTextBrowser::resizeEvent(e); fit(); }
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }          // the chat scrolls, not the message
private:
    void fit() {
        document()->setTextWidth(viewport()->width());
        const int h = int(document()->size().height()) + 2;
        if (h != height()) setFixedHeight(h);
    }
};

// A frame that reports clicks (headers of the collapsible cards).
class ClickFrame : public QFrame {
public:
    std::function<void()> onClick;
protected:
    void mouseReleaseEvent(QMouseEvent* e) override { QFrame::mouseReleaseEvent(e); if (e->button() == Qt::LeftButton && onClick) onClick(); }
};

// One line of text that elides with "..." instead of forcing the panel wider.
class ElideLabel : public QLabel {
public:
    explicit ElideLabel(const QString& t = QString(), QWidget* p = nullptr) : QLabel(t, p) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(palette().color(foregroundRole()));
        p.setFont(font());
        p.drawText(rect(), int(alignment()) | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};
} // namespace

AiPanel::AiPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("aiPanel");
    theme_.fillDefaults();
    nam_ = new QNetworkAccessManager(this);

    auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);

    // ---- header: title + actions, then the provider / model line
    auto* head = new QWidget; head->setObjectName("aiHead");
    auto* hv = new QVBoxLayout(head); hv->setContentsMargins(14, 10, 8, 8); hv->setSpacing(0);
    auto* h1 = new QHBoxLayout; h1->setSpacing(6);
    auto* spark = new QLabel(QString::fromUtf8("\u2726")); spark->setObjectName("aiSpark");
    auto* titleLbl = new QLabel(L("ai.title")); titleLbl->setObjectName("aiTitle");
    newBtn_ = new QToolButton; newBtn_->setObjectName("aiHeadBtn"); newBtn_->setText(QString::fromUtf8("\uff0b ") + L("ai.newShort")); newBtn_->setToolTip(L("ai.newChat")); newBtn_->setAutoRaise(true);
    setBtn_ = new QToolButton; setBtn_->setObjectName("aiHeadBtn"); setBtn_->setText(QString::fromUtf8("\u2699")); setBtn_->setToolTip(L("ai.settings")); setBtn_->setAutoRaise(true);
    h1->addWidget(spark); h1->addWidget(titleLbl); h1->addStretch(1); h1->addWidget(newBtn_); h1->addWidget(setBtn_);
    hv->addLayout(h1);
    auto* st = new ClickFrame; st->setObjectName("aiStatus"); st->setCursor(Qt::PointingHandCursor); st->setToolTip(L("ai.statusTip"));
    auto* sl = new QHBoxLayout(st); sl->setContentsMargins(20, 0, 0, 0); sl->setSpacing(0);
    auto* statusLbl = new ElideLabel; statusLbl->setObjectName("aiStatusText");
    sl->addWidget(statusLbl);
    st->onClick = [this] { emit settingsRequested(); };
    status_ = st;
    hv->addWidget(st);
    v->addWidget(head);

    // ---- "no API key" banner
    bannerBox_ = new QWidget; bannerBox_->setObjectName("aiBannerBox");
    auto* bb = new QVBoxLayout(bannerBox_); bb->setContentsMargins(12, 8, 12, 4);
    auto* card = new QFrame; card->setObjectName("aiBanner");
    auto* bv = new QVBoxLayout(card); bv->setContentsMargins(12, 10, 12, 10); bv->setSpacing(8);
    banner_ = new QLabel; banner_->setWordWrap(true);
    bannerBtn_ = new QPushButton(L("ai.openSettings"));
    bv->addWidget(banner_); bv->addWidget(bannerBtn_, 0, Qt::AlignLeft);
    bb->addWidget(card);
    v->addWidget(bannerBox_);

    // ---- the conversation: a scroll area of items (+ the empty state)
    scroll_ = new QScrollArea; scroll_->setObjectName("aiScroll"); scroll_->setWidgetResizable(true); scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget; content->setObjectName("aiContent");
    list_ = new QVBoxLayout(content); list_->setContentsMargins(12, 10, 12, 10); list_->setSpacing(10);

    emptyBox_ = new QWidget; emptyBox_->setObjectName("aiEmpty");
    auto* ev = new QVBoxLayout(emptyBox_); ev->setContentsMargins(8, 28, 8, 8); ev->setSpacing(10);
    auto* bigSpark = new QLabel(QString::fromUtf8("\u2726")); bigSpark->setObjectName("aiBigSpark"); bigSpark->setAlignment(Qt::AlignCenter);
    auto* emptyTxt = new QLabel(L("ai.empty")); emptyTxt->setObjectName("aiEmptyText"); emptyTxt->setWordWrap(true); emptyTxt->setAlignment(Qt::AlignCenter);
    ev->addWidget(bigSpark); ev->addWidget(emptyTxt);
    for (const char* key : {"ai.sug1", "ai.sug2", "ai.sug3"}) {
        auto* chip = new QPushButton(L(key)); chip->setObjectName("aiChip"); chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QPushButton::clicked, this, [this, chip] { input_->setPlainText(chip->text()); input_->setFocus(); input_->moveCursor(QTextCursor::End); });
        ev->addWidget(chip);
    }
    list_->addWidget(emptyBox_);
    list_->addStretch(1);
    scroll_->setWidget(content);
    v->addWidget(scroll_, 1);
    connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { if (stick_) scrollDown(true); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) { stick_ = val >= scroll_->verticalScrollBar()->maximum() - 24; });

    // ---- composer: a rounded box with the input and the send button
    auto* foot = new QWidget; foot->setObjectName("aiFoot");
    auto* fv = new QVBoxLayout(foot); fv->setContentsMargins(10, 4, 10, 10);
    composer_ = new QFrame; composer_->setObjectName("aiComposer");
    auto* cv = new QVBoxLayout(composer_); cv->setContentsMargins(10, 8, 8, 8); cv->setSpacing(4);
    input_ = new QPlainTextEdit; input_->setObjectName("aiInput"); input_->setPlaceholderText(L("ai.placeholder"));
    input_->setFrameShape(QFrame::NoFrame); input_->setMinimumHeight(42); input_->setMaximumHeight(140); input_->installEventFilter(this);
    cv->addWidget(input_);
    auto* row = new QHBoxLayout; row->setSpacing(8);
    includeDoc_ = new QCheckBox(L("ai.includeDoc")); includeDoc_->setObjectName("aiInclude"); includeDoc_->setToolTip(L("ai.includeDocTip")); includeDoc_->setChecked(true);
    sendBtn_ = new QPushButton(QString::fromUtf8("\u2191")); sendBtn_->setObjectName("aiSend"); sendBtn_->setToolTip(L("ai.sendTip")); sendBtn_->setCursor(Qt::PointingHandCursor);
    sendBtn_->setFixedSize(30, 30);
    row->addWidget(includeDoc_, 1); row->addWidget(sendBtn_);
    cv->addLayout(row);
    fv->addWidget(composer_);
    v->addWidget(foot);

    // ---- the animated "Thinking..." row (created once, moved to the end of the list while a request is open)
    pending_ = new QFrame; pending_->setObjectName("aiPending");
    auto* pl = new QHBoxLayout(pending_); pl->setContentsMargins(10, 6, 10, 6);
    pendingLbl_ = new QLabel; pendingLbl_->setObjectName("aiPendingText");
    pl->addWidget(pendingLbl_);
    pending_->hide();
    dots_ = new QTimer(this); dots_->setInterval(380);
    connect(dots_, &QTimer::timeout, this, [this] { dotStep_ = (dotStep_ + 1) % 4; pendingLbl_->setText(pendingBase_ + QString(dotStep_, '.')); });

    flush_ = new QTimer(this); flush_->setSingleShot(true); flush_->setInterval(50);
    connect(flush_, &QTimer::timeout, this, [this] { flushLive(); });

    connect(sendBtn_, &QPushButton::clicked, this, [this] { if (busy_) stop(); else submit(); });
    connect(newBtn_, &QToolButton::clicked, this, [this] { newChat(); });
    connect(setBtn_, &QToolButton::clicked, this, [this] { emit settingsRequested(); });
    connect(bannerBtn_, &QPushButton::clicked, this, [this] { emit settingsRequested(); });
    connect(input_, &QPlainTextEdit::textChanged, this, [this] { growInput(); });
    applyStyle();
    refresh();
    updateEmpty();
    growInput();
}

AiPanel::~AiPanel() {
    cancelled_ = true;
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
}

void AiPanel::growInput() {
    const int h = int(input_->document()->size().height()) + 14;
    input_->setFixedHeight(qBound(42, h, 140));
}

void AiPanel::applyTheme(const Theme& t) {
    theme_ = t;
    applyStyle();
    rebuild();
}

// The panel's look. Everything is drawn from the theme's colours, so it follows every preset.
void AiPanel::applyStyle() {
    const Theme& t = theme_;
    const QString accent = t.color("accent"), accentFg = t.color("accentFg"), border = t.color("border"), panel = t.color("panel"),
                  muted = t.color("muted"), fg = t.color("fg"), bg = t.color("bg"), codeBg = t.color("codeBg"), codeFg = t.color("codeFg");
    const int r = qBound(4, t.radius, 14);
    QString q;
    auto add = [&q](const QString& s) { q += s + "\n"; };
    add(QString("#aiPanel { background: %1; }").arg(bg));
    add("#aiScroll, #aiContent, #aiEmpty { background: transparent; }");
    add(QString("#aiHead { background: %1; border-bottom: 1px solid %2; }").arg(panel, border));
    add(QString("#aiSpark { color: %1; font-size: 15px; }").arg(accent));
    add(QString("#aiTitle { color: %1; font-weight: 600; }").arg(fg));
    add(QString("#aiHeadBtn { color: %1; border: none; border-radius: %2px; padding: 3px 7px; }").arg(muted).arg(r));
    add(QString("#aiHeadBtn:hover { background: %1; color: %2; }").arg(border, fg));
    add(QString("#aiStatusText { color: %1; font-size: 11px; }").arg(muted));
    add(QString("#aiStatus:hover #aiStatusText { color: %1; }").arg(accent));
    add(QString("#aiBanner { background: %1; border: 1px solid %2; border-left: 3px solid %3; border-radius: %4px; }").arg(panel, border, accent).arg(r));
    add(QString("#aiBanner QLabel { color: %1; }").arg(fg));
    add(QString("#aiBigSpark { color: %1; font-size: 34px; }").arg(accent));
    add(QString("#aiEmptyText { color: %1; }").arg(muted));
    add(QString("#aiChip { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 7px 12px; text-align: left; }").arg(panel, fg, border).arg(r + 4));
    add(QString("#aiChip:hover { border-color: %1; color: %1; }").arg(accent));
    // conversation items
    add(QString("#aiUser { background: %1; border: 1px solid %2; border-radius: %3px; }").arg(rgba(accent, 0.13), rgba(accent, 0.35)).arg(r + 4));
    add(QString("#aiUser QLabel { color: %1; background: transparent; }").arg(fg));
    add(QString("#aiAsstName { color: %1; font-weight: 600; font-size: 11px; }").arg(accent));
    add(QString("#aiThink, #aiTool { background: %1; border: 1px solid %2; border-radius: %3px; }").arg(panel, border).arg(r));
    add(QString("#aiCardTitle { color: %1; font-size: 11px; }").arg(muted));
    add(QString("#aiCardSub { color: %1; font-size: 11px; }").arg(muted));
    add(QString("#aiChevron { color: %1; }").arg(muted));
    add(QString("#aiThinkBody { color: %1; font-style: italic; background: transparent; padding: 0px 10px 8px 10px; }").arg(muted));
    add(QString("#aiToolBody { color: %1; background: %2; border-top: 1px solid %3; border-bottom-left-radius: %4px; border-bottom-right-radius: %4px; padding: 8px 10px; }").arg(codeFg, codeBg, border).arg(r));
    add(QString("#aiError { background: %1; border: 1px solid %2; border-radius: %3px; }").arg("rgba(209,36,47,0.12)", "rgba(209,36,47,0.55)").arg(r));
    add(QString("#aiError QLabel { color: %1; }").arg(fg));
    add(QString("#aiNotice { color: %1; font-style: italic; }").arg(muted));
    add(QString("#aiPending { background: transparent; }"));
    add(QString("#aiPendingText { color: %1; font-style: italic; }").arg(muted));
    // composer
    add(QString("#aiFoot { background: %1; }").arg(bg));
    add(QString("#aiComposer { background: %1; border: 1px solid %2; border-radius: %3px; }").arg(t.color("editorBg"), border).arg(r + 4));
    add(QString("#aiComposer[focus=\"true\"] { border: 1px solid %1; }").arg(accent));
    add(QString("#aiInput { background: transparent; border: none; color: %1; selection-background-color: %2; }").arg(t.color("editorFg"), t.color("selection")));
    add(QString("#aiInclude { color: %1; font-size: 11px; }").arg(muted));
    add(QString("#aiSend { background: %1; color: %2; border: none; border-radius: 15px; font-size: 15px; font-weight: bold; padding: 0px; }").arg(accent, accentFg));
    add(QString("#aiSend:hover { background: %1; }").arg(rgba(accent, 0.85)));
    add(QString("#aiSend[stop=\"true\"] { background: %1; }").arg(muted));
    setStyleSheet(q);
    for (QFrame* f : findChildren<QFrame*>("aiComposer")) { f->style()->unpolish(f); f->style()->polish(f); }
}

void AiPanel::focusInput() { input_->setFocus(); }

void AiPanel::setIncludeDocument(bool on) { includeDoc_->setChecked(on); }

void AiPanel::refresh() {
    const Ai::Config& c = Ai::config();
    const Ai::Provider* p = Ai::findProvider(c.provider);
    const QString name = p ? p->name : c.provider;
    const QString model = c.model(c.provider);
    if (auto* l = status_->findChild<QLabel*>("aiStatusText")) l->setText(model.isEmpty() ? name : name + " \u00b7 " + model);
    const bool noKey = p && !p->custom && c.key(p->id).isEmpty();
    bannerBox_->setVisible(noKey);
    banner_->setText(L("ai.noKey").arg(name));
}

void AiPanel::updateStatus() { if (!busy_) refresh(); }

bool AiPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == input_) {
        if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut) {            // the composer's border lights up while typing
            composer_->setProperty("focus", e->type() == QEvent::FocusIn);
            composer_->style()->unpolish(composer_); composer_->style()->polish(composer_);
        } else if (e->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(e);
            if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !(k->modifiers() & Qt::ShiftModifier)) {
                if (!busy_) submit();
                return true;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

void AiPanel::submit() {
    const QString text = input_->toPlainText().trimmed();
    if (text.isEmpty() || busy_) return;
    input_->clear();
    send(text);
}

// ---------------------------------------------------------------------------------------------
// conversation items
// ---------------------------------------------------------------------------------------------
int AiPanel::thinkingBlocks() const {
    int n = 0;
    for (const Entry& e : entries_) if (e.kind == Entry::Thinking) ++n;
    return n;
}

void AiPanel::updateEmpty() { emptyBox_->setVisible(entries_.isEmpty() && !busy_); }

void AiPanel::scrollDown(bool force) {
    if (!force && !stick_) return;
    QTimer::singleShot(0, this, [this] { scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum()); });
}

// A collapsible card: a clickable header (dot, title, sub text, chevron) and a body that opens on click.
static QWidget* cardWith(const QString& objectName, const QString& title, const QString& sub, const QString& dotColor, QWidget* body,
                         bool expanded, std::function<void(bool)> toggled) {
    auto* card = new QFrame; card->setObjectName(objectName);
    auto* v = new QVBoxLayout(card); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    auto* head = new ClickFrame; head->setCursor(Qt::PointingHandCursor); head->setProperty("aiToggle", true);   // (the tests click these)
    auto* h = new QHBoxLayout(head); h->setContentsMargins(10, 7, 10, 7); h->setSpacing(7);
    if (!dotColor.isEmpty()) { auto* dot = new QLabel(QString::fromUtf8("\u25cf")); dot->setStyleSheet("color:" + dotColor + "; background: transparent; font-size: 9px;"); h->addWidget(dot); }
    auto* t = new ElideLabel(title); t->setObjectName("aiCardTitle");
    h->addWidget(t, 1);
    if (!sub.isEmpty()) { auto* s = new QLabel(sub); s->setObjectName("aiCardSub"); h->addWidget(s); }
    auto* chev = new QLabel(expanded ? QString::fromUtf8("\u25be") : QString::fromUtf8("\u25b8")); chev->setObjectName("aiChevron"); h->addWidget(chev);
    v->addWidget(head);
    v->addWidget(body);
    body->setVisible(expanded);
    head->onClick = [body, chev, toggled] {
        const bool on = !body->isVisible();
        body->setVisible(on);
        chev->setText(on ? QString::fromUtf8("\u25be") : QString::fromUtf8("\u25b8"));
        toggled(on);
    };
    return card;
}

QWidget* AiPanel::makeItem(int index) {
    const Entry& e = entries_.at(index);
    const QString muted = theme_.color("muted");
    auto remember = [this, index](bool on) { if (index < entries_.size()) { entries_[index].expanded = on; entries_[index].touched = true; } };
    switch (e.kind) {
    case Entry::User: {
        auto* f = new QFrame; f->setObjectName("aiUser");
        auto* l = new QVBoxLayout(f); l->setContentsMargins(11, 8, 11, 8);
        auto* t = new QLabel(e.text); t->setWordWrap(true); t->setTextFormat(Qt::PlainText); t->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(t);
        auto* row = new QWidget; auto* rl = new QHBoxLayout(row); rl->setContentsMargins(24, 0, 0, 0); rl->addWidget(f);
        return row;
    }
    case Entry::Assistant: {
        auto* w = new QWidget; auto* v = new QVBoxLayout(w); v->setContentsMargins(2, 0, 2, 0); v->setSpacing(4);
        auto* name = new QLabel(L("ai.assistant")); name->setObjectName("aiAsstName");
        auto* b = new FitBrowser;
        b->document()->setDefaultStyleSheet(previewCss(theme_));
        b->setFont(ThemeStore::makeFont(theme_.previewFont, theme_.previewSize));
        b->setHtml(bodyOf(renderMarkdownToHtml(e.text, theme_, false, host_.workDir ? host_.workDir() : QString())));
        v->addWidget(name); v->addWidget(b);
        return w;
    }
    case Entry::Thinking: {
        auto* body = new QLabel(e.text); body->setObjectName("aiThinkBody"); body->setWordWrap(true); body->setTextFormat(Qt::PlainText);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return cardWith("aiThink", QString::fromUtf8("\u2726 ") + L("ai.thought"), e.title, QString(), body, e.expanded, remember);
    }
    case Entry::Tool: {
        const QString dot = e.status == "ok" ? "#2da44e" : e.status == "denied" ? "#d29922" : "#d1242f";
        auto* body = new QLabel(e.text); body->setObjectName("aiToolBody"); body->setWordWrap(true); body->setTextFormat(Qt::PlainText);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        body->setFont(ThemeStore::makeFont(theme_.codeFont, qMax(8, theme_.previewSize - 1), true));
        return cardWith("aiTool", e.title, QString(), dot, body, e.expanded, remember);
    }
    case Entry::Notice: {
        auto* l = new QLabel(e.text); l->setObjectName("aiNotice"); l->setWordWrap(true); l->setAlignment(Qt::AlignCenter);
        return l;
    }
    case Entry::Error: {
        auto* f = new QFrame; f->setObjectName("aiError");
        auto* l = new QVBoxLayout(f); l->setContentsMargins(11, 8, 11, 8);
        auto* t = new QLabel("<b>" + L("ai.error").toHtmlEscaped() + "</b> " + e.text.toHtmlEscaped()); t->setWordWrap(true); t->setTextFormat(Qt::RichText);
        t->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(t);
        return f;
    }
    }
    return new QWidget;
}

int AiPanel::addEntry(Entry::Kind k, const QString& text, const QString& title, const QString& status, bool expanded) {
    Entry e{k, title, text, QString(), status, expanded, false};
    if (k == Entry::Assistant) {
        QTextDocument d; d.setHtml(bodyOf(renderMarkdownToHtml(text, theme_, false, QString())));
        e.plain = d.toPlainText().trimmed();
    } else if (k == Entry::Error) e.plain = L("ai.error") + " " + text;
    else if (k == Entry::Tool) e.plain = title + "\n" + text;
    else if (k == Entry::Thinking) e.plain = L("ai.thought") + "\n" + text;
    else e.plain = text;
    entries_.append(e);
    const bool wasPending = list_->indexOf(pending_) >= 0;
    QWidget* w = makeItem(entries_.size() - 1);
    list_->insertWidget(list_->count() - 1 - (wasPending ? 1 : 0), w);      // before the stretch (and the pending row)
    items_.append(w);
    updateEmpty();
    if (k == Entry::User) stick_ = true;
    scrollDown(false);
    return entries_.size() - 1;
}

// Rebuild one item's widget (after its state changed in a way that is easier to redraw than to patch).
void AiPanel::refreshItem(int index) {
    if (index < 0 || index >= items_.size()) return;
    QWidget* old = items_[index];
    QWidget* neu = makeItem(index);
    list_->insertWidget(list_->indexOf(old), neu);
    list_->removeWidget(old);
    old->deleteLater();
    items_[index] = neu;
}

void AiPanel::removeLastEntry() {
    if (entries_.isEmpty()) return;
    QWidget* w = items_.takeLast();
    list_->removeWidget(w); w->deleteLater();
    entries_.removeLast();
    updateEmpty();
}

QString AiPanel::wordsLabel(const QString& text) {
    return Ln("ai.words", text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size());
}

void AiPanel::setAssistantText(int index, const QString& markdown) {
    if (index < 0 || index >= entries_.size()) return;
    entries_[index].text = markdown;
    if (auto* b = items_[index]->findChild<QTextBrowser*>()) {
        b->setHtml(bodyOf(renderMarkdownToHtml(markdown, theme_, false, host_.workDir ? host_.workDir() : QString())));
        entries_[index].plain = b->toPlainText().trimmed();
    }
}

void AiPanel::setThinkingText(int index, const QString& text) {
    if (index < 0 || index >= entries_.size()) return;
    Entry& e = entries_[index];
    e.text = text; e.title = wordsLabel(text); e.plain = L("ai.thought") + "\n" + text;
    if (auto* l = items_[index]->findChild<QLabel*>("aiThinkBody")) l->setText(text);
    if (auto* l = items_[index]->findChild<QLabel*>("aiCardSub")) l->setText(e.title);
}

void AiPanel::rebuild() {
    for (QWidget* w : items_) { list_->removeWidget(w); w->deleteLater(); }
    items_.clear();
    const bool wasPending = list_->indexOf(pending_) >= 0;
    for (int i = 0; i < entries_.size(); ++i) {
        QWidget* w = makeItem(i);
        list_->insertWidget(list_->count() - 1 - (wasPending ? 1 : 0), w);
        items_.append(w);
    }
    updateEmpty();
}

void AiPanel::showPending(const QString& text) {
    pendingBase_ = text; dotStep_ = 0;
    pendingLbl_->setText(text);
    if (list_->indexOf(pending_) < 0) list_->insertWidget(list_->count() - 1, pending_);
    pending_->show();
    dots_->start();
    updateEmpty();
    scrollDown(false);
}

void AiPanel::setPendingText(const QString& text) {
    pendingBase_ = text;
    pendingLbl_->setText(text + QString(dotStep_, '.'));
}

QString AiPanel::pendingText() const { return list_->indexOf(pending_) >= 0 ? pendingBase_ : QString(); }

void AiPanel::hidePending() {
    pendingIsTool_ = false;
    dots_->stop();
    pending_->hide();
    list_->removeWidget(pending_);
}

QString AiPanel::transcriptText() const {
    QStringList parts;
    for (const Entry& e : entries_) parts << (e.kind == Entry::User ? L("ai.you") + "\n" + e.text : e.plain);
    return parts.join("\n\n");
}

void AiPanel::newChat() {
    if (busy_) stop();
    for (QWidget* w : items_) { list_->removeWidget(w); w->deleteLater(); }
    items_.clear(); entries_.clear(); history_.clear();
    sessionFiles_ = sessionCommands_ = sessionWeb_ = false;
    updateEmpty();
}

void AiPanel::setBusy(bool on) {
    busy_ = on;
    sendBtn_->setText(on ? QString::fromUtf8("\u25a0") : QString::fromUtf8("\u2191"));
    sendBtn_->setToolTip(on ? L("ai.stopTip") : L("ai.sendTip"));
    sendBtn_->setProperty("stop", on);
    sendBtn_->style()->unpolish(sendBtn_); sendBtn_->style()->polish(sendBtn_);
    if (!on) hidePending();
    updateEmpty();
}

void AiPanel::finish() {
    setBusy(false);
    emit turnFinished();
}

void AiPanel::stop() {
    if (!busy_) return;
    cancelled_ = true;
    if (reply_) reply_->abort();
    if (proc_) platform::killProcessTree(proc_);
}

// ---------------------------------------------------------------------------------------------
// the agent loop
// ---------------------------------------------------------------------------------------------
void AiPanel::send(const QString& text) {
    if (busy_ || text.trimmed().isEmpty()) return;
    QString content = text;
    if (includeDoc_->isChecked() && host_.editorText) {
        const QString file = host_.currentFile ? host_.currentFile() : QString();
        content += "\n\n<editor file=\"" + (file.isEmpty() ? QString("untitled") : file) + "\">\n" + Ai::clip(host_.editorText(), 24000) + "\n</editor>";
        const QString sel = host_.selection ? host_.selection() : QString();
        if (!sel.isEmpty()) content += "\n<selection>\n" + Ai::clip(sel, 8000) + "\n</selection>";
    }
    history_.append({"user", content});
    addEntry(Entry::User, text);
    steps_ = 0; cancelled_ = false;
    setBusy(true);
    callModel();
}

void AiPanel::callModel() {
    const Ai::Config& cfg = Ai::config();
    if (steps_ >= cfg.maxSteps) { addEntry(Entry::Notice, L("ai.stepLimit").arg(cfg.maxSteps)); finish(); return; }
    ++steps_;
    showPending(L("ai.thinking"));

    Ai::PromptContext pc;
    pc.workDir = host_.workDir ? host_.workDir() : QString();
    pc.openFile = host_.currentFile ? host_.currentFile() : QString();
    pc.custom = cfg.customPrompt;
    pc.os = QSysInfo::prettyProductName();
    pc.shell = platform::shellName();
    pc.web = cfg.web;
    const Ai::Request rq = Ai::buildRequest(cfg, Ai::systemPrompt(pc), history_, cfg.stream);
    if (!rq.error.isEmpty()) { addEntry(Entry::Error, rq.error); finish(); return; }

    QNetworkRequest req{QUrl(rq.url)};
    for (const auto& hd : rq.headers) req.setRawHeader(hd.first, hd.second);
    req.setTransferTimeout(300000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    const Ai::Provider* prov = Ai::findProvider(cfg.provider);
    const Ai::Api api = prov ? prov->api : Ai::Api::OpenAI;
    parser_.reset(new Ai::StreamParser(api));
    rawBody_.clear(); httpStatus_ = 0; liveStream_ = cfg.stream; nonSse_ = sniffed_ = thinkCollapsed_ = earlyStop_ = false;
    liveThink_ = liveAsst_ = -1;
    reply_ = nam_->post(req, rq.body);
    connect(reply_, &QNetworkReply::readyRead, this, [this] { onData(); });
    connect(reply_, &QNetworkReply::finished, this, [this, api] { onReply(api); });
}

// Bytes of the reply arrived. A streamed reply is parsed on the fly; an error body or a plain JSON reply (a server that
// ignores "stream") is only collected and handled when the reply ends.
void AiPanel::onData() {
    if (!reply_ || cancelled_ || earlyStop_) return;
    const QByteArray chunk = reply_->readAll();
    if (httpStatus_ == 0) httpStatus_ = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (rawBody_.size() < (8 << 20)) rawBody_ += chunk;
    if (!liveStream_ || httpStatus_ < 200 || httpStatus_ >= 300) return;
    if (!sniffed_) {
        for (char ch : rawBody_) {
            if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
            sniffed_ = true; nonSse_ = ch == '{' || ch == '[';
            if (nonSse_) break;
            parser_->feed(rawBody_);                       // everything so far (the first chunk) goes to the parser
            flushLive();
            return;
        }
        if (!sniffed_) return;
    }
    if (nonSse_) return;
    parser_->feed(chunk);
    if (Ai::inventedResultAt(parser_->text()) >= 0) {                      // the model is making up the tool's answer: enough
        earlyStop_ = true;
        flush_->stop();
        QMetaObject::invokeMethod(reply_, "abort", Qt::QueuedConnection);
        return;
    }
    if (!flush_->isActive()) flush_->start();
}

// Shows what the stream has produced so far: the thinking card (open while it grows) and the answer.
void AiPanel::flushLive() {
    flush_->stop();
    if (!parser_ || cancelled_ || nonSse_) return;
    const Ai::Config& cfg = Ai::config();
    const QString think = parser_->thinking().trimmed();
    const QString raw = parser_->text();
    const QString text = Ai::streamPreview(raw);
    bool changed = false;
    if (cfg.showThinking && !think.isEmpty()) {
        if (liveThink_ < 0) { hidePending(); liveThink_ = addEntry(Entry::Thinking, think, wordsLabel(think), QString(), true); }
        else setThinkingText(liveThink_, think);
        changed = true;
    }
    if (!text.isEmpty()) {
        if (liveThink_ >= 0 && !thinkCollapsed_) {                     // the answer started: fold the reasoning away
            thinkCollapsed_ = true;
            if (!entries_[liveThink_].touched) { entries_[liveThink_].expanded = false; refreshItem(liveThink_); }
        }
        if (liveAsst_ < 0) { hidePending(); liveAsst_ = addEntry(Entry::Assistant, text); }
        else setAssistantText(liveAsst_, text);
        changed = true;
    }
    QString tname, tpath; int tchars = 0;
    if (Ai::streamToolInfo(raw, &tname, &tpath, &tchars)) {                  // an action is being written: say which, and how far
        QString what = tname.isEmpty() ? QString() : tname + (tpath.isEmpty() ? QString() : " " + tpath);
        const QString label = what.isEmpty() ? L("ai.preparing") : L("ai.preparingDetail").arg(what, QLocale().formattedDataSize(qint64(tchars) * 1, 1, QLocale::DataSizeTraditionalFormat));
        if (list_->indexOf(pending_) < 0) showPending(label); else setPendingText(label);
        pendingIsTool_ = true;
        changed = true;
    } else if (pendingIsTool_) {                                               // the block is finished: the row has done its job
        hidePending();
    }
    if (changed) { scrollDown(false); emit contentUpdated(); }
}

void AiPanel::onReply(Ai::Api api) {
    QNetworkReply* r = reply_;
    reply_ = nullptr;
    r->deleteLater();
    flush_->stop();
    const QByteArray rest = r->isOpen() ? r->readAll() : QByteArray();      // an aborted reply is closed
    if (rawBody_.size() < (8 << 20)) rawBody_ += rest;
    if (httpStatus_ == 0) httpStatus_ = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const int status = httpStatus_;
    hidePending();
    if (cancelled_) { addEntry(Entry::Notice, L("ai.stopped")); finish(); return; }          // what streamed in stays on screen
    const bool okStatus = status >= 200 && status < 300;
    if (status == 0 || (okStatus && !earlyStop_ && r->error() != QNetworkReply::NoError)) { addEntry(Entry::Error, r->errorString()); finish(); return; }

    Ai::Reply rep;
    if (liveStream_ && okStatus && sniffed_ && !nonSse_) {
        parser_->feed(rest);
        parser_->finish();
        if (!parser_->error().isEmpty()) { addEntry(Entry::Error, parser_->error()); finish(); return; }
        rep.ok = true;
        rep.text = parser_->text().trimmed();
        if (parser_->truncated()) rep.text += "\n\n[MakDown: the reply was cut off at the output limit]";
        rep.thinking = parser_->thinking().trimmed();
    } else {
        rep = Ai::parseReply(api, status, rawBody_);
    }
    if (!rep.ok) { addEntry(Entry::Error, rep.error); finish(); return; }
    const bool cutOff = rep.text.contains("[MakDown: the reply was cut off");
    if (const int fake = Ai::inventedResultAt(rep.text); fake >= 0) rep.text = rep.text.left(fake).trimmed();     // made-up results and what follows

    const bool showThink = Ai::config().showThinking && !rep.thinking.isEmpty();
    if (rep.text.trimmed().isEmpty()) {
        if (showThink && liveThink_ < 0) addEntry(Entry::Thinking, rep.thinking, wordsLabel(rep.thinking));
        addEntry(Entry::Notice, L("ai.emptyReply")); finish(); return;
    }

    history_.append({"assistant", rep.text});
    // the reasoning card: finish the live one (folded), or make it now for a reply that did not stream
    if (showThink) {
        if (liveThink_ >= 0) {
            setThinkingText(liveThink_, rep.thinking);
            if (!entries_[liveThink_].touched && entries_[liveThink_].expanded) { entries_[liveThink_].expanded = false; refreshItem(liveThink_); }
        } else addEntry(Entry::Thinking, rep.thinking, wordsLabel(rep.thinking));
    }
    Ai::Parsed parsed = Ai::parseToolCalls(rep.text);
    if (cutOff)                                                              // a tool call cut by the output limit: tell the model why
        for (Ai::ToolCall& tc : parsed.calls)
            if (!tc.error.isEmpty()) tc.error += ". The reply hit the output limit while this call was being written. Write a much shorter call: split a big file into several smaller ones, or build it up in steps";
    if (liveAsst_ >= 0) {
        if (parsed.text.isEmpty() && liveAsst_ == entries_.size() - 1) removeLastEntry();   // only a tool call was written
        else setAssistantText(liveAsst_, parsed.text);
    } else if (!parsed.text.isEmpty()) addEntry(Entry::Assistant, parsed.text);
    liveThink_ = liveAsst_ = -1;
    if (parsed.calls.isEmpty()) { finish(); return; }

    QStringList results;
    bool ended = false;
    for (const Ai::ToolCall& call : parsed.calls) {
        if (cancelled_) break;
        if (ended) { results << Ai::toolResult(call.name, "error", "skipped: an earlier ask_user in this message is waiting for the user's answer. Send this call again if it is still needed."); continue; }
        results << runTool(call, &ended);
    }
    if (cancelled_) { addEntry(Entry::Notice, L("ai.stopped")); finish(); return; }
    history_.append({"user", results.join("\n\n")});
    QTimer::singleShot(0, this, [this] { callModel(); });          // next round, out of this signal handler
}

// ---------------------------------------------------------------------------------------------
// tools
// ---------------------------------------------------------------------------------------------
QString AiPanel::runTool(const Ai::ToolCall& c, bool* endBatch) {
    const Ai::Config& cfg = Ai::config();
    const QString name = c.name;
    auto done = [&](const QString& status, const QString& body, const QString& title = QString()) {
        addEntry(Entry::Tool, Ai::clip(body, 1500), title.isEmpty() ? name : title, status);
        return Ai::toolResult(name, status, Ai::clip(body, 30000));
    };
    auto arg = [&](const char* k) { return c.args.value(QString::fromLatin1(k)); };
    auto has = [&](const char* k) { return c.args.contains(QString::fromLatin1(k)); };

    if (!c.error.isEmpty()) return done("error", "malformed tool block: " + c.error);
    if (!Ai::toolNames().contains(name)) return done("error", "unknown tool \"" + name + "\". Available tools: " + Ai::toolNames().join(", "));
    const QString root = host_.workDir ? host_.workDir() : QString();

    // ---- create_file / write_file
    if (name == "create_file" || name == "write_file") {
        const bool overwrite = name == "write_file";
        if (!has("path") || !has("content")) return done("error", "needs <path> and <content>");
        const QString rel = arg("path").trimmed(), title = name + " \u00b7 " + rel;
        QString err;
        const QString abs = Ai::resolveInside(root, rel, &err);
        if (abs.isEmpty()) return done("error", err, title);
        const bool exists = QFileInfo::exists(abs);
        if (!overwrite && exists) return done("error", "the file already exists: " + rel + " (use write_file to overwrite it)", title);
        if (overwrite && !exists) return done("error", "the file does not exist: " + rel + " (use create_file for new files)", title);
        if (cfg.confirmFiles && !confirm(L(overwrite ? "ai.confirm.write" : "ai.confirm.create").arg(rel), Ai::clip(arg("content"), 4000), &sessionFiles_, true))
            return done("denied", "the user denied this action", title);
        if (overwrite && host_.applyToOpenFile && host_.applyToOpenFile(abs, arg("content")))
            return done("ok", "Overwrote " + rel + " through the editor and saved it (the user can undo with Ctrl+Z)", title);
        const Ai::FileOp op = Ai::createFile(root, rel, arg("content"), overwrite);
        if (op.ok && !overwrite && arg("open").trimmed().toLower() == "true" && host_.openFile) host_.openFile(op.path);
        return done(op.ok ? "ok" : "error", op.message, title);
    }

    // ---- read_file
    if (name == "read_file") {
        if (!has("path")) return done("error", "needs <path>");
        const QString rel = arg("path").trimmed(), title = name + " \u00b7 " + rel;
        QString err;
        const QString abs = Ai::resolveInside(root, rel, &err);
        if (abs.isEmpty()) return done("error", err, title);
        if (host_.currentFile && host_.editorText && !host_.currentFile().isEmpty() && QFileInfo(host_.currentFile()).absoluteFilePath() == abs)
            return done("ok", Ai::clip(host_.editorText(), 60000), title);              // the editor may hold unsaved changes
        const Ai::FileOp op = Ai::readTextFile(root, rel);
        return done(op.ok ? "ok" : "error", op.message, title);
    }

    // ---- list_dir
    if (name == "list_dir") {
        const QString rel = has("path") ? arg("path").trimmed() : QString(".");
        const Ai::FileOp op = Ai::listDir(root, rel);
        return done(op.ok ? "ok" : "error", op.message, name + " \u00b7 " + rel);
    }

    // ---- open_file
    if (name == "open_file") {
        if (!has("path")) return done("error", "needs <path>");
        const QString rel = arg("path").trimmed(), title = name + " \u00b7 " + rel;
        QString err;
        const QString abs = Ai::resolveInside(root, rel, &err);
        if (abs.isEmpty()) return done("error", err, title);
        if (!QFileInfo(abs).isFile()) return done("error", "no such file: " + rel, title);
        if (!host_.openFile) return done("error", "the editor is not available", title);
        host_.openFile(abs);
        return done("ok", "Opened " + rel + " in the editor", title);
    }

    // ---- new_document
    if (name == "new_document") {
        if (!has("content")) return done("error", "needs <content>");
        if (!host_.newDocument) return done("error", "the editor is not available");
        host_.newDocument(arg("content"));
        return done("ok", "Opened a new untitled document with " + QString::number(arg("content").size()) + " characters");
    }

    // ---- run_command
    if (name == "run_command") {
        const QString cmd = arg("command").trimmed();
        if (cmd.isEmpty()) return done("error", "needs <command>");
        if (root.isEmpty() || !QFileInfo(root).isDir()) return done("error", "no working folder is open", name + " \u00b7 " + cmd);
        if (cfg.confirmCommands && !confirm(L("ai.confirm.run").arg(root, cmd), QString(), &sessionCommands_, false))
            return done("denied", "the user denied this action", name + " \u00b7 " + cmd);
        bool timedOut = false; int code = -1;
        showPending(L("ai.running"));
        const QString out = runShell(cmd, 120000, &timedOut, &code);
        hidePending();
        QString body = out.isEmpty() ? QString("(no output)") : out;
        if (cancelled_) body += "\n[stopped by the user]";
        else if (timedOut) body += "\n[timed out after 120 seconds and was stopped]";
        else body += QString("\n[exit code %1]").arg(code);
        return done(!cancelled_ && !timedOut && code == 0 ? "ok" : "error", body, name + " \u00b7 " + cmd);
    }

    // ---- ask_user
    if (name == "ask_user") {
        const QString q = arg("question").trimmed();
        if (q.isEmpty()) return done("error", "needs <question>");
        QStringList options;
        for (const QString& line : arg("options").split('\n')) if (!line.trimmed().isEmpty()) options << line.trimmed();
        *endBatch = true;
        bool answered = false;
        const QString a = askUser(q, options, &answered);
        if (!answered) return done("error", "Q: " + q + "\nThe user closed the question without answering.", name);
        return done("ok", "Q: " + q + "\nThe user answered: " + a, name);
    }

    // ---- web_search
    if (name == "web_search") {
        const QString q = arg("query").trimmed(), title = name + " \u00b7 " + q;
        if (q.isEmpty()) return done("error", "no search words received" + (c.args.isEmpty() ? QString() : " (parameters seen: <" + c.args.keys().join(">, <") + ">)")
                                                + ". Write exactly: <makdown_tool name=\"web_search\"><query>your search words</query></makdown_tool>");
        if (!cfg.web) return done("error", "web access is switched off in MakDown's settings (Settings > Preferences > AI). Tell the user if you need it.", title);
        if (q.size() > 400) return done("error", "the query is too long (400 characters at most): use a few key words", title);
        if (cfg.confirmWeb && !confirm(L("ai.confirm.search").arg(q), QString(), &sessionWeb_, true))
            return done("denied", "the user denied this action", title);
        QByteArray body; QString ctype, err; QUrl fin; bool cut = false;
        showPending(L("ai.searching"));
        const bool ok = webGet(Ai::searchUrl(q), Ai::searchUrlOverridden(), &body, &ctype, &fin, &cut, &err);
        hidePending();
        if (cancelled_) return done("error", "[stopped by the user]", title);
        if (!ok) return done("error", "the search failed: " + err, title);
        const QString html = Ai::decodeBody(body, ctype);
        const QList<Ai::SearchHit> hits = Ai::parseSearchResults(html);
        if (hits.isEmpty()) {
            if (Ai::looksLikeChallengePage(html)) return done("error", "the search engine asked for a human check instead of answering (too many searches?). Try again later, or use web_fetch with an address you know.", title);
            return done("ok", "No results for \"" + q + "\". Try other or fewer words.", title);
        }
        return done("ok", Ai::formatSearchResults(q, hits), title);
    }

    // ---- web_fetch
    if (name == "web_fetch") {
        const QString raw = arg("url").trimmed(), title = name + " \u00b7 " + raw;
        if (raw.isEmpty()) return done("error", "no address received" + (c.args.isEmpty() ? QString() : " (parameters seen: <" + c.args.keys().join(">, <") + ">)")
                                                 + ". Write exactly: <makdown_tool name=\"web_fetch\"><url>https://example.com/page</url></makdown_tool>");
        if (!cfg.web) return done("error", "web access is switched off in MakDown's settings (Settings > Preferences > AI). Tell the user if you need it.", title);
        QUrl url(raw.contains("://") ? raw : "https://" + raw);
        QString err;
        if (!Ai::checkFetchUrl(url, &err)) return done("error", err, title);
        bool okStart = true;
        const int start = has("start") ? arg("start").trimmed().toInt(&okStart) : 0;
        if (!okStart || start < 0) return done("error", "<start> must be a whole number of characters, 0 or more", title);
        if (cfg.confirmWeb && !confirm(L("ai.confirm.fetch").arg(url.toString()), QString(), &sessionWeb_, true))
            return done("denied", "the user denied this action", title);
        QByteArray body; QString ctype; QUrl fin; bool cut = false;
        showPending(L("ai.fetching"));
        const bool ok = webGet(url, false, &body, &ctype, &fin, &cut, &err);
        hidePending();
        if (cancelled_) return done("error", "[stopped by the user]", title);
        if (!ok) return done("error", "could not load the page: " + err, title);
        const QString ct = ctype.toLower();
        const bool isHtml = ct.contains("html") || (ct.isEmpty() && body.trimmed().startsWith("<"));
        const bool isText = ct.startsWith("text/") || ct.contains("json") || ct.contains("xml") || ct.contains("javascript") || ct.contains("markdown");
        if (!isHtml && !isText) return done("error", "cannot read this content type (" + (ctype.isEmpty() ? QString("unknown") : ctype) + "): only web pages and text files are supported", title);
        QString text = Ai::decodeBody(body, ctype);
        if (isHtml) text = Ai::htmlToText(text, fin);
        if (text.size() > 400000) { text.truncate(400000); cut = true; }
        const int window = 20000;
        if (start > 0 && start >= text.size()) return done("error", QString("<start> %1 is past the end of the page (%2 characters)").arg(start).arg(text.size()), title);
        QString out = "URL: " + fin.toString() + "\n\n" + text.mid(start, window);
        if (start + window < text.size()) out += QString("\n\n[characters %1-%2 of %3%4. To read on, call web_fetch again with <start>%2</start>]").arg(start).arg(start + window).arg(text.size()).arg(cut ? "+" : "");
        else if (cut) out += "\n\n[the page was cut off at the size limit]";
        if (text.trimmed().isEmpty()) out = "URL: " + fin.toString() + "\n\n(the page has no readable text: it probably needs JavaScript or a login)";
        return done("ok", out, title);
    }
    return done("error", "unhandled tool");
}

bool AiPanel::hostIsPublic(const QString& host, QString* error) {
    QEventLoop loop;
    QHostInfo info; bool got = false;
    QHostInfo::lookupHost(host, &loop, [&](const QHostInfo& i) { info = i; got = true; loop.quit(); });
    QTimer limit; limit.setSingleShot(true); QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit); limit.start(10000);
    QTimer tick; QObject::connect(&tick, &QTimer::timeout, &loop, [&] { if (cancelled_) loop.quit(); }); tick.start(100);
    QPointer<AiPanel> self(this);
    if (!got) loop.exec();
    if (!self) return false;
    if (!got) { *error = cancelled_ ? QString("stopped by the user") : "the host name " + host + " did not resolve in time"; return false; }
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) { *error = "cannot resolve " + host + ": " + info.errorString(); return false; }
    for (const QHostAddress& a : info.addresses())
        if (!Ai::isPublicAddress(a)) { *error = host + " points to a local or private network address, which is not opened"; return false; }
    return true;
}

bool AiPanel::webGet(const QUrl& start, bool trusted, QByteArray* body, QString* contentType, QUrl* finalUrl, bool* truncated, QString* error) {
    const int kMaxBytes = 2 * 1024 * 1024;
    QUrl url = start;
    *truncated = false;
    QPointer<AiPanel> self(this);
    for (int hop = 0; hop <= 5; ++hop) {
        if (cancelled_) { *error = "stopped by the user"; return false; }
        if (!trusted && !Ai::checkFetchUrl(url, error)) return false;
        if (url.scheme() != "http" && url.scheme() != "https") { *error = "only http:// and https:// addresses can be opened"; return false; }
        QHostAddress literal;
        if (!trusted && !Ai::allowLocalWeb() && !literal.setAddress(url.host()) && !hostIsPublic(url.host(), error)) return false;
        if (!self) return false;

        QNetworkRequest req(url);
        req.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0 MakDown");
        req.setRawHeader("Accept", "text/html,application/xhtml+xml,text/plain,text/markdown,application/json;q=0.9,*/*;q=0.1");
        req.setRawHeader("Accept-Language", "en,*;q=0.5");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(20000);
        QNetworkReply* r = nam_->get(req);
        QByteArray data; bool tooBig = false, timedOut = false;
        QEventLoop loop;
        QObject::connect(r, &QNetworkReply::readyRead, &loop, [&] { data += r->readAll(); if (data.size() > kMaxBytes) { tooBig = true; r->abort(); } });
        QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer tick; QObject::connect(&tick, &QTimer::timeout, &loop, [&] { if (cancelled_) r->abort(); }); tick.start(100);
        QTimer total; total.setSingleShot(true); QObject::connect(&total, &QTimer::timeout, &loop, [&] { timedOut = true; r->abort(); }); total.start(30000);
        if (!r->isFinished()) loop.exec();
        if (!self) { r->deleteLater(); return false; }
        if (r->isOpen()) data += r->readAll();                  // an aborted reply is closed
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray loc = r->rawHeader("Location");
        const QString ctype = r->header(QNetworkRequest::ContentTypeHeader).toString();
        const QNetworkReply::NetworkError netErr = r->error();
        const QString netMsg = r->errorString();
        r->deleteLater();

        if (cancelled_) { *error = "stopped by the user"; return false; }
        if (status >= 300 && status < 400 && !loc.isEmpty()) { url = url.resolved(QUrl::fromEncoded(loc.trimmed())); continue; }
        if (tooBig) { data.truncate(kMaxBytes); *truncated = true; }
        else if (timedOut) { *error = "the server did not answer in time (30 s)"; return false; }
        else if (status >= 400) { *error = QString("the server answered HTTP %1").arg(status); return false; }
        else if (netErr != QNetworkReply::NoError) { *error = netMsg; return false; }
        *body = data; *contentType = ctype; *finalUrl = url;
        return true;
    }
    *error = "too many redirects";
    return false;
}

bool AiPanel::confirm(const QString& what, const QString& detail, bool* sessionFlag, bool defaultAllow) {
    if (*sessionFlag) return true;
    QMessageBox box(QMessageBox::Question, L("ai.confirm.title"), what, QMessageBox::NoButton, this);
    if (!detail.isEmpty()) box.setDetailedText(detail);
    QPushButton* allow = box.addButton(L("ai.confirm.allow"), QMessageBox::AcceptRole);
    QPushButton* always = box.addButton(L("ai.confirm.always"), QMessageBox::AcceptRole);
    QPushButton* deny = box.addButton(L("ai.confirm.deny"), QMessageBox::RejectRole);
    box.setDefaultButton(defaultAllow ? allow : deny);
    box.setEscapeButton(deny);
    box.exec();
    if (box.clickedButton() == always) { *sessionFlag = true; return true; }
    return box.clickedButton() == allow;
}

QString AiPanel::askUser(const QString& question, const QStringList& options, bool* answered) {
    QDialog dlg(this);
    dlg.setWindowTitle(L("ai.ask.title"));
    dlg.setMinimumWidth(420);
    auto* v = new QVBoxLayout(&dlg);
    auto* q = new QLabel(question); q->setWordWrap(true); q->setTextFormat(Qt::PlainText); v->addWidget(q);
    QString result;
    *answered = false;
    for (const QString& opt : options) {
        auto* b = new QPushButton(opt); b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, &dlg, [&, opt] { result = opt; *answered = true; dlg.accept(); });
        v->addWidget(b);
    }
    auto* edit = new QLineEdit; edit->setPlaceholderText(L("ai.ask.other")); v->addWidget(edit);
    auto* row = new QHBoxLayout;
    auto* skip = new QPushButton(L("ai.ask.skip")); skip->setAutoDefault(false);
    auto* ok = new QPushButton(L("ai.ask.send")); ok->setObjectName("primary"); ok->setDefault(true);
    row->addStretch(1); row->addWidget(skip); row->addWidget(ok);
    v->addLayout(row);
    connect(ok, &QPushButton::clicked, &dlg, [&] { if (edit->text().trimmed().isEmpty()) return; result = edit->text().trimmed(); *answered = true; dlg.accept(); });
    connect(edit, &QLineEdit::returnPressed, ok, &QPushButton::click);
    connect(skip, &QPushButton::clicked, &dlg, &QDialog::reject);
    edit->setFocus();
    dlg.exec();
    return result;
}

QString AiPanel::runShell(const QString& command, int timeoutMs, bool* timedOut, int* exitCode) {
    *timedOut = false; *exitCode = -1;
    QProcess p;
    proc_ = &p;
    p.setWorkingDirectory(host_.workDir ? host_.workDir() : QDir::currentPath());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("MAKDOWN", "1");                       // same marker as the console panel
    env.insert("TERM", "dumb"); env.insert("NO_COLOR", "1");
    env.insert("PAGER", "cat"); env.insert("GIT_PAGER", "cat"); env.insert("GIT_TERMINAL_PROMPT", "0");
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    platform::prepareShellCommand(&p, command);

    QByteArray out;
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    connect(&p, &QProcess::readyRead, &loop, [&] { const QByteArray b = p.readAll(); if (out.size() < (1 << 20)) out += b; });
    connect(&p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    connect(&p, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) loop.quit(); });
    connect(&timer, &QTimer::timeout, &loop, [&] { *timedOut = true; loop.quit(); });

    QPointer<AiPanel> self(this);
    p.start();
    p.closeWriteChannel();                            // no stdin: a command that asks a question fails instead of hanging
    if (!p.waitForStarted(5000)) { proc_ = nullptr; return "could not start the shell: " + p.errorString(); }
    timer.start(timeoutMs);
    if (p.state() != QProcess::NotRunning) loop.exec();
    if (!self) return QString();
    if (p.state() != QProcess::NotRunning) { platform::killProcessTree(&p); p.waitForFinished(2000); }
    out += p.readAll();
    proc_ = nullptr;
    *exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;

    static const QRegularExpression ansi("\x1b\\[[0-9;?]*[ -/]*[@-~]");
    QString text = QString::fromLocal8Bit(out);
    text.remove(ansi); text.remove('\r');
    return Ai::clip(text.trimmed(), 16000);
}
