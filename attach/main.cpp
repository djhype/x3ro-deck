// x3ro-deck-attach: the attach client on its own, without the GUI's
// libraries. The embedded agent terminals run it; `x3ro-deck attach` is the
// same code.

#include "AttachClient.h"

int main(int argc, char *argv[])
{
    return runAttach(argc, argv);
}
