# x3ro-deck

Qt 6 / KF6 desktop app (C++20, Widgets + QML panels) for running coding agents and Docker Compose stacks across several checkouts. It drives Claude Code and Codex, and local models (for example A model on a local server) through either CLI. It targets Linux with KDE Plasma only, so portability is a non-goal.

# Commands

- `cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local`: configure. Plain CMake, without extra-cmake-modules.
- `cmake --build build`: build both targets. The build must stay warning-free.
- `cmake --install build`: install the binaries to `~/.local/bin`, plus the desktop file, notifyrc and hicolor icons.
- `kbuildsycoca6`: refresh KDE's caches after changing the desktop file or icons.

There is no test suite. Verify changes with a headless screenshot (see Workflow).

# Architecture

- `src/main.cpp`: app setup and `KDBusService` Unique (a second launch raises the first window). It relaunches the binary after `MainWindow::restart()`. Dev flags: `--tab <docker|agents>`, `--select N`, `--settings [checkouts|agents|docker|general]`, `--screenshot <file> [delayMs]`. The screenshot captures an open dialog instead of the window.
- `src/AppController.{h,cpp}`: the only state owner. The context property `app` exposes it to QML through Q_PROPERTYs, `VariantListModel`s and Q_INVOKABLEs.
  - Each checkout runs one agent at a time in one tmux session. `AgentInfo::profile` names its `AgentProfile`; the pick persists in QSettings under `agents/<checkout>`.
  - A session that survives a restart keeps that pick. `ensureAgentSession` switches a freshly created session to the checkout's configured agent, and a renamed checkout takes over its old session, matched by path.
  - `reloadConfig` applies saved settings while running. A changed checkout list (names, paths or order) takes a restart.
  - `onHookEvent` maps hook payloads to the agent state machine: `offline | ready | working | permission | question | done`.
  - `publishAttention` builds the NEEDS YOU items. Priority order: permission, question, docker, done.
  - Each hook payload's `cwd` is matched to a checkout by longest canonical path prefix.
- `src/Config.{h,cpp}`: `~/.config/x3ro-deck/config.json`, written with defaults on first run. User keys override defaults at the top level.
  - `agents` is an ordered list of `AgentProfile`s. `kind` (`claude`, `codex` or `custom`) selects `kindDefaults`: command, resume args, process name, permission keys, prompt buttons and submit delay. Profile keys override them.
  - `custom` runs any CLI. It sends no hooks, so only the pane poll tracks it, and its `process` defaults to the command's first word.
  - `env` makes a profile run its CLI against another backend. `launchLine` types it as `KEY="value"` prefixes, so `$VAR` expands in the session shell.
  - A checkout's `agent` key names the profile a fresh session starts; `defaultAgent` covers the rest.
- `src/SettingsDialog.cpp`: the Settings dialog, a Widgets form over `config.json`. It edits the JSON itself, so keys without a field survive a save, and it leaves profile keys at their kind's defaults out of the file. `data/style.qss` styles it under `SettingsDialog` and `QMessageBox` selectors.
- `src/MainWindow.cpp`: builds the widget layout with `QSplitter`s and one `QQuickWidget` per QML panel; all panels share one `QQmlEngine`.
  - It also owns the terminals, shortcuts, the tray icon and `KNotification`s.
  - Ctrl+Q and the tray's Quit go through `confirmQuit`, which offers to stop the agents by killing the tmux server.
  - Layout state persists in `~/.config/x3ro-deck/x3ro-deck.conf`.
- `src/TerminalHost.cpp`: embeds Konsole via `konsolepart` (`KParts::PartLoader`), so terminals use the user's Konsole profile.
- `src/TmuxManager.cpp`: agents run in tmux on socket `-L x3ro-deck`, with config `~/.config/x3ro-deck/tmux.conf`. `ensureSession` only creates sessions that are missing, so agents survive app restarts. `submit` types text and presses Enter in one tmux call, or after a delay.
- `src/AgentBus.cpp`: `QLocalServer` on `$XDG_RUNTIME_DIR/x3ro-deck.sock`. It reads one JSON hook payload per connection.
- `hook/x3ro-deck-hook.cpp`: plain C++ with no Qt. Claude Code runs it from `~/.claude/settings.json` and Codex from `~/.codex/hooks.json`; both send the same Claude-style JSON. It must always exit 0, print nothing and finish in milliseconds.
- `src/DockerClient.cpp`: Docker Engine API over `/var/run/docker.sock` (HTTP/1.0 over `QLocalSocket`). It subscribes to `/events` and filters containers by the `com.docker.compose.project` label.
- `src/GitWatcher.cpp`: watches `HEAD` and `logs/HEAD` with `QFileSystemWatcher`.
- `src/PrWatcher.cpp`: polls `gh pr view <branch> --json …` per checkout.
- `qml/`: `Theme.qml` holds every color and font. Use its tokens and helpers (`agentColor`, `healthColor`, `elapsed`) and do not hardcode colors.
- New QML files and data files must be added to `resources.qrc`. New C++ sources must be added to `CMakeLists.txt`.

# Code style

- Match the existing Qt idiom: `QStringLiteral`/`QLatin1String`, `Q_EMIT`/`Q_SIGNALS`, `std::as_const` in range-for loops, and anonymous namespaces for file-local helpers.
- Keep comments sparse. Explain why, not what.
- Name QML model roles carefully: `id` is reserved in QML delegates. The attention model therefore exposes the id as `itemId`.
- QML `font.pixelSize` takes an int. A fractional value fails with "Invalid property assignment" and blanks the whole panel.

# Workflow

- QML is compiled into the binary through `resources.qrc`, so rebuild before every screenshot.
- Run a headless screenshot after every UI change and inspect the PNG. Add `--settings <page>` to capture the Settings dialog.
  ```
  QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1 \
    dbus-run-session -- ./build/x3ro-deck --tab agents --select 0 --screenshot out.png 9000
  ```
- Simulate agents by piping hook JSON into `./build/x3ro-deck-hook` with the test instance's `XDG_RUNTIME_DIR`.
- IMPORTANT: the user usually has the app running. When testing alongside it:
  - Use `dbus-run-session`. Without it, the test only activates the live window and exits.
  - Point `XDG_RUNTIME_DIR` at a private 0700 directory with a short path, such as `/run/user/1000/xdt`. `AgentBus` removes an existing socket and would take the live app's hooks, and a socket path over 107 bytes fails to bind.
  - Point `TMUX_TMPDIR` at the same directory. The test instance then runs its own `-L x3ro-deck` tmux server, which is safe to kill.
  - Point `XDG_CONFIG_HOME` at a temporary directory containing `{"autostartAgents": false}` in `x3ro-deck/config.json`, and set `GH_CONFIG_DIR=$HOME/.config/gh` so `gh` stays authenticated.
  - Without `TMUX_TMPDIR`, the test instance attaches to the live `-L x3ro-deck` tmux server. NEVER run `tmux kill-server` or `kill-session` against that server, or choose "Stop agents and quit" in a test instance attached to it, because its sessions hold the user's live agents.
- QML errors go to journald unless `QT_FORCE_STDERR_LOGGING=1` is set.
- `cmake --install` succeeds while the app runs. The user restarts the app (Ctrl+Q, then relaunch) to pick up the new binary, and agents keep running in tmux.
- Edit `~/.claude/settings.json` and `~/.codex/hooks.json` only by merging. Other hooks (for example `agent-notify`) live alongside `x3ro-deck-hook`.

# Quirks

- Permission buttons send keys to the agent's tmux pane. Claude Code uses `1` for Allow, `2` for Always and `Escape` for Deny; Codex uses `y`, `a` and `Escape`. A profile's `permissionKeys` overrides them.
- Claude Code offers Always only when the payload has `permission_suggestions`; Codex offers it on every prompt.
- `AskUserQuestion`, `request_user_input` and `ExitPlanMode` become questions, not permissions, because their prompts are not allow/deny choices.
- Codex differs from Claude Code in four ways:
  - It sends no `Notification` events.
  - It sends `Interrupt` when a turn is cancelled.
  - It reports file edits as `apply_patch`, whose patch text names the files.
  - It runs a user hook only after the user trusts it with `/hooks`. The session panel warns when a freshly started agent sends no hook events for 15 s.
- Codex treats Enter within 120 ms of fast input as a newline, so its profiles wait `submitDelayMs` (300) before pressing Enter.
- A profile that points Claude Code at another server must set `ANTHROPIC_AUTH_TOKEN`; otherwise Claude Code sends the claude.ai login to that server.
- The pane poll (`onPaneCommands`) changes agent state only on transitions of an agent process inside our own tmux session. All other state comes from hooks. When the pane runs another profile's `process`, the checkout switches to that profile.
- Exited containers with exit code 0 count as `completed`. They are excluded from up/total counts and never raise NEEDS YOU items.
- `PrWatcher` skips `main`, `master` and `develop`. A closed, unmerged PR turns "View PR" back into the profile's `prPrompt`.
- `KStatusNotifierItem` and `KGlobalAccel` log errors under `dbus-run-session`. These are expected in headless tests.
- The icon source is `data/icons/x3ro-deck.svg`. After editing it, regenerate the PNGs with `rsvg-convert -w N -h N` and the ICO with `magick x3ro-deck-{16,24,32,48,64,128,256}.png x3ro-deck.ico`.
