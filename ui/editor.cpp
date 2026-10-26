// editor.cpp - markdown editor widget + vim engine.
#include <makdown/ui/editor.h>
#include <makdown/ui/synhl.h>
#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QScrollBar>

// ============================================================================================
//  MdHighlighter
// ============================================================================================
void MdHighlighter::setTheme(const Theme& t) {
    auto fmt = [](const QString& color) { QTextCharFormat f; f.setForeground(QColor(color)); return f; };
    fHead = fmt(t.color("heading"));  fHead.setFontWeight(QFont::Bold);
    fQuote = fmt(t.color("muted"));   fQuote.setFontItalic(true);
    fList = fmt(t.color("accent"));   fList.setFontWeight(QFont::Bold);
    fBold = QTextCharFormat();        fBold.setFontWeight(QFont::Bold);
    fItalic = QTextCharFormat();      fItalic.setFontItalic(true);
    fCode = fmt(t.color("codeFg"));   fCode.setBackground(QColor(t.color("codeBg")));
    fLink = fmt(t.color("link"));
    fFence = fmt(t.color("codeFg"));
    fMuted = fmt(t.color("muted"));
    fStrike = QTextCharFormat();      fStrike.setFontStrikeOut(true);
    rehighlight();
}

void MdHighlighter::highlightBlock(const QString& text) {
    static const QRegularExpression rxFence(R"(^\s*(```|~~~))");
    static const QRegularExpression rxHead(R"(^\s{0,3}#{1,6}\s)");
    static const QRegularExpression rxQuote(R"(^\s*>)");
    static const QRegularExpression rxList(R"(^\s*([-*+]|\d+[.)])\s(\[[ xX]\]\s)?)");
    static const QRegularExpression rxHr(R"(^\s{0,3}([-*_])(\s*\1){2,}\s*$)");
    static const QRegularExpression rxBold(R"(\*\*[^*\n]+\*\*|__[^_\n]+__)");
    static const QRegularExpression rxItal(R"((?<![*\w])\*[^*\s][^*\n]*\*(?!\*)|(?<![\w_])_[^_\s][^_\n]*_(?![\w_]))");
    static const QRegularExpression rxCode(R"(`[^`\n]+`)");
    static const QRegularExpression rxLink(R"(!?\[[^\]\n]*\]\([^)\n]*\))");
    static const QRegularExpression rxStrike(R"(~~[^~\n]+~~)");

    // block state: 1 = we are inside a fenced code block, 0 = normal text
    const bool inFence = previousBlockState() == 1;
    if (rxFence.match(text).hasMatch()) {              // the ``` line itself toggles the state
        setFormat(0, text.size(), fMuted);
        setCurrentBlockState(inFence ? 0 : 1);
        return;
    }
    if (inFence) { setFormat(0, text.size(), fFence); setCurrentBlockState(1); return; }
    setCurrentBlockState(0);

    if (rxHead.match(text).hasMatch()) setFormat(0, text.size(), fHead);
    else if (rxQuote.match(text).hasMatch()) setFormat(0, text.size(), fQuote);
    else if (rxHr.match(text).hasMatch()) { setFormat(0, text.size(), fMuted); return; }
    auto ml = rxList.match(text);
    if (ml.hasMatch()) setFormat(ml.capturedStart(), ml.capturedLength(), fList);

    // inline styles are MERGED on top of whatever the line already has (so bold inside a heading stays coloured)
    auto apply = [&](const QRegularExpression& rx, const QTextCharFormat& f) {
        auto it = rx.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            for (int i = m.capturedStart(); i < m.capturedEnd(); ++i) {
                QTextCharFormat cur = format(i);
                cur.merge(f);
                setFormat(i, 1, cur);
            }
        }
    };
    apply(rxBold, fBold); apply(rxItal, fItalic); apply(rxStrike, fStrike);
    apply(rxLink, fLink); apply(rxCode, fCode);
}

// ============================================================================================
//  Gutter (line numbers)
// ============================================================================================
class GutterArea : public QWidget {
public:
    explicit GutterArea(MarkdownEditor* e) : QWidget(e), ed_(e) {}
    QSize sizeHint() const override { return QSize(ed_->gutterWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* ev) override { ed_->gutterPaint(ev); }
private:
    MarkdownEditor* ed_;
};

// ============================================================================================
//  MarkdownEditor: setup
// ============================================================================================
MarkdownEditor::MarkdownEditor(QWidget* parent) : QPlainTextEdit(parent) {
    gutter_ = new GutterArea(this);
    hl_ = new MdHighlighter(document());
    sh_ = new SyntaxHighlighter(nullptr);        // attached only while a syntax pack is in use
    setFrameShape(QFrame::NoFrame);
    setCursorWidth(2);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
        if (dy) gutter_->scroll(0, dy); else gutter_->update(0, r.y(), gutter_->width(), r.height());
        if (r.contains(viewport()->rect())) updateGutterWidth();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { updateExtraSelections(); gutter_->update(); });
    updateGutterWidth();
}

void MarkdownEditor::applyTheme(const Theme& t) {
    QFont f = ThemeStore::makeFont(t.editorFont, t.editorSize, true);
    setFont(f);
    gutter_->setFont(f);
    gutterBg_ = QColor(t.color("gutterBg")); gutterFg_ = QColor(t.color("gutterFg"));
    fg_ = QColor(t.color("editorFg"));      lineHl_ = QColor(t.color("lineHighlight"));
    muted_ = QColor(t.color("muted"));
    if (t.chrome == "glass") { gutterBg_.setAlpha(t.dark ? 50 : 70); lineHl_.setAlpha(t.dark ? 60 : 90); }   // let the glass show through
    findBg_ = QColor(t.color("accent")); findBg_.setAlpha(80);
    hl_->setTheme(t);
    sh_->setTheme(t);
    setTabWidthChars(tabWidth_);
    updateGutterWidth();
    updateExtraSelections();
    viewport()->update();
}

void MarkdownEditor::setTabWidthChars(int n) {
    tabWidth_ = qBound(1, n, 16);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * tabWidth_);
}
void MarkdownEditor::setWrap(bool on) { setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap); }
void MarkdownEditor::setLineNumbers(bool on) { lineNumbers_ = on; updateGutterWidth(); gutter_->setVisible(on); }
void MarkdownEditor::setHighlightLine(bool on) { hlLine_ = on; updateExtraSelections(); }
void MarkdownEditor::setShowParagraphMarks(bool on) { paraMarks_ = on; viewport()->update(); }
void MarkdownEditor::setShowSpaces(bool on) {
    QTextOption o = document()->defaultTextOption();
    QTextOption::Flags fl = o.flags();
    if (on) fl |= QTextOption::ShowTabsAndSpaces; else fl &= ~QTextOption::Flags(QTextOption::ShowTabsAndSpaces);
    o.setFlags(fl);
    document()->setDefaultTextOption(o);
}

int MarkdownEditor::gutterWidth() const {
    if (!lineNumbers_) return 0;
    int digits = 1, mx = qMax(1, blockCount());
    while (mx >= 10) { mx /= 10; ++digits; }
    return 20 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}
void MarkdownEditor::updateGutterWidth() { setViewportMargins(gutterWidth(), 0, 0, 0); }

void MarkdownEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    gutter_->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void MarkdownEditor::gutterPaint(QPaintEvent* ev) {
    QPainter p(gutter_);
    p.fillRect(ev->rect(), gutterBg_);
    p.setFont(font());
    QTextBlock b = firstVisibleBlock();
    int num = b.blockNumber();
    int top = qRound(blockBoundingGeometry(b).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(b).height());
    const int cur = textCursor().blockNumber();
    while (b.isValid() && top <= ev->rect().bottom()) {
        if (b.isVisible() && bottom >= ev->rect().top()) {
            p.setPen(num == cur ? fg_ : gutterFg_);                       // current line number is brighter
            p.drawText(0, top, gutter_->width() - 10, fontMetrics().height(), Qt::AlignRight, QString::number(num + 1));
        }
        b = b.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(b).height());
        ++num;
    }
}

void MarkdownEditor::updateExtraSelections() {
    QList<QTextEdit::ExtraSelection> sels;
    if (hlLine_ && !textCursor().hasSelection()) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(lineHl_);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = textCursor();
        s.cursor.clearSelection();
        sels << s;
    }
    for (const auto& r : findRanges_) {                          // find bar matches
        QTextEdit::ExtraSelection s;
        s.format.setBackground(findBg_);
        s.cursor = QTextCursor(document());
        s.cursor.setPosition(r.first); s.cursor.setPosition(r.second, QTextCursor::KeepAnchor);
        sels << s;
    }
    setExtraSelections(sels);
}

void MarkdownEditor::setFindMatches(const QVector<QPair<int, int>>& ranges) {
    if (ranges.isEmpty() && findRanges_.isEmpty()) return;
    findRanges_ = ranges;
    updateExtraSelections();
}

// Word-style formatting marks: ¶ at the end of every paragraph (block).
void MarkdownEditor::paintEvent(QPaintEvent* e) {
    QPlainTextEdit::paintEvent(e);
    if (!paraMarks_) return;
    QPainter p(viewport());
    p.setPen(muted_);
    const QPointF off = contentOffset();
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        const QRectF g = blockBoundingGeometry(b).translated(off);
        if (g.top() > e->rect().bottom()) break;
        if (g.bottom() < e->rect().top() || !b.isVisible()) continue;
        QTextLayout* lay = b.layout();
        if (!lay || lay->lineCount() == 0) continue;
        const QTextLine ln = lay->lineAt(lay->lineCount() - 1);          // last visual line of the paragraph
        const qreal x = g.left() + ln.naturalTextRect().right() + 3;
        p.drawText(QPointF(x, g.top() + ln.y() + ln.ascent()), QStringLiteral("\u00B6"));
    }
}

// ============================================================================================
//  Markdown helpers
// ============================================================================================
void MarkdownEditor::wrapSelection(const QString& pre, const QString& post) {
    QTextCursor c = textCursor();
    if (!c.hasSelection()) {                                   // nothing selected: insert pair, caret between
        c.insertText(pre + post);
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, post.size());
        setTextCursor(c);
        return;
    }
    QString sel = c.selectedText().replace(QChar::ParagraphSeparator, '\n');
    const int start = c.selectionStart();
    QString out;
    if (sel.size() >= pre.size() + post.size() && sel.startsWith(pre) && sel.endsWith(post))
        out = sel.mid(pre.size(), sel.size() - pre.size() - post.size());              // already wrapped -> unwrap
    else
        out = pre + sel + post;
    c.insertText(out);
    c.setPosition(start); c.setPosition(start + out.size(), QTextCursor::KeepAnchor);  // keep it selected: press again to undo the wrap
    setTextCursor(c);
}

void MarkdownEditor::toggleLinePrefix(const QString& prefix) {
    QTextCursor c = textCursor();
    const int a = document()->findBlock(c.selectionStart()).blockNumber();
    const int b = document()->findBlock(c.selectionEnd()).blockNumber();
    // if EVERY line already has the prefix we remove it, otherwise we add it
    bool all = true;
    for (int i = a; i <= b; ++i) if (!document()->findBlockByNumber(i).text().startsWith(prefix)) all = false;
    c.beginEditBlock();
    for (int i = a; i <= b; ++i) {
        QTextCursor l(document()->findBlockByNumber(i));
        if (all) { l.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, prefix.size()); l.removeSelectedText(); }
        else l.insertText(prefix);
    }
    c.endEditBlock();
}

void MarkdownEditor::changeHeading(int delta) {
    QTextCursor c = textCursor();
    const QString line = c.block().text();
    static const QRegularExpression rx(R"(^(#{0,6})\s*)");
    const auto m = rx.match(line);
    const int level = qBound(0, m.captured(1).size() + delta, 6);
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, m.capturedLength());
    c.insertText(level ? QString(level, QChar('#')) + " " : QString());
    c.endEditBlock();
}

void MarkdownEditor::insertLink() {
    QTextCursor c = textCursor();
    const QString txt = c.hasSelection() ? c.selectedText() : QStringLiteral("text");
    const int start = c.selectionStart();
    c.insertText("[" + txt + "](url)");
    c.setPosition(start + txt.size() + 3);                      // select the word "url" so it can be typed over
    c.setPosition(start + txt.size() + 6, QTextCursor::KeepAnchor);
    setTextCursor(c);
}

void MarkdownEditor::gotoLine(int n) {
    QTextBlock b = document()->findBlockByNumber(qBound(0, n - 1, blockCount() - 1));
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
}

bool MarkdownEditor::findNext(const QString& text, bool backwards) {
    if (text.isEmpty()) return false;
    lastSearch_ = text;
    QTextDocument::FindFlags fl; if (backwards) fl |= QTextDocument::FindBackward;
    QTextCursor from = textCursor();
    if (!backwards) { from.clearSelection(); if (!from.atEnd()) from.movePosition(QTextCursor::Right); }
    else            { from.setPosition(from.selectionStart()); }
    QTextCursor hit = document()->find(text, from, fl);
    if (hit.isNull()) {                                          // wrap around
        QTextCursor edge(document());
        edge.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        hit = document()->find(text, edge, fl);
    }
    if (hit.isNull()) return false;
    if (vim_) hit.setPosition(hit.selectionStart());             // vim puts the caret at the match start
    setTextCursor(hit);
    ensureCursorVisible();
    return true;
}

// Enter on "- item" / "1. item" / "- [ ] task" starts the next item (and an empty item ends the list).
bool MarkdownEditor::continueList() {
    QTextCursor c = textCursor();
    if (c.hasSelection()) return false;
    const QString line = c.block().text();
    static const QRegularExpression rx(R"(^(\s*)([-*+]|\d+[.)])(\s+)(\[[ xX]\]\s+)?(.*)$)");
    const auto m = rx.match(line);
    if (!m.hasMatch() || c.positionInBlock() != line.size()) return false;
    c.beginEditBlock();
    if (m.captured(5).isEmpty()) {                               // empty item -> remove the marker, end the list
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else {
        QString marker = m.captured(2);
        if (marker[0].isDigit()) marker = QString::number(marker.left(marker.size() - 1).toInt() + 1) + marker.right(1);
        c.insertText("\n" + m.captured(1) + marker + m.captured(3) + (m.captured(4).isEmpty() ? "" : "[ ] "));
    }
    c.endEditBlock();
    setTextCursor(c);
    return true;
}

void MarkdownEditor::indentLines(bool outdent) {
    QTextCursor c = textCursor();
    const int a = document()->findBlock(c.selectionStart()).blockNumber();
    const int b = document()->findBlock(c.selectionEnd()).blockNumber();
    const QString pad(tabWidth_, ' ');
    c.beginEditBlock();
    for (int i = a; i <= b; ++i) {
        QTextCursor l(document()->findBlockByNumber(i));
        if (!outdent) { l.insertText(pad); continue; }
        const QString t = l.block().text();
        int n = 0; while (n < tabWidth_ && n < t.size() && t[n] == ' ') ++n;
        if (n == 0 && !t.isEmpty() && t[0] == '\t') n = 1;
        l.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, n);
        l.removeSelectedText();
    }
    c.endEditBlock();
}

void MarkdownEditor::moveLine(int dir) {                          // Alt+Up / Alt+Down
    QTextCursor c = textCursor();
    QTextBlock b = c.block(), o = dir < 0 ? b.previous() : b.next();
    if (!o.isValid()) return;
    const int col = c.positionInBlock(), nb = b.blockNumber(), no = o.blockNumber();
    const QString tb = b.text(), to = o.text();
    c.beginEditBlock();
    auto setLine = [&](int num, const QString& text) {
        QTextCursor l(document()->findBlockByNumber(num));
        l.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        l.insertText(text);
    };
    setLine(nb, to); setLine(no, tb);
    c.endEditBlock();
    QTextCursor n(document()->findBlockByNumber(no));
    n.setPosition(n.position() + qMin(col, tb.size()));
    setTextCursor(n);
}

void MarkdownEditor::duplicateLine() {                            // Ctrl+Shift+D
    QTextCursor c = textCursor();
    const QString t = c.block().text();
    c.beginEditBlock();
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText("\n" + t);
    c.endEditBlock();
    setTextCursor(c);
}

// ============================================================================================
//  Keyboard handling
// ============================================================================================
void MarkdownEditor::keyPressEvent(QKeyEvent* e) {
    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool alt   = e->modifiers() & Qt::AltModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const int  key   = e->key();

    if (!vim_ && key == Qt::Key_Escape) emit escapePressed();     // lets the find bar close itself

    // --- extra hotkeys that vimkeys adds (work in every vim mode) ---
    if (vim_) {
        if (alt && key == Qt::Key_Up)   { moveLine(-1); return; }
        if (alt && key == Qt::Key_Down) { moveLine(+1); return; }
        if (ctrl && shift && key == Qt::Key_D) { duplicateLine(); return; }
    }

    // --- vim normal / visual modes are handled by the vim engine ---
    if (vim_ && mode_ != Mode::Insert) { vimKey(e); return; }

    // --- insert mode (or vim disabled) ---
    if (vim_ && (key == Qt::Key_Escape || (ctrl && key == Qt::Key_BracketLeft))) {
        QTextCursor c = textCursor();
        if (c.positionInBlock() > 0) c.movePosition(QTextCursor::Left);
        setTextCursor(c);
        setMode(Mode::Normal);
        return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !ctrl && continueList()) return;
    if (key == Qt::Key_Tab && !ctrl)     { if (textCursor().hasSelection()) indentLines(false); else insertPlainText(QString(tabWidth_, ' ')); return; }
    if (key == Qt::Key_Backtab)          { indentLines(true); return; }
    QPlainTextEdit::keyPressEvent(e);
}

// ============================================================================================
//  Vim engine
// ============================================================================================
void MarkdownEditor::setMarkdownHighlighting(bool on) {
    sh_->setDocument(nullptr);
    hl_->setDocument(on ? document() : nullptr);
}

void MarkdownEditor::setSyntaxPack(const SyntaxPtr& pack) {
    hl_->setDocument(nullptr);
    sh_->setDocument(nullptr);                   // detach first: setPack() must not re-colour the old text
    sh_->setPack(pack);
    if (pack) sh_->setDocument(document());      // attaching re-highlights the whole document
}

void MarkdownEditor::setVimEnabled(bool on) {
    if (on == vim_) return;                      // applyAll() calls this often; don't knock insert mode back to normal
    vim_ = on;
    pending_.clear(); count_ = 0;
    if (on) setMode(Mode::Normal);
    else { setMode(Mode::Insert); emit vimModeChanged(QString()); }
}

void MarkdownEditor::setMode(Mode m) {
    mode_ = m;
    setOverwriteMode(vim_ && m != Mode::Insert);     // overwrite-mode == block caret in a QPlainTextEdit
    pending_.clear(); count_ = 0;
    emitStatus();
}
void MarkdownEditor::enterInsert() { setMode(Mode::Insert); }

void MarkdownEditor::emitStatus() {
    if (!vim_) return;
    QString t;
    switch (mode_) {
        case Mode::Normal:     t = "-- NORMAL --"; break;
        case Mode::Insert:     t = "-- INSERT --"; break;
        case Mode::Visual:     t = "-- VISUAL --"; break;
        case Mode::VisualLine: t = "-- V-LINE --"; break;
    }
    if (count_ || !pending_.isEmpty()) t += "   " + (count_ ? QString::number(count_) : QString()) + pending_;
    emit vimModeChanged(t);
}

void MarkdownEditor::leaveVisual() {
    QTextCursor c = textCursor();
    c.clearSelection();
    setTextCursor(c);
    setMode(Mode::Normal);
}

// Visual-line: select whole lines between the anchor and the moving head.
void MarkdownEditor::updateVisualLine() {
    QTextDocument* d = document();
    const int lo = qMin(visAnchor_, visHead_), hi = qMax(visAnchor_, visHead_);
    const QTextBlock bl = d->findBlock(lo), bh = d->findBlock(hi);
    QTextCursor c(d);
    c.setPosition(bl.position());
    c.setPosition(qMin(bh.position() + bh.length(), d->characterCount() - 1), QTextCursor::KeepAnchor);
    setTextCursor(c);
}

namespace {
int charClass(QChar ch) {     // 0 = whitespace, 1 = word character, 2 = punctuation  (vim's "word" notion)
    if (ch.isNull() || ch.isSpace()) return 0;
    return (ch.isLetterOrNumber() || ch == '_') ? 1 : 2;
}
}

// Executes a vim motion on cursor c. Returns false if `key` isn't a motion.
//   n        : repeat count (>=1)       rawCount : count as typed (0 = none; needed by G / gg)
//   forOp    : motion is the target of d/c/y  (inclusive/exclusive details differ)
bool MarkdownEditor::doMotion(const QString& key, int n, int rawCount, QTextCursor& c,
                              QTextCursor::MoveMode mm, bool forOp) {
    QTextDocument* d = document();
    const int last = d->characterCount() - 1;                       // last valid cursor position
    auto at = [&](int p) { return p >= 0 && p <= last ? d->characterAt(p) : QChar(); };

    if (key == "h") { for (int i = 0; i < n && c.positionInBlock() > 0; ++i) c.movePosition(QTextCursor::Left, mm); }
    else if (key == "l") {
        const int lim = c.block().length() - 1 - ((forOp || c.block().length() <= 1) ? 0 : 1);   // normal mode can't sit on the newline
        for (int i = 0; i < n && c.positionInBlock() < lim; ++i) c.movePosition(QTextCursor::Right, mm);
    }
    else if (key == "j") c.movePosition(QTextCursor::Down, mm, n);
    else if (key == "k") c.movePosition(QTextCursor::Up, mm, n);
    else if (key == "0") c.movePosition(QTextCursor::StartOfBlock, mm);
    else if (key == "^") {
        c.movePosition(QTextCursor::StartOfBlock, mm);
        while (at(c.position()) == ' ' || at(c.position()) == '\t') c.movePosition(QTextCursor::Right, mm);
    }
    else if (key == "$") {
        c.movePosition(QTextCursor::EndOfBlock, mm);
        if (!forOp && c.positionInBlock() > 0) c.movePosition(QTextCursor::Left, mm);
    }
    else if (key == "w") {                                          // next word start
        int p = c.position();
        for (int i = 0; i < n; ++i) {
            const int cl = charClass(at(p));
            while (p < last && cl != 0 && charClass(at(p)) == cl) ++p;
            while (p < last && charClass(at(p)) == 0) ++p;
        }
        c.setPosition(p, mm);
    }
    else if (key == "b") {                                          // previous word start
        int p = c.position();
        for (int i = 0; i < n; ++i) {
            if (p > 0) --p;
            while (p > 0 && charClass(at(p)) == 0) --p;
            const int cl = charClass(at(p));
            while (p > 0 && charClass(at(p - 1)) == cl) --p;
        }
        c.setPosition(p, mm);
    }
    else if (key == "e") {                                          // end of word
        int p = c.position();
        for (int i = 0; i < n; ++i) {
            if (p < last) ++p;
            while (p < last && charClass(at(p)) == 0) ++p;
            const int cl = charClass(at(p));
            while (p < last && charClass(at(p + 1)) == cl) ++p;
        }
        if (forOp && p < last) ++p;                                 // operators include the last character
        c.setPosition(p, mm);
    }
    else if (key == "gg" || key == "G") {
        QTextBlock b = (key == "G" && rawCount == 0) ? d->lastBlock()
                     : d->findBlockByNumber(rawCount > 0 ? qMin(rawCount, d->blockCount()) - 1 : 0);
        c.setPosition(b.position(), mm);
    }
    else if (key == "}" || key == "{") {                            // paragraph motions: next / previous blank line
        const bool fwd = key == "}";
        QTextBlock b = fwd ? c.block().next() : c.block().previous();
        while (b.isValid() && b.text().trimmed().isEmpty()) b = fwd ? b.next() : b.previous();   // skip blanks we're on
        while (b.isValid() && !b.text().trimmed().isEmpty()) b = fwd ? b.next() : b.previous();
        if (!b.isValid()) c.movePosition(fwd ? QTextCursor::End : QTextCursor::Start, mm);
        else c.setPosition(b.position(), mm);
    }
    else return false;
    return true;
}

// Applies d / y / c to the selection held by cursor c (charwise).
void MarkdownEditor::opRange(QChar op, QTextCursor c) {
    reg_ = c.selectedText().replace(QChar::ParagraphSeparator, '\n');
    regLine_ = false;
    if (op == 'y') {
        const int s = c.selectionStart();
        c.clearSelection(); c.setPosition(s); setTextCursor(c);
        return;
    }
    c.removeSelectedText();
    setTextCursor(c);
    if (op == 'c') enterInsert();
}

// dd / yy / cc on n whole lines starting at the cursor line.
void MarkdownEditor::opLines(QChar op, int n) {
    QTextDocument* d = document();
    const QTextBlock b0 = textCursor().block();
    QTextBlock b1 = b0;
    for (int i = 1; i < n && b1.next().isValid(); ++i) b1 = b1.next();
    const int start = b0.position();
    const int endWithNl = qMin(b1.position() + b1.length(), d->characterCount() - 1);
    QTextCursor s(d);
    s.setPosition(start); s.setPosition(endWithNl, QTextCursor::KeepAnchor);
    QString text = s.selectedText().replace(QChar::ParagraphSeparator, '\n');
    if (!text.endsWith('\n')) text += '\n';
    reg_ = text; regLine_ = true;
    if (op == 'y') return;
    if (op == 'c') {                                  // keep one empty line to type into
        s.setPosition(start); s.setPosition(b1.position() + b1.length() - 1, QTextCursor::KeepAnchor);
        s.removeSelectedText(); setTextCursor(s); enterInsert();
        return;
    }
    const bool tailLines = !b1.next().isValid();      // evaluate BEFORE the blocks are deleted
    s.beginEditBlock();
    s.removeSelectedText();
    if (tailLines && start > 0) {          // deleted the last lines -> also drop the newline before them
        QTextCursor p(d); p.setPosition(start - 1); p.setPosition(start, QTextCursor::KeepAnchor); p.removeSelectedText();
        s.setPosition(qMax(0, start - 1));
        s.movePosition(QTextCursor::StartOfBlock);
    }
    s.endEditBlock();
    setTextCursor(s);
}

void MarkdownEditor::pasteReg(bool after) {
    if (reg_.isEmpty()) return;
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (regLine_) {
        QString t = reg_; if (t.endsWith('\n')) t.chop(1);
        if (after) { c.movePosition(QTextCursor::EndOfBlock); c.insertText("\n" + t); c.movePosition(QTextCursor::StartOfBlock); }
        else       { c.movePosition(QTextCursor::StartOfBlock); c.insertText(t + "\n"); c.movePosition(QTextCursor::Up); }
    } else {
        if (after && !c.atBlockEnd()) c.movePosition(QTextCursor::Right);
        c.insertText(reg_);
        c.movePosition(QTextCursor::Left);
    }
    c.endEditBlock();
    setTextCursor(c);
}

// The big one: one key press while in Normal / Visual / Visual-line mode.
void MarkdownEditor::vimKey(QKeyEvent* e) {
    const int key = e->key();
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool visual = mode_ == Mode::Visual || mode_ == Mode::VisualLine;
    auto done = [&]() { pending_.clear(); count_ = 0; emitStatus(); ensureCursorVisible(); };

    // ---- Escape cancels pending input / leaves visual mode
    if (key == Qt::Key_Escape || (ctrl && key == Qt::Key_BracketLeft)) {
        done();
        if (visual) leaveVisual();
        return;
    }
    // ---- Ctrl combos
    if (ctrl) {
        if (key == Qt::Key_R) { redo(); return; }
        if (key == Qt::Key_D || key == Qt::Key_U) {                           // half page down / up
            const int lines = qMax(1, viewport()->height() / fontMetrics().lineSpacing() / 2);
            QTextCursor c = textCursor();
            c.movePosition(key == Qt::Key_D ? QTextCursor::Down : QTextCursor::Up,
                           visual && mode_ == Mode::Visual ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor, lines);
            setTextCursor(c); ensureCursorVisible();
            return;
        }
        // copy / paste / select-all etc. keep working
        if (key == Qt::Key_C || key == Qt::Key_V || key == Qt::Key_X || key == Qt::Key_A || key == Qt::Key_Z || key == Qt::Key_Y)
            QPlainTextEdit::keyPressEvent(e);
        return;
    }

    // ---- translate special keys to their vim letters
    QString t = e->text();
    switch (key) {
        case Qt::Key_Left:  t = "h"; break;   case Qt::Key_Right: t = "l"; break;
        case Qt::Key_Up:    t = "k"; break;   case Qt::Key_Down:  t = "j"; break;
        case Qt::Key_Home:  t = "0"; break;   case Qt::Key_End:   t = "$"; break;
        case Qt::Key_Backspace: t = "h"; break;
        case Qt::Key_Return: case Qt::Key_Enter: t = "j"; break;
        default: break;
    }
    if (t.size() != 1 || !t[0].isPrint()) return;       // ignore anything else (Delete, F-keys, ...)

    // ---- pending "r": replace character under caret
    if (pending_ == "r") {
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        if (c.hasSelection() && !c.atBlockEnd()) { c.insertText(t); c.movePosition(QTextCursor::Left); setTextCursor(c); }
        done(); return;
    }
    // ---- numeric prefix  (0 only counts when already typing a number)
    if (t[0].isDigit() && !(t == "0" && count_ == 0)) { count_ = count_ * 10 + t.toInt(); emitStatus(); return; }
    const int n = qMax(1, count_);
    const int raw = count_;

    // ---- "gg"
    if (pending_ == "g") {
        QTextCursor c = (mode_ == Mode::VisualLine) ? QTextCursor(document()) : textCursor();
        if (mode_ == Mode::VisualLine) c.setPosition(visHead_);
        if (t == "g" && doMotion("gg", n, raw, c, mode_ == Mode::Visual ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor, false)) {
            if (mode_ == Mode::VisualLine) { visHead_ = c.position(); updateVisualLine(); } else setTextCursor(c);
        }
        done(); return;
    }
    // ---- ">>" / "<<"
    if (pending_ == ">" || pending_ == "<") { if (t == pending_) indentLines(pending_ == "<"); done(); return; }

    // ---- operator pending: d / c / y waiting for a motion or text object
    if (pending_.size() >= 1 && QString("dcy").contains(pending_[0])) {
        const QChar op = pending_[0];
        if (pending_.size() == 2) {                                  // text objects: diw, caw ...
            if (t == "w") {
                QTextCursor c = textCursor(); c.select(QTextCursor::WordUnderCursor);
                if (c.hasSelection()) opRange(op, c);
            }
            done(); return;
        }
        if (t == "i" || t == "a") { pending_ += t; emitStatus(); return; }
        if (t == QString(op)) { opLines(op, n); done(); return; }    // dd / yy / cc
        if (t == "j") { opLines(op, n + 1); done(); return; }        // dj: this line + n below
        if (t == "k") {
            QTextCursor c = textCursor(); c.movePosition(QTextCursor::Up, QTextCursor::MoveAnchor, n); setTextCursor(c);
            opLines(op, n + 1); done(); return;
        }
        QString motion = t;
        if (op == 'c' && t == "w") motion = "e";                     // vim quirk: cw behaves like ce
        QTextCursor c = textCursor(); c.clearSelection();
        const QTextBlock startBlock = c.block();
        if (doMotion(motion, n, raw, c, QTextCursor::KeepAnchor, true)) {
            if (motion == "w" && c.block() != startBlock) {          // dw never joins lines
                c.setPosition(startBlock.position() + startBlock.length() - 1, QTextCursor::KeepAnchor);
            }
            if (c.hasSelection()) opRange(op, c);
        }
        done(); return;
    }

    // ---- visual-mode operators act on the selection
    if (visual && QString("dxycXD").contains(t)) {
        QTextCursor c = textCursor();
        if (mode_ == Mode::Visual) {                                 // vim's visual selection is inclusive of the caret char
            int a = c.anchor(), p = c.position(); const int lastPos = document()->characterCount() - 1;
            if (p >= a) p = qMin(p + 1, lastPos); else a = qMin(a + 1, lastPos);
            c.setPosition(a); c.setPosition(p, QTextCursor::KeepAnchor);
        }
        const bool lines = mode_ == Mode::VisualLine;
        const QChar op = (t == "y") ? 'y' : (t == "c" || t == "C") ? 'c' : 'd';
        opRange(op, c);
        regLine_ = lines;
        if (lines && !reg_.endsWith('\n')) reg_ += '\n';
        if (mode_ != Mode::Insert) setMode(op == 'c' ? Mode::Insert : Mode::Normal);
        done(); return;
    }

    // ---- motions (move caret; in visual mode they extend the selection)
    {
        QTextCursor c = textCursor();
        if (mode_ == Mode::VisualLine) { c = QTextCursor(document()); c.setPosition(visHead_); }
        const auto mm = (mode_ == Mode::Visual) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
        if (t != "g" && doMotion(t, n, raw, c, mm, false)) {
            if (mode_ == Mode::VisualLine) { visHead_ = c.position(); updateVisualLine(); } else setTextCursor(c);
            done(); return;
        }
    }

    // ---- everything else
    QTextCursor c = textCursor();
    const QChar k = t[0];
    switch (k.unicode()) {
        case 'i': enterInsert(); return;
        case 'a': if (!c.atBlockEnd()) c.movePosition(QTextCursor::Right); setTextCursor(c); enterInsert(); return;
        case 'I': doMotion("^", 1, 0, c, QTextCursor::MoveAnchor, false); setTextCursor(c); enterInsert(); return;
        case 'A': c.movePosition(QTextCursor::EndOfBlock); setTextCursor(c); enterInsert(); return;
        case 'o':
            c.movePosition(QTextCursor::EndOfBlock); setTextCursor(c);
            if (!continueList()) { c.insertBlock(); setTextCursor(c); }
            enterInsert(); return;
        case 'O':
            c.movePosition(QTextCursor::StartOfBlock); c.insertBlock(); c.movePosition(QTextCursor::PreviousBlock);
            setTextCursor(c); enterInsert(); return;
        case 'x': case 's': {
            QTextCursor s = textCursor();
            for (int i = 0; i < n && !s.atBlockEnd(); ++i) s.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            if (s.hasSelection()) opRange(k == 's' ? 'c' : 'd', s); else if (k == 's') enterInsert();
            done(); return;
        }
        case 'X': {
            QTextCursor s = textCursor();
            for (int i = 0; i < n && s.positionInBlock() > 0; ++i) s.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
            if (s.hasSelection()) opRange('d', s);
            done(); return;
        }
        case 'D': case 'C': {
            QTextCursor s = textCursor(); s.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            if (s.hasSelection()) opRange(k == 'D' ? 'd' : 'c', s); else if (k == 'C') enterInsert();
            done(); return;
        }
        case 'p': pasteReg(true);  done(); return;
        case 'P': pasteReg(false); done(); return;
        case 'u': for (int i = 0; i < n; ++i) undo(); done(); return;
        case 'J': {
            c.movePosition(QTextCursor::EndOfBlock);
            if (!c.atEnd()) { c.deleteChar(); c.insertText(" "); c.movePosition(QTextCursor::Left); setTextCursor(c); }
            done(); return;
        }
        case '~': {
            QTextCursor s = textCursor(); s.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            if (s.hasSelection() && !s.atBlockEnd() ) {
                const QString ch = s.selectedText();
                s.insertText(ch == ch.toUpper() ? ch.toLower() : ch.toUpper());
            }
            done(); return;
        }
        case 'd': case 'c': case 'y': case 'g': case 'r': case '>': case '<':
            if (visual && (k == '>' || k == '<')) { indentLines(k == '<'); leaveVisual(); done(); return; }
            pending_ = t; emitStatus(); return;
        case 'v':
            if (mode_ == Mode::Visual) leaveVisual(); else { setMode(Mode::Visual); }
            return;
        case 'V':
            if (mode_ == Mode::VisualLine) { leaveVisual(); return; }
            setMode(Mode::VisualLine);
            visAnchor_ = visHead_ = textCursor().position();
            updateVisualLine();
            return;
        case ':': case '/': emit commandRequested(t); done(); return;      // MainWindow shows the command line
        case 'n': case 'N': findNext(lastSearch_, k == 'N'); done(); return;
        default: break;
    }
    done();
}
