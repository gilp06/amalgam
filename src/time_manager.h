#pragma once

#include "utils.h"

#include <optional>
#include <variant>

// this is passed from UCI
struct search_params {
    uint64_t max_depth = 0;
    uint64_t max_nodes = 0;
    uint64_t wtime = 0;
    uint64_t btime = 0;
    uint64_t winc = 0;
    uint64_t binc = 0;
    uint64_t movetime = 0;

    bool softnodes = false;
};

// these all manage different types of search limits
// I don't include depth as that should be handled separately
namespace limits {
    struct tournament {
        uint64_t our_time;
        uint64_t opponent_time;
        uint64_t our_inc;
        uint64_t opponent_inc;

        std::optional<uint64_t> moves_to_go;
    };

    struct nodes {
        uint64_t nodes;
    };

    struct softnodes {
        uint64_t soft_limit;
        uint64_t hard_limit;
    };

    struct time {
        uint64_t millis;
    };
}

using search_limit = std::variant<
    limits::tournament,
    limits::nodes,
    limits::softnodes,
    limits::time
>;

class time_manager {
private:
    std::vector<search_limit> ls;
    
    bool tournament_limit_exists = false;

    // For tournament time limits
    uint64_t hard_limit_time = 0; // Max time we should ever spend on a move
    uint64_t opt_limit_time = 0; // optimial time we should spend
    uint64_t soft_limit_time = 0; // time we should aim to stay under (can vary)

    std::optional<chess::Move> last_move;
    int16_t last_score;
    uint64_t last_move_stability;

    

public:
    std::optional<uint64_t> depth_limit = std::nullopt;
    std::chrono::steady_clock::time_point start_time;
    time_manager(const search_params& params, chess::Color us);

    bool hit_hard_limit(uint64_t cur_nodes, std::chrono::steady_clock::time_point cur_time) const; // check mid-depth
    bool hit_soft_limit(uint64_t cur_nodes, std::chrono::steady_clock::time_point cur_time) const; // check every time we complete a depth

    void compute_potential_bounds();
    void update_soft_time_limit(chess::Move best_move, int16_t best_score);
};










