#pragma once

#include <chess.hpp>

#include "nnue.h"
#include "utils.h"
#include "moveselect.h"
#include "time_manager.h"

#include <chrono>
#include <cstdint>

#define MAX_PLY 128


enum nodetype {
    PV_NODE,
    NORMAL
};


struct sstack_data {
    Accumulator accums[2];
    int16_t static_eval;
};

struct thread_data {
    bool worker;
    history_heuristic_table history_heuristic;
    globalstate* gs;
    time_manager* tm;
    chess::Board board;
    int64_t nodes;
    int64_t pending_nodes;
    sstack_data ss[MAX_PLY];

    chess::Move final_pv[MAX_PLY]; // this is the final valid PV
    int16_t final_pv_length;
    chess::Move pv_array[MAX_PLY][MAX_PLY];
    int16_t pv_lengths[MAX_PLY];
    std::array<std::array<chess::Move, 2>, MAX_PLY> killers;

    int16_t seldepth;
    int16_t completed_depth;
    int16_t score;

    void count_node() {
        nodes++;
        pending_nodes++;
        if (pending_nodes >= 1024) {
            flush_nodes();
        }
    }

    void flush_nodes() {
        if (pending_nodes != 0) {
            gs->nodes_searched.fetch_add(pending_nodes,
                                         std::memory_order_relaxed);
            pending_nodes = 0;
        }
    }
};

void startSearch(globalstate* gs, search_params* params, bool suppress_output = false);
void search(std::reference_wrapper<thread_data> td, bool main, bool suppress_output = false);
void performMove(thread_data& td, chess::Move move, sstack_data* ss);
int16_t evaluate(thread_data& td, int16_t ply);

template<nodetype node>
int16_t alphabeta(thread_data& td, int16_t alpha, int16_t beta, int16_t depth, int16_t ply, bool cut_node, bool allow_null=true);
int16_t quiescence(thread_data& td, int16_t alpha, int16_t beta, int16_t ply);
