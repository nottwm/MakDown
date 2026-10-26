// console.h - a console panel under the editor: runs the user's default shell as a login + interactive
// shell (so ~/.profile / ~/.bashrc / ~/.zshrc are sourced), in the explorer's folder.
//
//  Linux / macOS: the shell runs inside a pty (via the system "script" tool) so prompts, colours, job control
//                 and Ctrl+C behave. Windows: %COMSPEC% through pipes. It is a line-based console, not a full
//                 terminal emulator: full-screen programs (vim, top, less) will not work.
#pragma once
#include <QByteArray>
#include <QProcess>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <makdown/core/theme.h>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    ~ConsolePanel() override;
    void applyTheme(const Theme& t);
    void setWorkingDirectory(const QString& dir);   // used the next time the shell starts
    void ensureStarted();                           // starts the shell on first use
    void focusInput();
    void restart();
signals:
    void closeRequested();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Cell { QChar ch; quint8 fg = 0; bool bold = false; };   // fg: 0 default, 1-8 normal, 9-16 bright
    void startShell();
    void onOutput();
    void feed(const QString& text);                 // mini terminal: \r \b \n, SGR colours, erase-line, ignores the rest
    void renderLiveLine();
    void commitLine();
    void sendLine();
    void sendRaw(const QByteArray& b);
    void clearOutput();
    QColor colorFor(quint8 fg) const;

    QPlainTextEdit* out_ = nullptr;
    QLineEdit* in_ = nullptr;
    QLabel* title_ = nullptr;
    QProcess* proc_ = nullptr;
    QString dir_;
    QString shellName_;
    bool pty_ = false;                              // the shell has a (script-provided) pty: it echoes by itself
    bool exited_ = false;
    QByteArray pending_;                            // incomplete UTF-8 tail between reads
    QStringList history_; int histPos_ = 0;
    Theme theme_; bool haveTheme_ = false;

    QVector<Cell> line_; int col_ = 0;              // the line being written (last block of the document)
    quint8 curFg_ = 0; bool curBold_ = false;
    QString escBuf_; int escState_ = 0;             // 0 text, 1 after ESC, 2 in CSI, 3 in OSC
};
