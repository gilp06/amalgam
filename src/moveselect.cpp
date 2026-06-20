#include "moveselect.h"
#include "chess.hpp"
#include "utils.h"
#include <cassert>
#include <optional>

using namespace chess;

const static int16_t SEEPawn = 200;
const static int16_t SEEKnight = 760;
const static int16_t SEEBishop = 820;
const static int16_t SEERook = 1270;
const static int16_t SEEQueen = 2500;

const int16_t SEEPieceValues[14] = {
    SEEPawn,   SEEKnight, SEEBishop, SEERook,  SEEQueen, 0, SEEPawn,
    SEEKnight, SEEBishop, SEERook,   SEEQueen, 0,        0};

// victim piece type (row) vs attacker piece type (column)
const int16_t MVVValues[7][7] = {
    10, 11, 12, 13, 14, 15, 0,  20, 21, 22, 23, 24, 25, 0,  30, 31, 32,
    33, 34, 35, 0,  40, 41, 42, 43, 44, 45, 0,  50, 51, 52, 53, 54, 55,
    0,  60, 61, 62, 63, 64, 65, 0,  0,  0,  0,  0,  0,  0,  0};

void UpdateHistoryHeuristic(HistoryHeuristicTable &historyHeuristic,
                            chess::Color stm, chess::Move move,
                            int16_t bonus) {
    int16_t clampedBonus = CLAMP(bonus, -MAX_HISTORY, MAX_HISTORY);
    int16_t currentValue =
        historyHeuristic[(int)stm][move.from().index()][move.to().index()];
    historyHeuristic[(int)stm][move.from().index()][move.to().index()] +=
        clampedBonus - currentValue * abs(clampedBonus) / MAX_HISTORY;
}

bool StaticExchangeThresholded(const chess::Board &board, chess::Move move,
                               int16_t threshold, PinnersBlockersStruct pb[2]) {

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
MoveSelect::MoveSelect(const chess::Board &board,
                       std::optional<chess::Move> ttMove,
                       const HistoryHeuristicTable *historyHeuristic,
                       bool skipQuiets,
                       std::optional<std::array<chess::Move, 2>> killers)
    : board(board), skipQuiets(skipQuiets), ttMove(ttMove), killers(killers),
      historyHeuristic(historyHeuristic) {
    // cache pinners and blockers for SEE
    pb[(int)Color::WHITE] = fetchPinnersBlockers(board, Color::WHITE);
    pb[(int)Color::BLACK] = fetchPinnersBlockers(board, Color::BLACK);

    if (!ttMove.has_value()) {
        stage = MoveSelectStage::GenCaptures;
    } else {
        stage = MoveSelectStage::TTMove;
    }

    index = 0;
    quiet_index = 0;
}

Move MoveSelect::next() {

    if (stage == MoveSelectStage::TTMove) {
        // std::cout << "TT Move" << std::endl;
        stage = MoveSelectStage::GenCaptures;
        assert(ttMove.has_value());

        if (!skipQuiets || board.isCapture(*ttMove)) {
            return ttMove.value();
        }
    }

    if (stage == MoveSelectStage::GenCaptures) {
        // std::cout << "Gen Captures" << std::endl;
        movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
        stage = MoveSelectStage::GoodCaptures;

        // now score the captures
        for (size_t i = 0; i < moves.size(); i++) {
            if (!StaticExchangeThresholded(board, moves[i], 0, pb)) {
                // give bad score, we will rescore later
                moves[i].setScore(-1);
            } else {
                moves[i].setScore(MVVValues[board.at(moves[i].to()).type()]
                                           [board.at(moves[i].from()).type()]);
            }
        }
    }

    if (stage == MoveSelectStage::GoodCaptures) {
        // std::cout << "Good Captures" << std::endl;
        while (index < moves.size()) {
            size_t best_index = index;

            for (size_t i = index + 1; i < moves.size(); i++) {
                if (moves[i].score() >= moves[best_index].score()) {
                    best_index = i;
                }
            }

            if (moves[best_index].score() <= 0) {
                stage = skipQuiets ? BadCaptures : MoveSelectStage::GenQuiets;
                break;
            } else {
                std::swap(moves[index], moves[best_index]);
                index++;
                if (ttMove.has_value() && moves[index - 1] == ttMove.value())
                    continue;
                return moves[index - 1];
            }
        }
        stage = skipQuiets ? BadCaptures : MoveSelectStage::GenQuiets;
    }

    if (stage == MoveSelectStage::GenQuiets) {
        // std::cout << "Gen Quiets" << std::endl;
        movegen::legalmoves<movegen::MoveGenType::QUIET>(quietMoves, board);
        stage = MoveSelectStage::Quiets;

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

    if (stage == MoveSelectStage::Quiets) {
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

        stage = MoveSelectStage::BadCaptures;
    }

    if (stage == MoveSelectStage::BadCaptures) {

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
        stage = MoveSelectStage::Done;
    }

    if (stage == MoveSelectStage::Done) {
        // std::cout << "Done" << std::endl;
        return Move::NO_MOVE;
    }

    stage = MoveSelectStage::Done;
    return Move::NO_MOVE;
};
