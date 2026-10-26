// syntax.h - syntax-highlighting "packs": one .json file per language in <home>/syntax/.
//
//  A pack tells the generic highlighter (ui/synhl.cpp) how to colour a language. Stock packs
//  (c, cpp, makefile, ld, rust, bash, batch) are written to disk on first launch; drop your own
//  .json files next to them to add languages. Format: Documentation/syntax.md
#pragma once
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <memory>

struct SyntaxPack {
    struct Rule   { QString scope; QRegularExpression rx; };                         // single-line match
    struct Region { QString scope; QRegularExpression begin, end; QString escape;    // can span lines
                    bool multiline = true; };

    QString name;
    int     revision = 0;                 // stock pack revision (see core/syntaxstock.cpp)
    bool    builtin = false;              // stock packs are refreshed when MakDown ships a newer revision
    QStringList extensions;               // "c", "h" (no dot, case-insensitive)
    QStringList filenames;                // exact file names, e.g. "Makefile" (case-insensitive)
    QMap<QString, QString> colors;        // optional per-scope colour overrides, "#rrggbb"
    QList<Region> regions;                // block comments, strings ...
    QList<Rule>   rules;                  // words (compiled from "words") followed by the "rules" list
};
using SyntaxPtr = std::shared_ptr<const SyntaxPack>;

namespace SyntaxStore {
    QString syntaxDir();                                         // <home>/syntax
    void ensureStock();                                          // writes missing / outdated stock packs
    QList<SyntaxPtr> loadAll();                                  // every readable *.json in syntaxDir()
    SyntaxPtr forFile(const QList<SyntaxPtr>& packs, const QString& path);   // null when no pack matches
    SyntaxPtr parse(const QByteArray& json, QString* error = nullptr);       // used by loadAll(); handy for tests
}
