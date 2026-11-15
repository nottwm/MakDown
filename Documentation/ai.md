# AI assistant

MakDown has an optional AI assistant: a chat page in the side bar (`Ctrl+Shift+A`, or the sparkle icon on the rail) that can write and edit your files, read your project, run terminal commands and ask you questions.

Nothing is sent anywhere until you press **Send**, and everything can be switched off.

## Turning it off: No AI

**Settings > Preferences > AI > No AI.** With it ticked the AI tab, the AI menu and the assistant disappear (and their shortcuts stop working) and MakDown never contacts an AI provider. Untick it to bring everything back. The choice is stored as `"ai": false` in `settings.jsonc`.

## Settings > Preferences > AI

| Setting | What it does |
|---|---|
| **Provider** | Anthropic, OpenAI, OpenRouter, DeepSeek, Hugging Face, OpenCode Zen, OpenCode Go, or Custom (OpenAI compatible). Every provider keeps its own key, model and address. |
| **Base URL** | Empty = the provider's default (table below). For *Custom* enter any OpenAI-compatible base URL, e.g. `http://localhost:11434/v1` (Ollama), LM Studio, vLLM, a company gateway. A full `.../chat/completions` URL also works. |
| **API key** | Saved in `ai.jsonc` in MakDown's data folder, mode `0600`. If empty, the provider's environment variable is used. A custom provider may have no key. |
| **Model** | Pick a suggestion or type any model id your provider offers. Defaults are only suggestions; model names change, so check your provider's list. |
| **Custom prompt** | Text added on top of MakDown's own prompt in every conversation: language, tone, house style, project rules. It is placed after MakDown's prompt and cannot switch off the confirmation dialogs. *Show MakDown's built-in prompt...* displays the part you build on. |
| **Ask before files / commands** | Confirmation dialogs for creating or overwriting files and for running commands (both on by default). |
| **Most steps per message** | Safety limit for the tool loop (default 25). |

| Provider | Base URL | Key variable |
|---|---|---|
| Anthropic | `https://api.anthropic.com/v1` (Messages API) | `ANTHROPIC_API_KEY` |
| OpenAI | `https://api.openai.com/v1` | `OPENAI_API_KEY` |
| OpenRouter | `https://openrouter.ai/api/v1` | `OPENROUTER_API_KEY` |
| DeepSeek | `https://api.deepseek.com/v1` | `DEEPSEEK_API_KEY` |
| Hugging Face | `https://router.huggingface.co/v1` | `HF_TOKEN` |
| OpenCode Zen | `https://opencode.ai/zen/v1` | `OPENCODE_API_KEY` |
| OpenCode Go | `https://opencode.ai/zen/go/v1` | `OPENCODE_API_KEY` |
| Custom | yours | `MAKDOWN_AI_KEY` |

Everything except Anthropic uses the OpenAI `chat/completions` format. OpenCode Zen serves some models (GPT, Claude, Gemini) on other endpoints; only its chat-completions models work here.

## Using the assistant

Type in the box (`Enter` sends, `Shift+Enter` adds a line). **Send the open document** attaches the text of the editor and your selection to each message. `Stop` cancels a running request or command. *New* starts a fresh conversation.

## Tools

The tools are described in the system prompt and implemented in the app (`ui/aipanel.cpp`), so they behave the same with every provider. The model writes a text block and MakDown answers with a `<tool_result>`:

```
<makdown_tool name="create_file">
<path>notes/todo.md</path>
<content>
# Todo
</content>
</makdown_tool>
```

| Tool | Parameters | Notes |
|---|---|---|
| `create_file` | `path`, `content`, optional `open` | New file only; parent folders are created. Asks first. |
| `write_file` | `path`, `content` | Overwrites an existing file. If it is the open file, the change goes through the editor (one undo step) and is saved. Asks first. |
| `read_file` | `path` | Uses the editor's unsaved text for the open file. |
| `list_dir` | optional `path` | One level. |
| `open_file` | `path` | Shows the file in the editor. |
| `new_document` | `content` | A new untitled document (like File > New with text). |
| `run_command` | `command` | One shell command in the working folder (`/bin/sh -c`, `cmd.exe /c` on Windows). No stdin, no terminal, stopped after 120 s, output trimmed. Asks first. |
| `ask_user` | `question`, optional `options` (one per line) | Shows a dialog; the answer goes back to the model. Ends the batch. |

## Safety

- File tools only touch the **working folder** (the explorer root). `..`, absolute paths elsewhere and symbolic links leading out are refused; `.git` is off limits.
- `run_command` is *not* sandboxed: it runs with your permissions. It asks every time unless you pick *Allow for this conversation* or untick the setting. Read the command before you allow it.
- File contents and command output are passed to the model as data. A file that says "ignore your instructions" should not be obeyed, but no model is immune: do not point the assistant at untrusted folders with commands auto-approved.
- The text you send, the open document if enabled, and any file or command output the model asks for go to the provider you chose.
- API keys are stored in plain text (file mode `0600`) in `ai.jsonc`; do not commit or share that file.

## Limits

Replies are not streamed (you see the answer when it is complete). Conversations are not saved between runs. Tool calls are plain text, so a model that ignores the format simply answers in prose.
