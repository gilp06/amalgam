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


#define MAX_HISTORY 25000
using HistoryHeuristicTable = std::array<std::array<std::array<int16_t, 64>, 64>, 2>;
void UpdateHistoryHeuristic(HistoryHeuristicTable& historyHeuristic, chess::Color stm, chess::Move move, int16_t bonus);

const extern int16_t SEEPawn;
const extern int16_t SEEKnight;
const extern int16_t SEEBishop;
const extern int16_t SEERook;
const extern int16_t SEEQueen;

class MoveSelect {
    public:
        PinnersBlockersStruct pb[2];
        MoveSelectStage stage;
        MoveSelect(const chess::Board& board, std::optional<chess::Move> ttMove, const HistoryHeuristicTable* historyHeuristic = nullptr, bool skipQuiets = false, std::optional<std::array<chess::Move, 2>> killers = std::nullopt);
        chess::Move next();
    private:
        chess::Movelist moves, quietMoves;
        size_t index;
        size_t quiet_index;
        std::optional<chess::Move> ttMove;
        std::optional<std::array<chess::Move, 2>> killers;
        const HistoryHeuristicTable* historyHeuristic;
        const chess::Board& board;
        bool skipQuiets;
};

extern const int16_t MVVValues[7][7];
extern const int16_t SEEPieceValues[14];
bool StaticExchangeThresholded(const chess::Board& board, chess::Move move, int16_t threshold, PinnersBlockersStruct pb[2]);
