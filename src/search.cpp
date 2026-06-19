#include "moveselect.h"
#include "ttable.h"
#include <chess.hpp>
#include <chrono>
#include <optional>
using namespace chess;

#include "search.h"
#include "types.h"
#include "utils.h"

static const int CHECKMATE = 30000;
static const int INF = INT16_MAX;

static const int THREAD_COUNT = 1;
threaddata tds[THREAD_COUNT];

void startSearch(globalstate *gs, searchparams *params) {
    timemanager tm(*params, gs->board.sideToMove());
    gs->thread_stop = false;

    std::cout << tm.max_search_time << std::endl;

    Accumulator initial_accums[2];
    initial_accums[0] = LOADED_NNUE->feature_biases;
    initial_accums[1] = LOADED_NNUE->feature_biases;
    populateAccums(initial_accums, gs->board);

    for (int i = 0; i < THREAD_COUNT; i++) {
        tds[i].gs = gs;
        tds[i].tm = &tm;

        // copy board and accumulators per thread
        tds[i].board = chess::Board(gs->board);

        tds[i].nodes = 0;
        tds[i].worker = i != 0;
        tds[i].seldepth = 0;

        std::fill(&(tds[i].pv[0][0]), &(tds[i].pv[0][0]) + MAX_PLY * MAX_PLY,
                  Move::NO_MOVE);
        std::fill(tds[i].pv_lengths, tds[i].pv_lengths + MAX_PLY, 0);
        std::fill(tds[i].ss, tds[i].ss + MAX_PLY, sstack_data());

        tds[i].ss[0].accums[0] = initial_accums[0];
        tds[i].ss[0].accums[1] = initial_accums[1];
    }

    search(tds[0]);

    std::cout << "bestmove "
              << uci::moveToUci(tds[0].best_move, tds[0].board.chess960());
    if (tds[0].ponder != Move::NO_MOVE) {
        std::cout << " ponder "
                  << uci::moveToUci(tds[0].ponder, tds[0].board.chess960());
    }
    std::cout << std::endl;
}

void search(threaddata &td) {
    int16_t cur_depth = 1;
    while (true) {
        // clear PV lengths
        std::fill(td.pv_lengths, td.pv_lengths + MAX_PLY, 0);

        if (td.tm->max_depth != 0 && cur_depth > td.tm->max_depth) {
            td.gs->thread_stop = true;
            break;
        }

        int16_t alpha = -INF;
        int16_t beta = INF;
        int16_t delta = 10;

        while (true) {
            if (cur_depth >= 5) {
                alpha = td.score - delta;
                beta = td.score + delta;
            }

            int16_t score = alphabeta<PV_NODE>(td, alpha, beta, cur_depth, 0);

            if (checkStatus(td)) {
                td.gs->thread_stop = true;
                break;
            }

            if (score > alpha && score < beta) {
                td.score = score;
                cur_depth++;
                td.best_move = td.pv[0][0];
                if (td.pv_lengths[0] > 1)
                    td.ponder = td.pv[0][1];
                break;
            }

            if (score <= alpha) {
                // std::cout << "a.w. fail low" << std::endl;
                beta = (alpha + beta) / 2;
                alpha = MAX(-INF, alpha - delta);
            }
            if (score >= beta) {
                // std::cout << "a.w. fail high" << std::endl;
                beta = MIN(INF, beta + delta);
            }

            delta = delta + delta / 2;
        }

        if (td.gs->thread_stop) {
            break;
        }

        double time_spent = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - td.tm->start)
                                .count();
        std::cout << "info depth " << cur_depth - 1 << " seldepth "
                  << td.seldepth << " score cp " << td.score << " nodes "
                  << td.nodes << " time " << (int)(time_spent * 1000) << " nps "
                  << (int)(td.nodes / time_spent) << " pv";
        for (int i = 0; i < td.pv_lengths[0]; i++) {
            std::cout << " "
                      << uci::moveToUci(td.pv[0][i], td.board.chess960());
        }
        std::cout << std::endl;
    }
}

template <nodetype node>
int16_t alphabeta(threaddata &td, int16_t alpha, int16_t beta, int16_t depth,
                  int16_t ply) {

    depth = MAX(0, depth);
    bool is_pv = (node == PV_NODE);
    bool is_root = (ply == 0);

    if (checkStatus(td)) {
        td.gs->thread_stop = true;
        return 0;
    }

    if (ply >= MAX_PLY - 1) {
        return evaluate(td, ply);
    }

    int16_t static_eval = evaluate(td, ply);

    int16_t alpha_orig = alpha;
    td.nodes++;
    td.pv_lengths[ply] = 0;

    if (ply > td.seldepth) {
        td.seldepth = ply;
    }

    if (td.board.isHalfMoveDraw() || td.board.isRepetition(2) ||
        td.board.isInsufficientMaterial()) {
        return 0;
    }

    std::optional<Move> tt_move = std::nullopt;
    Move best_move = Move::NO_MOVE;
    std::optional<TTable::Entry> probe = td.gs->tt.probe(td.board.hash());

    if (probe.has_value()) {

        if (!is_root && probe->depth >= depth && (depth == 0 || !is_pv)) {
            if (probe->bd == TTable::EXACT) {
                return probe->score;
            } else if (probe->bd == TTable::LOWER && probe->score >= beta) {
                return probe->score;
            } else if (probe->bd == TTable::UPPER && probe->score <= alpha) {
                return probe->score;
            }
        }
        if (probe->best_move != Move::NO_MOVE)
            tt_move = probe->best_move;
    }

    bool in_check = td.board.inCheck();
    bool can_prune = !in_check && !is_root;


    if (can_prune) {

        // Null Move Pruning
        if (depth >= 3 && !is_pv && static_eval + depth * 100 > beta &&
            td.board.hasNonPawnMaterial(td.board.sideToMove())) {
            int reduction = 4.0 + depth / 3.0;

            td.ss[ply+1].accums[0] = td.ss[ply].accums[0];
            td.ss[ply+1].accums[1] = td.ss[ply].accums[1];

            td.board.makeNullMove();
            int16_t score = -alphabeta<NORMAL>(td, -beta, -beta + 1, depth - 1 - reduction, ply + 1);
            td.board.unmakeNullMove();

            if (score >= beta) {
                return score;
            }
        }


    }


    if (depth == 0) {
        return quiescence(td, alpha, beta, ply);
    }

    int16_t best_score = INT16_MIN;
    size_t played = 0;
    MoveSelect ms(td.board, tt_move);

    while (true) {
        Move m = ms.next();
        if (ms.stage == MoveSelectStage::Done)
            break;
        assert(m != Move::NO_MOVE);

        performMove(td, m, &td.ss[ply]);
        played++;

        int16_t score;
        if (played == 1) {
            score = -alphabeta<node>(td, -beta, -alpha, depth - 1, ply + 1);
        } else {
            score =
                -alphabeta<NORMAL>(td, -alpha - 1, -alpha, depth - 1, ply + 1);
            if (score > alpha && score < beta) {
                score =
                    -alphabeta<PV_NODE>(td, -beta, -alpha, depth - 1, ply + 1);
            }
        }
        td.board.unmakeMove(m);

        if (score > best_score) {
            best_score = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;

                td.pv[ply][0] = m;
                for (int j = 0; j < td.pv_lengths[ply + 1]; j++) {
                    td.pv[ply][j + 1] = td.pv[ply + 1][j];
                }
                td.pv_lengths[ply] = td.pv_lengths[ply + 1] + 1;

                if (alpha >= beta) {
                    break;
                }
            }
        }
    }

    if (played == 0) {
        if (td.board.inCheck()) {
            best_score = -CHECKMATE + ply;
        } else {
            best_score = 0;
        }
    }

    if (ply != 0) {
        TTable::Entry entry;
        entry.depth = depth;
        entry.best_move = best_move.move();
        entry.score = best_score;
        entry.bd =
            best_score >= beta
                ? TTable::LOWER
                : (best_score <= alpha_orig ? TTable::UPPER : TTable::EXACT);
        entry.key = td.board.hash();
        td.gs->tt.store(entry);
    }

    return best_score;
}

int16_t quiescence(threaddata &td, int16_t alpha, int16_t beta, int16_t ply) {
    if (checkStatus(td)) {
        td.gs->thread_stop = true;
        return 0;
    }

    if (ply >= MAX_PLY - 1) {
        return evaluate(td, ply);
    }

    td.nodes++;

    if (ply > td.seldepth) {
        td.seldepth = ply;
    }

    int16_t alpha_orig = alpha;

    std::optional<Move> tt_move = std::nullopt;
    Move best_move = Move::NO_MOVE;
    std::optional<TTable::Entry> probe = td.gs->tt.probe(td.board.hash());

    if (probe.has_value()) {

        if (alpha == beta - 1) {
            if (probe->bd == TTable::EXACT) {
                return probe->score;
            } else if (probe->bd == TTable::LOWER && probe->score >= beta) {
                return probe->score;
            } else if (probe->bd == TTable::UPPER && probe->score <= alpha) {
                return probe->score;
            }
        }
        if (probe->best_move != Move::NO_MOVE)
            tt_move = probe->best_move;
    }

    int16_t best_score = INT16_MIN;
    bool incheck = td.board.inCheck();
    int16_t stand_pat = 0;
    if (!incheck) {
        stand_pat = evaluate(td, ply);
        best_score = stand_pat;
        if (stand_pat >= beta) {
            return beta;
        }
        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    MoveSelect ms(td.board, tt_move, !incheck);
    int16_t played = 0;

    while (true) {
        Move m = ms.next();
        if (ms.stage == MoveSelectStage::Done)
            break;
        assert(m != Move::NO_MOVE);
        played++;

        performMove(td, m, &td.ss[ply]);
        int16_t score = -quiescence(td, -beta, -alpha, ply + 1);
        td.board.unmakeMove(m);

        if (score > best_score) {
            best_score = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    break;
                }
            }
        }
    }

    if (played == 0) {
        if (incheck) {
            best_score = -CHECKMATE + ply;
        }
    }

    TTable::Entry entry;
    entry.depth = 0;
    entry.score = best_score;
    entry.bd = best_score >= beta
                   ? TTable::LOWER
                   : (best_score <= alpha_orig ? TTable::UPPER : TTable::EXACT);
    entry.best_move =
        (entry.bd == TTable::UPPER) ? Move::NO_MOVE : best_move.move();
    entry.key = td.board.hash();
    td.gs->tt.store(entry);

    return best_score;
}

int16_t evaluate(threaddata &td, int16_t ply) {
    if (td.board.sideToMove() == Color::WHITE) {
        return NNUE::evaluate(&td.ss[ply].accums[0], &td.ss[ply].accums[1],
                              td.board.all().count());
    } else {
        return NNUE::evaluate(&td.ss[ply].accums[1], &td.ss[ply].accums[0],
                              td.board.all().count());
    }
}

bool checkStatus(threaddata &td) {
    if (td.gs->thread_stop) {
        return true;
    }

    if (td.tm->max_nodes != 0 && td.nodes >= td.tm->max_nodes) {
        return true;
    }

    if ((td.nodes & 2047) == 0) {
        if (td.tm->max_search_time != 0) {
            double time_spent =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - td.tm->start)
                    .count();
            if (time_spent >= td.tm->max_search_time) {
                return true;
            }
        }
    }

    return false;
}

void performMove(threaddata &td, Move move, sstack_data *ss) {

    (ss + 1)->accums[0] = ss->accums[0];
    (ss + 1)->accums[1] = ss->accums[1];

    Piece from = td.board.at(move.from());
    Piece to = td.board.at(move.to());
    Color stm = td.board.sideToMove();

    if (move.typeOf() == Move::CASTLING) {
        Square dest = stm == Color::WHITE ? Square::SQ_E1 : Square::SQ_E8;
        Square king_to = (move.from() > move.to()) ? dest - 2 : dest + 2;
        Square rook_to = (move.from() > move.to()) ? king_to + 1 : king_to - 1;

        nnue_remove_piece((ss + 1)->accums, move.to(),
                          stm == Color::WHITE ? Piece::WHITEROOK
                                              : Piece::BLACKROOK);
        nnue_add_piece((ss + 1)->accums, rook_to,
                       stm == Color::WHITE ? Piece::WHITEROOK
                                           : Piece::BLACKROOK);

        nnue_remove_piece((ss + 1)->accums, move.from(),
                          stm == Color::WHITE ? Piece::WHITEKING
                                              : Piece::BLACKKING);
        nnue_add_piece((ss + 1)->accums, king_to,
                       stm == Color::WHITE ? Piece::WHITEKING
                                           : Piece::BLACKKING);
    } else if (move.typeOf() == Move::ENPASSANT) {
        nnue_remove_piece((ss + 1)->accums, move.from(), from);
        nnue_add_piece((ss + 1)->accums, move.to(), from);
        nnue_remove_piece((ss + 1)->accums, move.to().ep_square(),
                          stm == Color::WHITE ? Piece::BLACKPAWN
                                              : Piece::WHITEPAWN);
    } else if (move.typeOf() == Move::PROMOTION) {
        nnue_remove_piece((ss + 1)->accums, move.from(), from);
        nnue_add_piece((ss + 1)->accums, move.to(),
                       Piece(move.promotionType(), stm));
        if (to != Piece::NONE) {
            nnue_remove_piece((ss + 1)->accums, move.to(), to);
        }
    } else {
        nnue_remove_piece((ss + 1)->accums, move.from(), from);
        nnue_add_piece((ss + 1)->accums, move.to(), from);
        if (to != Piece::NONE) {
            nnue_remove_piece((ss + 1)->accums, move.to(), to);
        }
    }

    td.board.makeMove(move);
}
