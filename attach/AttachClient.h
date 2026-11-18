// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// `x3ro-deck attach [--socket PATH] [--no-replay] <checkout>`: connects the
// calling terminal to a checkout's x3ro-deck-sessiond session, like
// `tmux attach`. Ctrl+\ detaches. argv[0] is the command's own name.
//
// Both x3ro-deck-attach and x3ro-deck (as its `attach` subcommand) run it. It
// needs no QCoreApplication.
int runAttach(int argc, char **argv);
