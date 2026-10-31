// lang.cpp - language files: loading, lookup, plural rules (see include/makdown/core/lang.h).
#include <makdown/core/lang.h>
#include <makdown/core/buildinfo.h>
#include <makdown/core/theme.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QTranslator>
#include <algorithm>
#include <memory>

// defined in core/langstock.cpp (GENERATED from lang/*.json by scripts/gen-lang.py)
extern const char* const kStockLangFiles[];       // file names, null-terminated list ("en.json" first)
extern const char* const kStockLangJson[];        // matching JSON texts

namespace {
struct Table {
    QHash<QString, QStringList> forms;      // key -> one string, or the plural forms
    QString rule = "one_other";
    QString code = "en";
};

QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool parseTable(const QByteArray& json, Table* t, Lang::Info* info, QString* error) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(ThemeStore::stripJsonComments(QString::fromUtf8(json)).toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "invalid JSON: " + pe.errorString();
        return false;
    }
    const QJsonObject o = doc.object();
    if (info) {
        info->name = o["name"].toString();
        info->revision = o["revision"].toInt(0);
        info->builtin = o["builtin"].toBool(false);
    }
    if (t) t->rule = o["plural"].toString("one_other");
    const QJsonObject strings = o["strings"].toObject();
    if (strings.isEmpty()) { if (error) *error = "no \"strings\" object"; return false; }
    if (t) {
        for (auto it = strings.begin(); it != strings.end(); ++it) {
            QStringList forms;
            if (it.value().isArray()) { for (const QJsonValue& v : it.value().toArray()) forms << v.toString(); }
            else forms << it.value().toString();
            if (!forms.isEmpty()) t->forms.insert(it.key(), forms);
        }
    }
    return true;
}

// built-in English: the stock "en.json" embedded in the binary, parsed once. Needs no files on disk and no init,
// so widgets created outside MainWindow (tests, tools) still get proper text.
const Table& english() {
    static const Table t = [] {
        Table e;
        if (kStockLangFiles[0]) parseTable(kStockLangJson[0], &e, nullptr, nullptr);
        return e;
    }();
    return t;
}

Table g_active;                       // empty until load() succeeds; english() fills every gap
bool g_haveActive = false;
std::unique_ptr<QTranslator> g_qtTr;  // Qt's own texts (standard dialog buttons ...) when the Qt translations are installed

QString norm(QString c) { return c.replace('_', '-').toLower(); }

QString pick(const QStringList& forms, const QString& rule, int n) {
    if (forms.isEmpty()) return QString();
    return forms.at(qMin(Lang::pluralIndex(rule, n), int(forms.size()) - 1));
}

void installQtTranslator(const QString& code) {
    if (QCoreApplication* app = QCoreApplication::instance()) {
        if (g_qtTr) { app->removeTranslator(g_qtTr.get()); g_qtTr.reset(); }
        if (code == "en") return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
        const QString dir = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
        auto tr = std::make_unique<QTranslator>();
        if (tr->load("qtbase_" + code, dir) || tr->load("qtbase_" + code.section('-', 0, 0), dir)) {
            app->installTranslator(tr.get());
            g_qtTr = std::move(tr);
        }
    }
}
} // namespace

int Lang::pluralIndex(const QString& rule, int n) {
    n = qAbs(n);
    const int m10 = n % 10, m100 = n % 100;
    if (rule == "single") return 0;
    if (rule == "east_slavic") {                       // 1 файл, 2 файла, 5 файлов
        if (m10 == 1 && m100 != 11) return 0;
        if (m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14)) return 1;
        return 2;
    }
    if (rule == "polish") {                            // 1 plik, 2 pliki, 5 plików
        if (n == 1) return 0;
        if (m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14)) return 1;
        return 2;
    }
    return n == 1 ? 0 : 1;                             // "one_other" and anything unknown
}

bool Lang::parseInfo(const QByteArray& json, Info* info, QString* error) {
    return parseTable(json, nullptr, info, error);
}

QStringList Lang::keys(const QByteArray& json) {
    Table t;
    QStringList out;
    if (!parseTable(json, &t, nullptr, nullptr)) return out;
    for (auto it = t.forms.begin(); it != t.forms.end(); ++it) out << it.key() + "=" + it.value().join('\n');
    out.sort();
    return out;
}

QString Lang::langDir() { return ThemeStore::appHome() + "/lang"; }

void Lang::ensureStock() {
    QDir().mkpath(langDir());
    for (int i = 0; kStockLangFiles[i]; ++i) {
        const QString path = langDir() + "/" + kStockLangFiles[i];
        const QByteArray stock = kStockLangJson[i];
        bool write = !QFile::exists(path);
        if (!write) {   // refresh outdated stock files - but never touch files the user took over ("builtin": false)
            Info old, now;
            const bool a = parseInfo(readText(path).toUtf8(), &old), b = parseInfo(stock, &now);
            write = a && b && old.builtin && old.revision < now.revision;
        }
        if (!write) continue;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(stock);
    }
}

QList<Lang::Info> Lang::available() {
    QList<Info> out;
    const QFileInfoList files = QDir(langDir()).entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        Info info; QString err;
        if (!parseInfo(readText(fi.filePath()).toUtf8(), &info, &err)) { mdLog("language file " + fi.fileName() + " skipped: " + err); continue; }
        info.code = fi.completeBaseName();
        if (info.name.isEmpty()) info.name = info.code;
        out << info;
    }
    const bool haveEn = std::any_of(out.begin(), out.end(), [](const Info& i) { return norm(i.code) == "en"; });
    if (!haveEn) { Info en; en.code = "en"; en.name = "English"; en.builtin = true; out.prepend(en); }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) {          // English first, then by code
        const bool ea = norm(a.code) == "en", eb = norm(b.code) == "en";
        return ea != eb ? ea : a.code.toLower() < b.code.toLower();
    });
    return out;
}

QString Lang::resolve(const QString& setting) {
    const QList<Info> all = available();
    auto find = [&](const QString& code) -> QString {
        for (const Info& i : all) if (norm(i.code) == norm(code)) return i.code;
        return QString();
    };
    if (!setting.isEmpty() && setting != "auto") {
        const QString c = find(setting);
        return c.isEmpty() ? QString("en") : c;
    }
    for (const QString& ui : QLocale::system().uiLanguages()) {       // "pl-PL", "en-US" ... best match first
        QString c = find(ui);
        if (c.isEmpty()) c = find(ui.section(QRegularExpression("[-_]"), 0, 0));
        if (!c.isEmpty()) return c;
    }
    return "en";
}

bool Lang::load(const QString& codeIn) {
    g_active = Table(); g_haveActive = false;
    QString code = codeIn.isEmpty() ? QString("en") : codeIn;
    bool ok = true;
    if (norm(code) != "en" || QFile::exists(langDir() + "/" + code + ".json")) {
        const QString path = langDir() + "/" + code + ".json";
        QString err;
        Table t;
        if (QFile::exists(path) && parseTable(readText(path).toUtf8(), &t, nullptr, &err)) { t.code = code; g_active = t; g_haveActive = true; }
        else { mdLog("language " + code + " not usable" + (err.isEmpty() ? QString() : ": " + err) + " - using English"); code = "en"; ok = norm(codeIn) == "en"; }
    }
    if (!g_haveActive) g_active.code = "en";
    installQtTranslator(norm(g_active.code));
    mdLog("language: " + g_active.code);
    return ok;
}

QString Lang::current() { return g_active.code; }

QString Lang::text(const QString& key) {
    if (g_haveActive) {
        const auto it = g_active.forms.constFind(key);
        if (it != g_active.forms.constEnd()) return it->first();
    }
    const Table& en = english();
    const auto it = en.forms.constFind(key);
    return it != en.forms.constEnd() ? it->first() : key;
}

QString Lang::plural(const QString& key, int n) {
    QString s;
    if (g_haveActive) {
        const auto it = g_active.forms.constFind(key);
        if (it != g_active.forms.constEnd()) s = pick(*it, g_active.rule, n);
    }
    if (s.isEmpty()) {
        const Table& en = english();
        const auto it = en.forms.constFind(key);
        s = it != en.forms.constEnd() ? pick(*it, en.rule, n) : key;
    }
    return s.replace("%n", QString::number(n));
}
