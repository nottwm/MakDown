// browsertab.h - a browser tab (QtWebEngine) that opens a local page or a URL and live-reloads
// when the page's .html / .js / .css (and other web assets) change on disk.
//
// QtWebEngine is an optional dependency (-DMAKDOWN_WEBENGINE, set by the Makefile). Without it the
// class still compiles; MainWindow checks available() and hands pages to the system browser instead.
#pragma once
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QWidget>

class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QToolButton;
class QWebEngineView;

class BrowserTab : public QWidget {
    Q_OBJECT
public:
    static bool available();                       // true when built with QtWebEngine

    explicit BrowserTab(QWidget* parent = nullptr);

    // A local file, a folder (-> its index.html), a file:// URL, or an http(s) URL / host[:port].
    void openTarget(const QString& pathOrUrl);
    void setLiveReload(bool on);
    void reloadNow();                              // reload, bypassing the cache
    void focusAddressBar();
    QString target() const { return target_; }     // local path or URL currently shown

signals:
    void titleChanged(const QString& title);
    void liveToggled(bool on);                     // the user clicked this tab's "Live" button

private:
    void rescan();                                 // (re)build the watch list for root_ + extra_
    void trackResources();                         // after a load: also watch files the page pulled in from outside root_
    void onUrlChanged(const QUrl& u);
    void updateNav();

    QWebEngineView* view_ = nullptr;               // null when built without QtWebEngine
    QFileSystemWatcher* watcher_ = nullptr;
    QLineEdit* address_ = nullptr;
    QToolButton *back_ = nullptr, *fwd_ = nullptr, *reload_ = nullptr, *live_ = nullptr;
    QLabel* info_ = nullptr;
    QTimer reloadTimer_;                           // debounce: editors save in several steps
    QString target_;
    QString root_;                                 // folder of the local page; everything below it is watched
    QSet<QString> extra_;                          // watched files outside root_ (e.g. ../shared/app.js)
};
