#include <chess.hpp>
#include "utils.h"
#include "types.h"
#include "uci.h"


int main() {

    initLookup();

    bool done = false;

    TTable table(128); // default size of 16 MB
    globalstate gs = {
        chess::Board(),
        true,
        true,
        table,
    };

    while(!done) {
        done = uci_loop(&gs);
    }

    return 0;
}
