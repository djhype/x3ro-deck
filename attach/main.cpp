// SPDX-FileCopyrightText: 2026 x3ro
// SPDX-License-Identifier: GPL-3.0-or-later

// x3ro-deck-attach: the attach client on its own, without the GUI's
// libraries. The embedded agent terminals run it; `x3ro-deck attach` is the
// same code.

#include "AttachClient.h"

int main(int argc, char *argv[])
{
    return runAttach(argc, argv);
}
