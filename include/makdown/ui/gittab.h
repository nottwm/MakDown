// gittab.h - a Git tab (right-hand, next to the preview): clone a repository over http(s) or ssh into a path you choose.
//
// Runs the system "git" (no shell involved: the arguments are passed as a list). Prompts are disabled, so a
// private http(s) repo needs a credential helper or a token in the URL, and ssh needs a key the agent / ssh can use
// without asking (unknown hosts are accepted on first use unless you set GIT_SSH_COMMAND yourself).
#pragma once
#include <QProcess>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class GitTab : public QWidget {
    Q_OBJECT
public:
    explicit GitTab(QWidget* parent = nullptr);
    ~GitTab() override;

    void setDefaultParent(const QString& dir);     // folder suggested for new clones (the explorer root)
    void focusUrl();
    bool busy() const { return proc_ && proc_->state() != QProcess::NotRunning; }

    static bool validUrl(const QString& url);      // http(s)://, ssh://, git@host:path - nothing else
    static QString repoName(const QString& url);   // "https://x/y/foo.git" -> "foo"

signals:
    void openFolderRequested(const QString& dir);  // after a successful clone: "Open in explorer"

private:
    void browse();
    void startClone();
    void cancelClone();
    void onUrlEdited();
    void onOutput();
    void onFinished(int code, QProcess::ExitStatus st);
    void appendLog(const QString& line, bool replaceLast = false);
    void setRunning(bool on);

    QLineEdit *url_ = nullptr, *path_ = nullptr;
    QPushButton *browse_ = nullptr, *clone_ = nullptr, *cancel_ = nullptr, *open_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QLabel* status_ = nullptr;
    QProcess* proc_ = nullptr;
    QString defaultParent_, target_, tail_;
    bool pathEdited_ = false;                      // the user typed the path: stop auto-filling it from the URL
    bool cancelled_ = false;
};
