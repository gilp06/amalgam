#pragma once

#include "nnue.h"
#include "ttable.h"
#include <chess.hpp>

struct globalstate {
    chess::Board board;
    bool thread_stop;
    bool chess960;
    TTable& tt;
};
