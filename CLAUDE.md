# x3ro-deck

Qt 6 / KF6 desktop app (C++20, Widgets + QML panels) for running coding agents and Docker Compose stacks across several checkouts. It drives Claude Code and Codex, and local models (for example A model on a local server) through either CLI. It targets Linux with KDE Plasma only, so portability is a non-goal.

# Commands

- `cmake --preset linux`: configure into `build/` with Ninja, RelWithDebInfo and `CMAKE_INSTALL_PREFIX=$HOME/.local`. Plain CMake, without extra-cmake-modules. `CMakePresets.json` also has `macos` and `windows` presets; each shows only on its own host.
- `cmake --build --preset linux`: build both targets. The build must stay warning-free on GCC, Clang and MSVC; `x3ro_deck_set_warnings` in `CMakeLists.txt` picks the flags. Add `-DX3RO_DECK_WERROR=ON` to the configure step to turn warnings into errors (CI does).
- `cmake --install build`: install the binaries to `~/.local/bin`, plus the desktop file, notifyrc and hicolor icons. The desktop, notifyrc and icon rules apply only on `UNIX AND NOT APPLE`. The desktop file comes from `data/x3ro-deck.desktop.in` with an absolute `Exec`, because Plasma's session PATH lacks `~/.local/bin`.
- `kbuildsycoca6`: refresh KDE's caches after changing the desktop file or icons.
- `scripts/check-format.sh [--fix]`: check (or apply) `.clang-format` on every tracked C++ file.
- `scripts/smoke-test.sh [binary] [out.png]`: headless screenshot in an isolated environment. It fails without a PNG or on any QML error.
- `ctest --test-dir build`: run the QtTest suites in `tests/`. They link `x3ro-deck-core`, the static library holding everything except `main.cpp`. `tst_sessionflow` drives AppController against a private tmux server.

CI (`.github/workflows/ci.yml`) runs in an `archlinux` container. It checks formatting, builds with `-DX3RO_DECK_WERROR=ON`, runs `ctest` and the smoke test, and uploads the screenshot. `scripts/ci-linux-deps.sh` lists the packages. For UI changes, also inspect a screenshot yourself (see Workflow).

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
  - `env` makes a profile run its CLI against another backend. `AgentProfile::sessionEnv` expands `$VAR`/`${VAR}` against the app's own environment (unset → empty), and the session backend sets the result when it creates the session (`tmux new-session -e`). It is never typed, so it stays out of the pane and shell history.
  - A session's env is fixed at creation, and tmux records it in the session option `@x3ro-deck-env`. Start or Continue with a profile whose env differs kills the idle session and creates a fresh one; the attached terminal reattaches.
  - A checkout's `agent` key names the profile a fresh session starts; `defaultAgent` covers the rest.
- `src/SettingsDialog.cpp`: the Settings dialog, a Widgets form over `config.json`. It edits the JSON itself, so keys without a field survive a save, and it leaves profile keys at their kind's defaults out of the file. `data/style.qss` styles it under `SettingsDialog` and `QMessageBox` selectors.
- `src/MainWindow.cpp`: builds the widget layout with `QSplitter`s and one `QQuickWidget` per QML panel; all panels share one `QQmlEngine`.
  - It also owns the terminals, shortcuts, the tray icon and `KNotification`s.
  - Ctrl+Q and the tray's Quit go through `confirmQuit`, which offers to stop the agents by killing the tmux server.
  - Layout state persists in `~/.config/x3ro-deck/x3ro-deck.conf`.
- `src/TerminalHost.cpp`: embeds Konsole via `konsolepart` (`KParts::PartLoader`), so terminals use the user's Konsole profile.
- `src/SessionBackend.h`: the interface that keeps agent sessions alive outside the app (create, ensure, rename, send keys, submit, foreground-process poll, attach command, kill). AppController and MainWindow use only this interface; nothing outside the backend may call tmux.
- `src/TmuxManager.cpp`: the tmux backend. Agents run on socket `-L x3ro-deck`, with config `~/.config/x3ro-deck/tmux.conf`. `ensureSession` only creates sessions that are missing, so agents survive app restarts. `submit` types text and presses Enter in one tmux call, or after a delay.
- `src/AgentBus.cpp`: `QLocalServer` on the hook socket. It reads one JSON hook payload per connection.
- `common/x3ro-deck-socket.h`: the socket path, shared with the hook and free of Qt. `$X3RO_DECK_SOCKET` overrides it. Otherwise it is `$XDG_RUNTIME_DIR/x3ro-deck.sock` on Linux and `$TMPDIR/x3ro-deck.sock` on macOS, or `/tmp/x3ro-deck-<uid>/x3ro-deck.sock` when those are unset or too long for `sun_path` (107 bytes on Linux, 103 on macOS).
- `hook/x3ro-deck-hook.cpp`: plain C++ with no Qt. Claude Code runs it from `~/.claude/settings.json` and Codex from `~/.codex/hooks.json`; both send the same Claude-style JSON. It must always exit 0, print nothing and finish in milliseconds.
- `src/DockerClient.cpp`: Docker Engine API over `/var/run/docker.sock` (HTTP/1.0 over `QLocalSocket`). It subscribes to `/events` and filters containers by the `com.docker.compose.project` label.
- `src/GitWatcher.cpp`: watches `HEAD` and `logs/HEAD` with `QFileSystemWatcher`.
- `src/PrWatcher.cpp`: polls `gh pr view <branch> --json …` per checkout.
- `qml/`: `Theme.qml` holds every color and font. Use its tokens and helpers (`agentColor`, `healthColor`, `elapsed`) and do not hardcode colors.
- New QML files and data files must be added to `resources.qrc`. New C++ sources must be added to the `x3ro-deck-core` library in `CMakeLists.txt`, and new tests to `tests/CMakeLists.txt`.

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
- Simulate agents by piping hook JSON into `./build/x3ro-deck-hook` with the test instance's `XDG_RUNTIME_DIR` (or `X3RO_DECK_SOCKET`).
- IMPORTANT: the user usually has the app running. When testing alongside it:
  - Use `dbus-run-session`. Without it, the test only activates the live window and exits.
  - Point `XDG_RUNTIME_DIR` at a private 0700 directory with a short path, such as `/run/user/1000/xdt`. `AgentBus` removes an existing socket and would take the live app's hooks. Setting `X3RO_DECK_SOCKET` on the test instance and the hook does the same for the socket alone.
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
