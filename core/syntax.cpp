// syntax.cpp - loading / matching of syntax packs (see include/makdown/core/syntax.h).
#include <makdown/core/syntax.h>
#include <makdown/core/buildinfo.h>
#include <makdown/core/theme.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

// defined in core/syntaxstock.cpp
extern const char* const kStockSyntaxFiles[];     // file names, null-terminated list
extern const char* const kStockSyntaxJson[];      // matching JSON texts

namespace {
QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QRegularExpression compile(const QString& pattern, bool caseSensitive, const QString& what, QString* err) {
    QRegularExpression::PatternOptions o = QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive) o |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression rx(pattern, o);
    if (!rx.isValid() && err) *err += what + ": " + rx.errorString() + "\n";
    return rx;
}

QStringList strList(const QJsonValue& v) {
    QStringList l;
    for (const QJsonValue& x : v.toArray()) if (x.isString() && !x.toString().isEmpty()) l << x.toString();
    return l;
}
} // namespace

SyntaxPtr SyntaxStore::parse(const QByteArray& json, QString* error) {
    QString errs;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(ThemeStore::stripJsonComments(QString::fromUtf8(json)).toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "invalid JSON: " + pe.errorString();
        return nullptr;
    }
    const QJsonObject o = doc.object();
    auto p = std::make_shared<SyntaxPack>();
    p->name = o["name"].toString();
    p->revision = o["revision"].toInt(0);
    p->builtin = o["builtin"].toBool(false);
    p->extensions = strList(o["extensions"]);
    p->filenames = strList(o["filenames"]);
    for (QString& e : p->extensions) { if (e.startsWith('.')) e.remove(0, 1); e = e.toLower(); }
    for (QString& f : p->filenames) f = f.toLower();
    const QJsonObject cols = o["colors"].toObject();
    for (auto it = cols.begin(); it != cols.end(); ++it) p->colors[it.key()] = it.value().toString();
    const bool cs = o["caseSensitive"].toBool(true);

    // regions (block comments, strings ...). They win ties against everything else.
    for (const QJsonValue& v : o["regions"].toArray()) {
        const QJsonObject r = v.toObject();
        SyntaxPack::Region reg;
        reg.scope = r["scope"].toString();
        reg.begin = compile(r["begin"].toString(), cs, "region begin", &errs);
        const QString end = r["end"].toString();
        reg.end = compile(end, cs, "region end", &errs);
        reg.escape = r["escape"].toString();
        reg.multiline = r["multiline"].toBool(true) && !end.isEmpty();
        if (r["begin"].toString().isEmpty() || !reg.begin.isValid() || !reg.end.isValid()) continue;
        p->regions << reg;
    }

    // "words": { scope: [w1, w2 ...] } -> one whole-word rule per scope (longest words first)
    const QJsonObject words = o["words"].toObject();
    for (auto it = words.begin(); it != words.end(); ++it) {
        QStringList w = strList(it.value());
        if (w.isEmpty()) continue;
        std::sort(w.begin(), w.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        for (QString& x : w) x = QRegularExpression::escape(x);
        SyntaxPack::Rule r;
        r.scope = it.key();
        r.rx = compile("(?<!\\w)(?:" + w.join('|') + ")(?!\\w)", cs, "words " + it.key(), &errs);
        if (r.rx.isValid()) p->rules << r;
    }
    for (const QJsonValue& v : o["rules"].toArray()) {
        const QJsonObject r = v.toObject();
        SyntaxPack::Rule rule;
        rule.scope = r["scope"].toString();
        rule.rx = compile(r["pattern"].toString(), cs, "rule " + r["scope"].toString(), &errs);
        if (rule.rx.isValid() && !r["pattern"].toString().isEmpty()) p->rules << rule;
    }
    if (p->extensions.isEmpty() && p->filenames.isEmpty()) errs += "no \"extensions\" or \"filenames\"\n";
    if (error) *error = errs;
    return p;
}

QString SyntaxStore::syntaxDir() { return ThemeStore::appHome() + "/syntax"; }

void SyntaxStore::ensureStock() {
    QDir().mkpath(syntaxDir());
    for (int i = 0; kStockSyntaxFiles[i]; ++i) {
        const QString path = syntaxDir() + "/" + kStockSyntaxFiles[i];
        const QByteArray stock = kStockSyntaxJson[i];
        bool write = !QFile::exists(path);
        if (!write) {   // refresh outdated stock packs - but never touch files the user took over ("builtin": false)
            const SyntaxPtr old = parse(readText(path).toUtf8());
            const SyntaxPtr now = parse(stock);
            write = old && now && old->builtin && old->revision < now->revision;
        }
        if (!write) continue;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(stock);
    }
}

QList<SyntaxPtr> SyntaxStore::loadAll() {
    QList<SyntaxPtr> out;
    const QFileInfoList files = QDir(syntaxDir()).entryInfoList({"*.json", "*.jsonc"}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        QString err;
        SyntaxPtr p = parse(readText(fi.filePath()).toUtf8(), &err);
        if (!p) { mdLog("syntax pack " + fi.fileName() + " skipped: " + err); continue; }
        if (!err.isEmpty()) mdLog("syntax pack " + fi.fileName() + ":\n" + err.trimmed());
        if (p->extensions.isEmpty() && p->filenames.isEmpty()) continue;
        out << p;
    }
    return out;
}

SyntaxPtr SyntaxStore::forFile(const QList<SyntaxPtr>& packs, const QString& path) {
    if (path.isEmpty()) return nullptr;
    const QFileInfo fi(path);
    const QString name = fi.fileName().toLower(), ext = fi.suffix().toLower();
    for (const SyntaxPtr& p : packs) if (p->filenames.contains(name)) return p;     // exact names win ...
    if (!ext.isEmpty())
        for (const SyntaxPtr& p : packs) if (p->extensions.contains(ext)) return p; // ... then extensions
    return nullptr;
}
