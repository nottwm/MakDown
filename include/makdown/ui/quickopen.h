// quickopen.h - "Go to file" panel (Ctrl+P, sidebar > Search > File names): type part of a file name, fuzzy-matched against
// every file below the working folder (the explorer root). Up/Down + Enter (or a click) opens it. "name:42" opens it at line 42.
#pragma once
#include <QWidget>
#include <QStringList>
#include <QTimer>
#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QThread;

class QuickOpen : public QWidget {
    Q_OBJECT
public:
    explicit QuickOpen(QWidget* parent = nullptr);
    ~QuickOpen() override;
    void start(const QString& root);               // (re)index the folder in the background
    void focusInput();

signals:
    void fileChosen(const QString& absPath, int line);   // line 0 = no line given

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    struct Job;
    void stop();
    void drain();
    void refilter();
    void choose(QListWidgetItem* it);

    QLineEdit* edit_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer poll_;
    std::shared_ptr<Job> job_;
    QThread* thread_ = nullptr;
    QString root_;
    QStringList files_;                            // relative paths, '/' separated
    bool indexing_ = false;
};
