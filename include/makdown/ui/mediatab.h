// mediatab.h - image / audio preview in a right-hand tab, VS Code style.
//   images: checkerboard background, fit / 100% / zoom (Ctrl+wheel, click toggles fit), animated GIF / WebP
//   audio : play / pause, seek bar, volume, loop, cover art (needs QtMultimedia: build flag MAKDOWN_MULTIMEDIA)
#pragma once
#include <QWidget>

class MediaTab : public QWidget {
    Q_OBJECT
public:
    enum Kind { None, Image, Audio };
    static Kind kindOf(const QString& path);       // by extension (+ the build's capabilities); svg counts as Image
    static bool audioAvailable();

    explicit MediaTab(const QString& path, QWidget* parent = nullptr);
    ~MediaTab() override;
    QString path() const { return path_; }
    Kind kind() const { return kind_; }

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Impl;
    void buildImage();
    void buildAudio();
    void setZoom(double z, bool animate = true);
    void fitToWindow();
    void updateZoomLabel();

    QString path_;
    Kind kind_ = None;
    Impl* d_ = nullptr;
};
