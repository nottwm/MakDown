// ai.h - everything about the AI assistant that needs no widgets: providers, saved configuration, the system
// prompt, request / response handling for the two wire formats, the text-based tool protocol and the sandboxed
// file helpers the tools use. The chat panel (ui/aipanel.h) only drives these.
//
//  Providers (Settings > Preferences > AI): Anthropic, OpenAI, OpenRouter, DeepSeek, Hugging Face, OpenCode Zen,
//  OpenCode Go and "Custom (OpenAI compatible)". Two wire formats exist: Anthropic Messages and OpenAI chat completions.
//
//  Tools are NOT the providers' native function calling (that differs per provider). They are described in the
//  system prompt and written by the model as plain text blocks, which works the same everywhere:
//
//      <makdown_tool name="create_file">
//      <path>notes/todo.md</path>
//      <content>
//      # Todo
//      </content>
//      </makdown_tool>
//
//  MakDown runs the block and answers with a <tool_result> message. Documentation/ai.md lists every tool.
#pragma once
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Ai {

enum class Api { OpenAI, Anthropic };

struct Provider {
    QString id;                 // stable key used in ai.jsonc: "anthropic", "openai", "openrouter", ...
    QString name;               // display name
    Api api = Api::OpenAI;
    QString baseUrl;            // up to, not including, the endpoint: "https://api.openai.com/v1"
    QString defaultModel;
    QStringList models;         // suggestions for the model box (any model id can be typed)
    QString keyEnv;             // environment variable used when no key is stored
    bool custom = false;        // the user must supply the base URL
};

QList<Provider> providers();                    // in menu order
const Provider* findProvider(const QString& id);

struct ProviderSettings {
    QString apiKey;
    QString model;              // empty = the provider's default
    QString baseUrl;            // empty = the provider's default
};

struct Config {
    QString provider = "anthropic";
    QMap<QString, ProviderSettings> per;        // by provider id
    QString customPrompt;                       // added on top of MakDown's own prompt
    bool confirmFiles = true;                   // ask before the AI creates / overwrites files
    bool confirmCommands = true;                // ask before the AI runs a terminal command
    int  maxSteps = 25;                         // model calls per message (a tool loop stops here)

    ProviderSettings& cur() { return per[provider]; }
    QString key(const QString& id) const;       // stored key, else the environment variable, else empty
    QString model(const QString& id) const;     // stored or default
    QString baseUrl(const QString& id) const;   // stored or default
};

Config& config();                               // process-wide, loaded on first use
void loadConfig();                              // re-reads <home>/ai.jsonc (used by tests)
void saveConfig();                              // writes <home>/ai.jsonc (mode 0600: it holds API keys)
QString configPath();

// ---- chat ---------------------------------------------------------------------------------------
struct Message { QString role; QString content; };            // role: "user" | "assistant"

struct Request {
    QString url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    QString error;                                            // non-empty: the request cannot be made (no key, bad URL ...)
};
Request buildRequest(const Config& c, const QString& system, const QList<Message>& history);

struct Reply { bool ok = false; QString text; QString error; };
Reply parseReply(Api api, int httpStatus, const QByteArray& body);

QString endpointUrl(const Provider& p, const QString& baseUrl);

// ---- system prompt --------------------------------------------------------------------------------
struct PromptContext {
    QString workDir;            // the explorer's folder: the only place the tools may touch
    QString openFile;           // path of the file in the editor, or empty
    QString custom;             // the user's custom prompt
    QString os;                 // "Linux", "Windows", ...
    QString shell;              // "/bin/sh" or "cmd.exe"
};
QString basePrompt();                                         // MakDown's own prompt (tool descriptions included)
QString systemPrompt(const PromptContext& ctx);               // base + environment + the custom prompt overlay

// ---- tool protocol ---------------------------------------------------------------------------------
struct ToolCall {
    QString name;
    QMap<QString, QString> args;
    QString error;              // the block was malformed
};
struct Parsed {
    QString text;               // the reply without tool blocks
    QList<ToolCall> calls;
};
Parsed parseToolCalls(const QString& reply);
QStringList toolNames();
QString toolResult(const QString& name, const QString& status, const QString& body);   // the <tool_result> text; status: ok | error | denied

// ---- sandboxed file helpers (all paths are relative to, and confined to, `root`) ------------------
QString resolveInside(const QString& root, const QString& path, QString* error);   // absolute path or empty
QString clip(const QString& text, int max);                                        // keeps head + tail, says what was cut
struct FileOp { bool ok = false; QString message; QString path; };
FileOp createFile(const QString& root, const QString& rel, const QString& content, bool overwrite);
FileOp readTextFile(const QString& root, const QString& rel, int maxChars = 60000);
FileOp listDir(const QString& root, const QString& rel);

} // namespace Ai
