// browsertab.cpp - browser tab with live reload. All QtWebEngine use sits behind MAKDOWN_WEBENGINE.
#include <makdown/ui/browsertab.h>
#include <makdown/core/lang.h>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#ifdef MAKDOWN_WEBENGINE
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#endif

namespace {

// Files whose change should reload the page: markup, scripts, styles, data and media a page can pull in.
bool isWatchedSuffix(const QString& s) {
    static const QStringList ok = {"html", "htm", "xhtml", "css", "js", "mjs", "cjs", "json", "svg", "png", "jpg",
                                   "jpeg", "gif", "webp", "avif", "ico", "woff", "woff2", "ttf", "otf", "wasm"};
    return ok.contains(s.toLower());
}

bool under(const QString& root, const QString& path) {
    const QString r = QDir::cleanPath(root);
    return !r.isEmpty() && QDir::cleanPath(path).startsWith(r.endsWith('/') ? r : r + '/');
}

// Collect every directory (to notice created / deleted files) and watched file below `dir`.
// inotify watches are a shared, limited resource, so depth and total count are capped.
void walk(const QString& dir, int depth, QStringList& dirs, QStringList& files) {
    static const QStringList skip = {"node_modules", ".git", ".hg", ".svn", "__pycache__", ".idea", ".vscode"};
    constexpr int kMaxEntries = 2000, kMaxDepth = 8;
    dirs << dir;
    if (depth >= kMaxDepth) return;
    const QFileInfoList list = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : list) {
        if (dirs.size() + files.size() >= kMaxEntries) return;
        if (fi.isDir()) {
            if (!skip.contains(fi.fileName())) walk(fi.absoluteFilePath(), depth + 1, dirs, files);
        } else if (isWatchedSuffix(fi.suffix())) {
            files << fi.absoluteFilePath();
        }
    }
}

} // namespace

bool BrowserTab::available() {
#ifdef MAKDOWN_WEBENGINE
    return true;
#else
    return false;
#endif
}

BrowserTab::BrowserTab(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* bar = new QFrame;
    bar->setObjectName("browserBar");
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(6);

    back_ = new QToolButton;   back_->setText("\u25C0");   back_->setToolTip(L("browser.back"));
    fwd_ = new QToolButton;    fwd_->setText("\u25B6");    fwd_->setToolTip(L("browser.forward"));
    reload_ = new QToolButton; reload_->setText("\u21BB"); reload_->setToolTip(L("browser.reload"));
    back_->setEnabled(false);
    fwd_->setEnabled(false);

    address_ = new QLineEdit;
    address_->setPlaceholderText(L("browser.address"));

    live_ = new QToolButton;
    live_->setText(L("browser.liveButton"));
    live_->setCheckable(true);
    live_->setChecked(true);
    live_->setToolTip(L("browser.liveTip"));

    info_ = new QLabel;
    info_->setObjectName("pill");
    info_->hide();

    h->addWidget(back_);
    h->addWidget(fwd_);
    h->addWidget(reload_);
    h->addWidget(address_, 1);
    h->addWidget(live_);
    h->addWidget(info_);
    v->addWidget(bar);

    watcher_ = new QFileSystemWatcher(this);
    reloadTimer_.setSingleShot(true);
    reloadTimer_.setInterval(200);
    auto kick = [this] { if (live_->isChecked()) reloadTimer_.start(); };
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, kick);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, kick);
    connect(&reloadTimer_, &QTimer::timeout, this, [this] {
        rescan();                                   // atomic saves replace the file and drop its watch
        reloadNow();
    });

#ifdef MAKDOWN_WEBENGINE
    view_ = new QWebEngineView;
    // A local index.html that pulls a library from a CDN is the normal case while developing.
    view_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    v->addWidget(view_, 1);

    connect(back_, &QToolButton::clicked, view_, [this] { view_->back(); });
    connect(fwd_, &QToolButton::clicked, view_, [this] { view_->forward(); });
    connect(reload_, &QToolButton::clicked, this, [this] { reloadNow(); });
    connect(view_, &QWebEngineView::titleChanged, this, &BrowserTab::titleChanged);
    connect(view_, &QWebEngineView::urlChanged, this, [this](const QUrl& u) { onUrlChanged(u); });
    connect(view_, &QWebEngineView::loadFinished, this, [this](bool) {
        updateNav();
        trackResources();
    });
#else
    auto* msg = new QLabel(L("browser.noEngine"));
    msg->setWordWrap(true);
    msg->setMargin(24);
    v->addWidget(msg, 1);
#endif

    connect(address_, &QLineEdit::returnPressed, this, [this] { openTarget(address_->text()); });
    connect(live_, &QToolButton::clicked, this, [this](bool on) {
        rescan();
        emit liveToggled(on);
    });
}

void BrowserTab::openTarget(const QString& input) {
    const QString s = input.trimmed();
    if (s.isEmpty()) return;

    QString local;
    QUrl url;
    if (s.startsWith("file:", Qt::CaseInsensitive)) {
        url = QUrl(s);
        local = url.toLocalFile();
    } else {
        const QFileInfo fi(s);
        if (fi.isDir()) {
            for (const char* n : {"index.html", "index.htm"}) {
                const QString p = fi.absoluteFilePath() + "/" + n;
                if (QFileInfo::exists(p)) { local = p; break; }
            }
            if (local.isEmpty()) {
                info_->setText(L("browser.noIndex"));
                info_->show();
                return;
            }
        } else if (fi.isFile()) {
            local = fi.absoluteFilePath();
        }
    }
    if (!local.isEmpty()) url = QUrl::fromLocalFile(local);
    else if (url.isEmpty()) url = QUrl::fromUserInput(s);     // "localhost:3000" -> http://localhost:3000
    if (!url.isValid()) {
        info_->setText(L("browser.invalidAddress"));
        info_->show();
        return;
    }

#ifdef MAKDOWN_WEBENGINE
    root_ = local.isEmpty() ? QString() : QFileInfo(local).absolutePath();
    target_ = local.isEmpty() ? url.toString() : local;
    extra_.clear();
    address_->setText(local.isEmpty() ? url.toString() : QDir::toNativeSeparators(local));
    view_->setUrl(url);
    rescan();
#else
    QDesktopServices::openUrl(url);
#endif
}

void BrowserTab::setLiveReload(bool on) {
    if (live_->isChecked() == on) return;
    live_->setChecked(on);                          // setChecked() emits toggled, not clicked: no feedback loop
    rescan();
}

void BrowserTab::reloadNow() {
#ifdef MAKDOWN_WEBENGINE
    view_->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
#endif
}

void BrowserTab::focusAddressBar() {
    address_->setFocus();
    address_->selectAll();
}

void BrowserTab::updateNav() {
#ifdef MAKDOWN_WEBENGINE
    back_->setEnabled(view_->history()->canGoBack());
    fwd_->setEnabled(view_->history()->canGoForward());
#endif
}

// The page navigated (link click, redirect, back/forward): keep the address bar and the watch root in step.
void BrowserTab::onUrlChanged(const QUrl& u) {
    if (u.isLocalFile()) {
        const QString p = u.toLocalFile();
        const QString dir = QFileInfo(p).absolutePath();
        target_ = p;
        address_->setText(QDir::toNativeSeparators(p));
        if (root_.isEmpty() || !(dir == root_ || under(root_, dir))) {
            root_ = dir;
            extra_.clear();
            rescan();
        }
    } else {
        target_ = u.toString();
        address_->setText(target_);
        root_.clear();
        extra_.clear();
        rescan();
    }
    updateNav();
}

void BrowserTab::rescan() {
    const QStringList old = watcher_->files() + watcher_->directories();
    if (!old.isEmpty()) watcher_->removePaths(old);

    if (root_.isEmpty()) {                          // remote page: nothing to watch
        info_->hide();
        return;
    }
    if (!live_->isChecked()) {
        info_->setText(L("browser.liveOff"));
        info_->show();
        return;
    }
    QStringList dirs, files;
    walk(root_, 0, dirs, files);
    for (const QString& e : extra_)
        if (QFileInfo::exists(e) && !files.contains(e)) files << e;
    watcher_->addPaths(dirs);
    watcher_->addPaths(files);
    info_->setText(Ln("browser.watching", watcher_->files().size()));
    info_->show();
}

// Files the page loads from outside its own folder (../shared/app.js, a sibling css/ ...) are not under
// root_, so ask the page which local files it actually used and watch those individually.
void BrowserTab::trackResources() {
#ifdef MAKDOWN_WEBENGINE
    if (root_.isEmpty()) return;
    static const char* kJs =
        "(function(){var r=[];"
        "try{performance.getEntriesByType('resource').forEach(function(e){r.push(e.name);});}catch(e){}"
        "document.querySelectorAll('script[src],link[href]').forEach(function(n){r.push(n.src||n.href);});"
        "return r;})()";
    view_->page()->runJavaScript(QString::fromLatin1(kJs), [this](const QVariant& res) {
        QSet<QString> found;
        for (const QVariant& x : res.toList()) {
            const QUrl u(x.toString());
            if (!u.isLocalFile()) continue;
            const QString p = u.toLocalFile();
            if (p.isEmpty() || under(root_, p) || !isWatchedSuffix(QFileInfo(p).suffix())) continue;
            found.insert(p);
        }
        if (found != extra_) {
            extra_ = found;
            rescan();
        }
    });
#endif
}
