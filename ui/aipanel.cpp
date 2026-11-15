// aipanel.cpp - see include/makdown/ui/aipanel.h
#include <makdown/ui/aipanel.h>
#include <makdown/arch/platform.h>
#include <makdown/core/lang.h>
#include <makdown/core/markdown.h>

#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSysInfo>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// The part of a rendered document that goes between <body> and </body>, so several can share one page.
QString bodyOf(const QString& html) {
    int a = html.indexOf("<body");
    if (a < 0) return html;
    a = html.indexOf('>', a) + 1;
    const int b = html.lastIndexOf("</body>");
    return b > a ? html.mid(a, b - a) : html.mid(a);
}
QString plainHtml(const QString& s) { return s.toHtmlEscaped().replace('\n', "<br>"); }
} // namespace

AiPanel::AiPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("aiPanel");
    theme_.fillDefaults();
    nam_ = new QNetworkAccessManager(this);

    auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);

    // ---- header: title, provider / model, new conversation, settings
    auto* head = new QWidget; head->setObjectName("aiHead");
    auto* h = new QHBoxLayout(head); h->setContentsMargins(12, 8, 6, 6); h->setSpacing(2);
    auto* titleLbl = new QLabel(L("ai.title")); titleLbl->setObjectName("aiTitle");
    QFont tf = titleLbl->font(); tf.setBold(true); titleLbl->setFont(tf);
    h->addWidget(titleLbl);
    status_ = new QLabel; status_->setObjectName("hint"); status_->setTextInteractionFlags(Qt::NoTextInteraction);
    h->addWidget(status_, 1);
    newBtn_ = new QToolButton; newBtn_->setText(L("ai.newShort")); newBtn_->setToolTip(L("ai.newChat")); newBtn_->setAutoRaise(true);
    setBtn_ = new QToolButton; setBtn_->setText(QString::fromUtf8("\u2699")); setBtn_->setToolTip(L("ai.settings")); setBtn_->setAutoRaise(true);
    h->addWidget(newBtn_); h->addWidget(setBtn_);
    v->addWidget(head);

    // ---- "no API key" banner
    bannerBox_ = new QWidget; bannerBox_->setObjectName("aiBanner");
    auto* bv = new QVBoxLayout(bannerBox_); bv->setContentsMargins(12, 6, 12, 8); bv->setSpacing(6);
    banner_ = new QLabel; banner_->setWordWrap(true); banner_->setObjectName("hint");
    bannerBtn_ = new QPushButton(L("ai.openSettings"));
    bv->addWidget(banner_); bv->addWidget(bannerBtn_, 0, Qt::AlignLeft);
    v->addWidget(bannerBox_);

    // ---- transcript
    view_ = new QTextBrowser; view_->setObjectName("aiView"); view_->setOpenLinks(false); view_->setFrameShape(QFrame::NoFrame);
    v->addWidget(view_, 1);

    // ---- input
    auto* foot = new QWidget; foot->setObjectName("aiFoot");
    auto* fv = new QVBoxLayout(foot); fv->setContentsMargins(10, 6, 10, 10); fv->setSpacing(6);
    input_ = new QPlainTextEdit; input_->setObjectName("aiInput"); input_->setPlaceholderText(L("ai.placeholder"));
    input_->setMinimumHeight(64); input_->setMaximumHeight(120); input_->installEventFilter(this);
    fv->addWidget(input_);
    auto* row = new QHBoxLayout; row->setSpacing(8);
    includeDoc_ = new QCheckBox(L("ai.includeDoc")); includeDoc_->setToolTip(L("ai.includeDocTip")); includeDoc_->setChecked(true);
    sendBtn_ = new QPushButton(L("ai.send")); sendBtn_->setObjectName("primary");
    row->addWidget(includeDoc_, 1); row->addWidget(sendBtn_);
    fv->addLayout(row);
    v->addWidget(foot);

    connect(sendBtn_, &QPushButton::clicked, this, [this] { if (busy_) stop(); else submit(); });
    connect(newBtn_, &QToolButton::clicked, this, [this] { newChat(); });
    connect(setBtn_, &QToolButton::clicked, this, [this] { emit settingsRequested(); });
    connect(bannerBtn_, &QPushButton::clicked, this, [this] { emit settingsRequested(); });
    refresh();
    rebuild();
}

AiPanel::~AiPanel() {
    cancelled_ = true;
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
}

void AiPanel::applyTheme(const Theme& t) {
    theme_ = t;
    view_->document()->setDefaultStyleSheet(previewCss(t));
    view_->setFont(ThemeStore::makeFont(t.previewFont, t.previewSize));
    rebuild(true);
}

void AiPanel::focusInput() { input_->setFocus(); }

void AiPanel::setIncludeDocument(bool on) { includeDoc_->setChecked(on); }

void AiPanel::refresh() { updateStatus(); }

void AiPanel::updateStatus() {
    const Ai::Config& c = Ai::config();
    const Ai::Provider* p = Ai::findProvider(c.provider);
    const QString name = p ? p->name : c.provider;
    const QString model = c.model(c.provider);
    status_->setText(model.isEmpty() ? name : name + " \u00b7 " + model);
    status_->setToolTip(status_->text());
    const bool noKey = p && !p->custom && c.key(p->id).isEmpty();
    bannerBox_->setVisible(noKey);
    banner_->setText(L("ai.noKey").arg(name));
}

bool AiPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == input_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !(k->modifiers() & Qt::ShiftModifier)) {
            if (!busy_) submit();
            return true;
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
// transcript
// ---------------------------------------------------------------------------------------------
void AiPanel::addEntry(Entry::Kind k, const QString& text, const QString& title) {
    entries_.append({k, title, text});
    rebuild();
}

void AiPanel::rebuild(bool keepScroll) {
    QScrollBar* sb = view_->verticalScrollBar();
    const int old = sb->value();
    const QString accent = theme_.color("accent"), muted = theme_.color("muted");
    QString html;
    if (entries_.isEmpty()) html = "<p style='color:" + muted + "'>" + plainHtml(L("ai.empty")) + "</p>";
    for (const Entry& e : entries_) {
        switch (e.kind) {
        case Entry::User:
            html += "<p><b style='color:" + accent + "'>" + plainHtml(L("ai.you")) + "</b><br>" + plainHtml(e.text) + "</p>";
            break;
        case Entry::Assistant:
            html += "<p><b style='color:" + accent + "'>" + plainHtml(L("ai.assistant")) + "</b></p>"
                    + bodyOf(renderMarkdownToHtml(e.text, theme_, false, host_.workDir ? host_.workDir() : QString()));
            break;
        case Entry::Tool:
            html += "<p style='color:" + muted + "'><b>\u2699 " + plainHtml(e.title) + "</b></p><pre>" + e.text.toHtmlEscaped() + "</pre>";
            break;
        case Entry::Notice:
            html += "<p style='color:" + muted + "'><i>" + plainHtml(e.text) + "</i></p>";
            break;
        case Entry::Error:
            html += "<p style='color:#d1242f'><b>" + plainHtml(L("ai.error")) + "</b> " + plainHtml(e.text) + "</p>";
            break;
        }
    }
    view_->setHtml(html);
    if (keepScroll) sb->setValue(old); else sb->setValue(sb->maximum());
}

QString AiPanel::transcriptText() const { return view_->toPlainText(); }

void AiPanel::newChat() {
    if (busy_) stop();
    entries_.clear(); history_.clear();
    sessionFiles_ = sessionCommands_ = false;
    rebuild();
}

void AiPanel::setBusy(bool on) {
    busy_ = on;
    sendBtn_->setText(on ? L("ai.stop") : L("ai.send"));
    if (on) status_->setText(L("ai.thinking")); else updateStatus();
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

    Ai::PromptContext pc;
    pc.workDir = host_.workDir ? host_.workDir() : QString();
    pc.openFile = host_.currentFile ? host_.currentFile() : QString();
    pc.custom = cfg.customPrompt;
    pc.os = QSysInfo::prettyProductName();
    pc.shell = platform::shellName();
    const Ai::Request rq = Ai::buildRequest(cfg, Ai::systemPrompt(pc), history_);
    if (!rq.error.isEmpty()) { addEntry(Entry::Error, rq.error); finish(); return; }

    QNetworkRequest req{QUrl(rq.url)};
    for (const auto& hd : rq.headers) req.setRawHeader(hd.first, hd.second);
    req.setTransferTimeout(300000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    const Ai::Provider* prov = Ai::findProvider(cfg.provider);
    const Ai::Api api = prov ? prov->api : Ai::Api::OpenAI;
    reply_ = nam_->post(req, rq.body);
    connect(reply_, &QNetworkReply::finished, this, [this, api] { onReply(api); });
}

void AiPanel::onReply(Ai::Api api) {
    QNetworkReply* r = reply_;
    reply_ = nullptr;
    r->deleteLater();
    if (cancelled_) { addEntry(Entry::Notice, L("ai.stopped")); finish(); return; }

    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = r->readAll();
    if (status == 0) { addEntry(Entry::Error, r->errorString()); finish(); return; }
    const Ai::Reply rep = Ai::parseReply(api, status, body);
    if (!rep.ok) { addEntry(Entry::Error, rep.error); finish(); return; }
    if (rep.text.trimmed().isEmpty()) { addEntry(Entry::Notice, L("ai.emptyReply")); finish(); return; }

    history_.append({"assistant", rep.text});
    const Ai::Parsed parsed = Ai::parseToolCalls(rep.text);
    if (!parsed.text.isEmpty()) addEntry(Entry::Assistant, parsed.text);
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
        addEntry(Entry::Tool, Ai::clip(body, 1500), title.isEmpty() ? name : title);
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
        const QString out = runShell(cmd, 120000, &timedOut, &code);
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
    return done("error", "unhandled tool");
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
