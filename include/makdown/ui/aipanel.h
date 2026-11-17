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
#include <memory>
#include <makdown/core/ai.h>
#include <makdown/core/theme.h>

class QCheckBox;
class QFrame;
class QScrollArea;
class QTimer;
class QVBoxLayout;
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
    QString pendingText() const;                         // the progress row's text, empty when it is not shown (tests)
    int thinkingBlocks() const;                          // how many thinking blocks the conversation shows (tests)

signals:
    void settingsRequested();                            // "Open AI settings" was clicked
    void turnFinished();                                 // the model is done (or failed / was stopped)
    void contentUpdated();                               // streamed text or thinking was added to the view

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    // One line of the conversation as shown; the widgets are built from these (and rebuilt on a theme change).
    struct Entry {
        enum Kind { User, Assistant, Thinking, Tool, Notice, Error } kind;
        QString title;           // tool: "create_file \u00b7 notes/a.md"
        QString text;            // user: the text; assistant: Markdown; thinking / tool: plain text
        QString plain;           // plain text of what is shown (transcriptText())
        QString status;          // tool: ok | error | denied
        bool expanded = false;   // thinking / tool: body open
        bool touched = false;    // the user opened / closed it himself: do not auto-collapse
    };

    void callModel();
    void onReply(Ai::Api api);
    QString runTool(const Ai::ToolCall& call, bool* endBatch);
    QString runShell(const QString& command, int timeoutMs, bool* timedOut, int* exitCode);
    bool confirm(const QString& what, const QString& detail, bool* sessionFlag, bool defaultAllow);
    QString askUser(const QString& question, const QStringList& options, bool* answered);
    // One GET for web_search / web_fetch: redirects are followed by hand and every hop is checked (Ai::checkFetchUrl and the
    // addresses the name resolves to), the size and the time are limited and Stop cancels it. `trusted` skips the local-address checks.
    bool webGet(const QUrl& start, bool trusted, QByteArray* body, QString* contentType, QUrl* finalUrl, bool* truncated, QString* error);
    bool hostIsPublic(const QString& host, QString* error);
    int addEntry(Entry::Kind k, const QString& text, const QString& title = QString(), const QString& status = QString(), bool expanded = false);
    void onData();                                       // streamed bytes arrived
    void flushLive();                                    // show what the stream has so far (thinking card, answer)
    void refreshItem(int index);                         // rebuild one item's widget
    void removeLastEntry();
    void setAssistantText(int index, const QString& markdown);
    void setThinkingText(int index, const QString& text);
    static QString wordsLabel(const QString& text);
    QWidget* makeItem(int index);                        // the widget for entries_[index]
    void rebuild();                                      // all widgets again (theme change)
    void applyStyle();                                   // the panel's style sheet, from the theme
    void showPending(const QString& text);               // the animated "Thinking..." row
    void hidePending();
    void setPendingText(const QString& text);
    void scrollDown(bool force);
    void updateEmpty();
    void growInput();
    void setBusy(bool on);
    void finish();
    void updateStatus();
    void submit();

    AiHost host_;
    Theme theme_;
    QList<Entry> entries_;
    QList<Ai::Message> history_;
    bool busy_ = false, cancelled_ = false;
    bool sessionFiles_ = false, sessionCommands_ = false, sessionWeb_ = false;     // "always allow this session"
    int steps_ = 0;

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    // the reply being received
    std::unique_ptr<Ai::StreamParser> parser_;
    QByteArray rawBody_;
    int httpStatus_ = 0;
    bool liveStream_ = false, nonSse_ = false, sniffed_ = false, thinkCollapsed_ = false, earlyStop_ = false, pendingIsTool_ = false;
    int liveThink_ = -1, liveAsst_ = -1;                 // entries filled while the reply streams in
    QTimer* flush_ = nullptr;                            // coalesces updates (the Markdown is rendered again each time)
    QProcess* proc_ = nullptr;

    QWidget* status_ = nullptr;                          // provider / model line (click = Settings)
    QLabel* banner_ = nullptr;
    QPushButton *bannerBtn_ = nullptr, *sendBtn_ = nullptr;
    QToolButton *newBtn_ = nullptr, *setBtn_ = nullptr;
    QCheckBox* includeDoc_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QVBoxLayout* list_ = nullptr;                        // the items, with a stretch at the end
    QList<QWidget*> items_;
    QWidget* emptyBox_ = nullptr;
    QFrame* composer_ = nullptr;
    QFrame* pending_ = nullptr; QLabel* pendingLbl_ = nullptr; QTimer* dots_ = nullptr; QString pendingBase_; int dotStep_ = 0;
    QPlainTextEdit* input_ = nullptr;
    QWidget* bannerBox_ = nullptr;
    bool stick_ = true;                                  // keep the view at the bottom while it grows
};
