// synhl.h - generic highlighter driven by a SyntaxPack (core/syntax.h). Scopes (keyword, string,
// comment ...) are coloured from the active theme; a pack can override any of them in "colors".
#pragma once
#include <QHash>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <makdown/core/syntax.h>
#include <makdown/core/theme.h>

class SyntaxHighlighter : public QSyntaxHighlighter {
public:
    explicit SyntaxHighlighter(QTextDocument* doc = nullptr) : QSyntaxHighlighter(doc) {}
    void setPack(const SyntaxPtr& p);            // call while detached from the document (see MarkdownEditor)
    void setTheme(const Theme& t);
protected:
    void highlightBlock(const QString& text) override;
private:
    void buildFormats();
    QTextCharFormat format(const QString& scope) const { return fmts_.value(scope); }

    SyntaxPtr pack_;
    Theme theme_;
    bool haveTheme_ = false;
    QHash<QString, QTextCharFormat> fmts_;
};
