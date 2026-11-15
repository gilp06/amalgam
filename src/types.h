#pragma once

#include "nnue.h"
#include "ttable.h"
#include <chess.hpp>
#include <atomic>

struct globalstate {
    chess::Board board;
    std::atomic_bool thread_stop;
    std::atomic<uint64_t> nodes_searched;
    bool chess960;
    int num_threads;
    TTable& tt;
};
