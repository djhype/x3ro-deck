# x3ro-deck

Qt 6 / KF6 desktop app (C++20, Widgets + QML panels) for running coding agents and Docker Compose stacks across several checkouts. It drives Claude Code and Codex, and local models (for example A model on a local server) through either CLI. It targets Linux with KDE Plasma only, so portability is a non-goal.

# Commands

- `cmake --preset linux`: configure into `build/` with Ninja, RelWithDebInfo and `CMAKE_INSTALL_PREFIX=$HOME/.local`. Plain CMake, without extra-cmake-modules. `CMakePresets.json` also has `macos` and `windows` presets; each shows only on its own host.
- `cmake --build --preset linux`: build both targets. The build must stay warning-free on GCC, Clang and MSVC; `x3ro_deck_set_warnings` in `CMakeLists.txt` picks the flags. Add `-DX3RO_DECK_WERROR=ON` to the configure step to turn warnings into errors (CI does).
- `cmake --install build`: install the binaries to `~/.local/bin`, plus the desktop file, notifyrc and hicolor icons. The desktop, notifyrc and icon rules apply only on `UNIX AND NOT APPLE`. The desktop file comes from `data/x3ro-deck.desktop.in` with an absolute `Exec`, because Plasma's session PATH lacks `~/.local/bin`.
- `kbuildsycoca6`: refresh KDE's caches after changing the desktop file or icons.
- `-DX3RO_DECK_CORE_ONLY=ON`: build without KF6, which leaves out the app itself but keeps the hook, `x3ro-deck-sessiond`, `x3ro-deck-attach` and every test. The Qt floor is 6.4 (Ubuntu 24.04 and Debian 12). KF6 needs Qt 6.5+, so the full app can't build on those distros until KF6 is gone.
- `scripts/check-format.sh [--fix]`: check (or apply) `.clang-format` on every tracked C++ file.
- `scripts/smoke-test.sh [binary] [out.png]`: headless screenshot in an isolated environment. It fails without a PNG or on any QML error.
- `ctest --test-dir build`: run the QtTest suites in `tests/`. They link `x3ro-deck-base`, the static library holding everything except the KF6-dependent `main.cpp`, `MainWindow` and `TerminalHost`. `tst_qml` compiles every panel in `qml/`. `tst_sessionflow` drives AppController against a private tmux server, and again as `sessionflow-daemon` against a private `x3ro-deck-sessiond`. `tst_sessiond` covers the daemon's protocol (including the foreground job and process tree), `DaemonBackend`, the attach client in a PTY and `SessionScreen` redraws (checked against a `TerminalScreen` fed the raw output).

CI (`.github/workflows/ci.yml`) runs in an `archlinux` container, plus a core-only job on `ubuntu:24.04` with its distro Qt 6.4 and tmux 3.4. It checks formatting, builds with `-DX3RO_DECK_WERROR=ON`, runs `ctest` and the smoke test, and uploads the screenshot. `scripts/ci-linux-deps.sh` lists the packages. For UI changes, also inspect a screenshot yourself (see Workflow).

# Architecture

- `src/main.cpp`: app setup and `KDBusService` Unique (a second launch raises the first window). It relaunches the binary after `MainWindow::restart()`. Dev flags: `--tab <docker|agents>`, `--select N`, `--settings [checkouts|agents|docker|general]`, `--screenshot <file> [delayMs]`. The screenshot captures an open dialog instead of the window.
- `src/AppController.{h,cpp}`: the only state owner. The context property `app` exposes it to QML through Q_PROPERTYs, `VariantListModel`s and Q_INVOKABLEs.
  - Each checkout runs one agent at a time in one tmux session. `AgentInfo::profile` names its `AgentProfile`; the pick persists in QSettings under `agents/<checkout>`.
  - A session that survives a restart keeps that pick. `ensureAgentSession` switches a freshly created session to the checkout's configured agent, and a renamed checkout takes over its old session, matched by path.
  - `reloadConfig` applies saved settings while running. A changed checkout list (names, paths or order) takes a restart.
  - `onHookEvent` maps hook payloads to the agent state machine: `offline | ready | working | permission | question | done`.
  - `publishAttention` builds the NEEDS YOU items. Priority order: permission, question, docker, done.
  - Each hook payload's `cwd` is matched to a checkout by the longest checkout path that contains it, with `Paths::isWithin`, so `/a/bc` never matches `/a/b`.
- `src/Paths.{h,cpp}`: path normalization (`~`, symlinks, separators, Windows drive letters) and comparison. It is case-insensitive on macOS and Windows. Checkout paths, hook cwds, tmux session paths and the Settings path fields all go through it.
- `src/Config.{h,cpp}`: `~/.config/x3ro-deck/config.json`, written with defaults on first run. User keys override defaults at the top level.
  - `configVersion` names the file's layout (missing = 0). `src/ConfigMigrations.cpp` holds one function per version step. `readJson` runs them on the user's keys before merging defaults, keeps the original as `config.json.v<N>.bak`, and writes the result. A file from a newer build is left untouched, and `configError` warns in the checkout rail, which also keeps Settings from saving over it.
  - To change the layout, append a migration. Its index is the version it upgrades from; add a fixture and a case to `tests/tst_configmigrations.cpp`.
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
- `src/TerminalHost.cpp`: hosts one terminal. By default it embeds Konsole via `konsolepart` (`KParts::PartLoader`), so terminals use the user's Konsole profile. With `terminal: "builtin"` (takes a restart) it shows a `TerminalView` instead and runs the program (the backend's attach command, or `$SHELL` for shell tabs) in a `PtyChannel`.
- The built-in terminal lives in `x3ro-deck-base`, so it needs no KF6 and `tst_terminal` covers it:
  - `TerminalScreen` wraps libvterm. It holds the cell grid and turns keys and pastes into bytes, honoring the program's modes. Ctrl combos use legacy control bytes, because libvterm would send CSI u.
  - `TerminalView` is a `QWidget` that paints the screen cell by cell and maps key events. Its grid follows the widget size and resizes the channel. It claims every key except the window shortcuts passed to `setReservedShortcuts`.
  - `TerminalChannel` is the byte stream behind a view. `PtyChannel` implements it with `forkpty`.
  - `TerminalAppearance` holds the font, colors and padding. `fallback()` uses the system monospace font and the theme's colors.
- `src/SessionBackend.h`: the interface that keeps agent sessions alive outside the app (create, ensure, rename, send keys, submit, foreground-process poll, attach command, kill). AppController and MainWindow use only this interface; nothing outside the backend may call tmux. The config key `sessionBackend` picks the implementation at startup: `tmux` (default) or `daemon`. Changing it takes a restart.
- `src/TmuxManager.cpp`: the tmux backend. Agents run on socket `-L x3ro-deck`, with config `~/.config/x3ro-deck/tmux.conf`. `ensureSession` only creates sessions that are missing, so agents survive app restarts. `submit` types text and presses Enter in one tmux call, or after a delay.
- `src/DaemonBackend.cpp`: the `daemon` backend, a blocking request/reply client of `x3ro-deck-sessiond`.
  - Only `createSession` starts the daemon (`x3ro-deck-sessiond --detach`, found next to the app or on PATH; `$X3RO_DECK_SESSIOND_BIN` overrides). Everything else treats a missing daemon as no sessions.
  - `sendKeys` maps tmux key names to bytes (`keyBytes`). The session env is stored by the daemon and returned by `list`, the analogue of `@x3ro-deck-env`.
  - `attachCommand` runs `x3ro-deck-attach <session>`; the hint is `x3ro-deck attach <checkout>`.
- `sessiond/`: `x3ro-deck-sessiond`, QtCore and QtNetwork only. It owns one `forkpty` per session running the user's login shell in the checkout, and outlives the GUI (`--detach` forks, `setsid`s and returns once listening).
  - One daemon per socket, guarded by `flock` on `<socket>.lock`. It exits 60 s after its last session and client are gone, or when its socket file disappears.
  - It feeds each session's output into a `SessionScreen`. `attach` resizes the PTY and the screen to the client first, then sends `redraw()` as the first `Output` frame. Resizes from any client apply to both, and the last one wins. `screen` returns the screen as text.
  - `list` reports `foreground`, tmux's `pane_current_command`. With `processes: true` (the pane poll) it adds `foregroundGroup`, every process name in the foreground job with the leader first, so an agent behind a wrapper (`node codex.js`, a launcher script) still matches its profile. `processes` returns the session's process tree, for detected links.
- `src/ProcessInfo.{h,cpp}`: process inspection for the daemon (static lib `x3ro-deck-processinfo`, QtCore only). `commandName` follows tmux's `parse_window_name`; `foregroundNames` builds `foregroundGroup`. The platform layer (`foregroundGroup`, `process`, `tree`) is one file per OS: `ProcessInfoLinux.cpp` reads `/proc` (`task/*/children`, with a full scan as fallback), and `ProcessInfoNone.cpp` reports nothing elsewhere. macOS (libproc) and Windows (Toolhelp32) add their own files.
- `src/SessionScreen.{h,cpp}`: the daemon's libvterm screen, QtCore only (static lib `x3ro-deck-sessionscreen`, since `TerminalScreen` needs QtGui). `redraw()` emits escape sequences that rebuild the cells, cursor, pen, title and alternate screen on a fresh terminal. It writes soft-wrapped rows without a cursor move, so the receiving terminal keeps the wrap. libvterm keeps the input modes private, so a small scanner tracks them from the same bytes: cursor keys, keypad, bracketed paste, mouse, focus, kitty keyboard flags and modifyOtherKeys, plus scroll region and origin mode. Scrollback and the primary screen hidden behind the alternate screen are not redrawn.
- `src/SessiondProtocol.h`: the daemon's wire format, documented there. Frames are a big-endian u32 length, a type byte and a payload: `Control` (JSON with `op`/`id`, replies with `re`, events with `event`), `Output` and `Input` (u32 session id plus bytes). Add ops or fields rather than bumping `kProtocolVersion`.
- `attach/AttachClient.cpp`: `x3ro-deck attach [--socket PATH] <checkout>`. It runs both as `x3ro-deck-attach` (QtCore only) and as the `attach` subcommand that `main.cpp` handles before `QApplication`. It uses raw mode, forwards `SIGWINCH`, and Ctrl+\ detaches. On exit it resets every mode a redraw can set.
- `src/AgentBus.cpp`: `QLocalServer` on the hook socket. It reads one JSON hook payload per connection.
- `common/x3ro-deck-socket.h`: the socket paths, shared with the hook and free of Qt. `sessiondSocketPath()` resolves the daemon's `x3ro-deck-sessiond.sock` the same way, with `$X3RO_DECK_SESSIOND_SOCKET` as its override. `$X3RO_DECK_SOCKET` overrides it. Otherwise it is `$XDG_RUNTIME_DIR/x3ro-deck.sock` on Linux and `$TMPDIR/x3ro-deck.sock` on macOS, or `/tmp/x3ro-deck-<uid>/x3ro-deck.sock` when those are unset or too long for `sun_path` (107 bytes on Linux, 103 on macOS).
- `hook/x3ro-deck-hook.cpp`: plain C++ with no Qt. Claude Code runs it from `~/.claude/settings.json` and Codex from `~/.codex/hooks.json`; both send the same Claude-style JSON. It must always exit 0, print nothing and finish in milliseconds.
- `src/DockerClient.cpp`: Docker Engine API over a Unix socket (HTTP/1.0 over `QLocalSocket`). It subscribes to `/events` and filters containers by the `com.docker.compose.project` label.
- `src/DockerEndpoint.cpp`: picks that socket. The order is the `dockerHost` setting, `DOCKER_HOST`, the active docker context, then rootless Docker, Podman, Colima, OrbStack and `/var/run/docker.sock`. Only `unix://` works. Settings → Docker shows the result.
- Compose and quick commands expand `{compose}` to the `composeBinary` setting (`docker compose`, `docker-compose` or `podman compose`).
- `src/GitWatcher.cpp`: watches `HEAD` and `logs/HEAD` with `QFileSystemWatcher`.
- `src/PrWatcher.cpp`: polls `gh pr view <branch> --json …` per checkout.
- `qml/`: `Theme.qml` holds every color and font. Use its tokens and helpers (`agentColor`, `healthColor`, `elapsed`) and do not hardcode colors.
- New QML files and data files must be added to `resources.qrc`. New C++ sources must be added to `CMakeLists.txt`, to `x3ro-deck-base` unless they need KF6, and new tests to `tests/CMakeLists.txt`.

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
  - Point `TMUX_TMPDIR` at the same directory. The test instance then runs its own `-L x3ro-deck` tmux server, which is safe to kill. The same `XDG_RUNTIME_DIR` also gives a `sessionBackend: "daemon"` test instance its own `x3ro-deck-sessiond`.
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
- The pane poll (`onPaneCommands`) changes agent state only on transitions of an agent process inside our own session. All other state comes from hooks. `foregroundProcesses` gives each session a list of names: tmux sends `pane_current_command` alone, and the daemon adds the rest of the foreground job. A profile runs when any name starts with its `process`. When the pane runs another profile's `process`, the checkout switches to that profile; the current profile is checked first, then the names in order.
- libvterm's `CSI ?1049l` without an earlier `?1049h` restores an all-zero pen, which paints text black on black. `SessionScreen::redraw` therefore leaves the alternate screen with `?1047l`.
- tmux before 3.5 prints tabs in `-F` output as `_`, so `TmuxManager` separates fields with `:`. Session names never contain `:`.
- Exited containers with exit code 0 count as `completed`. They are excluded from up/total counts and never raise NEEDS YOU items.
- `PrWatcher` skips `main`, `master` and `develop`. A closed, unmerged PR turns "View PR" back into the profile's `prPrompt`.
- `KStatusNotifierItem` and `KGlobalAccel` log errors under `dbus-run-session`. These are expected in headless tests.
- The icon source is `data/icons/x3ro-deck.svg`. After editing it, regenerate the PNGs with `rsvg-convert -w N -h N` and the ICO with `magick x3ro-deck-{16,24,32,48,64,128,256}.png x3ro-deck.ico`.
