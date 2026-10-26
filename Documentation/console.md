# Console

`View > Console` (Ctrl+`) opens a panel under the editor and preview. It starts your **default shell** (`$SHELL`, `/bin/sh` if unset)
as a **login + interactive** shell in the explorer's folder, so `/etc/profile`, `~/.profile` / `~/.bash_profile`, `~/.bashrc`, `~/.zshrc` ...
are sourced and your prompt, aliases and PATH look like in any terminal. On Windows it runs `%COMSPEC%` (cmd.exe).

* Type in the line at the bottom, Enter sends it. Up / Down = history, Tab = shell completion (Linux/macOS), Ctrl+C interrupts,
  Ctrl+D sends end-of-input, Ctrl+L clears.
* Linux / macOS: the shell runs inside a pty (through the system `script` tool), so job control, colours and Ctrl+C behave. Without `script`
  it falls back to plain pipes (no prompt colours, no job control).
* 16-colour ANSI output is shown in colour; `\r`, backspace and erase-line are handled. This is a **line console, not a terminal emulator**:
  full-screen programs (vim, top, less, htop) will not work.
* The shell starts the first time the panel is shown. "Restart" starts a fresh one in the current explorer folder, "Clear" clears the output.
* Visibility is remembered (`console` in `settings.jsonc`). `MAKDOWN=1` is exported to the shell so your rc files can detect it.
