# AI assistant

MakDown has an optional AI assistant: a chat page in the side bar (`Ctrl+Shift+A`, or the sparkle icon on the rail) that can write and edit your files, read your project, run terminal commands, search the web and ask you questions.

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
| **Web access** | On by default. Lets the model use `web_search` and `web_fetch` when it needs current or unknown information. Off: both tools fail and the model is told it has no web access. See [Web access](#web-access). |
| **Ask before the AI searches the web or opens a page** | On by default. One dialog per search or page; *Allow for this conversation* covers both. |
| **Stream replies** | On by default. The thinking card and the answer grow while the model writes them (server-sent events, Anthropic and OpenAI formats). Turn it off for a server that cannot stream; a server that answers in one piece despite the request is handled automatically. |
| **Show the model's thinking** | Shows the reasoning some models return as a collapsed *Thinking* card above the answer (click to open). Reasoning is for you only and is never sent back to the model. On by default. |
| **Extended thinking budget** | Anthropic only: off, or at least 1024 tokens the model may think with before answering. Other providers think by themselves (reasoning models) or not at all. |
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

The chat shows your messages as bubbles, the assistant's answers as rendered Markdown, and everything the assistant *does* as cards: a collapsed **Thinking** card (with a word count) when the model reasons, and one card per tool call with a green / amber / red dot for ok / denied / error. Click a card to open its details (the file written, the command's output). While a request is waiting for its first words an animated *Thinking...* row is shown; then the reasoning appears in an open Thinking card that grows live and folds away when the answer starts (it stays open if you opened it yourself), and the answer is typed out below it. Tool calls are never shown as raw text: while the model writes one the row reads *Preparing an action: create_file notes/a.md (3.2 KiB)* and grows with it, and the finished call appears as a card. *Stop* keeps what has arrived but does not add the partial answer to the conversation. The provider and model line under the title opens the AI settings. An empty chat offers a few starter prompts.

Thinking is picked up from Anthropic `thinking` blocks, the `reasoning_content` / `reasoning` fields (DeepSeek, OpenCode, OpenRouter, ...) and inline `<think>...</think>` text (Qwen and similar).


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
| `web_search` | `query` | Searches the web (DuckDuckGo, no key needed) and returns up to 8 results: title, address, snippet. Asks first. |
| `web_fetch` | `url`, optional `start` | Downloads one http(s) page and returns it as plain text (headings and lists kept, each link followed by its address in parentheses). Long pages come back in parts of 20000 characters; `start` is the character to continue from. Asks first. |

## Web access

The model searches when it decides it needs to (recent events, current versions, documentation, a fact it is unsure about) and reads the best results with `web_fetch`; its answer should name the pages it used. You see every search and every page as a card, and by default you approve each one first.

- **What leaves your computer:** the search words go to DuckDuckGo; `web_fetch` contacts the site you see in the dialog (and sites it redirects to). What the model reads comes back into the conversation, so it goes to your AI provider like everything else. The model is told never to put file contents or secrets in a query, but you see the query before it is sent while the confirmation is on.
- **Only the public internet:** `web_fetch` refuses `localhost`, `.local` / `.internal` names, private and link-local addresses (`10.x`, `192.168.x`, `169.254.x`, IPv6 equivalents) and names that resolve to them, on every redirect hop. It accepts `http://` and `https://` only, no `user:password@` addresses, and nothing larger than 2 MB. Use `read_file` for files.
- **What it can read:** HTML pages, plain text, Markdown, JSON and XML. Images, PDFs and other binaries are refused. Pages that build their text with JavaScript or need a login come back empty; the model is told so.
- **Search results and pages are data.** A page that says "ignore your instructions" is not obeyed, but see the warning under Safety.
- **Stop** cancels a running search or download. Every request has a 20 s transfer and 30 s total limit.
- **Switching it off:** untick *Web access* (`"web": false` in `ai.jsonc`). *No AI* switches it off together with everything else.
- DuckDuckGo may answer a burst of searches with a human check; the model is told and falls back to `web_fetch` or its own knowledge. A self-hosted engine with the same result page layout can be used by setting `MAKDOWN_WEB_SEARCH_URL` (the query is added as `?q=`); `MAKDOWN_WEB_ALLOW_LOCAL=1` lifts the local-address rule (both exist for the test-suite and self-hosting).

## When the model misbehaves

- **Made-up results:** a model that does not stop after its tool call and invents the `<tool_result>` (and further calls) is cut off at that point: the invented part is not shown, not stored and never run.
- **Output limit:** a very long file can hit the model's output limit in the middle of a tool call. The call is then not run, and the model is told why and asked to write smaller pieces. For big documents ask for them in several files or steps.

## Safety

- File tools only touch the **working folder** (the explorer root). `..`, absolute paths elsewhere and symbolic links leading out are refused; `.git` is off limits.
- `run_command` is *not* sandboxed: it runs with your permissions. It asks every time unless you pick *Allow for this conversation* or untick the setting. Read the command before you allow it.
- File contents and command output are passed to the model as data. A file that says "ignore your instructions" should not be obeyed, but no model is immune: do not point the assistant at untrusted folders with commands auto-approved.
- The text you send, the open document if enabled, and any file, command output or web page the model asks for go to the provider you chose.
- Web pages are written by strangers. A page can try to steer the model; with commands auto-approved that is the dangerous combination, so keep *Ask before the AI runs terminal commands* on when you let it read the web.
- API keys are stored in plain text (file mode `0600`) in `ai.jsonc`; do not commit or share that file.

## Limits

Conversations are not saved between runs. Tool calls run after the model has finished its message, not while it is still writing it. Tool calls are plain text, so a model that ignores the format simply answers in prose.
