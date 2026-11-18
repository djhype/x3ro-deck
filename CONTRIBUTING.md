# Contributing to x3ro deck

Thanks for helping. This file covers building, the code style, and how
changes get in.

## Building

x3ro deck is a Qt 6 (6.4 or newer) C++20 app built with CMake and Ninja. It
also needs libvterm (built from its release tarball when pkg-config doesn't
find one), and git, tmux, Docker and gh at runtime.

| OS | Dependencies | Configure and build |
|---|---|---|
| Linux | `scripts/ci-linux-deps.sh` lists the packages | `cmake --preset linux && cmake --build --preset linux` |
| macOS | `scripts/ci-macos-deps.sh` (Homebrew) | `cmake --preset macos -DCMAKE_PREFIX_PATH=$(brew --prefix qt) && cmake --build --preset macos` |
| Windows | Qt 6 for MSVC, Visual Studio 2022, Ninja | from a Developer prompt: `cmake --preset windows && cmake --build --preset windows` |

Before sending a change, run:

```sh
ctest --test-dir build              # the QtTest suites
scripts/check-format.sh             # clang-format (--fix applies it)
scripts/check-spdx.sh               # SPDX headers (--fix adds them)
scripts/smoke-test.sh               # Linux: headless screenshot, fails on QML errors
```

`x3ro-deck --demo --screenshot out.png` renders the window with made-up data,
which is the way to take screenshots for issues and pull requests.

## Code style

`CLAUDE.md` describes the architecture and the conventions. In short:

- Match the Qt idiom already in the code: `QStringLiteral`, `Q_EMIT`,
  `std::as_const` in range-for loops, and anonymous namespaces for file-local
  helpers.
- Keep comments sparse, and explain why rather than what.
- Colors and fonts come from the theme (`qml/Theme.qml` tokens); no
  hardcoded colors in QML.
- The build stays warning-free on GCC, Clang and MSVC.
- Every new source file starts with the SPDX header.
- UI changes come with a screenshot.

## Sign-off (DCO)

x3ro deck uses the [Developer Certificate of Origin](https://developercertificate.org/)
rather than a contributor license agreement. Sign off every commit:

```sh
git commit -s
```

This adds a `Signed-off-by:` line, which certifies that you wrote the change
or otherwise have the right to submit it under the project's license
(GPL-3.0-or-later with the additional terms in `LICENSE`). Contributions stay
under that license; nobody signs over copyright.

## Pull requests

- One topic per pull request, with tests where the change can be tested.
- Describe what changed and how you checked it; the template asks for both.
- Changes visible to users get a line in `CHANGELOG.md` under *Unreleased*.

## Name and logo

Forks are welcome under the license, but the "x3ro deck" name and logo are
reserved; see `TRADEMARKS.md`.
