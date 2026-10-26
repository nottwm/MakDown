// markdown.cpp - block + inline markdown parser producing Qt rich-text HTML.
// Supported: headings, paragraphs, **bold**, *italic*, ~~strike~~, `code`, links, images,
// bullet / numbered / nested / task lists, > quotes (nestable), fenced code, tables, rules.
// Every heading gets a GitHub-style anchor (<a name="my-heading">) so [text](#my-heading) links work.
#include <makdown/core/markdown.h>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <QVector>
#include <functional>

namespace {

// Everything the renderer needs, passed around by const reference.
struct Ctx { const Theme& t; bool marks; QString baseDir; QHash<QString, int>& slugs; };

// ---- regexes (compiled once) ------------------------------------------------
const QRegularExpression rxFence (R"(^\s*(```|~~~)\s*([\w+#.\-]*))");
const QRegularExpression rxHead  (R"(^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$)");
const QRegularExpression rxHr    (R"(^\s{0,3}([-*_])(\s*\1){2,}\s*$)");
const QRegularExpression rxQuote (R"(^\s{0,3}>\s?(.*)$)");
const QRegularExpression rxList  (R"(^(\s*)([-*+]|\d+[.)])\s+(.*)$)");
const QRegularExpression rxSep   (R"(^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$)");

// Ghost glyphs for "reveal markers" mode. Characters are numeric entities so the inline
// emphasis regexes can never mistake them for real markdown.
QString ghost(const Ctx& c, const QString& htmlText) {
    if (!c.marks) return {};
    return QString("<span style=\"color:%1\">%2</span>").arg(c.t.color("muted"), htmlText);
}

// QString::replace with a callback (Qt has no built-in for this).
QString replaceAll(const QString& s, const QRegularExpression& rx,
                   const std::function<QString(const QRegularExpressionMatch&)>& fn) {
    QString out; int last = 0;
    auto it = rx.globalMatch(s);
    while (it.hasNext()) {
        auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += fn(m);
        last = m.capturedEnd();
    }
    return out + s.mid(last);
}

// ---- inline level -------------------------------------------------------------
QString emphasis(QString s, const Ctx& c) {
    auto wrap = [&](const QRegularExpression& rx, const QString& markEntity, const char* open, const char* close) {
        const QString g = ghost(c, markEntity);
        s.replace(rx, g + open + "\\1" + close + g);
    };
    static const QRegularExpression bold1(R"(\*\*(?=\S)(.+?)(?<=\S)\*\*)");
    static const QRegularExpression bold2(R"((?<!\w)__(?=\S)(.+?)(?<=\S)__(?!\w))");
    static const QRegularExpression strike(R"(~~(?=\S)(.+?)(?<=\S)~~)");
    static const QRegularExpression ital1(R"(\*(?=[^\s*])(.+?)(?<=[^\s*])\*)");
    static const QRegularExpression ital2(R"((?<!\w)_(?=[^\s_])(.+?)(?<=[^\s_])_(?!\w))");
    wrap(bold1, "&#42;&#42;", "<b>", "</b>");
    wrap(bold2, "&#95;&#95;", "<b>", "</b>");
    wrap(strike, "&#126;&#126;", "<s>", "</s>");
    wrap(ital1, "&#42;", "<i>", "</i>");
    wrap(ital2, "&#95;", "<i>", "</i>");
    return s;
}

QString imageHtml(QString src, const QString& alt, const Ctx& c) {
    src.replace("&amp;", "&");
    if (src.startsWith("http://") || src.startsWith("https://"))      // never hit the network from the preview
        return QString("<a href=\"%1\">[image: %2]</a>").arg(src, alt);
    QFileInfo fi(src);
    if (!fi.isAbsolute()) fi.setFile(c.baseDir + "/" + src);
    if (fi.exists())
        return QString("<img src=\"%1\" alt=\"%2\">").arg(QUrl::fromLocalFile(fi.absoluteFilePath()).toString(), alt);
    return QString("<span style=\"color:%1\">[missing image: %2]</span>").arg(c.t.color("muted"), alt);
}

QString inlineMd(const QString& src, const Ctx& c) {
    // Pieces that must not be touched by later regexes (code, links, images) are "stashed"
    // behind \x01<index>\x02 tokens and restored at the very end.
    QStringList stash;
    auto put = [&](const QString& html) {
        stash << html;
        return QString(QChar(1)) + QString::number(stash.size() - 1) + QString(QChar(2));
    };
    QString s = src;

    // 1) inline code - contents are literal
    static const QRegularExpression rxCode("`([^`]+)`");
    s = replaceAll(s, rxCode, [&](const QRegularExpressionMatch& m) {
        const QString g = ghost(c, "&#96;");
        return put(g + QString("<span style=\"font-family:%1; background-color:%2; color:%3\">&nbsp;%4&nbsp;</span>")
                       .arg(ThemeStore::cssFamily(c.t.codeFont, '\''), c.t.color("codeBg"), c.t.color("codeFg"),
                            m.captured(1).toHtmlEscaped()) + g);
    });

    // 2) escape HTML in everything else
    s = s.toHtmlEscaped();

    // 3) images, links, <autolinks>
    static const QRegularExpression rxImg(R"(!\[([^\]]*)\]\(([^)\s]+)[^)]*\))");
    s = replaceAll(s, rxImg, [&](const QRegularExpressionMatch& m) { return put(imageHtml(m.captured(2), m.captured(1), c)); });
    static const QRegularExpression rxLink(R"(\[([^\]]+)\]\(([^)\s]+)[^)]*\))");
    s = replaceAll(s, rxLink, [&](const QRegularExpressionMatch& m) {
        return put(QString("<a href=\"%1\">%2</a>").arg(m.captured(2), emphasis(m.captured(1), c)));
    });
    static const QRegularExpression rxAuto(R"(&lt;(https?://[^\s&]+)&gt;)");
    s = replaceAll(s, rxAuto, [&](const QRegularExpressionMatch& m) {
        return put(QString("<a href=\"%1\">%1</a>").arg(m.captured(1)));
    });

    // 4) emphasis, then restore stashed pieces (highest index first: links may contain code tokens)
    s = emphasis(s, c);
    for (int i = stash.size() - 1; i >= 0; --i)
        s.replace(QString(QChar(1)) + QString::number(i) + QString(QChar(2)), stash[i]);
    return s;
}

// ---- block helpers ------------------------------------------------------------
QString codeBlock(const QString& code, const QString& lang, const Ctx& c) {
    const Theme& t = c.t;
    const double pt = t.previewSize * t.codeFontScale / 100.0;
    const QString border = t.codeBorder
        ? QString(" border=\"1\" style=\"border-color:%1; border-collapse:collapse\"").arg(t.color("border"))
        : QString(" border=\"0\"");
    QString label;
    if (t.codeLangLabel && !lang.isEmpty())
        label = QString("<div style=\"color:%1; font-size:%2pt\">%3</div>")
                    .arg(t.color("muted")).arg(qMax(7.0, pt - 1.5)).arg(lang.toHtmlEscaped());
    return QString("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"10\"%1><tr><td bgcolor=\"%2\">%3"
                   "<pre style=\"margin:0; font-family:%4; font-size:%5pt; color:%6\">%7</pre></td></tr></table>"
                   "<p style=\"margin:0\"></p>")
        .arg(border, t.color("codeBg"), label, ThemeStore::cssFamily(t.codeFont, '\''))
        .arg(pt).arg(t.color("codeFg"), code.toHtmlEscaped());
}

bool isBlockStart(const QString& l) {
    return rxFence.match(l).hasMatch() || rxHead.match(l).hasMatch() || rxHr.match(l).hasMatch() ||
           rxQuote.match(l).hasMatch() || rxList.match(l).hasMatch();
}

QStringList splitRow(QString r) {
    r = r.trimmed();
    if (r.startsWith('|')) r = r.mid(1);
    if (r.endsWith('|')) r.chop(1);
    QStringList cells = r.split('|');
    for (QString& x : cells) x = x.trimmed();
    return cells;
}

struct Item { int indent = 0; bool ordered = false; QString marker, text; int task = -1; };  // task: -1 none, 0 open, 1 done

int indentWidth(const QString& ws) { int n = 0; for (QChar ch : ws) n += (ch == '\t') ? 4 : 1; return n; }

QString listHtml(const QVector<Item>& items, const Ctx& c) {
    QString h; QVector<int> indents; QVector<bool> ordered;
    auto closeTop = [&]() { h += ordered.last() ? "</li></ol>" : "</li></ul>"; indents.pop_back(); ordered.pop_back(); };
    for (const Item& it : items) {
        while (!indents.isEmpty() && it.indent < indents.last()) closeTop();       // dedent: close nested lists
        if (indents.isEmpty() || it.indent > indents.last()) {                      // deeper: open a nested list
            h += it.ordered ? "<ol>" : "<ul>"; indents << it.indent; ordered << it.ordered;
        } else {
            h += "</li>";                                                           // same level: next sibling
        }
        QString prefix = ghost(c, it.marker.toHtmlEscaped() + "&nbsp;");
        if (it.task == 0) prefix += QStringLiteral("\u2610&nbsp;");                 // ☐
        if (it.task == 1) prefix += QStringLiteral("\u2611&nbsp;");                 // ☑
        h += "<li>" + prefix + inlineMd(it.text, c);
    }
    while (!indents.isEmpty()) closeTop();
    return h;
}

QString blocks(const QStringList& L, const Ctx& c);

QString quoteHtml(const QStringList& inner, const Ctx& c) {
    return QString("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                   "<td width=\"4\" bgcolor=\"%1\"></td><td width=\"12\"></td>"
                   "<td style=\"color:%2\">%3%4</td></tr></table><p style=\"margin:0\"></p>")
        .arg(c.t.color("quoteBar"), c.t.color("muted"), ghost(c, "&gt;"), blocks(inner, c));
}

QString tableHtml(const QStringList& head, const QStringList& align, const QVector<QStringList>& rows, const Ctx& c) {
    auto alignAttr = [&](int i) {
        const QString a = i < align.size() ? align[i] : QString();
        const bool l = a.startsWith(':'), r = a.endsWith(':');
        return l && r ? " align=\"center\"" : (r ? " align=\"right\"" : " align=\"left\"");
    };
    QString h = QString("<table cellspacing=\"0\" cellpadding=\"7\" border=\"1\" style=\"border-color:%1; border-collapse:collapse\">")
                    .arg(c.t.color("border"));
    h += "<tr>";
    for (int i = 0; i < head.size(); ++i)
        h += QString("<th bgcolor=\"%1\"%2>%3</th>").arg(c.t.color("tableHeader"), alignAttr(i), inlineMd(head[i], c));
    h += "</tr>";
    for (const QStringList& r : rows) {
        h += "<tr>";
        for (int i = 0; i < head.size(); ++i)
            h += QString("<td%1>%2</td>").arg(alignAttr(i), inlineMd(i < r.size() ? r[i] : QString(), c));
        h += "</tr>";
    }
    return h + "</table><p style=\"margin:0\"></p>";
}


// ---- heading anchors ----------------------------------------------------------
// GitHub-style slug: strip inline markdown, lower-case, drop punctuation, spaces -> '-'.
// Duplicates get -1, -2 ... suffixes (same order GitHub uses), so [x](#intro-1) matches the second "Intro".
QString headingSlug(const QString& heading, QHash<QString, int>& used) {
    static const QRegularExpression rxImgS(R"(!\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression rxLinkS(R"(\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression rxMark(R"(\*+|~~|`)");
    static const QRegularExpression rxTag(R"(<[^>]*>)");
    QString t = heading;
    t.replace(rxImgS, "\\1").replace(rxLinkS, "\\1").replace(rxTag, "").replace(rxMark, "");
    QString base;
    for (const QChar ch : t.trimmed().toLower()) {
        if (ch.isLetterOrNumber() || ch == QChar('-') || ch == QChar('_')) base += ch;
        else if (ch.isSpace()) base += QChar('-');
    }
    if (base.isEmpty()) base = "section";
    const int n = used.value(base, 0);
    used[base] = n + 1;
    return n == 0 ? base : base + "-" + QString::number(n);
}

// ---- block level --------------------------------------------------------------
QString blocks(const QStringList& L, const Ctx& c) {
    QString h; int i = 0; const int n = L.size();
    while (i < n) {
        const QString& ln = L[i];
        if (ln.trimmed().isEmpty()) { ++i; continue; }

        // fenced code block
        auto mf = rxFence.match(ln);
        if (mf.hasMatch()) {
            const QString fence = mf.captured(1), lang = mf.captured(2);
            QStringList code; ++i;
            while (i < n && !L[i].trimmed().startsWith(fence)) code << L[i++];
            ++i;                                                   // skip closing fence
            h += codeBlock(code.join('\n'), lang, c);
            continue;
        }
        // heading
        auto mh = rxHead.match(ln);
        if (mh.hasMatch()) {
            const int lvl = mh.captured(1).size();
            const QString slug = headingSlug(mh.captured(2), c.slugs);            // target for [text](#slug) links
            h += QString("<h%1><a name=\"%2\"></a>%3%4</h%1>").arg(lvl).arg(slug, ghost(c, QString(lvl, QChar('#')) + "&nbsp;"), inlineMd(mh.captured(2), c));
            if (c.t.headingRules && lvl <= 2) h += "<hr>";
            ++i; continue;
        }
        // horizontal rule
        if (rxHr.match(ln).hasMatch()) { h += "<hr>"; ++i; continue; }
        // block quote (contents are parsed recursively, so quotes can hold lists, code, quotes...)
        if (rxQuote.match(ln).hasMatch()) {
            QStringList inner;
            while (i < n && rxQuote.match(L[i]).hasMatch()) inner << rxQuote.match(L[i++]).captured(1);
            h += quoteHtml(inner, c);
            continue;
        }
        // list
        if (rxList.match(ln).hasMatch()) {
            QVector<Item> items;
            while (i < n) {
                auto m = rxList.match(L[i]);
                if (m.hasMatch() && !rxHr.match(L[i]).hasMatch()) {
                    Item it; it.indent = indentWidth(m.captured(1)); it.marker = m.captured(2);
                    it.ordered = it.marker[0].isDigit(); it.text = m.captured(3);
                    static const QRegularExpression rxTask(R"(^\[([ xX])\]\s+(.*)$)");
                    auto mt = rxTask.match(it.text);
                    if (mt.hasMatch()) { it.task = mt.captured(1) == " " ? 0 : 1; it.text = mt.captured(2); }
                    items << it; ++i;
                } else if (!L[i].trimmed().isEmpty() && L[i].startsWith(' ') && !items.isEmpty()) {
                    items.last().text += " " + L[i].trimmed(); ++i;               // wrapped continuation line
                } else if (L[i].trimmed().isEmpty() && i + 1 < n && rxList.match(L[i + 1]).hasMatch()) {
                    ++i;                                                          // blank line inside a loose list
                } else break;
            }
            h += listHtml(items, c) + "<p style=\"margin:0\"></p>";
            continue;
        }
        // table: a row containing '|' followed by a |---|---| separator row
        if (ln.contains('|') && i + 1 < n && rxSep.match(L[i + 1]).hasMatch() && L[i + 1].contains('-')) {
            const QStringList head = splitRow(ln), align = splitRow(L[i + 1]);
            QVector<QStringList> rows; i += 2;
            while (i < n && !L[i].trimmed().isEmpty() && L[i].contains('|')) rows << splitRow(L[i++]);
            h += tableHtml(head, align, rows, c);
            continue;
        }
        // paragraph: gather lines until blank line / another block starts
        // (hard line breaks - two trailing spaces - are carried through inlineMd as a private token)
        static const QString BR = QString(QChar(1)) + "BR" + QString(QChar(2));
        QString para;
        while (i < n && !L[i].trimmed().isEmpty() && (para.isEmpty() || !isBlockStart(L[i]))) {
            const bool hardBreak = L[i].endsWith("  ");
            para += L[i].trimmed() + (hardBreak ? BR : QString(" "));
            ++i;
        }
        QString body = inlineMd(para.trimmed(), c);
        body.replace(BR, "<br>");
        h += "<p>" + body + "</p>";
    }
    return h;
}

} // namespace

QString renderMarkdownToHtml(const QString& markdown, const Theme& theme, bool revealMarks, const QString& baseDir) {
    QString text = markdown;
    text.replace("\r\n", "\n");
    QHash<QString, int> slugs;                                  // heading slugs used so far (for duplicates)
    Ctx c{theme, revealMarks, baseDir, slugs};
    return "<html><body>" + blocks(text.split('\n'), c) + "</body></html>";
}

QString previewCss(const Theme& t) {
    const double s = t.previewSize;
    return QString(
        "body { color:%1; font-family:%2; font-size:%3pt; }"
        "a { color:%4; text-decoration:none; }"
        "h1,h2,h3,h4,h5,h6 { color:%5; font-weight:600; margin-top:18px; margin-bottom:8px; }"
        "h1 { font-size:%6pt; } h2 { font-size:%7pt; } h3 { font-size:%8pt; }"
        "h4 { font-size:%3pt; } h5 { font-size:%3pt; } h6 { font-size:%3pt; color:%9; }"
        "p { margin-top:0px; margin-bottom:10px; } li { margin-bottom:3px; } th { font-weight:600; }")
        .arg(t.color("fg"), ThemeStore::cssFamily(t.previewFont, '"'))
        .arg(s).arg(t.color("link"), t.color("heading"))
        .arg(s * 1.9).arg(s * 1.5).arg(s * 1.25).arg(t.color("muted"));
}
