#pragma once


#include <chess.hpp>
#include "utils.h"
enum ms_stage {
    TTMove,
    GenNoisy,
    GoodNoisy,
    // Killer Moves,
    GenQuiets,
    Quiets,
    BadNoisy,
    Done,
};


#define MAX_HISTORY 25000
using history_heuristic_table = std::array<std::array<std::array<int16_t, 64>, 64>, 2>;
void UpdateHistoryHeuristic(history_heuristic_table& historyHeuristic, chess::Color stm, chess::Move move, int16_t bonus);

const extern int16_t SEEPawn;
const extern int16_t SEEKnight;
const extern int16_t SEEBishop;
const extern int16_t SEERook;
const extern int16_t SEEQueen;

class move_select {
    public:
        pb_struct pb[2];
        ms_stage stage;
        int16_t seeThreshold;
        bool skip_quiets;
        move_select(const chess::Board& board, std::optional<chess::Move> ttMove, const history_heuristic_table* historyHeuristic = nullptr, bool skipQuiets = false, std::optional<std::array<chess::Move, 2>> killers = std::nullopt, int16_t seeThreshold = 0);
        chess::Move next();
    private:
        chess::Movelist moves, quietMoves;
        size_t index;
        size_t quiet_index;
        std::optional<chess::Move> ttMove;
        std::optional<std::array<chess::Move, 2>> killers;
        const history_heuristic_table* historyHeuristic;
        const chess::Board& board;
};

extern const int16_t MVVValues[7][7];
extern const int16_t SEEPieceValues[14];
bool StaticExchangeThresholded(const chess::Board& board, chess::Move move, int16_t threshold, pb_struct pb[2]);
