// search.h - UI-independent search helpers shared by the find bar, "go to file" and "find in files".
#pragma once
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace Search {

struct Options {
    QString query;
    bool matchCase = false, wholeWord = false, regex = false;
};

// Builds the pattern for the options. Returns an invalid expression (and sets *error) for an empty or bad query.
QRegularExpression makeRegex(const Options& o, QString* error = nullptr);

// Directories never entered by the file walkers: hidden folders (.git ...), node_modules, __pycache__.
bool skippedDir(const QString& name);

// Walks `root` depth-first (names sorted, symlinks not followed) and calls onFile(absolutePath, size) for every file.
// Stops when onFile returns false, `cancel` becomes true, or `maxFiles` files were reported. Returns the number reported.
int walk(const QString& root, const std::atomic<bool>& cancel, int maxFiles,
         const std::function<bool(const QString& absPath, qint64 size)>& onFile);

bool looksBinary(const QByteArray& head);          // NUL byte or lots of control characters

// "*.md, src/**/*.cpp" -> patterns. A pattern without '/' is matched against the file name, otherwise the relative path.
QStringList splitGlobs(const QString& spec);
bool globMatch(const QStringList& globs, const QString& relPath);

struct Hit {
    int line = 0;          // 1-based
    int col = 0;           // 0-based column of the match in the full line (UTF-16 units, like the editor)
    int len = 0;
    QString preview;       // the line, leading blanks cut, shortened around the match
    int pcol = 0, plen = 0; // match position inside `preview`
};

// Searches `text` line by line (so ^ and $ work per line). Zero-length matches are ignored.
QVector<Hit> searchText(const QString& text, const QRegularExpression& rx, int maxHits);

// Fuzzy "go to file" score: -1 when `query` is not a subsequence of `path`, else higher = better.
int fuzzyScore(const QString& path, const QString& query);

} // namespace Search
