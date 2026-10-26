// findbar.h - VS Code style find / replace bar above the editor (used when vim keys are off).
//   Ctrl+F find, Ctrl+H replace, Enter / Shift+Enter next / previous, F3 / Shift+F3, Esc closes,
//   toggles: Alt+C match case, Alt+W whole word, Alt+R regex; replace: Enter = replace, Ctrl+Alt+Enter = replace all.
//   Every match is highlighted in the editor and the bar shows "3 of 12".  Replacements accept $1..$9 in regex mode.
#pragma once
#include <QPair>
#include <QTimer>
#include <QVector>
#include <QWidget>

class MarkdownEditor;
class QLabel;
class QLineEdit;
class QToolButton;

class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(MarkdownEditor* editor, QWidget* parent = nullptr);

    void open(bool withReplace);                 // show, seed from the selection / word under the caret, focus
    void close();                                // hide, clear the highlights, focus back to the editor
    void step(bool backwards);                   // F3 / Shift+F3 (also works while the bar is hidden: reopens it)

signals:
    void message(const QString& text);           // status-bar text ("3 replaced")

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refresh(bool jump);                     // recompute matches, update highlights + counter
    void select(int idx);
    int  nextIndex(bool backwards) const;
    void replaceOne();
    void replaceAll();
    QString expand(const QString& repl, const QString& docText, int start) const;

    MarkdownEditor* ed_;
    QLineEdit *find_ = nullptr, *repl_ = nullptr;
    QToolButton *case_ = nullptr, *word_ = nullptr, *re_ = nullptr, *prev_ = nullptr, *next_ = nullptr, *toggle_ = nullptr, *close_ = nullptr;
    QToolButton *replOne_ = nullptr, *replAll_ = nullptr;
    QWidget* replRow_ = nullptr;
    QLabel* count_ = nullptr;
    QVector<QPair<int, int>> matches_;           // [start, end) in document positions
    int cur_ = -1;
    QTimer docTimer_;                            // re-run after the document changes
    bool selfEdit_ = false;
};
