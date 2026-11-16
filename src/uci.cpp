#define CHESS_USE_PEXT
#include "chess.hpp"

#include "uci.h"
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <random>

#include "search.h"
#include "utils.h"

searchparams params = {};
std::thread search_thread;

static void stopSearch(globalstate *gs) {
    if (search_thread.joinable()) {
        gs->thread_stop.store(true, std::memory_order_relaxed);
        search_thread.join();
    }
}

bool UCILoop(globalstate *gs) {

    std::string input;
    std::cin.clear();
    std::getline(std::cin, input);

    std::istringstream toks(input);
    std::string token;

    toks >> token;

    if (token == "uci") {
        std::cout << "id name amalgam 1.0\n";
        std::cout << "id author gilp06\n";
        std::cout << "option name Hash type spin default 16 min 1 max 1024\n";
        std::cout << "option name Threads type spin default 1 min 1 max 256\n";
        std::cout << "option name UCI_Chess960 type check default false\n";
        std::cout << "option name Softnodes type check default false\n";
        std::cout << "uciok\n";
        return false;
    }

    if (token == "isready") {
        std::cout << "readyok\n";
        return false;
    }

    if (token == "ucinewgame") {
        stopSearch(gs);
        // we should just clear the hash here
        gs->tt.clear();
        return false;
    }

    if (token == "position") {
        stopSearch(gs);
        toks >> token;
        if (token == "startpos") {
            gs->board = chess::Board();
        } else if (token == "fen") {

            std::string fen;
            std::string part;

            for (int i = 0; i < 6; i++) {
                if (!(toks >> part)) {
                    std::cout << "invalid fen\n";
                    return false;
                }

                if (i)
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
                    std::cout << "illegal move: " << token
                              << " stopping early!\n";
                    return false;
                }
                gs->board.makeMove(move);
            }
        }
        return false;
    }

    if (token == "go") {
        stopSearch(gs);

        params = {};

        while (toks >> token) {
            if (token == "perft") {
                uint64_t depth;
                toks >> depth;

                perft_top(gs->board, depth);
                return false;
            } else if (token == "nodes") {
                toks >> params.max_nodes;
            } else if (token == "depth") {
                toks >> params.max_depth;
            } else if (token == "wtime") {
                toks >> params.wtime;
            } else if (token == "btime") {
                toks >> params.btime;
            } else if (token == "winc") {
                toks >> params.winc;
            } else if (token == "binc") {
                toks >> params.binc;
            } else if (token == "movetime") {
                toks >> params.movetime;
            }
        }

        gs->thread_stop.store(false, std::memory_order_relaxed);
        search_thread = std::thread(startSearch, gs, &params, false);

        return false;
    }

    if (token == "stop") {
        gs->thread_stop.store(true, std::memory_order_relaxed);
        stopSearch(gs);
        return false;
    }

    if (token == "quit") {
        stopSearch(gs);
        return true;
    }

    if (token == "setoption") {
        if (toks >> token && token == "name") {

            std::string opt_name;
            toks >> opt_name;

            if (toks >> token && token == "value") {
                updateUCIOpts(gs, opt_name, toks);
                return false;
            }
        }
    }

    if (token == "genfens") {
        // https://github.com/AndyGrant/OpenBench/wiki/Data-Generation-Workloads#genfens-interface
        // Currently I will not support
        // first read the count
        uint64_t count = 0;
        if (toks >> count) {
            if (toks >> token && token == "seed") {
                uint64_t seed = 0;
                if (toks >> seed) {
                    if (toks >> token && token == "book") {
                        if (toks >> token && token == "None") {
                            while (toks >> token) {
                                if (token == "depth") {
                                    uint64_t depth = 0;
                                    if (toks >> depth) {
                                        genFens(count, seed, depth);
                                        return false;
                                    } else {
                                        std::cout
                                            << "invalid depth in: " << input
                                            << std::endl;
                                        return false;
                                    }
                                } else {

                                    return false;
                                }
                            }
                            genFens(count, seed, 8);
                            return false;
                        } else {
                            std::cout << "invalid book option in: " << input
                                      << std::endl;
                            return false;
                        }
                    }
                }
            }
        }
    }

    std::cout << "invalid argument in: " << input << std::endl;

    return false;
}

void updateUCIOpts(globalstate *gs, const std::string &opt_name,
                   std::istringstream &remaining) {
    stopSearch(gs);

    if (opt_name == "Hash") {
        int size;
        remaining >> size;
        if (size < 1)
            size = 1;
        if (size > 1024)
            size = 1024;
        gs->tt.resize(size);
    }

    if (opt_name == "Threads") {
        int threads;
        remaining >> threads;
        if (threads < 1)
            threads = 1;
        if (threads > 256)
            threads = 256;
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

    if (opt_name == "Softnodes") {
        std::string val;
        remaining >> val;
        if (val == "true") {
            gs->softnodes = true;
        } else {
            gs->softnodes = false;
        }
    }
}

// stole this from the discord :)
const std::vector<std::string> BENCH_FENS = {
    "r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
    "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
    "r3qbrk/6p1/2b2pPp/p3pP1Q/PpPpP2P/3P1B2/2PB3K/R5R1 w - - 16 42",
    "6k1/1R3p2/6p1/2Bp3p/3P2q1/P7/1P2rQ1K/5R2 b - - 4 44",
    "8/8/1p2k1p1/3p3p/1p1P1P1P/1P2PK2/8/8 w - - 3 54",
    "7r/2p3k1/1p1p1qp1/1P1Bp3/p1P2r1P/P7/4R3/Q4RK1 w - - 0 36",
    "r1bq1rk1/pp2b1pp/n1pp1n2/3P1p2/2P1p3/2N1P2N/PP2BPPP/R1BQ1RK1 b - - 2 10",
    "3r3k/2r4p/1p1b3q/p4P2/P2Pp3/1B2P3/3BQ1RP/6K1 w - - 3 87",
    "2r4r/1p4k1/1Pnp4/3Qb1pq/8/4BpPp/5P2/2RR1BK1 w - - 0 42",
    "4q1bk/6b1/7p/p1p4p/PNPpP2P/KN4P1/3Q4/4R3 b - - 0 37",
    "2q3r1/1r2pk2/pp3pp1/2pP3p/P1Pb1BbP/1P4Q1/R3NPP1/4R1K1 w - - 2 34",
    "1r2r2k/1b4q1/pp5p/2pPp1p1/P3Pn2/1P1B1Q1P/2R3P1/4BR1K b - - 1 37",
    "r3kbbr/pp1n1p1P/3ppnp1/q5N1/1P1pP3/P1N1B3/2P1QP2/R3KB1R b KQkq b3 0 17",
    "8/6pk/2b1Rp2/3r4/1R1B2PP/P5K1/8/2r5 b - - 16 42",
    "1r4k1/4ppb1/2n1b1qp/pB4p1/1n1BP1P1/7P/2PNQPK1/3RN3 w - - 8 29",
    "8/p2B4/PkP5/4p1pK/4Pb1p/5P2/8/8 w - - 29 68",
    "3r4/ppq1ppkp/4bnp1/2pN4/2P1P3/1P4P1/PQ3PBP/R4K2 b - - 2 20",
    "5rr1/4n2k/4q2P/P1P2n2/3B1p2/4pP2/2N1P3/1RR1K2Q w - - 1 49",
    "1r5k/2pq2p1/3p3p/p1pP4/4QP2/PP1R3P/6PK/8 w - - 1 51",
    "q5k1/5ppp/1r3bn1/1B6/P1N2P2/BQ2P1P1/5K1P/8 b - - 2 34",
    "r1b2k1r/5n2/p4q2/1ppn1Pp1/3pp1p1/NP2P3/P1PPBK2/1RQN2R1 w - - 0 22",
    "r1bqk2r/pppp1ppp/5n2/4b3/4P3/P1N5/1PP2PPP/R1BQKB1R w KQkq - 0 5",
    "r1bqr1k1/pp1p1ppp/2p5/8/3N1Q2/P2BB3/1PP2PPP/R3K2n b Q - 1 12",
    "r1bq2k1/p4r1p/1pp2pp1/3p4/1P1B3Q/P2B1N2/2P3PP/4R1K1 b - - 2 19",
    "r4qk1/6r1/1p4p1/2ppBbN1/1p5Q/P7/2P3PP/5RK1 w - - 2 25",
    "r7/6k1/1p6/2pp1p2/7Q/8/p1P2K1P/8 w - - 0 32",
    "r3k2r/ppp1pp1p/2nqb1pn/3p4/4P3/2PP4/PP1NBPPP/R2QK1NR w KQkq - 1 5",
    "3r1rk1/1pp1pn1p/p1n1q1p1/3p4/Q3P3/2P5/PP1NBPPP/4RRK1 w - - 0 12",
    "5rk1/1pp1pn1p/p3Brp1/8/1n6/5N2/PP3PPP/2R2RK1 w - - 2 20",
    "8/1p2pk1p/p1p1r1p1/3n4/8/5R2/PP3PPP/4R1K1 b - - 3 27",
    "8/4pk2/1p1r2p1/p1p4p/Pn5P/3R4/1P3PP1/4RK2 w - - 1 33",
    "8/5k2/1pnrp1p1/p1p4p/P6P/4R1PK/1P3P2/4R3 b - - 1 38",
    "8/8/1p1kp1p1/p1pr1n1p/P6P/1R4P1/1P3PK1/1R6 b - - 15 45",
    "8/8/1p1k2p1/p1prp2p/P2n3P/6P1/1P1R1PK1/4R3 b - - 5 49",
    "8/8/1p4p1/p1p2k1p/P2npP1P/4K1P1/1P6/3R4 w - - 6 54",
    "8/8/1p4p1/p1p2k1p/P2n1P1P/4K1P1/1P6/6R1 b - - 6 59",
    "8/5k2/1p4p1/p1pK3p/P2n1P1P/6P1/1P6/4R3 b - - 14 63",
    "8/1R6/1p1K1kp1/p6p/P1p2P1P/6P1/1Pn5/8 w - - 0 67",
    "1rb1rn1k/p3q1bp/2p3p1/2p1p3/2P1P2N/PP1RQNP1/1B3P2/4R1K1 b - - 4 23",
    "4rrk1/pp1n1pp1/q5p1/P1pP4/2n3P1/7P/1P3PB1/R1BQ1RK1 w - - 3 22",
    "r2qr1k1/pb1nbppp/1pn1p3/2ppP3/3P4/2PB1NN1/PP3PPP/R1BQR1K1 w - - 4 12",
    "2r2k2/8/4P1R1/1p6/8/P4K1N/7b/2B5 b - - 0 55",
    "6k1/5pp1/8/2bKP2P/2P5/p4PNb/B7/8 b - - 1 44",
    "2rqr1k1/1p3p1p/p2p2p1/P1nPb3/2B1P3/5P2/1PQ2NPP/R1R4K w - - 3 25",
    "r1b2rk1/p1q1ppbp/6p1/2Q5/8/4BP2/PPP3PP/2KR1B1R b - - 2 14",
    "6r1/5k2/p1b1r2p/1pB1p1p1/1Pp3PP/2P1R1K1/2P2P2/3R4 w - - 1 36",
    "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 2",
    "2rr2k1/1p4bp/p1q1p1p1/4Pp1n/2PB4/1PN3P1/P3Q2P/2RR2K1 w - f6 0 20",
    "3br1k1/p1pn3p/1p3n2/5pNq/2P1p3/1PN3PP/P2Q1PB1/4R1K1 w - - 0 23",
    "2r2b2/5p2/5k2/p1r1pP2/P2pB3/1P3P2/K1P3R1/7R w - - 23 93"};

void bench() {

    uint64_t total_nodes = 0;
    auto start_time = std::chrono::steady_clock::now();

    for (const auto &fen : BENCH_FENS) {
        TTable ttable(16);
        globalstate gs = {chess::Board(fen), true, 0, false, false, 1, ttable};

        searchparams params = {};
        params.max_depth = 10;
        gs.thread_stop = false;
        startSearch(&gs, &params, true); // run search in the main thread

        total_nodes += gs.nodes_searched.load(std::memory_order_relaxed);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    auto nps = (uint64_t)(total_nodes * 1000.0) / duration.count();
    std::cout << total_nodes << " nodes " << nps << " nps" << std::endl;
}

void genFens(uint64_t count, uint64_t seed, uint64_t depth) {
    std::mt19937_64 rng(seed);
    for (uint64_t i = 0; i < count; i++) {
        chess::Board board;
        for (uint64_t j = 0; j < depth; j++) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            if (moves.size() == 0) {
                break;
            }
            std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
            auto move = moves[dist(rng)];
            board.makeMove(move);
        }
        std::cout << "info string genfens " << board.getFen() << std::endl;
    }
}