#define CHESS_USE_PEXT
#include "chess.hpp"

#include <sstream>
#include <string>
#include <cstdint>
#include <thread>
#include "uci.h"

#include "utils.h"
#include "search.h"

searchparams params = {};
std::thread search_thread;

static void stopSearch(globalstate* gs) {
    if (search_thread.joinable()) {
        gs->thread_stop.store(true, std::memory_order_relaxed);
        search_thread.join();
    }
}

bool UCILoop(globalstate* gs) {

    std::string input;
    std::cin.clear();
    std::getline(std::cin, input);

    std::istringstream toks(input);
    std::string token;

    toks >> token;

    if(token == "uci") {
        std::cout << "id name amalgam 1.0\n";
        std::cout << "id author gilp06\n";
        std::cout << "option name Hash type spin default 16 min 1 max 1024\n";
        std::cout << "option name Threads type spin default 1 min 1 max 256\n";
        std::cout << "option name UCI_Chess960 type check default false\n";
        std::cout << "uciok\n";
        return false;
    }

    if(token == "isready") {
        std::cout << "readyok\n";
        return false;
    }

    if(token == "ucinewgame") {
        stopSearch(gs);
        // we should just clear the hash here
        gs->tt.clear();
        return false;
    }

    if(token == "position") {
        stopSearch(gs);
        toks >> token;
        if(token == "startpos") {
            gs->board = chess::Board();
        }
        else if(token == "fen") {

            std::string fen;
            std::string part;

            for (int i = 0; i < 6; i++) {
                if (!(toks >> part)) {
                    std::cout << "invalid fen\n";
                    return false;
                }

                if(i)
                    fen += ' ';
                fen += part;
            }

            gs->board = chess::Board(fen);
        } else {
            std::cout << "invalid option, expected startpos or fen\n";
            return false;
        }

        if (toks >> token && token == "moves") {
            while (toks >> token) {
                auto move = chess::uci::uciToMove(gs->board, token);
                if (!gs->board.isLegal(move)) {
                    std::cout << "illegal move: " << token << " stopping early!\n";
                    return false;
                }
                gs->board.makeMove(move);
            }
        }
        return false;
    }

    if(token == "go") {
        stopSearch(gs);

        params = {};

        while (toks >> token) {
            if (token == "perft") {
                uint64_t depth;
                toks >> depth;

                perft_top(gs->board, depth);
                return false;
            }
            else if (token == "nodes") {
                toks >> params.max_nodes;
            }
            else if (token == "depth") {
                toks >> params.max_depth;
            }
            else if (token == "wtime") {
                toks >> params.wtime;
            }
            else if (token == "btime") {
                toks >> params.btime;
            }
            else if (token == "winc") {
                toks >> params.winc;
                }
            else if (token == "binc") {
                toks >> params.binc;
            }
            else if (token == "movetime") {
                toks >> params.movetime;
            }
        }

        gs->thread_stop.store(false, std::memory_order_relaxed);
        search_thread = std::thread(startSearch, gs, &params);

        return false;
    }

    if(token == "stop") {
        gs->thread_stop.store(true, std::memory_order_relaxed);
        stopSearch(gs);
        return false;
    }

    if(token == "quit") {
        stopSearch(gs);
        return true;
    }

    if(token == "setoption") {
        if (toks >> token && token == "name") {

            std::string opt_name;
            toks >> opt_name;

            if (toks >> token && token == "value") {
                    updateUCIOpts(gs, opt_name, toks);
                    return false;
            }
        }
    }

    std::cout << "invalid argument in: " << input << std::endl;

    return false;
}


void updateUCIOpts(globalstate* gs, const std::string &opt_name, std::istringstream &remaining) {
    stopSearch(gs);

    if (opt_name == "Hash") {
        int size;
        remaining >> size;
        if (size < 1) size = 1;
        if (size > 1024) size = 1024;
        gs->tt.resize(size);
    }

    if (opt_name == "Threads") {
        int threads;
        remaining >> threads;
        if (threads < 1) threads = 1;
        if (threads > 256) threads = 256;
        gs->num_threads = threads;
    }

    if (opt_name == "UCI_Chess960") {
        std::string val;
        remaining >> val;
        if (val == "true") {
            gs->board = chess::Board(chess::constants::STARTPOS, true);
            gs->chess960 = true;
        } else {
            gs->board = chess::Board(chess::constants::STARTPOS, false);
            gs->chess960 = false;
        }
    }
}
