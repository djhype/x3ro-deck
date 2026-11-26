# Changelog

All notable changes to x3ro deck are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.1] - 2026-11-25

### Added

- Quick commands per checkout: a checkout's own `quickCommands` show next to
  the global ones, and one with a global command's label replaces it.
  Settings → Checkouts edits them.

### Changed

- Settings → Checkouts groups each checkout's options in General, Coding
  Agent and Docker tabs.

### Fixed

- Windows: git, gh, the compose command and the agent CLIs no longer all
  count as missing.
- macOS: Ctrl+C and the other Ctrl keys reach the program in the built-in
  terminal; ⌘ copies, pastes and runs the window's shortcuts.
- macOS: the session daemon starts reliably when it detaches.
- Linux: the package repositories get their first build; 0.1.0 failed on
  the openSUSE Build Service.

## [0.1.0] - 2026-11-19

First release.

### Added

- Agent sessions for Claude Code, Codex or any CLI per checkout, in tmux or
  the bundled session daemon (`x3ro-deck-sessiond`), with a built-in terminal
  and `x3ro-deck attach` from any terminal.
- NEEDS YOU: permission prompts, questions, finished turns and unhealthy
  containers, with Allow/Deny from the window, desktop notifications and the
  tray.
- Hook installer for Claude Code and Codex in Settings → Agents.
- Docker panel per checkout: compose files, profiles, env file and project
  name; containers matched by compose labels; browser links for published
  ports and for dev servers started in the checkout.
- Quick commands detected from Makefile, package.json, justfile, Taskfile,
  mise, Cargo, Go, Deno, pyproject (uv, poetry, poe), tox, nox, Composer,
  Laravel, Rake, Gradle and Maven, with pin and hide.
- Terminal settings import from Konsole, kitty, Ghostty, Alacritty, GNOME
  Terminal, GNOME Console, Ptyxis, MATE Terminal, xfce4-terminal, COSMIC
  Terminal, iTerm2, Terminal.app, Windows Terminal and Console Host.
- Themes: Cyberdecker and Cyberdecker Light, a System theme that follows
  light and dark mode, user themes and a theme editor.
- Rebindable shortcuts, a global show/hide hotkey, and a macOS menu bar.
- First-run welcome screen with git worktree discovery.
- `--demo` mode for screenshots.

### Packages

- Linux: signed pacman, apt, dnf and zypper repositories for Arch, Debian 12
  and 13, Ubuntu 24.04 to 26.10, Fedora 41 to 44 and openSUSE Tumbleweed,
  set up by `install.sh` from https://deck.x3ro.life/, plus a binary tarball.
- macOS 12 and later: a universal DMG (Apple silicon and Intel).
- Windows 10 and 11: an installer, also set up by `install.ps1`.
- The macOS and Windows builds are not code-signed yet; the README shows how
  to open them the first time.
