#define CHESS_USE_PEXT
#include <chess.hpp>
#include "utils.h"
#include "types.h"
#include "uci.h"


int main() {

    initLookup();

    bool done = false;

    TTable table(1024);
    globalstate gs = {
        chess::Board(),
        true,
        0,
        true,
        4,
        table,
    };

    while(!done) {
        done = UCILoop(&gs);
    }

    return 0;
}
