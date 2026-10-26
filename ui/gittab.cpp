// gittab.cpp - see include/makdown/ui/gittab.h
#include <makdown/ui/gittab.h>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
// shown in the log: hide "user:token@" in http(s) URLs
QString masked(QString s) {
    static const QRegularExpression rx(R"((https?://)[^/@\s]+@)", QRegularExpression::CaseInsensitiveOption);
    return s.replace(rx, "\\1***@");
}
} // namespace

bool GitTab::validUrl(const QString& u) {
    if (u.isEmpty() || u.startsWith('-') || u.contains(QRegularExpression(R"([\s\x00-\x1f])"))) return false;   // no option injection
    static const QRegularExpression web(R"(^(https?|ssh)://[^/\s]+/.+$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression scp(R"(^[\w.\-]+@[\w.\-]+:(?!//).+$)");                                    // git@github.com:user/repo.git
    return web.match(u).hasMatch() || scp.match(u).hasMatch();
}

QString GitTab::repoName(const QString& url) {
    QString s = url.trimmed();
    while (s.endsWith('/')) s.chop(1);
    if (s.endsWith(".git", Qt::CaseInsensitive)) s.chop(4);
    const int cut = qMax(s.lastIndexOf('/'), s.lastIndexOf(':'));
    return s.mid(cut + 1);
}

GitTab::GitTab(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(8);

    auto* title = new QLabel("<b>Clone a repository</b>");
    v->addWidget(title);

    { auto* l = new QLabel("Repository URL (https://..., ssh://... or git@host:user/repo.git)"); l->setWordWrap(true); v->addWidget(l); }
    url_ = new QLineEdit; url_->setPlaceholderText("https://github.com/user/repo.git");
    v->addWidget(url_);

    v->addWidget(new QLabel("Clone into (full path of the new folder)"));
    auto* row = new QHBoxLayout; row->setSpacing(6);
    path_ = new QLineEdit; path_->setPlaceholderText(QDir::homePath() + "/projects/repo");
    browse_ = new QPushButton("Browse...");
    row->addWidget(path_, 1); row->addWidget(browse_);
    v->addLayout(row);

    auto* btns = new QHBoxLayout; btns->setSpacing(6);
    clone_ = new QPushButton("Clone"); clone_->setObjectName("primary");
    cancel_ = new QPushButton("Cancel"); cancel_->setEnabled(false);
    open_ = new QPushButton("Open folder in explorer"); open_->hide();
    btns->addWidget(clone_); btns->addWidget(cancel_); btns->addStretch(1);
    v->addLayout(btns);
    v->addWidget(open_);

    bar_ = new QProgressBar; bar_->setRange(0, 100); bar_->setTextVisible(true); bar_->setFixedHeight(14); bar_->hide();
    v->addWidget(bar_);
    status_ = new QLabel; status_->setWordWrap(true);
    v->addWidget(status_);

    log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMaximumBlockCount(2000);
    v->addWidget(log_, 1);

    connect(url_, &QLineEdit::textEdited, this, [this] { onUrlEdited(); });
    connect(url_, &QLineEdit::returnPressed, this, [this] { startClone(); });
    connect(path_, &QLineEdit::textEdited, this, [this] { pathEdited_ = true; });
    connect(path_, &QLineEdit::returnPressed, this, [this] { startClone(); });
    connect(browse_, &QPushButton::clicked, this, [this] { browse(); });
    connect(clone_, &QPushButton::clicked, this, [this] { startClone(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { cancelClone(); });
    connect(open_, &QPushButton::clicked, this, [this] { if (!target_.isEmpty()) emit openFolderRequested(target_); });

    setFocusProxy(url_);
}

GitTab::~GitTab() {
    if (proc_ && proc_->state() != QProcess::NotRunning) {      // tab closed mid-clone: stop git (it cleans up on SIGTERM)
        proc_->disconnect(this);
        proc_->terminate();
        if (!proc_->waitForFinished(1500)) { proc_->kill(); proc_->waitForFinished(500); }
    }
}

void GitTab::setDefaultParent(const QString& dir) {
    defaultParent_ = dir;
    if (!pathEdited_) onUrlEdited();
}

void GitTab::focusUrl() { url_->setFocus(); url_->selectAll(); }

void GitTab::onUrlEdited() {                                     // suggest <default parent>/<repo name> until the user edits the path
    if (pathEdited_) return;
    const QString name = repoName(url_->text());
    if (defaultParent_.isEmpty() || name.isEmpty()) { path_->clear(); return; }
    path_->setText(QDir::toNativeSeparators(QDir(defaultParent_).filePath(name)));
}

void GitTab::browse() {
    const QString start = path_->text().trimmed().isEmpty() ? (defaultParent_.isEmpty() ? QDir::homePath() : defaultParent_)
                                                            : QFileInfo(path_->text().trimmed()).absolutePath();
    const QString parent = QFileDialog::getExistingDirectory(this, "Choose the folder to clone into", start);
    if (parent.isEmpty()) return;
    const QString name = repoName(url_->text());
    path_->setText(QDir::toNativeSeparators(name.isEmpty() ? parent : QDir(parent).filePath(name)));
    pathEdited_ = true;
}

void GitTab::appendLog(const QString& line, bool replaceLast) {
    if (replaceLast) {                                           // git progress rewrites its line with \r
        QTextCursor c = log_->textCursor();
        c.movePosition(QTextCursor::End);
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        c.insertText(line);
    } else {
        log_->appendPlainText(line);
    }
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

void GitTab::setRunning(bool on) {
    clone_->setEnabled(!on); cancel_->setEnabled(on);
    url_->setEnabled(!on); path_->setEnabled(!on); browse_->setEnabled(!on);
    bar_->setVisible(on);
    if (on) { bar_->setRange(0, 0); open_->hide(); }             // busy until git reports a percentage
}

void GitTab::startClone() {
    if (busy()) return;
    const QString url = url_->text().trimmed();
    QString path = path_->text().trimmed();
    if (!validUrl(url)) { status_->setText("That is not a valid http(s) or ssh repository URL."); return; }
    if (path.isEmpty()) { status_->setText("Specify the path to clone into."); path_->setFocus(); return; }
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QFileInfo fi(path);
    if (fi.exists() && (!fi.isDir() || !QDir(path).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))) {
        status_->setText("\"" + QDir::toNativeSeparators(path) + "\" already exists and is not an empty folder.");
        return;
    }
    const QString git = QStandardPaths::findExecutable("git");
    if (git.isEmpty()) { status_->setText("git was not found in PATH. Install git and restart MakDown."); return; }

    QDir().mkpath(QFileInfo(path).absolutePath());
    target_ = path; tail_.clear(); cancelled_ = false;
    log_->clear();
    appendLog("$ git clone --progress -- " + masked(url) + " " + QDir::toNativeSeparators(path));
    status_->setText("Cloning...");

    delete proc_;
    proc_ = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GIT_TERMINAL_PROMPT", "0");                       // never wait for a password on a terminal that is not there
    if (!env.contains("GIT_SSH_COMMAND") && !env.contains("GIT_SSH"))
        env.insert("GIT_SSH_COMMAND", "ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new");
    proc_->setProcessEnvironment(env);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, &QProcess::readyRead, this, [this] { onOutput(); });
    connect(proc_, &QProcess::finished, this, [this](int c, QProcess::ExitStatus s) { onFinished(c, s); });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { setRunning(false); status_->setText("Could not start git."); }
    });
    setRunning(true);
    proc_->start(git, {"clone", "--progress", "--", url, path});
}

void GitTab::cancelClone() {
    if (!busy()) return;
    cancelled_ = true;
    proc_->terminate();
}

void GitTab::onOutput() {
    tail_ += QString::fromUtf8(proc_->readAll());
    static const QRegularExpression pct(R"((\d+)%)");
    // split on \n (new line) and \r (progress rewrite); keep the unfinished tail for the next read
    int i = 0; bool replace = false;
    while (true) {
        const int n = tail_.indexOf(QRegularExpression("[\r\n]"), i);
        if (n < 0) break;
        const QString line = tail_.mid(i, n - i);
        const bool cr = tail_[n] == '\r';
        i = n + 1;
        if (line.isEmpty()) { if (!cr) replace = false; continue; }
        appendLog(masked(line), replace);
        replace = cr;                                           // a \r line gets overwritten by the next one
        if (const auto m = pct.match(line); m.hasMatch()) { bar_->setRange(0, 100); bar_->setValue(m.captured(1).toInt()); }
    }
    tail_.remove(0, i);
}

void GitTab::onFinished(int code, QProcess::ExitStatus st) {
    onOutput();
    if (!tail_.isEmpty()) { appendLog(masked(tail_)); tail_.clear(); }
    setRunning(false);
    if (cancelled_) { status_->setText("Cancelled."); return; }
    if (st == QProcess::NormalExit && code == 0) {
        status_->setText("Done: " + QDir::toNativeSeparators(target_));
        open_->show();
    } else {
        status_->setText(QString("git exited with code %1. See the log below.").arg(code));
    }
}
