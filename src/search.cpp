
#define CHESS_USE_PEXT
#include <chess.hpp>

#include "moveselect.h"
#include "ttable.h"

#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>

using namespace chess;

#include "search.h"
#include "utils.h"

static const int CHECKMATE = 30000;
static const int INF = INT16_MAX;

static int16_t scoreToTT(int16_t score, int16_t ply) {
    if (score > CHECKMATE - MAX_PLY) {
        return score + ply;
    }
    if (score < -CHECKMATE + MAX_PLY) {
        return score - ply;
    }
    return score;
}

static int16_t scoreFromTT(int16_t score, int16_t ply) {
    if (score > CHECKMATE - MAX_PLY) {
        return score - ply;
    }
    if (score < -CHECKMATE + MAX_PLY) {
        return score + ply;
    }
    return score;
}

std::vector<thread_data> tds;

int convert_mate(int16_t score) {
    if (score > CHECKMATE - MAX_PLY) {
        int ply_to_mate = CHECKMATE - score;
        return (ply_to_mate + 1) / 2;
    }
    if (score < -CHECKMATE + MAX_PLY) {
        int ply_to_mate = score + CHECKMATE;
        return -(((ply_to_mate) + 1) / 2);
    }
    return INT16_MAX;
}

bool is_better_thread(const thread_data& cur, const thread_data& best) {
    bool cur_mating = cur.score > CHECKMATE - MAX_PLY;
    bool best_mating = best.score > CHECKMATE - MAX_PLY;

    if (cur_mating && cur.score > best.score)
        return true;

    if (cur.completed_depth > best.completed_depth)
        return cur.score > best.score || !best_mating;

    if (cur.completed_depth == best.completed_depth)
        return cur.score > best.score;

    return false;
}

void print_info_string(const thread_data& td) {
    int16_t mate = convert_mate(td.score);

    double time_spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - td.tm->start_time).count();

    uint64_t nodes =
        td.gs->nodes_searched.load(std::memory_order_relaxed);
    std::cout << "info depth " << td.completed_depth << " seldepth " << td.seldepth; 

    if (mate != INT16_MAX) {
        std::cout << " score mate " << mate;
    } else {
        std::cout << " score cp " << td.score;
    }
    std::cout << " nodes "
                << nodes << " time " << (int)(time_spent * 1000)
                << " nps " << (int)(nodes / time_spent) << " pv";
    for (int i = 0; i < td.final_pv_length; i++) {
        std::cout << " "
                    << uci::moveToUci(td.final_pv[i], td.board.chess960());
    }
    std::cout << std::endl;
}

void startSearch(globalstate *gs, search_params *params, bool suppress_output) {
    tds.resize(gs->num_threads);
    gs->nodes_searched.store(0, std::memory_order_relaxed);
    time_manager tm(*params, gs->board.sideToMove());
    Accumulator initial_accums[2];
    initial_accums[0] = LOADED_NNUE->feature_biases;
    initial_accums[1] = LOADED_NNUE->feature_biases;
    populateAccums(initial_accums, gs->board);

    for (int i = 0; i < gs->num_threads; i++) {
        tds[i].gs = gs;
        tds[i].tm = &tm;

        // copy board and accumulators per thread
        tds[i].board = chess::Board(gs->board);

        tds[i].nodes = 0;
        tds[i].pending_nodes = 0;
        tds[i].worker = i != 0;
        tds[i].seldepth = 0;
        tds[i].score = 0;
        tds[i].final_pv_length = 0;
        tds[i].completed_depth = 0;

        std::fill(tds[i].final_pv, tds[i].final_pv + MAX_PLY,
                  Move::NO_MOVE);
        
        std::fill(&(tds[i].pv_array[0][0]), &(tds[i].pv_array[0][0]) + MAX_PLY * MAX_PLY,
                  Move::NO_MOVE);
        std::fill(tds[i].pv_lengths, tds[i].pv_lengths + MAX_PLY, 0);
        std::fill(tds[i].ss, tds[i].ss + MAX_PLY, sstack_data());

        tds[i].ss[0].accums[0] = initial_accums[0];
        tds[i].ss[0].accums[1] = initial_accums[1];
    }

    std::vector<std::thread> workers;
    for (int i = 1; i < gs->num_threads; i++) {
        // spin up threads
        workers.emplace_back(search, std::ref(tds[i]), false, suppress_output);
    }
    search(std::ref(tds[0]), true, suppress_output);
    // wait for all threads to finish
    for (auto &worker : workers) {
        worker.join();
    }

    for (int i = 0; i < gs->num_threads; i++) {
        tds[i].flush_nodes();
    }

    for (int i = 1; i < gs->num_threads; i++) {
        tds[i].gs->thread_stop.store(true, std::memory_order_relaxed);
    }

    // thread voting! based on ethereal
    // we should pick the deepest thread, unless we have found checkmate
    // in that case we should probably pick the thread that has the fastest mate
    // if its the same depth, pick the one that has a better score for us
    size_t best_thread = 0;
    for (int i = 1; i < gs->num_threads; i++) {
        if (is_better_thread(tds[i], tds[best_thread])){
            best_thread = i;
        }
    }

    if (!suppress_output) {
        print_info_string(tds[best_thread]);

        std::cout << "bestmove "
                  << uci::moveToUci(tds[best_thread].final_pv[0], tds[best_thread].board.chess960());
        if (tds[best_thread].final_pv_length > 1) {
            std::cout << " ponder "
                      << uci::moveToUci(tds[best_thread].final_pv[1], tds[best_thread].board.chess960());
        }
        std::cout << std::endl;
    }
}

void search(std::reference_wrapper<thread_data> td_ref, bool main, bool suppress_output) {
    thread_data &td = td_ref.get();

    int16_t cur_depth = 1;
    while (true) {
        // clear PV lengths and history heuristic
        std::fill(td.pv_lengths, td.pv_lengths + MAX_PLY, 0);
        td.history_heuristic = history_heuristic_table();

        if (td.tm->depth_limit.has_value() && cur_depth > td.tm->depth_limit.value()) {
            td.gs->thread_stop.store(true, std::memory_order_relaxed);
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

            auto cur_time = std::chrono::steady_clock::now();
            if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
                break;
            }
            if (td.tm->hit_hard_limit(td.gs->nodes_searched.load(std::memory_order_relaxed), cur_time)) {
                td.gs->thread_stop.store(true, std::memory_order_relaxed);
                break;
            }

            if (score > alpha && score < beta) {
                td.score = score;
                td.completed_depth = cur_depth;
                cur_depth++;
                for (int i = 0; i < td.pv_lengths[0]; i++) {
                    td.final_pv[i] = td.pv_array[0][i];
                }
                td.final_pv_length = td.pv_lengths[0];
                break;
            }

            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = MAX(-INF, alpha - delta);
            }
            if (score >= beta) {
                beta = MIN(INF, beta + delta);
            }

            delta = delta + delta / 2;
        }

        // flush at end of depth
        td.flush_nodes();
        int16_t mate = convert_mate(td.score);

        if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
            break;
        }

        if (main) {
            if (!suppress_output) {
                print_info_string(td);
            }

            td.tm->update_soft_time_limit(td.final_pv[0], td.score);
            
            if (td.tm->hit_soft_limit(td.gs->nodes_searched.load(std::memory_order_relaxed), std::chrono::steady_clock::now())) {
                td.gs->thread_stop.store(true, std::memory_order_relaxed);
                break;
            }
        }

        if (mate != INT16_MAX) {
            td.gs->thread_stop.store(true, std::memory_order_relaxed);
            break;
        }
    }
}

template <nodetype node>
int16_t alphabeta(thread_data &td, int16_t alpha, int16_t beta, int16_t depth,
                  int16_t ply) {

    if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
        return 0;
    }

    if (td.nodes % 1024 == 0 && td.tm->hit_hard_limit(td.gs->nodes_searched.load(std::memory_order_relaxed), std::chrono::steady_clock::now())) {
        td.gs->thread_stop.store(true, std::memory_order_relaxed);
        return 0;
    }

    td.count_node();
    depth = MAX(0, depth);
    bool is_pv = (node == PV_NODE);
    bool is_root = (ply == 0);

    if (ply >= MAX_PLY - 1) {
        return evaluate(td, ply);
    }

    int16_t static_eval = evaluate(td, ply);

    int16_t alpha_orig = alpha;
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
        probe->score = scoreFromTT(probe->score, ply);

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
    bool can_prune = !in_check && !is_root && !is_pv;

    if (can_prune) {

        // Null Move Pruning
        if (depth >= 3 && static_eval + depth * 100 > beta &&
            td.board.hasNonPawnMaterial(td.board.sideToMove())) {
            int reduction = 4.0 + depth / 3.0;

            // we don't need to update the accumulators
            // so we just copy them over to the next ply
            td.ss[ply + 1].accums[0] = td.ss[ply].accums[0];
            td.ss[ply + 1].accums[1] = td.ss[ply].accums[1];

            td.board.makeNullMove();
            int16_t score = -alphabeta<NORMAL>(td, -beta, -beta + 1,
                                               depth - 1 - reduction, ply + 1);
            td.board.unmakeNullMove();

            // If we need to abort
            if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
                return 0;
            }

            if (score >= beta) {
                return score;
            }
        }

        // Reverse Futility Pruning
        if (depth <= 3 && static_eval >= beta + 100 * depth) {
            return beta + (static_eval - beta) / 4;
        }
    }

    if (depth == 0) {
        return quiescence(td, alpha, beta, ply);
    }


    // Interal Iterative Reduction
    // if we don't have a hash move while being this deep, we should have found one by this point.
    // So reduce the depth since this is unlikely to be good move.
    if (depth >= 8 && (is_pv) && (tt_move == Move::NO_MOVE || (probe.has_value() && probe->depth + 4 < depth))) {
        depth -= 1;
    }


    int16_t best_score = INT16_MIN;
    size_t played = 0;
    ;
    move_select ms(td.board, tt_move, &td.history_heuristic, false,
                  td.killers[ply]);
    std::array<Move, 256> quietMoves;
    size_t quietCount = 0;

    while (true) {
        Move m = ms.next();
        if (ms.stage == ms_stage::Done)
            break;
        assert(m != Move::NO_MOVE);

        bool is_capture = td.board.isCapture(m);
        bool is_quiet = !is_capture && m.typeOf() != Move::PROMOTION;
        bool is_checking = td.board.givesCheck(m) != CheckType::NO_CHECK;

        if (is_quiet) {
            quietMoves[quietCount++] = m;
        }

        int reductions = 0;

        // Late Move Reductions
        if (played >= 3 && depth >= 3 && !in_check && !is_checking &&
            !is_pv && m != td.killers[ply][0] && m != td.killers[ply][1]) {
            if (is_quiet) {
                reductions +=
                    (int16_t)(0.8 + log(played) * log(depth) / 2.5);
            } else {
                reductions +=
                    (int16_t)(0.4 + log(played) * log(depth) / 3.5);
            }
        }

        int16_t reduced_depth = depth - reductions;

        // Pruning 
        // We should never prune while within a PV, or in check/being mated
        if (ply != 0 && !is_pv && !in_check && best_score > -CHECKMATE + MAX_PLY) {

            // Late Move Pruning
            if (reduced_depth < 8 && played >= 3 + 1.5 * depth * depth) {
                // skip the rest of the quiet moves
                ms.skip_quiets = true;
            }


            // Futility Pruning
            int16_t futility_margin = 80 + 100 * depth;
            // don't prune if the move gives check
            if (is_quiet && reduced_depth < 12 && static_eval + futility_margin <= alpha && !is_checking) {
                ms.skip_quiets = true;
            }
        }

         

        performMove(td, m, &td.ss[ply]);
        played++;

        int16_t score;
        if (played == 1) {
            score = -alphabeta<node>(td, -beta, -alpha, depth - 1, ply + 1);
        } else if (reductions != 0) {
            score = -alphabeta<NORMAL>(td, -alpha - 1, -alpha,
                                       reduced_depth - 1, ply + 1);

            if (score > alpha && reductions > 0) {
                score = -alphabeta<NORMAL>(td, -alpha - 1, -alpha, reduced_depth - 1,
                                           ply + 1);
            }

            if (score > alpha && score < beta) {
                score =
                    -alphabeta<PV_NODE>(td, -beta, -alpha, reduced_depth - 1, ply + 1);
            }

        } else {
            score =
                -alphabeta<NORMAL>(td, -alpha - 1, -alpha, reduced_depth - 1, ply + 1);
            if (score > alpha && score < beta) {
                score =
                    -alphabeta<PV_NODE>(td, -beta, -alpha, reduced_depth - 1, ply + 1);
            }
        }
        td.board.unmakeMove(m);

        
        // Early exit check
        if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
            return 0;
        }

        if (score > best_score) {
            best_score = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;

                td.pv_array[ply][0] = m;
                for (int j = 0; j < td.pv_lengths[ply + 1]; j++) {
                    td.pv_array[ply][j + 1] = td.pv_array[ply + 1][j];
                }
                td.pv_lengths[ply] = td.pv_lengths[ply + 1] + 1;

                if (alpha >= beta) {
                    if (is_quiet) {
                        if (td.killers[ply][0] != m) {
                            td.killers[ply][1] = td.killers[ply][0];
                            td.killers[ply][0] = m;
                        }

                        int16_t bonus = 300 * depth - 250;
                        UpdateHistoryHeuristic(td.history_heuristic,
                                               td.board.sideToMove(), m, bonus);

                        for (size_t i = 0; i < quietCount; i++) {
                            if (quietMoves[i] != m) {
                                UpdateHistoryHeuristic(td.history_heuristic,
                                                       td.board.sideToMove(),
                                                       quietMoves[i], -bonus);
                            }
                        }
                    }

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
        entry.best_move =
            best_move != Move::NO_MOVE && td.board.isLegal(best_move)
                ? best_move.move()
                : Move::NO_MOVE;
        entry.score = scoreToTT(best_score, ply);
        entry.bd =
            best_score >= beta
                ? TTable::LOWER
                : (best_score <= alpha_orig ? TTable::UPPER : TTable::EXACT);
        entry.key = td.board.hash();
        if (!td.gs->thread_stop.load(std::memory_order_relaxed)) {
            td.gs->tt.store(entry);
        }
    }

    return best_score;
}

int16_t quiescence(thread_data &td, int16_t alpha, int16_t beta, int16_t ply) {
    if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
        return 0;
    }

    if (td.nodes % 1024 == 0 && td.tm->hit_hard_limit(td.gs->nodes_searched.load(std::memory_order_relaxed), std::chrono::steady_clock::now())) {
        td.gs->thread_stop.store(true, std::memory_order_relaxed);
        return 0;
    }

    td.count_node();

    if (ply >= MAX_PLY - 1) {
        return evaluate(td, ply);
    }

    if (ply > td.seldepth) {
        td.seldepth = ply;
    }

    int16_t alpha_orig = alpha;

    std::optional<Move> tt_move = std::nullopt;
    Move best_move = Move::NO_MOVE;
    std::optional<TTable::Entry> probe = td.gs->tt.probe(td.board.hash());

    if (probe.has_value()) {
        probe->score = scoreFromTT(probe->score, ply);

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

    move_select ms(td.board, tt_move, nullptr, !incheck);
    int16_t played = 0;

    while (true) {
        Move m = ms.next();
        if (ms.stage == ms_stage::Done)
            break;
        assert(m != Move::NO_MOVE);
        played++;

        performMove(td, m, &td.ss[ply]);
        int16_t score = -quiescence(td, -beta, -alpha, ply + 1);
        td.board.unmakeMove(m);

        if (td.gs->thread_stop.load(std::memory_order_relaxed)) {
            return 0;
        }

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
    entry.score = scoreToTT(best_score, ply);
    entry.bd = best_score >= beta
                   ? TTable::LOWER
                   : (best_score <= alpha_orig ? TTable::UPPER : TTable::EXACT);
    entry.best_move =
        (entry.bd == TTable::UPPER || best_move == Move::NO_MOVE ||
         !td.board.isLegal(best_move))
            ? Move::NO_MOVE
            : best_move.move();
    entry.key = td.board.hash();
    if (!td.gs->thread_stop.load(std::memory_order_relaxed)) {
        td.gs->tt.store(entry);
    }

    return best_score;
}

int16_t evaluate(thread_data &td, int16_t ply) {

    int32_t score = 0;
    if (td.board.sideToMove() == Color::WHITE) {
        score = NNUE::evaluate(&td.ss[ply].accums[0], &td.ss[ply].accums[1],
                              td.board.occ().count());
    } else {
        score = NNUE::evaluate(&td.ss[ply].accums[1], &td.ss[ply].accums[0],
                              td.board.occ().count());
    }

    score = std::clamp(score, -CHECKMATE + MAX_PLY + 1, CHECKMATE - MAX_PLY - 1);

    return score;
}

void performMove(thread_data &td, Move move, sstack_data *ss) {

    Piece from = td.board.at(move.from());
    Piece to = td.board.at(move.to());
    Color stm = td.board.sideToMove();

    if (move.typeOf() == Move::CASTLING) {
        Square dest = stm == Color::WHITE ? Square::SQ_E1 : Square::SQ_E8;
        Square king_to = (move.from() > move.to()) ? dest - 2 : dest + 2;
        Square rook_to = (move.from() > move.to()) ? king_to + 1 : king_to - 1;

        NNUE::accum_add2_sub2_feats(
            ss->accums[0].values,
            (ss+1)->accums[0].values,
            calculate_nnue_index(Color::WHITE, rook_to, stm == Color::WHITE ? Piece::WHITEROOK : Piece::BLACKROOK),
            calculate_nnue_index(Color::WHITE, king_to, stm == Color::WHITE ? Piece::WHITEKING : Piece::BLACKKING),
            calculate_nnue_index(Color::WHITE, move.to(), stm == Color::WHITE ? Piece::WHITEROOK : Piece::BLACKROOK),
            calculate_nnue_index(Color::WHITE, move.from(), stm == Color::WHITE ? Piece::WHITEKING : Piece::BLACKKING)
        );

        NNUE::accum_add2_sub2_feats(
            ss->accums[1].values,
            (ss+1)->accums[1].values,
            calculate_nnue_index(Color::BLACK, rook_to, stm == Color::WHITE ? Piece::WHITEROOK : Piece::BLACKROOK),
            calculate_nnue_index(Color::BLACK, king_to, stm == Color::WHITE ? Piece::WHITEKING : Piece::BLACKKING),
            calculate_nnue_index(Color::BLACK, move.to(), stm == Color::WHITE ? Piece::WHITEROOK : Piece::BLACKROOK),
            calculate_nnue_index(Color::BLACK, move.from(), stm == Color::WHITE ? Piece::WHITEKING : Piece::BLACKKING)
        );
    } else if (move.typeOf() == Move::ENPASSANT) {

        NNUE::accum_add_sub2_feats(
            ss->accums[0].values,
            (ss+1)->accums[0].values,
            calculate_nnue_index(Color::WHITE, move.to(), from),
            calculate_nnue_index(Color::WHITE, move.to().ep_square(), stm == Color::WHITE ? Piece::BLACKPAWN : Piece::WHITEPAWN),
            calculate_nnue_index(Color::WHITE, move.from(), from)
        );

        NNUE::accum_add_sub2_feats(
            ss->accums[1].values,
            (ss+1)->accums[1].values,
            calculate_nnue_index(Color::BLACK, move.to(), from),
            calculate_nnue_index(Color::BLACK, move.to().ep_square(), stm == Color::WHITE ? Piece::BLACKPAWN : Piece::WHITEPAWN),
            calculate_nnue_index(Color::BLACK, move.from(), from)
        );

    } else {
        Piece moved_piece =
            move.typeOf() == Move::PROMOTION ? Piece(move.promotionType(), stm)
                                             : from;
        size_t white_add = calculate_nnue_index(Color::WHITE, move.to(), moved_piece);
        size_t white_remove = calculate_nnue_index(Color::WHITE, move.from(), from);
        size_t black_add = calculate_nnue_index(Color::BLACK, move.to(), moved_piece);
        size_t black_remove = calculate_nnue_index(Color::BLACK, move.from(), from);

        if (to != Piece::NONE) {
            NNUE::accum_add_sub2_feats(
                ss->accums[0].values,
                (ss+1)->accums[0].values,
                white_add,
                calculate_nnue_index(Color::WHITE, move.to(), to),
                white_remove
            );

            NNUE::accum_add_sub2_feats(
                ss->accums[1].values,
                (ss+1)->accums[1].values,
                black_add,
                calculate_nnue_index(Color::BLACK, move.to(), to),
                black_remove
            );
        } else {
            NNUE::accum_add_sub_feats(ss->accums[0].values,
                                      (ss + 1)->accums[0].values, white_add,
                                      white_remove);
            NNUE::accum_add_sub_feats(ss->accums[1].values,
                                      (ss + 1)->accums[1].values, black_add,
                                      black_remove);
        }
    }

    td.board.makeMove(move);
}
