// synhl.cpp - the highlighting engine for syntax packs.
//
// Per line: from the current position, find the earliest match among all region starts and rules
// (ties: regions first, then the pack's order), colour it, continue behind it. Inside a region
// (block comment, multi-line string) only the region's end is searched. The block state stores
// "which region are we in" so the next line can resume.
#include <makdown/ui/synhl.h>
#include <QColor>
#include <QRegularExpression>
#include <QTextBlock>
#include <QVector>

namespace {
// next match of rx at/after `from` that is not empty (empty matches would never advance)
QRegularExpressionMatch nextMatch(const QRegularExpression& rx, const QString& text, int from) {
    QRegularExpressionMatch m = rx.match(text, from);
    while (m.hasMatch() && m.capturedLength() == 0 && m.capturedStart() < text.size())
        m = rx.match(text, m.capturedStart() + 1);
    return (m.hasMatch() && m.capturedLength() > 0) ? m : QRegularExpressionMatch();
}

int colorDistance(const QColor& a, const QColor& b) {
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

// One cached "next match" per pattern, so a line with N tokens does not run every regex N times.
struct Cache {
    QRegularExpressionMatch m;
    int from = -1;          // position the search started from
    bool done = false;      // no match at/after `from`
};
} // namespace

void SyntaxHighlighter::setPack(const SyntaxPtr& p) { pack_ = p; buildFormats(); }

void SyntaxHighlighter::setTheme(const Theme& t) { theme_ = t; haveTheme_ = true; buildFormats(); }

void SyntaxHighlighter::buildFormats() {
    fmts_.clear();
    if (haveTheme_) {
        const bool dark = theme_.dark;
        const QColor bg(theme_.color("editorBg")), fg(theme_.color("editorFg")), accent(theme_.color("accent"));
        // keywords use the theme's accent colour when it is readable on this background, else a fixed one
        const bool accentOk = colorDistance(accent, bg) > 160 && colorDistance(accent, fg) > 80;
        auto C = [&](const char* light, const char* darkc) { return QColor(dark ? darkc : light); };
        struct Row { const char* scope; QColor color; bool bold, italic; };
        const QList<Row> rows = {
            {"keyword",      accentOk ? accent : C("#cf222e", "#f7768e"), true,  false},
            {"type",         C("#0b7285", "#2ac3de"), false, false},
            {"constant",     C("#a35200", "#e0af68"), false, false},
            {"builtin",      C("#8250df", "#7dcfff"), false, false},
            {"function",     C("#6639ba", "#bb9af7"), false, false},
            {"variable",     C("#0550ae", "#7aa2f7"), false, false},
            {"string",       C("#0a7a3d", "#9ece6a"), false, false},
            {"escape",       C("#cf222e", "#f7768e"), false, false},
            {"number",       C("#b5530a", "#ff9e64"), false, false},
            {"comment",      QColor(theme_.color("muted")), false, true},
            {"preprocessor", C("#9a6700", "#e0af68"), false, false},
            {"attribute",    C("#9a6700", "#e0af68"), false, false},
            {"macro",        C("#9a6700", "#e0af68"), false, false},
            {"label",        C("#953800", "#e0af68"), false, false},
        };
        for (const Row& r : rows) {
            QTextCharFormat f;
            f.setForeground(r.color);
            if (r.bold) f.setFontWeight(QFont::Bold);
            if (r.italic) f.setFontItalic(true);
            fmts_[r.scope] = f;
        }
    }
    if (pack_) {                                  // pack overrides: "colors": { "keyword": "#ff0000" }
        for (auto it = pack_->colors.begin(); it != pack_->colors.end(); ++it) {
            const QColor c(it.value());
            if (!c.isValid()) continue;
            QTextCharFormat f = fmts_.value(it.key());
            f.setForeground(c);
            fmts_[it.key()] = f;
        }
    }
    if (document()) rehighlight();
}

void SyntaxHighlighter::highlightBlock(const QString& text) {
    if (!pack_) return;
    const SyntaxPack& P = *pack_;
    const int n = text.size(), nReg = P.regions.size(), nRule = P.rules.size();
    int region = previousBlockState() > 0 ? previousBlockState() - 1 : -1;
    if (region >= nReg) region = -1;                         // pack was edited under us

    QVector<Cache> regionCache(nReg), ruleCache(nRule);
    auto find = [&](Cache& c, const QRegularExpression& rx, int pos) -> bool {
        if (c.from >= 0 && c.from <= pos) {                  // a match found from an earlier position is still the
            if (c.done) return false;                        // first one at/after `pos` as long as it starts there
            if (c.m.capturedStart() >= pos) return true;
        }
        c.m = nextMatch(rx, text, pos);
        c.from = pos;
        c.done = !c.m.hasMatch();
        return !c.done;
    };

    int pos = 0;
    while (pos < n) {
        if (region >= 0) {                                    // inside a block comment / string ...
            const SyntaxPack::Region& R = P.regions[region];
            int endAt = -1, endLen = 0, p = pos;
            while (true) {
                const QRegularExpressionMatch m = R.end.pattern().isEmpty() ? QRegularExpressionMatch() : nextMatch(R.end, text, p);
                const int e = m.hasMatch() ? m.capturedStart() : -1;
                if (!R.escape.isEmpty()) {
                    const int esc = text.indexOf(R.escape, p);
                    if (esc >= 0 && (e < 0 || esc < e)) { p = esc + R.escape.size() + 1; if (p > n) break; continue; }
                }
                if (e >= 0) { endAt = e; endLen = m.capturedLength(); }
                break;
            }
            if (endAt < 0) {                                  // runs to the end of the line
                setFormat(pos, n - pos, format(R.scope));
                if (!R.multiline) region = -1;
                break;
            }
            setFormat(pos, endAt + endLen - pos, format(R.scope));
            pos = endAt + endLen;
            region = -1;
            continue;
        }

        int bestStart = n + 1, bestLen = 0, bestRegion = -1, bestRule = -1;
        for (int i = 0; i < nReg; ++i)
            if (find(regionCache[i], P.regions[i].begin, pos) && regionCache[i].m.capturedStart() < bestStart) {
                bestStart = regionCache[i].m.capturedStart(); bestLen = regionCache[i].m.capturedLength();
                bestRegion = i; bestRule = -1;
            }
        for (int i = 0; i < nRule; ++i)
            if (find(ruleCache[i], P.rules[i].rx, pos) && ruleCache[i].m.capturedStart() < bestStart) {
                bestStart = ruleCache[i].m.capturedStart(); bestLen = ruleCache[i].m.capturedLength();
                bestRule = i; bestRegion = -1;
            }
        if (bestRegion < 0 && bestRule < 0) break;            // nothing else on this line

        if (bestRule >= 0) {
            setFormat(bestStart, bestLen, format(P.rules[bestRule].scope));
        } else {
            setFormat(bestStart, bestLen, format(P.regions[bestRegion].scope));
            region = bestRegion;                              // the rest of the line is searched for the end
        }
        pos = bestStart + bestLen;
    }
    setCurrentBlockState(region >= 0 && P.regions[region].multiline ? region + 1 : 0);
}
