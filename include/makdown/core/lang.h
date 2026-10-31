// lang.h - interface languages: one .json file per language in <home>/lang/.
//
//  Every user-visible string in MakDown is looked up by key:  L("menu.file")  ->  "&File" / "&Файл" / "&Plik".
//  Lookup order: the active language file -> built-in English -> the key itself, so a half-finished
//  translation never shows holes. Stock languages (en, ru, pl) are written to disk on first launch;
//  drop your own .json file next to them to add a language. Format: Documentation/languages.md
//
//  Plural strings are JSON arrays; the file's "plural" rule picks the form and "%n" becomes the number:
//      "status.words": ["%n word", "%n words"]        Ln("status.words", 3)  ->  "3 words"
#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace Lang {
    struct Info {
        QString code;            // file name without ".json": "en", "ru", "pt-BR" ...
        QString name;            // the language's own name, e.g. "Русский"
        int     revision = 0;    // stock file revision (see lang/*.json)
        bool    builtin = false; // stock files are refreshed when MakDown ships a newer revision
    };

    QString langDir();                         // <home>/lang
    void ensureStock();                        // writes missing / outdated stock language files
    QList<Info> available();                   // every readable *.json in langDir() (English is always listed)
    QString resolve(const QString& setting);   // "auto" (= the OS language) or a code -> a code that exists, else "en"
    bool load(const QString& code);            // makes `code` the active language; false = fell back to English
    QString current();                         // the active language code

    QString text(const QString& key);                  // the string for `key` (see lookup order above)
    QString plural(const QString& key, int n);         // plural form for n, with "%n" replaced

    // Which plural form (0-based) a rule picks for n. Rules: "one_other" (en, de ...), "east_slavic" (ru, uk, be),
    // "polish", "single" (ja, zh ...). Unknown rules behave like "one_other".
    int pluralIndex(const QString& rule, int n);
    // Parses a language file (JSONC allowed). Returns false and fills `error` when it is unusable.
    bool parseInfo(const QByteArray& json, Info* info, QString* error = nullptr);
    // The key / value pairs of a language file, plural entries joined with '\n' (used by tests).
    QStringList keys(const QByteArray& json);
}

// Short forms used all over the UI code.
inline QString L(const char* key) { return Lang::text(QString::fromLatin1(key)); }
inline QString Ln(const char* key, int n) { return Lang::plural(QString::fromLatin1(key), n); }
