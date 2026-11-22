#define CHESS_USE_PEXT
#include "chess.hpp"

#include "moveselect.h"
#include "utils.h"
#include <cassert>
#include <optional>

using namespace chess;

const int16_t SEEPawn = 100;
const int16_t SEEKnight = 300;
const int16_t SEEBishop = 300;
const int16_t SEERook = 500;
const int16_t SEEQueen = 900;

const int16_t SEEPieceValues[14] = {
    SEEPawn,   SEEKnight, SEEBishop, SEERook,  SEEQueen, 0, SEEPawn,
    SEEKnight, SEEBishop, SEERook,   SEEQueen, 0,        0};

// victim piece type (row) vs attacker piece type (column)
const int16_t MVVValues[7][7] = {
    10, 11, 12, 13, 14, 15, 0,  20, 21, 22, 23, 24, 25, 0,  30, 31, 32,
    33, 34, 35, 0,  40, 41, 42, 43, 44, 45, 0,  50, 51, 52, 53, 54, 55,
    0,  60, 61, 62, 63, 64, 65, 0,  0,  0,  0,  0,  0,  0,  0};

void UpdateHistoryHeuristic(history_heuristic_table &historyHeuristic,
                            chess::Color stm, chess::Move move,
                            int16_t bonus) {
    int16_t clampedBonus = CLAMP(bonus, -MAX_HISTORY, MAX_HISTORY);
    int16_t currentValue =
        historyHeuristic[(int)stm][move.from().index()][move.to().index()];
    historyHeuristic[(int)stm][move.from().index()][move.to().index()] +=
        clampedBonus - currentValue * abs(clampedBonus) / MAX_HISTORY;
}

bool StaticExchangeThresholded(const chess::Board &board, chess::Move move,
                               int16_t threshold, pb_struct pb[2]) {

    // assume for other move types that the move is good enough to be played,
    // since they are promotions etc.
    if (move.typeOf() != Move::NORMAL) {
        return 0 >= threshold;
    }

    Square from = move.from(), to = move.to();
    chess::Piece p;

    int16_t swap = SEEPieceValues[board.at(to)] - threshold;
    if (swap < 0)
        return false;

    swap = SEEPieceValues[board.at(from)] - swap;
    if (swap <= 0)
        return true;

    Bitboard occupied =
        board.occ() ^ (1ULL << from.index()) ^ (1ULL << to.index());
    Color stm = board.sideToMove();
    Bitboard attackers = attackers_to_square(board, to, occupied);
    Bitboard stmAttackers, bb;
    Bitboard pinners[2] = {0, 0};

    // generate pinned rays

    bool result = true;

    while (true) {
        stm = ~stm;
        attackers &= occupied;

        if (!(stmAttackers = attackers & board.us(stm)))
            break;

        // If the opposing side has a pinner, remove any pinned pieces from
        // candidate attackers
        if (pb[!stm].our_pinners & occupied) {
            stmAttackers &= ~pb[!stm].their_blockers;

            if (!stmAttackers)
                break;
        }

        result = !result;

        if ((bb = stmAttackers & board.pieces(PieceType::PAWN))) {
            if ((swap = SEEPawn - swap) < result)
                break;
            occupied ^= (1ULL << bb.lsb());
            attackers |= attacks::bishop(to, occupied) &
                         (board.pieces(PieceType::QUEEN, PieceType::BISHOP));
        } else if ((bb = stmAttackers & board.pieces(PieceType::KNIGHT))) {
            if ((swap = SEEKnight - swap) < result)
                break;
            occupied ^= (1ULL << bb.lsb());
        } else if ((bb = stmAttackers & board.pieces(PieceType::BISHOP))) {
            if ((swap = SEEBishop - swap) < result)
                break;
            occupied ^= (1ULL << bb.lsb());
            attackers |= attacks::bishop(to, occupied) &
                         (board.pieces(PieceType::QUEEN, PieceType::BISHOP));
        } else if ((bb = stmAttackers & board.pieces(PieceType::ROOK))) {
            if ((swap = SEERook - swap) < result)
                break;
            occupied ^= (1ULL << bb.lsb());
            attackers |= attacks::rook(to, occupied) &
                         (board.pieces(PieceType::QUEEN, PieceType::ROOK));
        } else if ((bb = stmAttackers & board.pieces(PieceType::QUEEN))) {
            swap = SEEQueen - swap;
            assert(swap >= result);
            occupied ^= (1ULL << bb.lsb());
            attackers |= (attacks::rook(to, occupied) &
                          board.pieces(PieceType::QUEEN, PieceType::ROOK)) |
                         (attacks::bishop(to, occupied) &
                          board.pieces(PieceType::QUEEN, PieceType::BISHOP));
        } else {
            return (attackers & ~board.us(stm)) ? !result : result;
        }
    }

    return result;
}
move_select::move_select(const chess::Board &board,
                       std::optional<chess::Move> ttMove,
                       const history_heuristic_table *historyHeuristic,
                       bool skipQuiets,
                       std::optional<std::array<chess::Move, 2>> killers,
                       int16_t seeThreshold)
    : board(board), skip_quiets(skipQuiets), ttMove(ttMove), killers(killers),
      historyHeuristic(historyHeuristic), seeThreshold(seeThreshold) {

    if (this->ttMove.has_value() && !board.isLegal(*this->ttMove)) {
        this->ttMove.reset();
    }

    // cache pinners and blockers for SEE
    pb[(int)Color::WHITE] = fetchPinnersBlockers(board, Color::WHITE);
    pb[(int)Color::BLACK] = fetchPinnersBlockers(board, Color::BLACK);


    // skip TT move stage if it doesn't exist or if it is a quiet move and we are skipping quiets
    if (!this->ttMove.has_value() || (skip_quiets && !board.isCapture(*this->ttMove))) {
        stage = ms_stage::GenCaptures;
    } else {
        stage = ms_stage::TTMove;
    }

    index = 0;
    quiet_index = 0;
}

Move move_select::next() {

    if (stage == ms_stage::TTMove) {
        stage = ms_stage::GenCaptures;
        assert(ttMove.has_value());
        return ttMove.value();
    }

    if (stage == ms_stage::GenCaptures) {
        // std::cout << "Gen Captures" << std::endl;
        movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
        stage = ms_stage::GoodCaptures;

        // now score the captures
        for (size_t i = 0; i < moves.size(); i++) {
            if (!StaticExchangeThresholded(board, moves[i], seeThreshold, pb)) {
                // give bad score, we will rescore later
                moves[i].setScore(-1);
            } else {
                moves[i].setScore(MVVValues[board.at(moves[i].to()).type()]
                                           [board.at(moves[i].from()).type()]);
            }
        }
    }

    if (stage == ms_stage::GoodCaptures) {
        // std::cout << "Good Captures" << std::endl;
        while (index < moves.size()) {
            size_t best_index = index;

            for (size_t i = index + 1; i < moves.size(); i++) {
                if (moves[i].score() >= moves[best_index].score()) {
                    best_index = i;
                }
            }

            if (moves[best_index].score() <= 0) {
                stage = skip_quiets ? BadCaptures : ms_stage::GenQuiets;
                break;
            } else {
                std::swap(moves[index], moves[best_index]);
                index++;
                if (ttMove.has_value() && moves[index - 1] == ttMove.value())
                    continue;
                return moves[index - 1];
            }
        }
        stage = skip_quiets ? BadCaptures : ms_stage::GenQuiets;
    }

    if (stage == ms_stage::GenQuiets) {
        // std::cout << "Gen Quiets" << std::endl;
        if (skip_quiets) {
            stage = ms_stage::BadCaptures;
            return next();
        }
        movegen::legalmoves<movegen::MoveGenType::QUIET>(quietMoves, board);
        stage = ms_stage::Quiets;

        for (size_t i = 0; i < quietMoves.size(); i++) {
            if (historyHeuristic != nullptr)
                quietMoves[i].setScore(
                    (*historyHeuristic)[board.sideToMove()]
                                       [quietMoves[i].from().index()]
                                       [quietMoves[i].to().index()]);
            if (killers.has_value()) {
                if (quietMoves[i] == (*killers)[0]) {
                    quietMoves[i].setScore(27000);
                } else if (quietMoves[i] == (*killers)[1]) {
                    quietMoves[i].setScore(26000);
                }
            }
        }
    }

    if (stage == ms_stage::Quiets) {

        if (skip_quiets) {
            stage = ms_stage::BadCaptures;
            return next();
        }

        // std::cout << "Quiets" << std::endl;
        while (quiet_index < quietMoves.size()) {
            size_t best_index = quiet_index;
            for (size_t i = quiet_index + 1; i < quietMoves.size(); i++) {
                if (quietMoves[i].score() >= quietMoves[best_index].score()) {
                    best_index = i;
                }
            }

            std::swap(quietMoves[quiet_index], quietMoves[best_index]);
            quiet_index++;
            if (ttMove.has_value() &&
                quietMoves[quiet_index - 1] == ttMove.value())
                continue;
            return quietMoves[quiet_index - 1];
        }

        stage = ms_stage::BadCaptures;
    }

    if (stage == ms_stage::BadCaptures) {

        // std::cout << "Bad Captures" << std::endl;
        // potentially rescore here

        for (size_t i = index; i < moves.size(); i++) {
            moves[i].setScore(MVVValues[board.at(moves[i].to()).type()]
                                       [board.at(moves[i].from()).type()]);
        }

        while (index < moves.size()) {
            size_t best_index = index;

            for (size_t i = index + 1; i < moves.size(); i++) {
                if (moves[i].score() >= moves[best_index].score()) {
                    best_index = i;
                }
            }

            std::swap(moves[index], moves[best_index]);
            index++;
            if (ttMove.has_value() && moves[index - 1] == ttMove.value())
                continue;
            return moves[index - 1];
        }
        stage = ms_stage::Done;
    }

    if (stage == ms_stage::Done) {
        // std::cout << "Done" << std::endl;
        return Move::NO_MOVE;
    }

    stage = ms_stage::Done;
    return Move::NO_MOVE;
};
