# x3ro-deck

Qt 6 / KF6 desktop app (C++20, Widgets + QML panels) for running Claude Code agents and Docker Compose stacks across several example checkouts. It targets Linux with KDE Plasma only, so portability is a non-goal.

# Commands

- `cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local`: configure. Plain CMake, without extra-cmake-modules.
- `cmake --build build`: build both targets. The build must stay warning-free.
- `cmake --install build`: install the binaries to `~/.local/bin`, plus the desktop file, notifyrc and hicolor icons.
- `kbuildsycoca6`: refresh KDE's caches after changing the desktop file or icons.

There is no test suite. Verify changes with a headless screenshot (see Workflow).

# Architecture

- `src/main.cpp`: app setup and `KDBusService` Unique (a second launch raises the first window). Dev flags: `--tab <docker|agents>`, `--select N`, `--screenshot <file> [delayMs]`.
- `src/AppController.{h,cpp}`: the only state owner. The context property `app` exposes it to QML through Q_PROPERTYs, `VariantListModel`s and Q_INVOKABLEs.
  - `onHookEvent` (`src/AppController.cpp:472`) maps hook payloads to the agent state machine: `offline | ready | working | permission | question | done`.
  - `publishAttention` (`src/AppController.cpp:634`) builds the NEEDS YOU items. Priority order: permission, question, docker, done.
  - Each hook payload's `cwd` is matched to a checkout by longest canonical path prefix.
- `src/MainWindow.cpp`: builds the widget layout with `QSplitter`s and one `QQuickWidget` per QML panel; all panels share one `QQmlEngine`.
  - It also owns the terminals, shortcuts, the tray icon and `KNotification`s.
  - Layout state persists in `~/.config/x3ro-deck/x3ro-deck.conf`.
- `src/TerminalHost.cpp`: embeds Konsole via `konsolepart` (`KParts::PartLoader`), so terminals use the user's Konsole profile.
- `src/TmuxManager.cpp`: agents run in tmux on socket `-L x3ro-deck`, with config `~/.config/x3ro-deck/tmux.conf`. `ensureSession` only creates sessions that are missing, so agents survive app restarts.
- `src/AgentBus.cpp`: `QLocalServer` on `$XDG_RUNTIME_DIR/x3ro-deck.sock`. It reads one JSON hook payload per connection.
- `hook/x3ro-deck-hook.cpp`: plain C++ with no Qt, registered as a Claude Code hook in `~/.claude/settings.json`. It must always exit 0, print nothing and finish in milliseconds.
- `src/DockerClient.cpp`: Docker Engine API over `/var/run/docker.sock` (HTTP/1.0 over `QLocalSocket`). It subscribes to `/events` and filters containers by the `com.docker.compose.project` label.
- `src/GitWatcher.cpp`: watches `HEAD` and `logs/HEAD` with `QFileSystemWatcher`.
- `src/PrWatcher.cpp`: polls `gh pr view <branch> --json …` per checkout.
- `src/Config.cpp`: `~/.config/x3ro-deck/config.json`, written with defaults on first run. User keys override defaults at the top level.
- `qml/`: `Theme.qml` holds every color and font. Use its tokens and helpers (`agentColor`, `healthColor`, `elapsed`) and do not hardcode colors.
- New QML files and data files must be added to `resources.qrc`. New C++ sources must be added to `CMakeLists.txt`.

# Code style

- Match the existing Qt idiom: `QStringLiteral`/`QLatin1String`, `Q_EMIT`/`Q_SIGNALS`, `std::as_const` in range-for loops, and anonymous namespaces for file-local helpers.
- Keep comments sparse. Explain why, not what.
- Name QML model roles carefully: `id` is reserved in QML delegates. The attention model therefore exposes the id as `itemId`.
- QML `font.pixelSize` takes an int. A fractional value fails with "Invalid property assignment" and blanks the whole panel.

# Workflow

- Run a headless screenshot after every UI change and inspect the PNG:
  ```
  QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1 \
    dbus-run-session -- ./build/x3ro-deck --tab agents --select 0 --screenshot out.png 9000
  ```
- IMPORTANT: the user usually has the app running. When testing alongside it:
  - Use `dbus-run-session`. Without it, the test only activates the live window and exits.
  - Point `XDG_RUNTIME_DIR` at a private 0700 directory. `AgentBus` removes an existing socket and would take the live app's hooks.
  - Point `XDG_CONFIG_HOME` at a temporary directory containing `{"autostartAgents": false}` in `x3ro-deck/config.json`, and set `GH_CONFIG_DIR=$HOME/.config/gh` so `gh` stays authenticated.
  - The test instance still attaches to the live `-L x3ro-deck` tmux server. NEVER run `tmux kill-server` or `kill-session` against it, because those sessions hold the user's live agents.
- QML errors go to journald unless `QT_FORCE_STDERR_LOGGING=1` is set.
- `cmake --install` succeeds while the app runs. The user restarts the app (Ctrl+Q, then relaunch) to pick up the new binary, and agents keep running in tmux.
- Edit `~/.claude/settings.json` only by merging. Other hooks (for example `agent-notify`) live alongside `x3ro-deck-hook`.

# Quirks

- Permission buttons send keys to the agent's tmux pane: `1` for Allow, `2` for Always, `Escape` for Deny. `permissionKeys` in config.json can change them.
- The pane poll (`onPaneCommands`) changes agent state only on transitions of `claude` inside our own tmux session. All other state comes from hooks.
- Exited containers with exit code 0 count as `completed`. They are excluded from up/total counts and never raise NEEDS YOU items.
- `PrWatcher` skips `main`, `master` and `develop`. A closed, unmerged PR turns "View PR" back into `/create-pr`.
- `KStatusNotifierItem` and `KGlobalAccel` log errors under `dbus-run-session`. These are expected in headless tests.
- The icon source is `data/icons/x3ro-deck.svg`. After editing it, regenerate the PNGs with `rsvg-convert -w N -h N` and the ICO with `magick x3ro-deck-{16,24,32,48,64,128,256}.png x3ro-deck.ico`.
