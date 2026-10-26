// markdown.h - MakDown's own Markdown -> Qt-rich-text (HTML subset) renderer.
//
// We render ourselves (instead of QTextDocument::setMarkdown) so that every element can be
// styled from the active theme, and so the preview can optionally REVEAL the markers that are
// normally hidden (the * of lists/emphasis, the # of headings, the > of quotes ...).
#pragma once
#include <QString>
#include <makdown/core/theme.h>

// Converts markdown text to an HTML fragment that QTextBrowser understands.
//   revealMarks : show the hidden markdown syntax characters as faint ghost glyphs
//   baseDir     : folder used to resolve relative image paths
QString renderMarkdownToHtml(const QString& markdown, const Theme& theme, bool revealMarks, const QString& baseDir);

// Default stylesheet for the rendered document (fonts, heading sizes, link colour ...).
QString previewCss(const Theme& theme);
