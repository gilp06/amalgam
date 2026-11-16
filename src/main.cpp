#define CHESS_USE_PEXT
#include <chess.hpp>
#include "utils.h"
#include "uci.h"


int main(int argc, char **argv) {

    
    

    initLookup();

    bool done = false;


    if (argc == 2 && strcmp(argv[1], "bench") == 0) {
        bench();
        return 0;
    }


    TTable table(1024);
    globalstate gs = {
        chess::Board(),
        true,
        0,
        false,
        false,
        1,
        table,
    };

    
    while(!done) {
        done = UCILoop(&gs);
    }

    return 0;
}
