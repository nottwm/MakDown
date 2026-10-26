// mediatab.cpp - see include/makdown/ui/mediatab.h
#include <makdown/ui/mediatab.h>
#include <makdown/core/anim.h>
#include <makdown/ui/fx.h>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QtMath>
#ifdef MAKDOWN_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QUrl>
#endif

namespace {
const QStringList kImageExt = {"png", "jpg", "jpeg", "jpe", "gif", "bmp", "webp", "ico", "svg", "svgz", "tif", "tiff",
                               "avif", "heic", "jp2", "ppm", "pgm", "pbm", "xpm", "xbm", "tga", "icns", "cur", "wbmp"};
const QStringList kAudioExt = {"mp3", "wav", "ogg", "oga", "opus", "flac", "m4a", "aac", "wma", "aif", "aiff", "weba", "mka"};

QString fmtTime(qint64 ms) {
    const qint64 s = qMax<qint64>(0, ms) / 1000;
    return s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
QString fmtSize(qint64 b) {
    if (b < 1024) return QString("%1 B").arg(b);
    if (b < 1024 * 1024) return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
}

// the picture on a checkerboard (so transparency is visible), drawn at the current zoom
class ImageCanvas : public QWidget {
public:
    QPixmap pm;
    double zoom = 1.0;
    QSize scaled() const { return QSize(qMax(1, qRound(pm.width() * zoom)), qMax(1, qRound(pm.height() * zoom))); }
    void refresh() { setFixedSize(scaled()); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const bool dark = palette().color(QPalette::Base).lightness() < 128;
        const QColor a = dark ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xf0, 0xf0, 0xf0), b = dark ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xdc, 0xdc, 0xdc);
        const int s = 10;
        for (int y = 0; y < height(); y += s)
            for (int x = 0; x < width(); x += s) p.fillRect(x, y, s, s, ((x / s + y / s) & 1) ? b : a);
        p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 2.0);     // pixel art stays crisp when magnified
        p.drawPixmap(rect(), pm);
    }
};

// decorative equalizer for the audio tab; only moves while playing at "Entertain me"
class Bars : public QWidget {
public:
    bool playing = false;
    explicit Bars(QWidget* p = nullptr) : QWidget(p) {
        setMinimumSize(260, 110);
        timer_.setInterval(33);
        connect(&timer_, &QTimer::timeout, this, [this] { phase_ += 0.09; update(); });
    }
    void sync() { const bool run = playing && Anim::level() >= Anim::Full; if (run != timer_.isActive()) run ? timer_.start() : timer_.stop(); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const int n = 24; const double bw = width() / double(n * 1.6);
        QColor c = palette().color(QPalette::Highlight);
        const bool live = timer_.isActive();
        for (int i = 0; i < n; ++i) {
            double h = live ? 0.12 + 0.88 * qAbs(qSin(phase_ * (1.0 + (i % 5) * 0.23) + i * 0.7) * qCos(phase_ * 0.6 + i * 0.31))
                            : (0.10 + 0.25 * qAbs(qSin(i * 0.55)));
            const double bh = qMax(3.0, h * height());
            c.setAlphaF(live ? 0.95 : 0.45);
            p.setPen(Qt::NoPen); p.setBrush(c);
            p.drawRoundedRect(QRectF(i * bw * 1.6 + bw * 0.3, height() - bh, bw, bh), bw / 2, bw / 2);
        }
    }
private:
    QTimer timer_; double phase_ = 0;
};
} // namespace

struct MediaTab::Impl {
    // common
    QLabel* info = nullptr;
    // image
    QScrollArea* scroll = nullptr; ImageCanvas* canvas = nullptr; QMovie* movie = nullptr;
    bool fit = true; QLabel* zoomLbl = nullptr; QPointer<QVariantAnimation> zoomAnim; QPoint dragFrom; bool dragging = false, moved = false;
#ifdef MAKDOWN_MULTIMEDIA
    QMediaPlayer* player = nullptr; QAudioOutput* out = nullptr;
    QPushButton *play = nullptr, *loop = nullptr, *mute = nullptr;
    QSlider *seek = nullptr, *vol = nullptr; QLabel *cur = nullptr, *total = nullptr, *title = nullptr, *sub = nullptr, *cover = nullptr;
    Bars* bars = nullptr; bool seeking = false;
#endif
};

bool MediaTab::audioAvailable() {
#ifdef MAKDOWN_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

MediaTab::Kind MediaTab::kindOf(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (kImageExt.contains(ext) && (ext.startsWith("svg") || QImageReader(path).canRead())) return Image;
    if (audioAvailable() && kAudioExt.contains(ext)) return Audio;
    return None;
}

MediaTab::MediaTab(const QString& path, QWidget* parent) : QWidget(parent), path_(path), kind_(kindOf(path)), d_(new Impl) {
    setObjectName("mediaTab");
    setFocusPolicy(Qt::StrongFocus);
    auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    auto* bar = new QWidget; bar->setObjectName("mediaBar");
    auto* h = new QHBoxLayout(bar); h->setContentsMargins(12, 6, 12, 6); h->setSpacing(4);
    d_->info = new QLabel; d_->info->setObjectName("mediaInfo"); h->addWidget(d_->info, 1);
    v->addWidget(bar);
    if (kind_ == Image) {
        buildImage();
        auto mk = [&](const QString& t, const QString& tip, auto fn) {
            auto* b = new QToolButton; b->setText(t); b->setToolTip(tip); b->setAutoRaise(true); connect(b, &QToolButton::clicked, this, fn); h->addWidget(b);
        };
        d_->zoomLbl = new QLabel; d_->zoomLbl->setMinimumWidth(48); d_->zoomLbl->setAlignment(Qt::AlignCenter); h->addWidget(d_->zoomLbl);
        mk("\u2212", "Zoom out", [this] { d_->fit = false; setZoom(d_->canvas->zoom / 1.25); });
        mk("+", "Zoom in (Ctrl+wheel)", [this] { d_->fit = false; setZoom(d_->canvas->zoom * 1.25); });
        mk("Fit", "Fit to window", [this] { fitToWindow(); });
        mk("100%", "Actual size", [this] { d_->fit = false; setZoom(1.0); });
        v->addWidget(d_->scroll, 1);
    } else if (kind_ == Audio) {
        buildAudio();
    } else {
        d_->info->setText("This file can't be previewed.");
    }
}

MediaTab::~MediaTab() {
#ifdef MAKDOWN_MULTIMEDIA
    if (d_->player) d_->player->stop();
#endif
    delete d_;
}

// ---------------------------------------------------------------------------------------------
// images
// ---------------------------------------------------------------------------------------------
void MediaTab::buildImage() {
    d_->canvas = new ImageCanvas;
    d_->scroll = new QScrollArea; d_->scroll->setFrameShape(QFrame::NoFrame);
    d_->scroll->setAlignment(Qt::AlignCenter); d_->scroll->setWidgetResizable(false);
    d_->scroll->setWidget(d_->canvas);
    d_->scroll->viewport()->installEventFilter(this);
    d_->canvas->installEventFilter(this);

    QImageReader r(path_); r.setAutoTransform(true);
    const QSize sz = r.size();
    const QString fmt = QString::fromLatin1(r.format()).toUpper();
    if (r.supportsAnimation() && r.imageCount() > 1) {            // GIF / animated WebP
        d_->movie = new QMovie(path_, QByteArray(), this);
        connect(d_->movie, &QMovie::frameChanged, this, [this] { d_->canvas->pm = d_->movie->currentPixmap(); d_->canvas->update(); });
        d_->movie->jumpToFrame(0);
        d_->canvas->pm = d_->movie->currentPixmap();
        d_->movie->start();
    } else {
        d_->canvas->pm = QPixmap::fromImage(r.read());
        if (d_->canvas->pm.isNull()) d_->canvas->pm = QPixmap(path_);
    }
    if (d_->canvas->pm.isNull()) { d_->info->setText("Can't read this image."); d_->canvas->pm = QPixmap(1, 1); }
    else d_->info->setText(QString("%1 \u00D7 %2  \u00B7  %3  \u00B7  %4").arg(d_->canvas->pm.width()).arg(d_->canvas->pm.height())
                           .arg(fmt.isEmpty() ? QFileInfo(path_).suffix().toUpper() : fmt, fmtSize(QFileInfo(path_).size())));
    Q_UNUSED(sz);
    d_->canvas->zoom = 1.0; d_->canvas->refresh();
    QTimer::singleShot(0, this, [this] { fitToWindow(); fx::fadeIn(d_->canvas, 220, 1); });
}

void MediaTab::updateZoomLabel() { if (d_->zoomLbl) d_->zoomLbl->setText(QString::number(qRound(d_->canvas->zoom * 100)) + "%"); }

void MediaTab::setZoom(double z, bool animate) {
    if (!d_->canvas) return;
    z = qBound(0.02, z, 32.0);
    if (d_->zoomAnim) { d_->zoomAnim->stop(); d_->zoomAnim.clear(); }   // QPointer: tween() deleteLater()s itself when it finishes
    auto apply = [this](int milli) { d_->canvas->zoom = milli / 1000.0; d_->canvas->refresh(); updateZoomLabel(); };
    if (animate) d_->zoomAnim = fx::tween(this, qRound(d_->canvas->zoom * 1000), qRound(z * 1000), 160, apply, {}, false, Anim::Normal);   // applies z itself if it doesn't animate
    else apply(qRound(z * 1000));
}

void MediaTab::fitToWindow() {
    if (!d_->canvas || d_->canvas->pm.isNull()) return;
    d_->fit = true;
    const QSize vp = d_->scroll->viewport()->size() - QSize(8, 8);
    const double z = qMin(1.0, qMin(vp.width() / double(d_->canvas->pm.width()), vp.height() / double(d_->canvas->pm.height())));
    setZoom(qMax(0.02, z), d_->canvas->zoom != 1.0);
}

void MediaTab::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (kind_ == Image && d_->fit && d_->canvas) {
        const QSize vp = d_->scroll->viewport()->size() - QSize(8, 8);
        const double z = qMin(1.0, qMin(vp.width() / double(d_->canvas->pm.width()), vp.height() / double(d_->canvas->pm.height())));
        setZoom(qMax(0.02, z), false);
    }
}

bool MediaTab::eventFilter(QObject* o, QEvent* e) {
    if (kind_ != Image || !d_->canvas) return QWidget::eventFilter(o, e);
    if (e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        if (we->modifiers() & Qt::ControlModifier) {                 // Ctrl+wheel zooms (plain wheel scrolls)
            d_->fit = false;
            setZoom(d_->canvas->zoom * (we->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), false);
            return true;
        }
    } else if (e->type() == QEvent::MouseButtonPress) {             // drag to pan, click (without drag) toggles fit / 100%
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::LeftButton) { d_->dragging = true; d_->moved = false; d_->dragFrom = me->globalPosition().toPoint(); }
    } else if (e->type() == QEvent::MouseMove && d_->dragging) {
        auto* me = static_cast<QMouseEvent*>(e);
        const QPoint now = me->globalPosition().toPoint(), dlt = now - d_->dragFrom;
        if (dlt.manhattanLength() > 3) d_->moved = true;
        if (d_->moved) {
            d_->scroll->horizontalScrollBar()->setValue(d_->scroll->horizontalScrollBar()->value() - dlt.x());
            d_->scroll->verticalScrollBar()->setValue(d_->scroll->verticalScrollBar()->value() - dlt.y());
            d_->dragFrom = now;
        }
    } else if (e->type() == QEvent::MouseButtonRelease && d_->dragging) {
        d_->dragging = false;
        if (!d_->moved && o == d_->canvas) { if (d_->fit) { d_->fit = false; setZoom(1.0); } else fitToWindow(); }
    }
    return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------------------------
#ifdef MAKDOWN_MULTIMEDIA
void MediaTab::buildAudio() {
    auto* page = new QWidget; auto* col = new QVBoxLayout(page); col->setContentsMargins(32, 20, 32, 24); col->setSpacing(10);
    col->addStretch(1);
    d_->cover = new QLabel; d_->cover->setAlignment(Qt::AlignCenter); d_->cover->hide(); col->addWidget(d_->cover, 0, Qt::AlignHCenter);
    d_->bars = new Bars; col->addWidget(d_->bars, 0, Qt::AlignHCenter);
    d_->title = new QLabel(QFileInfo(path_).completeBaseName()); d_->title->setObjectName("mediaTitle"); d_->title->setAlignment(Qt::AlignCenter);
    d_->title->setWordWrap(true); QFont f = d_->title->font(); f.setPointSizeF(f.pointSizeF() * 1.5); f.setBold(true); d_->title->setFont(f);
    d_->sub = new QLabel; d_->sub->setObjectName("hint"); d_->sub->setAlignment(Qt::AlignCenter);
    col->addWidget(d_->title); col->addWidget(d_->sub);

    auto* seekRow = new QHBoxLayout;
    d_->cur = new QLabel("0:00"); d_->total = new QLabel("0:00");
    d_->seek = new QSlider(Qt::Horizontal); d_->seek->setRange(0, 0);
    seekRow->addWidget(d_->cur); seekRow->addWidget(d_->seek, 1); seekRow->addWidget(d_->total);
    col->addLayout(seekRow);

    auto* ctl = new QHBoxLayout; ctl->setSpacing(8);
    d_->loop = new QPushButton("\u21BB"); d_->loop->setCheckable(true); d_->loop->setToolTip("Repeat"); d_->loop->setFixedWidth(40);
    d_->play = new QPushButton("\u25B6"); d_->play->setFixedSize(64, 38); d_->play->setToolTip("Play / pause (Space)");
    d_->mute = new QPushButton("\u266A"); d_->mute->setCheckable(true); d_->mute->setToolTip("Mute"); d_->mute->setFixedWidth(40);
    d_->vol = new QSlider(Qt::Horizontal); d_->vol->setRange(0, 100); d_->vol->setValue(80); d_->vol->setFixedWidth(110);
    ctl->addStretch(1); ctl->addWidget(d_->loop); ctl->addWidget(d_->play); ctl->addWidget(d_->mute); ctl->addWidget(d_->vol); ctl->addStretch(1);
    col->addLayout(ctl);
    col->addStretch(2);
    static_cast<QVBoxLayout*>(layout())->addWidget(page, 1);

    d_->player = new QMediaPlayer(this); d_->out = new QAudioOutput(this);
    d_->player->setAudioOutput(d_->out); d_->out->setVolume(0.8f);
    d_->player->setSource(QUrl::fromLocalFile(path_));
    d_->info->setText(QFileInfo(path_).suffix().toUpper() + "  \u00B7  " + fmtSize(QFileInfo(path_).size()));

    connect(d_->play, &QPushButton::clicked, this, [this] {
        d_->player->playbackState() == QMediaPlayer::PlayingState ? d_->player->pause() : d_->player->play();
    });
    connect(d_->loop, &QPushButton::toggled, this, [this](bool on) { d_->player->setLoops(on ? QMediaPlayer::Infinite : QMediaPlayer::Once); });
    connect(d_->mute, &QPushButton::toggled, this, [this](bool on) { d_->out->setMuted(on); d_->mute->setText(on ? "\u2715" : "\u266A"); });
    connect(d_->vol, &QSlider::valueChanged, this, [this](int v) { d_->out->setVolume(v / 100.0f); });
    connect(d_->seek, &QSlider::sliderPressed, this, [this] { d_->seeking = true; });
    connect(d_->seek, &QSlider::sliderReleased, this, [this] { d_->seeking = false; d_->player->setPosition(d_->seek->value()); });
    connect(d_->seek, &QSlider::actionTriggered, this, [this] { QTimer::singleShot(0, this, [this] { if (!d_->seeking) d_->player->setPosition(d_->seek->sliderPosition()); }); });
    connect(d_->player, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        if (!d_->seeking) d_->seek->setValue(int(p));
        d_->cur->setText(fmtTime(p));
    });
    connect(d_->player, &QMediaPlayer::durationChanged, this, [this](qint64 d) { d_->seek->setRange(0, int(d)); d_->total->setText(fmtTime(d)); });
    connect(d_->player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        d_->play->setText(s == QMediaPlayer::PlayingState ? "\u275A\u275A" : "\u25B6");
        d_->bars->playing = (s == QMediaPlayer::PlayingState); d_->bars->sync();
    });
    connect(d_->player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& msg) {
        d_->sub->setText("Can't play this file" + (msg.isEmpty() ? QString() : ": " + msg));
    });
    connect(d_->player, &QMediaPlayer::metaDataChanged, this, [this] {
        const QMediaMetaData md = d_->player->metaData();
        const QString t = md.stringValue(QMediaMetaData::Title);
        if (!t.isEmpty()) d_->title->setText(t);
        QStringList s;
        const QString artist = md.stringValue(QMediaMetaData::ContributingArtist), album = md.stringValue(QMediaMetaData::AlbumTitle);
        if (!artist.isEmpty()) s << artist;
        if (!album.isEmpty()) s << album;
        if (!s.isEmpty()) d_->sub->setText(s.join("  \u00B7  "));
        QImage img = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();
        if (img.isNull()) img = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
        if (!img.isNull()) { d_->cover->setPixmap(QPixmap::fromImage(img.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation))); d_->cover->show(); }
    });
}
#else
void MediaTab::buildAudio() {}
#endif

void MediaTab::keyPressEvent(QKeyEvent* e) {
#ifdef MAKDOWN_MULTIMEDIA
    if (kind_ == Audio && d_->player) {
        if (e->key() == Qt::Key_Space) { d_->play->click(); return; }
        if (e->key() == Qt::Key_Left)  { d_->player->setPosition(qMax<qint64>(0, d_->player->position() - 5000)); return; }
        if (e->key() == Qt::Key_Right) { d_->player->setPosition(d_->player->position() + 5000); return; }
    }
#endif
    if (kind_ == Image && d_->canvas) {
        if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) { d_->fit = false; setZoom(d_->canvas->zoom * 1.25); return; }
        if (e->key() == Qt::Key_Minus) { d_->fit = false; setZoom(d_->canvas->zoom / 1.25); return; }
        if (e->key() == Qt::Key_0) { fitToWindow(); return; }
        if (e->key() == Qt::Key_1) { d_->fit = false; setZoom(1.0); return; }
    }
    QWidget::keyPressEvent(e);
}
