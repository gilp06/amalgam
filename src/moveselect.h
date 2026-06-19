#pragma once


#include <chess.hpp>
#include "utils.h"
enum MoveSelectStage {
    TTMove,
    GenCaptures,
    GoodCaptures,
    // Killer Moves,
    GenQuiets,
    Quiets,
    BadCaptures,
    Done,
};

class MoveSelect {
    private:
        chess::Movelist moves, quietMoves;
        size_t index;
        size_t quiet_index;
        std::optional<chess::Move> ttMove;
        const chess::Board& board;
        bool skipQuiets;
        PinnersBlockersStruct pb[2];
    public:
        MoveSelectStage stage;
        MoveSelect(const chess::Board& board, std::optional<chess::Move> ttMove, bool skipQuiets = false);
        chess::Move next();
};

bool StaticExchangeThresholded(const chess::Board& board, chess::Move move, int16_t threshold);
