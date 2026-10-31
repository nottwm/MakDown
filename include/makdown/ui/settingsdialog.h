// settingsdialog.h - the Settings window (themed like the rest of the app).
#pragma once
#include <QDialog>
#include <QMap>
#include <functional>
#include <makdown/core/theme.h>

class QListWidget; class QStackedWidget; class QComboBox; class QPushButton; class QFontComboBox; class QSpinBox;
class QSlider; class QCheckBox; class QGroupBox; class QLabel; class QVBoxLayout; class QFrame; class QWidget;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // Both objects are owned by MainWindow; the dialog edits them in place and emits applied().
    SettingsDialog(AppSettings& settings, QList<Theme>& themes, QWidget* parent = nullptr);

signals:
    void applied();      // something changed -> MainWindow re-applies theme/settings live

private:
    QWidget* buildAppearancePage();
    QWidget* buildEditorPage();
    QWidget* buildPreviewPage();
    QWidget* buildAnimationsPage();
    QWidget* buildLanguagePage();
    QWidget* buildAboutPage();
    QCheckBox* addCheck(QVBoxLayout* l, const QString& text, const QString& hint, bool* target);

    Theme* current();                                  // the theme currently selected in the combo
    void rebuildThemeCombo();
    void loadThemeIntoControls();                      // theme values -> widgets (without triggering edits)
    void refreshSwatches();
    void edit(const std::function<void(Theme&)>& fn);  // mutate + save the selected custom theme, then emit applied()
    void createTheme(bool cloneCurrent);               // "Clone..." and "New..." buttons
    void deleteCurrent();
    void pickColor(const QString& key);
    void paintColorButton(const QString& key);

    AppSettings& s_;
    QList<Theme>& themes_;
    bool loading_ = false;

    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QComboBox* themeCombo_ = nullptr;
    QPushButton *cloneBtn_ = nullptr, *newBtn_ = nullptr, *delBtn_ = nullptr;
    QLabel* readOnlyHint_ = nullptr;
    QFrame* swatches_ = nullptr;
    QGroupBox* customBox_ = nullptr;
    QComboBox* chromeCombo_ = nullptr;
    QFontComboBox *uiFontBox_ = nullptr, *edFontBox_ = nullptr, *pvFontBox_ = nullptr, *codeFontBox_ = nullptr;
    QSpinBox *uiSize_ = nullptr, *edSize_ = nullptr, *pvSize_ = nullptr, *codeScale_ = nullptr, *pageWidth_ = nullptr, *frameWidth_ = nullptr;
    QSlider* radius_ = nullptr; QLabel* radiusVal_ = nullptr;
    QCheckBox *codeBorder_ = nullptr, *codeLabel_ = nullptr, *headRules_ = nullptr;
    QMap<QString, QPushButton*> colorBtns_;
};
