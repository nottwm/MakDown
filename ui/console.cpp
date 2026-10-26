// console.cpp - see include/makdown/ui/console.h
#include <makdown/ui/console.h>
#include <makdown/core/anim.h>
#include <makdown/core/buildinfo.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent) {
    setObjectName("consolePanel");
    auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);

    auto* head = new QWidget; head->setObjectName("consoleHead");
    auto* h = new QHBoxLayout(head); h->setContentsMargins(12, 3, 6, 3); h->setSpacing(2);
    title_ = new QLabel("Console"); title_->setObjectName("consoleTitle"); h->addWidget(title_, 1);
    auto mk = [&](const QString& t, const QString& tip, auto fn) {
        auto* b = new QToolButton; b->setText(t); b->setToolTip(tip); b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn); h->addWidget(b);
    };
    mk("Clear", "Clear the output", [this] { clearOutput(); });
    mk("Restart", "Kill the shell and start a new one", [this] { restart(); });
    mk("\u2715", "Hide the console (Ctrl+`)", [this] { emit closeRequested(); });
    v->addWidget(head);

    out_ = new QPlainTextEdit; out_->setObjectName("consoleOut"); out_->setReadOnly(true);
    out_->setMaximumBlockCount(5000); out_->setFrameShape(QFrame::NoFrame); out_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    out_->setUndoRedoEnabled(false);
    v->addWidget(out_, 1);

    in_ = new QLineEdit; in_->setObjectName("consoleIn"); in_->setFrame(false);
    in_->setPlaceholderText("type a command and press Enter  \u00B7  Ctrl+C interrupts  \u00B7  Ctrl+L clears");
    in_->installEventFilter(this);
    v->addWidget(in_);
    connect(in_, &QLineEdit::returnPressed, this, [this] { sendLine(); });
    out_->installEventFilter(this);                              // typing into the output goes to the input line
    line_.clear();
}

ConsolePanel::~ConsolePanel() {
    if (proc_) { proc_->disconnect(this); proc_->kill(); proc_->waitForFinished(300); }
}

void ConsolePanel::setWorkingDirectory(const QString& dir) { dir_ = dir; }
void ConsolePanel::focusInput() { in_->setFocus(); }
void ConsolePanel::ensureStarted() { if (!proc_) startShell(); }

QColor ConsolePanel::colorFor(quint8 fg) const {
    const bool dark = haveTheme_ ? theme_.dark : false;
    static const char* normDark[]  = {"#6e7681", "#ff7b72", "#7ee787", "#e3b341", "#79c0ff", "#d2a8ff", "#56d4dd", "#c9d1d9"};
    static const char* brightDark[] = {"#8b949e", "#ffa198", "#56d364", "#f2cc60", "#a5d6ff", "#e2c5ff", "#b3f0ff", "#f0f6fc"};
    static const char* normLight[]  = {"#57606a", "#cf222e", "#116329", "#9a6700", "#0550ae", "#8250df", "#1b7c83", "#6e7781"};
    static const char* brightLight[] = {"#6e7781", "#a40e26", "#1a7f37", "#7d4e00", "#0969da", "#6639ba", "#3192aa", "#8c959f"};
    if (fg == 0) return QColor(haveTheme_ ? theme_.color("editorFg") : "#000000");
    const int i = (fg - 1) % 8;
    return QColor(fg <= 8 ? (dark ? normDark[i] : normLight[i]) : (dark ? brightDark[i] : brightLight[i]));
}

void ConsolePanel::applyTheme(const Theme& t) {
    theme_ = t; haveTheme_ = true;
    const QFont f = ThemeStore::makeFont(t.codeFont, qMax(9, t.editorSize - 1), true);
    out_->setFont(f); in_->setFont(f);
    setStyleSheet(QString(
        "QWidget#consoleHead { background: %1; border-top: 1px solid %2; }"
        "QLabel#consoleTitle { color: %3; font-weight: 600; }"
        "QPlainTextEdit#consoleOut { background: %4; color: %5; border: none; selection-background-color: %6; selection-color: %7; }"
        "QLineEdit#consoleIn { background: %4; color: %5; border: none; border-top: 1px solid %2; padding: 5px 12px; }")
        .arg(t.color("panel"), t.color("border"), t.color("muted"), t.color("editorBg"), t.color("editorFg"), t.color("accent"), t.color("accentFg")));
    renderLiveLine();
}

// ---------------------------------------------------------------------------------------------
// the shell
// ---------------------------------------------------------------------------------------------
void ConsolePanel::startShell() {
    if (proc_) { proc_->disconnect(this); proc_->kill(); proc_->waitForFinished(300); proc_->deleteLater(); proc_ = nullptr; }
    exited_ = false; pending_.clear(); escState_ = 0; escBuf_.clear();
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    const QString cwd = (!dir_.isEmpty() && QDir(dir_).exists()) ? dir_ : QDir::homePath();
    proc_->setWorkingDirectory(cwd);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString program; QStringList args;
#ifdef Q_OS_WIN
    program = qEnvironmentVariable("COMSPEC", "cmd.exe");
    shellName_ = QFileInfo(program).fileName();
    pty_ = false;
#else
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFileInfo(shell).isExecutable()) shell = "/bin/sh";
    shellName_ = QFileInfo(shell).fileName();
    // login (-l) + interactive (-i): /etc/profile, ~/.profile or ~/.bash_profile, ~/.bashrc / ~/.zshrc ... are sourced.
    // csh / tcsh refuse "-l -i" together, they take -l alone.
    QStringList shArgs = (shellName_ == "csh" || shellName_ == "tcsh") ? QStringList{"-l"} : QStringList{"-l", "-i"};
    env.insert("TERM", "xterm-256color");
    env.insert("COLORTERM", "truecolor");
    env.insert("MAKDOWN", "1");
    const QString script = QStandardPaths::findExecutable("script");
    pty_ = false;
    program = shell; args = shArgs;
    if (!script.isEmpty()) {                       // give the shell a pty: prompt, colours, job control, Ctrl+C
#ifdef Q_OS_MACOS
        program = script; args = QStringList{"-q", "/dev/null", "/bin/sh", "-c", "stty cols 220 rows 40 2>/dev/null; exec " + shell + " " + shArgs.join(' ')}; pty_ = true;
#else
        QString quoted = "stty cols 220 rows 40 2>/dev/null; exec " + shell;   // wide pty: readline must not wrap long lines
        for (const QString& a : shArgs) quoted += " " + a;
        program = script; args = QStringList{"-qfec", quoted, "/dev/null"}; pty_ = true;
#endif
    }
#endif
    proc_->setProcessEnvironment(env);
    connect(proc_, &QProcess::readyRead, this, [this] { onOutput(); });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) feed("\r\n[could not start the shell]\r\n");
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        exited_ = true;
        feed(QString("\r\n[shell exited with code %1 - press Enter or Restart to start a new one]\r\n").arg(code));
    });
    mdLog("console: " + program + " " + args.join(' ') + "  (cwd " + cwd + ")");
    proc_->start(program, args);
    title_->setText(QString("Console  \u00B7  %1  \u00B7  %2").arg(shellName_, QDir::toNativeSeparators(cwd)));
}

void ConsolePanel::restart() { clearOutput(); startShell(); focusInput(); }

void ConsolePanel::clearOutput() { out_->clear(); line_.clear(); col_ = 0; curFg_ = 0; curBold_ = false; }

void ConsolePanel::sendRaw(const QByteArray& b) { if (proc_ && proc_->state() == QProcess::Running) proc_->write(b); }

void ConsolePanel::sendLine() {
    const QString text = in_->text();
    if (exited_ || !proc_) { in_->clear(); restart(); return; }
    if (!text.trimmed().isEmpty() && (history_.isEmpty() || history_.last() != text)) history_ << text;
    histPos_ = history_.size();
    in_->clear();
    if (!pty_) feed(text + "\r\n");                  // no pty -> nobody echoes what was typed
    sendRaw(text.toUtf8() + (pty_ ? "\n" : "\r\n"));
}

bool ConsolePanel::eventFilter(QObject* o, QEvent* e) {
    if (e->type() != QEvent::KeyPress) return QWidget::eventFilter(o, e);
    auto* k = static_cast<QKeyEvent*>(e);
    if (o == out_) {                                  // typing while the output has focus -> into the input line
        if (!(k->modifiers() & Qt::ControlModifier) && !k->text().isEmpty() && k->key() != Qt::Key_Escape) {
            in_->setFocus(); QCoreApplication::sendEvent(in_, k); return true;
        }
        return false;
    }
    if (o != in_) return false;
    const bool ctrl = k->modifiers() & Qt::ControlModifier;
    if (ctrl && k->key() == Qt::Key_C && !in_->hasSelectedText()) {      // interrupt
        if (pty_) sendRaw("\x03");
        else if (proc_) { proc_->kill(); }
        in_->clear();
        return true;
    }
    if (ctrl && k->key() == Qt::Key_D && in_->text().isEmpty()) { sendRaw("\x04"); return true; }
    if (ctrl && k->key() == Qt::Key_L) { clearOutput(); sendRaw(pty_ ? "\n" : "\r\n"); return true; }
    if (k->key() == Qt::Key_Up && !history_.isEmpty()) { histPos_ = qMax(0, histPos_ - 1); in_->setText(history_[histPos_]); return true; }
    if (k->key() == Qt::Key_Down && !history_.isEmpty()) {
        histPos_ = qMin(history_.size(), histPos_ + 1);
        in_->setText(histPos_ < history_.size() ? history_[histPos_] : QString()); return true;
    }
    if (k->key() == Qt::Key_Tab && pty_) {            // tab completion: hand the half-typed line to the shell
        sendRaw(in_->text().toUtf8() + "\t"); in_->clear(); return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// output -> text with colours
// ---------------------------------------------------------------------------------------------
void ConsolePanel::onOutput() {
    pending_ += proc_->readAll();
    int cut = pending_.size();
    int i = pending_.size() - 1, back = 0;                         // keep an incomplete UTF-8 sequence for the next read
    while (i >= 0 && back < 4 && (uchar(pending_[i]) & 0xC0) == 0x80) { --i; ++back; }
    if (i >= 0) {
        const uchar lead = uchar(pending_[i]);
        const int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (back + 1 < need) cut = i;
    }
    const QString text = QString::fromUtf8(pending_.constData(), cut);
    pending_.remove(0, cut);
    feed(text);
}

void ConsolePanel::feed(const QString& text) {
    QScrollBar* sb = out_->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 4;
    auto put = [this](QChar c) {
        Cell cell; cell.ch = c; cell.fg = curFg_; cell.bold = curBold_;
        if (col_ < line_.size()) line_[col_] = cell; else { while (line_.size() < col_) line_.append(Cell{' ', 0, false}); line_.append(cell); }
        ++col_;
    };
    for (const QChar c : text) {
        if (escState_ == 1) {                                       // after ESC
            if (c == '[') { escState_ = 2; escBuf_.clear(); }
            else if (c == ']') { escState_ = 3; escBuf_.clear(); }
            else escState_ = 0;                                     // other 2-byte escapes: ignore
            continue;
        }
        if (escState_ == 2) {                                       // CSI ... final byte
            if (c.unicode() >= 0x40 && c.unicode() <= 0x7e) {
                escState_ = 0;
                const QStringList parts = escBuf_.split(';');
                const int n = parts.value(0).toInt();
                if (c == 'm') {                                     // colours
                    const QStringList ps = escBuf_.isEmpty() ? QStringList{"0"} : parts;
                    for (int j = 0; j < ps.size(); ++j) {
                        const int p = ps[j].toInt();
                        if (p == 0) { curFg_ = 0; curBold_ = false; }
                        else if (p == 1) curBold_ = true;
                        else if (p == 22) curBold_ = false;
                        else if (p >= 30 && p <= 37) curFg_ = quint8(p - 29);
                        else if (p >= 90 && p <= 97) curFg_ = quint8(p - 89 + 8);
                        else if (p == 39) curFg_ = 0;
                        else if (p == 38 || p == 48) {              // 256 / truecolor: skip its arguments
                            if (j + 1 < ps.size() && ps[j + 1].toInt() == 5) j += 2;
                            else if (j + 1 < ps.size() && ps[j + 1].toInt() == 2) j += 4;
                        }
                    }
                } else if (c == 'K') {                              // erase in line
                    if (n == 0) { if (col_ < line_.size()) line_.resize(col_); }
                    else if (n == 2) { line_.clear(); }
                    else if (n == 1) { for (int j = 0; j < col_ && j < line_.size(); ++j) line_[j].ch = ' '; }
                } else if (c == 'C') col_ += qMax(1, n);            // cursor forward
                else if (c == 'D') col_ = qMax(0, col_ - qMax(1, n));
                else if (c == 'G') col_ = qMax(0, n - 1);
                else if (c == 'P') { for (int j = 0; j < qMax(1, n) && col_ < line_.size(); ++j) line_.remove(col_); }   // delete chars
                else if (c == 'J' && n >= 2) { clearOutput(); }
                // everything else (cursor addressing, modes, ...) is ignored
            } else escBuf_ += c;
            continue;
        }
        if (escState_ == 3) {                                       // OSC (window title ...) up to BEL or ESC \ .
            if (c == QChar(0x07)) escState_ = 0;
            else if (c == QChar(0x1b)) escState_ = 1;
            continue;
        }
        switch (c.unicode()) {
            case 0x1b: escState_ = 1; break;
            case '\r': col_ = 0; break;
            case '\n': commitLine(); break;
            case '\b': if (col_ > 0) --col_; break;
            case 0x07: break;
            case '\t': { do put(' '); while (col_ % 8); break; }
            default:   if (c.unicode() >= 0x20) put(c); break;
        }
    }
    renderLiveLine();
    if (atEnd) sb->setValue(sb->maximum());
}

void ConsolePanel::commitLine() {
    renderLiveLine();
    QTextCursor cur(out_->document());
    cur.movePosition(QTextCursor::End);
    cur.insertBlock();
    line_.clear(); col_ = 0;
}

void ConsolePanel::renderLiveLine() {
    QTextCursor cur(out_->document());
    cur.movePosition(QTextCursor::End);
    cur.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cur.removeSelectedText();
    int i = 0;
    while (i < line_.size()) {
        int j = i;
        QString run;
        while (j < line_.size() && line_[j].fg == line_[i].fg && line_[j].bold == line_[i].bold) run += line_[j++].ch;
        QTextCharFormat f;
        f.setForeground(colorFor(line_[i].fg));
        if (line_[i].bold) f.setFontWeight(QFont::Bold);
        cur.insertText(run, f);
        i = j;
    }
}
