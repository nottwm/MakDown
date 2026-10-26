# Git tab

**Git > Clone repository...** (Ctrl+Shift+G) opens the **Git** tab of the side bar (the branch icon on the left rail).

* **Repository URL**: `https://...`, `http://...`, `ssh://...` or the scp form `git@host:user/repo.git`. Anything else is refused.
* **Clone into**: the full path of the new folder. It is suggested as `<explorer folder>/<repo name>` until you type or browse for your own;
  it must not exist yet or be an empty folder.
* **Clone** runs `git clone --progress` with the system git (it must be in `PATH`). Progress bar + live log; **Cancel** stops it.
* **Open in explorer** (after success) makes the new folder the explorer root.

Notes
* No terminal prompts: a private https repo needs a credential helper or a token in the URL (it is masked in the log), and ssh needs a key
  the agent can use without asking. ssh runs with `BatchMode=yes` and `StrictHostKeyChecking=accept-new` (unknown hosts are trusted on first use);
  set `GIT_SSH_COMMAND` yourself to change that.
