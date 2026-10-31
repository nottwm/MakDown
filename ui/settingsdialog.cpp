// settingsdialog.cpp - sidebar-style settings: Appearance (themes + custom editor), Editor, Preview, About.
#include <makdown/ui/settingsdialog.h>
#include <makdown/core/buildinfo.h>
#include <makdown/core/lang.h>
#include <makdown/arch/platform.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QButtonGroup>
#include <QComboBox>
#include <QRadioButton>
#include <makdown/core/anim.h>
#include <makdown/ui/fx.h>
#include <QDesktopServices>
#include <QDir>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QLabel* title(const QString& t) { auto* l = new QLabel(t); l->setObjectName("pageTitle"); return l; }
QWidget* scrollPage(QWidget* inner) {          // wraps a page in a transparent scroll area
    auto* sa = new QScrollArea; sa->setWidgetResizable(true); sa->setFrameShape(QFrame::NoFrame);
    sa->viewport()->setAutoFillBackground(false); inner->setAutoFillBackground(false);
    sa->setWidget(inner); return sa;
}
}

SettingsDialog::SettingsDialog(AppSettings& s, QList<Theme>& themes, QWidget* parent)
    : QDialog(parent), s_(s), themes_(themes) {
    setWindowTitle(L("settings.title"));
    resize(960, 680);

    auto* root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    auto* body = new QHBoxLayout; body->setSpacing(0);

    nav_ = new QListWidget; nav_->setObjectName("nav"); nav_->setFixedWidth(210);
    nav_->addItems({L("nav.appearance"), L("nav.editor"), L("nav.preview"), L("nav.animations"), L("nav.language"), L("nav.about")});
    pages_ = new QStackedWidget;
    pages_->addWidget(scrollPage(buildAppearancePage()));
    pages_->addWidget(scrollPage(buildEditorPage()));
    pages_->addWidget(scrollPage(buildPreviewPage()));
    pages_->addWidget(scrollPage(buildAnimationsPage()));
    pages_->addWidget(scrollPage(buildLanguagePage()));
    pages_->addWidget(scrollPage(buildAboutPage()));
    connect(nav_, &QListWidget::currentRowChanged, this, [this](int i) {
        pages_->setCurrentIndex(i);
        fx::fadeIn(pages_->currentWidget(), 160);
    });
    nav_->setCurrentRow(0);
    body->addWidget(nav_); body->addWidget(pages_, 1);
    root->addLayout(body, 1);

    auto* foot = new QHBoxLayout; foot->setContentsMargins(20, 12, 20, 14);
    auto* folder = new QPushButton(L("settings.openPresets"));
    connect(folder, &QPushButton::clicked, this, [] {
        QDir().mkpath(ThemeStore::presetsDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(ThemeStore::presetsDir()));
    });
    auto* close = new QPushButton(L("settings.done")); close->setObjectName("primary"); close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(folder); foot->addStretch(1); foot->addWidget(close);
    root->addLayout(foot);

    rebuildThemeCombo();
    loadThemeIntoControls();
}

// ---------------------------------------------------------------------------------------------
// generic helper: a checkbox bound to a bool in AppSettings
// ---------------------------------------------------------------------------------------------
QCheckBox* SettingsDialog::addCheck(QVBoxLayout* l, const QString& text, const QString& hint, bool* target) {
    auto* cb = new QCheckBox(text); cb->setChecked(*target); l->addWidget(cb);
    if (!hint.isEmpty()) { auto* h = new QLabel(hint); h->setObjectName("hint"); h->setWordWrap(true); l->addWidget(h); }
    connect(cb, &QCheckBox::toggled, this, [this, target](bool v) { *target = v; emit applied(); });
    return cb;
}

// ---------------------------------------------------------------------------------------------
// Appearance page: theme picker + full custom-theme editor
// ---------------------------------------------------------------------------------------------
QWidget* SettingsDialog::buildAppearancePage() {
    auto* page = new QWidget; auto* v = new QVBoxLayout(page); v->setContentsMargins(28, 22, 28, 22); v->setSpacing(10);
    v->addWidget(title(L("nav.appearance")));

    // theme row
    auto* row = new QHBoxLayout;
    themeCombo_ = new QComboBox; themeCombo_->setMinimumWidth(240);
    cloneBtn_ = new QPushButton(L("theme.clone")); newBtn_ = new QPushButton(L("theme.new")); delBtn_ = new QPushButton(L("theme.delete"));
    row->addWidget(new QLabel(L("theme.label"))); row->addWidget(themeCombo_); row->addWidget(cloneBtn_); row->addWidget(newBtn_);
    row->addWidget(delBtn_); row->addStretch(1);
    v->addLayout(row);
    connect(themeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (loading_ || i < 0) return;
        s_.theme = themeCombo_->itemData(i).toString();
        loadThemeIntoControls(); emit applied();
    });
    connect(cloneBtn_, &QPushButton::clicked, this, [this] { createTheme(true); });
    connect(newBtn_, &QPushButton::clicked, this, [this] { createTheme(false); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { deleteCurrent(); });

    if (platform::customFrameSupported())
        addCheck(v, L("theme.frame"), L("theme.frameHint"), &s_.customFrame);

    // colour swatch strip (quick visual summary of the selected theme)
    swatches_ = new QFrame; swatches_->setObjectName("swatches");
    auto* sw = new QHBoxLayout(swatches_); sw->setContentsMargins(0, 4, 0, 4); sw->setSpacing(6);
    for (int i = 0; i < 8; ++i) sw->addWidget(new QLabel);
    sw->addStretch(1);
    v->addWidget(swatches_);

    readOnlyHint_ = new QLabel(L("theme.readOnlyHint"));
    readOnlyHint_->setWordWrap(true); readOnlyHint_->setObjectName("hint"); readOnlyHint_->setTextFormat(Qt::RichText);
    readOnlyHint_->setStyleSheet("margin-left:0px;");
    v->addWidget(readOnlyHint_);

    // ---- custom editor (enabled only for custom themes)
    customBox_ = new QGroupBox(L("theme.customize"));
    auto* cv = new QVBoxLayout(customBox_); cv->setSpacing(12);
    auto* form = new QFormLayout; form->setHorizontalSpacing(14); form->setVerticalSpacing(8);

    chromeCombo_ = new QComboBox;
    chromeCombo_->addItem(L("chrome.plain"), "plain"); chromeCombo_->addItem("GitHub", "github"); chromeCombo_->addItem("Windows 11", "win11");
    chromeCombo_->addItem(L("chrome.terminal"), "terminal"); chromeCombo_->addItem(L("chrome.warm"), "warm");
    chromeCombo_->addItem("VS Code", "vscode"); chromeCombo_->addItem("Glass", "glass");
    form->addRow(L("theme.layout"), chromeCombo_);
    connect(chromeCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        const QString v = chromeCombo_->itemData(i).toString(); edit([v](Theme& t) { t.chrome = v; });
    });

    // fonts: family + size on one row each
    auto fontRow = [&](const QString& label, QFontComboBox*& box, QSpinBox*& size, bool monoOnly,
                       std::function<void(Theme&, const QString&)> setFamily, std::function<void(Theme&, int)> setSize) {
        box = new QFontComboBox; box->setMinimumWidth(220);
        if (monoOnly) box->setFontFilters(QFontComboBox::MonospacedFonts);
        size = new QSpinBox; size->setRange(6, 40); size->setSuffix(" pt");
        auto* h = new QHBoxLayout; h->addWidget(box, 1); h->addWidget(size);
        form->addRow(label, h);
        connect(box, &QFontComboBox::currentFontChanged, this, [this, setFamily](const QFont& f) {
            const QString fam = f.family(); edit([=](Theme& t) { setFamily(t, fam); });
        });
        connect(size, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, setSize](int n) { edit([=](Theme& t) { setSize(t, n); }); });
    };
    fontRow(L("theme.uiFont"), uiFontBox_, uiSize_, false, [](Theme& t, const QString& f) { t.uiFont = f; }, [](Theme& t, int n) { t.uiSize = n; });
    fontRow(L("theme.editorFont"), edFontBox_, edSize_, true, [](Theme& t, const QString& f) { t.editorFont = f; }, [](Theme& t, int n) { t.editorSize = n; });
    fontRow(L("theme.previewFont"), pvFontBox_, pvSize_, false, [](Theme& t, const QString& f) { t.previewFont = f; }, [](Theme& t, int n) { t.previewSize = n; });

    // code preview styling
    codeFontBox_ = new QFontComboBox; codeFontBox_->setFontFilters(QFontComboBox::MonospacedFonts);
    codeScale_ = new QSpinBox; codeScale_->setRange(60, 140); codeScale_->setSuffix(" %");
    auto* ch = new QHBoxLayout; ch->addWidget(codeFontBox_, 1); ch->addWidget(codeScale_);
    form->addRow(L("theme.codeFont"), ch);
    connect(codeFontBox_, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) { const QString fam = f.family(); edit([=](Theme& t) { t.codeFont = fam; }); });
    connect(codeScale_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) { edit([=](Theme& t) { t.codeFontScale = n; }); });

    // UI corners
    radius_ = new QSlider(Qt::Horizontal); radius_->setRange(0, 20); radiusVal_ = new QLabel; radiusVal_->setMinimumWidth(36);
    auto* rh = new QHBoxLayout; rh->addWidget(radius_, 1); rh->addWidget(radiusVal_);
    form->addRow(L("theme.radius"), rh);
    connect(radius_, &QSlider::valueChanged, this, [this](int n) { radiusVal_->setText(QString::number(n) + " px"); edit([=](Theme& t) { t.radius = n; }); });

    if (platform::customFrameSupported()) {
        frameWidth_ = new QSpinBox; frameWidth_->setRange(0, 6); frameWidth_->setSuffix(" px"); frameWidth_->setSpecialValueText(L("theme.none"));
        form->addRow(L("theme.windowBorder"), frameWidth_);
        connect(frameWidth_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) { edit([=](Theme& t) { t.frameWidth = n; }); });
    }
    pageWidth_ = new QSpinBox; pageWidth_->setRange(480, 1800); pageWidth_->setSingleStep(20); pageWidth_->setSuffix(" px");
    form->addRow(L("theme.docWidth"), pageWidth_);
    connect(pageWidth_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) { edit([=](Theme& t) { t.pageWidth = n; }); });
    cv->addLayout(form);

    auto bindCheck = [&](QCheckBox*& cb, const QString& text, std::function<void(Theme&, bool)> fn) {
        cb = new QCheckBox(text); cv->addWidget(cb);
        connect(cb, &QCheckBox::toggled, this, [this, fn](bool on) { edit([=](Theme& t) { fn(t, on); }); });
    };
    bindCheck(codeBorder_, L("theme.codeBorder"), [](Theme& t, bool on) { t.codeBorder = on; });
    bindCheck(codeLabel_, L("theme.codeLabel"), [](Theme& t, bool on) { t.codeLangLabel = on; });
    bindCheck(headRules_, L("theme.headRules"), [](Theme& t, bool on) { t.headingRules = on; });

    // colour grid
    auto* grid = new QGridLayout; grid->setHorizontalSpacing(10); grid->setVerticalSpacing(8);
    int i = 0;
    for (const QString& key : ThemeStore::colorKeys()) {
        auto* b = new QPushButton; b->setMinimumHeight(32);
        colorBtns_[key] = b;
        connect(b, &QPushButton::clicked, this, [this, key] { pickColor(key); });
        grid->addWidget(b, i / 3, i % 3); ++i;
    }
    cv->addWidget(new QLabel(L("theme.colours")));
    cv->addLayout(grid);

    v->addWidget(customBox_);
    v->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildEditorPage() {
    auto* page = new QWidget; auto* v = new QVBoxLayout(page); v->setContentsMargins(28, 22, 28, 22);
    v->addWidget(title(L("nav.editor")));
    addCheck(v, L("editor.vim"), L("editor.vimHint"), &s_.vim);
    addCheck(v, L("editor.marks"), L("editor.marksHint"), &s_.paragraphMarks);
    addCheck(v, L("editor.spaces"), L("editor.spacesHint"), &s_.showSpaces);
    addCheck(v, L("editor.numbers"), QString(), &s_.lineNumbers);
    addCheck(v, L("editor.highlight"), QString(), &s_.highlightLine);
    addCheck(v, L("editor.wrap"), QString(), &s_.wrap);
    addCheck(v, L("editor.sidebar"), L("editor.sidebarHint"), &s_.explorer);
    auto* row = new QHBoxLayout; auto* tab = new QSpinBox; tab->setRange(1, 16); tab->setValue(s_.tabWidth);
    row->addWidget(new QLabel(L("editor.tabWidth"))); row->addWidget(tab); row->addStretch(1);
    v->addLayout(row);
    connect(tab, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) { s_.tabWidth = n; emit applied(); });
    v->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildPreviewPage() {
    auto* page = new QWidget; auto* v = new QVBoxLayout(page); v->setContentsMargins(28, 22, 28, 22);
    v->addWidget(title(L("nav.preview")));
    addCheck(v, L("preview.reveal"), L("preview.revealHint"), &s_.revealMarkers);
    addCheck(v, L("preview.live"), L("preview.liveHint"), &s_.livePreview);
    addCheck(v, L("preview.sync"), L("preview.syncHint"), &s_.syncScroll);
    addCheck(v, L("preview.liveReload"), L("preview.liveReloadHint"), &s_.liveReload);
    v->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildAnimationsPage() {
    auto* page = new QWidget; auto* v = new QVBoxLayout(page); v->setContentsMargins(28, 22, 28, 22);
    v->addWidget(title(L("nav.animations")));
    const char* const keys[4][2] = {{"anim.0.name", "anim.0.hint"}, {"anim.1.name", "anim.1.hint"}, {"anim.2.name", "anim.2.hint"}, {"anim.3.name", "anim.3.hint"}};
    auto* group = new QButtonGroup(page);
    for (int i = 0; i < 4; ++i) {
        auto* rb = new QRadioButton(L(keys[i][0])); rb->setChecked(s_.animations == i);
        group->addButton(rb, i); v->addWidget(rb);
        auto* h = new QLabel(L(keys[i][1])); h->setObjectName("hint"); h->setWordWrap(true); v->addWidget(h);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int i) { s_.animations = i; Anim::setLevel(i); emit applied(); });
    v->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildLanguagePage() {
    auto* page = new QWidget; auto* v = new QVBoxLayout(page); v->setContentsMargins(28, 22, 28, 22); v->setSpacing(10);
    v->addWidget(title(L("lang.title")));
    auto* row = new QHBoxLayout;
    auto* combo = new QComboBox; combo->setMinimumWidth(280);
    combo->addItem(L("lang.auto"), "auto");
    for (const Lang::Info& i : Lang::available()) combo->addItem(i.name + "  (" + i.code + ")", i.code);
    combo->setCurrentIndex(qMax(0, combo->findData(s_.language)));
    row->addWidget(new QLabel(L("lang.label"))); row->addWidget(combo); row->addStretch(1);
    v->addLayout(row);
    auto* note = new QLabel(L("lang.restart")); note->setObjectName("hint"); note->setWordWrap(true); note->hide();
    v->addWidget(note);
    connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo, note](int i) {
        s_.language = combo->itemData(i).toString();
        note->setVisible(Lang::resolve(s_.language) != Lang::current());      // the windows are built once: a restart switches
        emit applied();
    });
    auto* hint = new QLabel(L("lang.hint").arg(QDir::toNativeSeparators(Lang::langDir()).toHtmlEscaped()));
    hint->setObjectName("hint"); hint->setWordWrap(true); hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(hint);
    auto* open = new QPushButton(L("lang.openFolder"));
    connect(open, &QPushButton::clicked, this, [] {
        QDir().mkpath(Lang::langDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(Lang::langDir()));
    });
    auto* orow = new QHBoxLayout; orow->addWidget(open); orow->addStretch(1);
    v->addLayout(orow);
    v->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildAboutPage() {
    auto* page = new QWidget; auto* v = new QVBoxLayout(page); v->setContentsMargins(28, 22, 28, 22);
    v->addWidget(title(L("about.page")));
    auto* info = new QLabel(makdownBuildInfo());
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->setStyleSheet("font-family: monospace;");
    v->addWidget(info);
    auto* paths = new QLabel(L("about.paths").arg(ThemeStore::presetsDir().toHtmlEscaped(),
                                              (ThemeStore::appHome() + "/settings.jsonc").toHtmlEscaped()));
    paths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(paths); v->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------------------------
// theme list handling
// ---------------------------------------------------------------------------------------------
Theme* SettingsDialog::current() {
    for (Theme& t : themes_) if (t.name == s_.theme) return &t;
    return themes_.isEmpty() ? nullptr : &themes_.first();
}

void SettingsDialog::rebuildThemeCombo() {
    loading_ = true;
    themeCombo_->clear();
    for (const Theme& t : themes_) themeCombo_->addItem(t.builtin ? t.name : t.name + "   \u2605 " + L("theme.custom"), t.name);
    themeCombo_->setCurrentIndex(qMax(0, themeCombo_->findData(s_.theme)));
    loading_ = false;
}

void SettingsDialog::loadThemeIntoControls() {
    Theme* t = current(); if (!t) return;
    loading_ = true;                                     // block edit() while we push values into widgets
    const bool editable = !t->builtin;
    customBox_->setEnabled(editable);
    readOnlyHint_->setVisible(!editable);
    delBtn_->setEnabled(editable);
    chromeCombo_->setCurrentIndex(qMax(0, chromeCombo_->findData(t->chrome)));
    auto first = [](const QString& spec) { return spec.split(',').first().trimmed(); };
    uiFontBox_->setCurrentFont(QFont(first(t->uiFont)));     uiSize_->setValue(t->uiSize);
    edFontBox_->setCurrentFont(QFont(first(t->editorFont))); edSize_->setValue(t->editorSize);
    pvFontBox_->setCurrentFont(QFont(first(t->previewFont))); pvSize_->setValue(t->previewSize);
    codeFontBox_->setCurrentFont(QFont(first(t->codeFont))); codeScale_->setValue(t->codeFontScale);
    radius_->setValue(t->radius); radiusVal_->setText(QString::number(t->radius) + " px");
    pageWidth_->setValue(t->pageWidth);
    if (frameWidth_) frameWidth_->setValue(t->frameWidth);
    codeBorder_->setChecked(t->codeBorder); codeLabel_->setChecked(t->codeLangLabel); headRules_->setChecked(t->headingRules);
    for (const QString& k : ThemeStore::colorKeys()) paintColorButton(k);
    refreshSwatches();
    loading_ = false;
}

void SettingsDialog::refreshSwatches() {
    Theme* t = current(); if (!t) return;
    const QStringList keys = {"bg", "panel", "fg", "accent", "link", "heading", "codeBg", "codeFg"};
    int i = 0;
    for (QLabel* l : swatches_->findChildren<QLabel*>()) {
        if (i >= keys.size()) break;
        l->setStyleSheet("background:" + t->color(keys[i++]) + ";");
    }
}

void SettingsDialog::paintColorButton(const QString& key) {
    Theme* t = current(); if (!t) return;
    const QColor c(t->color(key));
    const QString fg = c.lightness() > 140 ? "#000000" : "#ffffff";
    colorBtns_[key]->setText(ThemeStore::colorLabel(key) + "  " + c.name());
    colorBtns_[key]->setStyleSheet(QString("QPushButton{background:%1;color:%2;text-align:left;padding-left:10px;}").arg(c.name(), fg));
}

void SettingsDialog::pickColor(const QString& key) {
    Theme* t = current(); if (!t || t->builtin) return;
    const QColor c = QColorDialog::getColor(QColor(t->color(key)), this, ThemeStore::colorLabel(key));
    if (!c.isValid()) return;
    edit([&](Theme& th) { th.colors[key] = c.name(); });
    paintColorButton(key);
}

void SettingsDialog::edit(const std::function<void(Theme&)>& fn) {
    if (loading_) return;
    Theme* t = current(); if (!t || t->builtin) return;
    fn(*t);
    ThemeStore::save(*t);          // custom presets are saved to <presets>/<name>.jsonc immediately
    refreshSwatches();
    emit applied();
}

void SettingsDialog::createTheme(bool cloneCurrent) {
    Theme* cur = current(); if (!cur) return;
    bool ok = false;
    QString name = QInputDialog::getText(this, cloneCurrent ? L("dlg.clonePreset") : L("dlg.newPreset"), L("dlg.presetName"), QLineEdit::Normal,
                                         cloneCurrent ? cur->name + L("theme.copySuffix") : QString(), &ok);
    name = ThemeStore::sanitizeName(name);
    if (!ok || name.isEmpty()) return;
    for (const Theme& t : themes_)
        if (t.name.compare(name, Qt::CaseInsensitive) == 0) { QMessageBox::warning(this, L("dlg.nameTaken.title"), L("dlg.nameTaken.text").arg(name)); return; }
    Theme n;
    if (cloneCurrent) n = *cur; else n.fillDefaults();       // "New" starts from a neutral dark palette
    n.name = name; n.builtin = false;
    ThemeStore::save(n);
    themes_ = ThemeStore::loadAll();
    s_.theme = name;
    rebuildThemeCombo(); loadThemeIntoControls(); emit applied();
}

void SettingsDialog::deleteCurrent() {
    Theme* t = current(); if (!t || t->builtin) return;
    if (QMessageBox::question(this, L("dlg.deletePreset.title"), L("dlg.deletePreset.text").arg(t->name)) != QMessageBox::Yes) return;
    ThemeStore::remove(*t);
    themes_ = ThemeStore::loadAll();
    s_.theme = "GitHub";
    rebuildThemeCombo(); loadThemeIntoControls(); emit applied();
}
