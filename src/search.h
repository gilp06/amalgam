#pragma once

#include <chess.hpp>
#include "nnue.h"
#include "utils.h"
#include "moveselect.h"
#include <chrono>
#include <cstdint>

#define MAX_PLY 128


enum nodetype {
    PV_NODE,
    NULL_MOVE,
    NORMAL
};

struct search_params {
    uint64_t max_depth = 0;
    uint64_t max_nodes = 0;
    uint64_t wtime = 0;
    uint64_t btime = 0;
    uint64_t winc = 0;
    uint64_t binc = 0;
    uint64_t movetime = 0;
    uint64_t align = 0;
};

struct time_manager {
    std::chrono::steady_clock::time_point start;
    uint64_t max_search_time = 0;
    uint64_t max_nodes = 0;
    uint64_t max_depth = 0;
    uint64_t node_batch_size = 256;

    explicit time_manager(const search_params& params, chess::Color stm,
                         uint64_t thread_count = 1) {
        start = std::chrono::steady_clock::now();
        if (params.max_depth != 0)
            max_depth = params.max_depth;
        if (params.max_nodes != 0)
            max_nodes = params.max_nodes;
        if (max_nodes != 0) {
            uint64_t target_batch = max_nodes / (thread_count * 4);
            if (target_batch == 0)
                target_batch = 1;
            node_batch_size = 1;
            while (node_batch_size < 256 &&
                   node_batch_size * 2 <= target_batch) {
                node_batch_size *= 2;
            }
        }

        uint64_t time_left;
        uint64_t increment;

        if (stm == chess::Color::WHITE) {
            time_left = params.wtime;
            increment = params.winc;
        } else {
            time_left = params.btime;
            increment = params.binc;
        }

        if (params.movetime != 0) {
            max_search_time = params.movetime;
        } else if (time_left != 0) {
            max_search_time = (time_left / 20.0 + increment / 2.0);
        }
        if (max_search_time > 1) {
            max_search_time -= 1;
        }
    }
};

struct sstack_data {
    Accumulator accums[2];
};

struct thread_data {
    bool worker;
    history_heuristic_table history_heuristic;
    globalstate* gs;
    const time_manager* tm;
    chess::Board board;
    int64_t nodes;
    sstack_data ss[MAX_PLY];

    chess::Move final_pv[MAX_PLY]; // this is the final valid PV
    int16_t final_pv_length;
    chess::Move pv_array[MAX_PLY][MAX_PLY];
    int16_t pv_lengths[MAX_PLY];
    std::array<std::array<chess::Move, 2>, MAX_PLY> killers;

    int16_t seldepth;
    int16_t completed_depth;
    int16_t score;
};



bool checkStatus(thread_data& td);
bool countNode(thread_data& td);
void startSearch(globalstate* gs, search_params* params, bool suppress_output = false);
void search(std::reference_wrapper<thread_data> td, bool main, bool suppress_output = false);
void performMove(thread_data& td, chess::Move move, sstack_data* ss);
int16_t evaluate(thread_data& td, int16_t ply);

template<nodetype node>
int16_t alphabeta(thread_data& td, int16_t alpha, int16_t beta, int16_t depth, int16_t ply);
int16_t quiescence(thread_data& td, int16_t alpha, int16_t beta, int16_t ply);
