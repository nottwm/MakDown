// search.cpp - see include/makdown/core/search.h
#include <makdown/core/search.h>

#include <QDir>
#include <QFileInfo>

namespace Search {

QRegularExpression makeRegex(const Options& o, QString* error) {
    if (error) error->clear();
    if (o.query.isEmpty()) return QRegularExpression();
    QString pat = o.regex ? o.query : QRegularExpression::escape(o.query);
    if (o.wholeWord) pat = "(?<![\\w])(?:" + pat + ")(?![\\w])";
    QRegularExpression::PatternOptions po = QRegularExpression::UseUnicodePropertiesOption;
    if (!o.matchCase) po |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression rx(pat, po);
    if (!rx.isValid()) { if (error) *error = rx.errorString(); }
    return rx;
}

bool skippedDir(const QString& n) {
    return n.startsWith('.') || n == "node_modules" || n == "__pycache__";
}

static int walkDir(const QString& dir, const std::atomic<bool>& cancel, int maxFiles, int& count,
                   const std::function<bool(const QString&, qint64)>& onFile, bool& stop) {
    const QFileInfoList list = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks,
                                                       QDir::Name | QDir::IgnoreCase);
    QList<QFileInfo> dirs;
    for (const QFileInfo& fi : list) {
        if (cancel.load() || stop) return count;
        if (fi.isDir()) { if (!skippedDir(fi.fileName())) dirs << fi; continue; }
        if (count >= maxFiles) { stop = true; return count; }
        ++count;
        if (!onFile(fi.absoluteFilePath(), fi.size())) { stop = true; return count; }
    }
    for (const QFileInfo& d : dirs) {
        if (cancel.load() || stop) break;
        walkDir(d.absoluteFilePath(), cancel, maxFiles, count, onFile, stop);
    }
    return count;
}

int walk(const QString& root, const std::atomic<bool>& cancel, int maxFiles,
         const std::function<bool(const QString&, qint64)>& onFile) {
    int count = 0; bool stop = false;
    return walkDir(root, cancel, maxFiles, count, onFile, stop);
}

bool looksBinary(const QByteArray& head) {
    if (head.contains('\0')) return true;
    int ctl = 0;
    for (const char c : head) { const uchar u = uchar(c); if (u < 0x20 && u != '\t' && u != '\n' && u != '\r' && u != '\f' && u != 0x1b) ++ctl; }
    return !head.isEmpty() && ctl * 100 / head.size() > 10;
}

QStringList splitGlobs(const QString& spec) {
    QStringList out;
    for (QString g : spec.split(',', Qt::SkipEmptyParts)) {
        g = g.trimmed();
        if (g.isEmpty()) continue;
        if (g.endsWith('/')) g += "**";                               // "src/" = everything below src
        out << g;
    }
    return out;
}

bool globMatch(const QStringList& globs, const QString& relPath) {
    const QString name = QFileInfo(relPath).fileName();
    for (const QString& g : globs) {
        QString re = QRegularExpression::wildcardToRegularExpression(g);       // anchored; "*" does not cross '/'
        re.replace("[^/]*[^/]*/", "(?:.*/)?");                                   // "**/" = any folders, or none
        re.replace("[^/]*[^/]*", ".*");                                          // other "**" crosses folders
        const QRegularExpression rx(re, QRegularExpression::CaseInsensitiveOption);
        if (rx.match(g.contains('/') ? relPath : name).hasMatch()) return true;
    }
    return false;
}

QVector<Hit> searchText(const QString& text, const QRegularExpression& rx, int maxHits) {
    QVector<Hit> hits;
    int lineNo = 0, pos = 0;
    const int n = text.size();
    while (pos <= n && hits.size() < maxHits) {
        int end = text.indexOf('\n', pos);
        if (end < 0) end = n;
        ++lineNo;
        int len = end - pos;
        if (len > 0 && text[pos + len - 1] == '\r') --len;
        if (len > 0) {
            const QString line = text.mid(pos, len);
            int from = 0;
            while (from <= line.size() && hits.size() < maxHits) {
                const QRegularExpressionMatch m = rx.match(line, from);
                if (!m.hasMatch()) break;
                const int s = m.capturedStart(), l = m.capturedLength();
                if (l == 0) { from = s + 1; continue; }
                Hit h; h.line = lineNo; h.col = s; h.len = l;
                int lead = 0; while (lead < line.size() && line[lead].isSpace()) ++lead;
                const int start = qMax(lead, s - 30);
                QString pv = line.mid(start, 220);
                int pcol = s - start;
                if (start > lead) { pv.prepend(QChar(0x2026)); ++pcol; }
                h.preview = pv; h.pcol = pcol; h.plen = qMin(l, qMax(0, pv.size() - pcol));
                hits << h;
                from = s + l;
            }
        }
        pos = end + 1;
    }
    return hits;
}

int fuzzyScore(const QString& path, const QString& query) {
    if (query.isEmpty()) return 0;
    const QString p = path.toLower(), q = query.toLower();
    const int nameStart = path.lastIndexOf('/') + 1;
    int score = 0, pi = 0, prev = -2;
    for (int qi = 0; qi < q.size(); ++qi) {
        if (q[qi] == '/' || q[qi] == '\\') { pi = p.indexOf('/', pi); if (pi < 0) return -1; prev = pi; ++pi; continue; }
        const int f = p.indexOf(q[qi], pi);
        if (f < 0) return -1;
        score += 10;
        if (f == prev + 1) score += 15;                                   // consecutive
        if (f == 0 || p[f - 1] == '/' || p[f - 1] == '_' || p[f - 1] == '-' || p[f - 1] == '.' || p[f - 1] == ' ') score += 12;   // word start
        if (f >= nameStart) score += 6;                                   // inside the file name
        prev = f; pi = f + 1;
    }
    const int idx = p.indexOf(q, nameStart);
    if (idx >= 0) score += 40 + (idx == nameStart ? 30 : 0);              // the whole query is a substring of the file name
    return score - path.size() / 8;
}

} // namespace Search
