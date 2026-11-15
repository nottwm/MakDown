// aipanel.h - the AI assistant: a chat page in the side bar.
//
//  The panel runs the agent loop: it sends the conversation to the selected provider (core/ai.h), looks for
//  <makdown_tool> blocks in the answer, runs them against the app through AiHost, sends the results back and
//  repeats until the model answers without a tool call (or maxSteps is reached, or the user presses Stop).
//
//  Tools that change things (creating / overwriting files, running commands) ask the user first unless the
//  user turned that off in Settings > Preferences > AI. Files stay inside the working folder (Ai::resolveInside).
#pragma once
#include <QList>
#include <QWidget>
#include <functional>
#include <makdown/core/ai.h>
#include <makdown/core/theme.h>

class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QToolButton;
class QProcess;

// What the panel needs from the application. MainWindow fills it in; every member may be left empty.
struct AiHost {
    std::function<QString()> workDir;                    // the explorer's folder (the tools' sandbox)
    std::function<QString()> currentFile;                // path of the file in the editor, empty = untitled
    std::function<QString()> editorText;                 // the editor's text, saved or not
    std::function<QString()> selection;                  // the editor's selected text
    // If `path` is the file open in the editor: replace its text (one undo step), save it, return true.
    std::function<bool(const QString& path, const QString& text)> applyToOpenFile;
    std::function<void(const QString& path)> openFile;   // show a file in the editor
    std::function<void(const QString& text)> newDocument;// a new untitled document with this text
};

class AiPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiPanel(QWidget* parent = nullptr);
    ~AiPanel() override;

    void setHost(const AiHost& host) { host_ = host; }
    void applyTheme(const Theme& t);
    void refresh();                                      // provider / key / model changed in Settings
    void newChat();
    void focusInput();
    bool busy() const { return busy_; }
    void setIncludeDocument(bool on);                    // the "Send the open document" box

    void send(const QString& text);                      // exactly like typing `text` and pressing Enter
    void stop();
    QString transcriptText() const;                      // plain text of the conversation (tests, debugging)
    int historySize() const { return history_.size(); }

signals:
    void settingsRequested();                            // "Open AI settings" was clicked
    void turnFinished();                                 // the model is done (or failed / was stopped)

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Entry { enum Kind { User, Assistant, Tool, Notice, Error } kind; QString title; QString text; };

    void callModel();
    void onReply(Ai::Api api);
    QString runTool(const Ai::ToolCall& call, bool* endBatch);
    QString runShell(const QString& command, int timeoutMs, bool* timedOut, int* exitCode);
    bool confirm(const QString& what, const QString& detail, bool* sessionFlag, bool defaultAllow);
    QString askUser(const QString& question, const QStringList& options, bool* answered);
    void addEntry(Entry::Kind k, const QString& text, const QString& title = QString());
    void rebuild(bool keepScroll = false);
    void setBusy(bool on);
    void finish();
    void updateStatus();
    void submit();

    AiHost host_;
    Theme theme_;
    QList<Entry> entries_;
    QList<Ai::Message> history_;
    bool busy_ = false, cancelled_ = false;
    bool sessionFiles_ = false, sessionCommands_ = false;     // "always allow this session"
    int steps_ = 0;

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QProcess* proc_ = nullptr;

    QLabel *status_ = nullptr, *banner_ = nullptr;
    QPushButton *bannerBtn_ = nullptr, *sendBtn_ = nullptr;
    QToolButton *newBtn_ = nullptr, *setBtn_ = nullptr;
    QCheckBox* includeDoc_ = nullptr;
    QTextBrowser* view_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QWidget* bannerBox_ = nullptr;
};
