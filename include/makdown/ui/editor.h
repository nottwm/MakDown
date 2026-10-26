// editor.h - the markdown editor widget.
//
//  Features
//   * line-number gutter, current-line highlight, markdown syntax highlighting
//   * Word-style formatting marks: pilcrow (¶) after every paragraph, dots for spaces, arrows for tabs
//   * "vimkeys": real modal editing (normal / insert / visual / visual-line / ex-line)
//     plus extra hotkeys (Alt+Up/Down move line, Ctrl+Shift+D duplicate line ...)
//   * markdown helpers: bold/italic/code/link wrapping, heading up/down, list continuation on Enter
#pragma once
#include <QPair>
#include <QPlainTextEdit>
#include <QVector>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <makdown/core/syntax.h>
#include <makdown/core/theme.h>

// --------------------------------------------------------------------------------------------
// Colours the markdown source while you type (headings, emphasis, code, links, quotes, lists...)
// --------------------------------------------------------------------------------------------
class MdHighlighter : public QSyntaxHighlighter {
public:
    explicit MdHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {}
    void setTheme(const Theme& t);               // rebuilds the formats from theme colours
protected:
    void highlightBlock(const QString& text) override;   // called by Qt for every changed line
private:
    QTextCharFormat fHead, fQuote, fList, fBold, fItalic, fCode, fLink, fFence, fMuted, fStrike;
};

class SyntaxHighlighter;   // generic pack-driven highlighter (ui/synhl.h)
class GutterArea;   // line-number strip drawn on the left of the editor

class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    enum class Mode { Normal, Insert, Visual, VisualLine };

    explicit MarkdownEditor(QWidget* parent = nullptr);

    // ---- configuration (called by MainWindow) ----
    void applyTheme(const Theme& t);
    void setVimEnabled(bool on);
    void setMarkdownHighlighting(bool on);       // off for .html/.js/.css etc. (markdown colours would be wrong); also detaches any syntax pack
    void setSyntaxPack(const SyntaxPtr& pack);   // colour the buffer with a syntax pack (null = no highlighting)
    bool vimEnabled() const { return vim_; }
    void setShowParagraphMarks(bool on);
    void setShowSpaces(bool on);
    void setLineNumbers(bool on);
    void setHighlightLine(bool on);
    void setTabWidthChars(int n);
    void setWrap(bool on);

    // ---- markdown editing helpers (menu / hotkeys) ----
    void wrapSelection(const QString& pre, const QString& post);  // toggles **bold**, *italic*, `code` ...
    void toggleLinePrefix(const QString& prefix);                  // "- ", "> ", "- [ ] " on the selected lines
    void changeHeading(int delta);                                 // # level up / down on the current line
    void insertLink();
    void gotoLine(int oneBased);
    bool findNext(const QString& text, bool backwards = false);   // wraps around the document (vim "/", n, N)
    QString lastSearch() const { return lastSearch_; }
    void setFindMatches(const QVector<QPair<int, int>>& ranges);   // find bar: highlight [start, end) ranges (empty clears)

    // ---- gutter plumbing (used by GutterArea) ----
    int  gutterWidth() const;
    void gutterPaint(QPaintEvent* ev);

signals:
    void vimModeChanged(const QString& text);       // e.g. "-- NORMAL --" (empty when vim is off)
    void commandRequested(const QString& prefix);   // vim ':' or '/' pressed -> MainWindow shows the command line
    void escapePressed();                           // Esc with vim off (closes the find bar)

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;       // draws ¶ marks on top of the text
    void resizeEvent(QResizeEvent* e) override;

private:
    // -- setup / appearance
    void updateGutterWidth();
    void updateExtraSelections();
    // -- generic editing
    bool continueList();                            // Enter inside "- item" starts the next item
    void indentLines(bool outdent);
    void moveLine(int dir);
    void duplicateLine();
    // -- vim engine
    void vimKey(QKeyEvent* e);
    bool doMotion(const QString& key, int n, int rawCount, QTextCursor& c, QTextCursor::MoveMode mm, bool forOp);
    void opRange(QChar op, QTextCursor c);          // d / y / c on a character range
    void opLines(QChar op, int n);                  // dd / yy / cc on n lines
    void pasteReg(bool after);
    void setMode(Mode m);
    void enterInsert();
    void leaveVisual();
    void updateVisualLine();
    void emitStatus();

    // -- state
    bool vim_ = false, paraMarks_ = false, lineNumbers_ = true, hlLine_ = true;
    int  tabWidth_ = 4;
    Mode mode_ = Mode::Insert;
    QString pending_;                // operator waiting for a motion ("d", "c", "y", "g", "r", ">", "<", "di" ...)
    int count_ = 0;                  // numeric prefix (3dd, 5j ...)
    QString reg_;                    // the unnamed yank register
    bool regLine_ = false;           // register holds whole lines
    int visAnchor_ = 0, visHead_ = 0;// visual-line bookkeeping
    QString lastSearch_;
    QVector<QPair<int, int>> findRanges_;
    QColor findBg_;

    GutterArea* gutter_ = nullptr;
    MdHighlighter* hl_ = nullptr;
    SyntaxHighlighter* sh_ = nullptr;
    QColor gutterBg_, gutterFg_, fg_, lineHl_, muted_;
};
