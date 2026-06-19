#pragma once

#include <chess.hpp>
#include "nnue.h"
#include "types.h"
#include <chrono>
#include <cstdint>

#define MAX_PLY 128


enum nodetype {
    PV_NODE,
    NORMAL
};

struct searchparams {
    uint64_t max_depth = 0;
    uint64_t max_nodes = 0;
    uint64_t wtime = 0;
    uint64_t btime = 0;
    uint64_t winc = 0;
    uint64_t binc = 0;
    uint64_t movetime = 0;
    uint64_t align = 0;
};

struct timemanager {
    std::chrono::steady_clock::time_point start;
    uint64_t max_search_time = 0;
    uint64_t max_nodes = 0;
    uint64_t max_depth = 0;

    explicit timemanager(const searchparams& params, chess::Color stm) {
        start = std::chrono::steady_clock::now();
        if (params.max_depth != 0)
            max_depth = params.max_depth;
        if (params.max_nodes != 0)
            max_nodes = params.max_nodes;

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

struct threaddata {
    bool worker;

    globalstate* gs;
    const timemanager* tm;
    chess::Board board;
    int64_t nodes;
    sstack_data ss[MAX_PLY];
    chess::Move pv[MAX_PLY][MAX_PLY];
    int16_t pv_lengths[MAX_PLY];

    chess::Move best_move, ponder;

    int16_t seldepth;
};



bool check_status(threaddata& td);
void start_search(globalstate* gs, searchparams* params);
void search(threaddata& td);
void perform_move(threaddata& td, chess::Move move, sstack_data* ss);
int16_t evaluate(threaddata& td, int16_t ply);

template<nodetype node>
int16_t alphabeta(threaddata& td, int16_t alpha, int16_t beta, int16_t depth, int16_t ply);
int16_t quiescence(threaddata& td, int16_t alpha, int16_t beta, int16_t ply);
