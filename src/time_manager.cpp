#include "time_manager.h"


time_manager::time_manager(const search_params& params, chess::Color us) {
    // from search params determine appropriate search limits
    if (params.max_nodes != 0) {
        if (params.softnodes) {                  
            ls.push_back(limits::softnodes{params.max_nodes, 100 * params.max_nodes});
        } else {
            ls.push_back(limits::nodes{params.max_nodes});
        }
    }
    if (params.movetime != 0) {
        ls.push_back(limits::time{params.movetime});
    }

    if (params.wtime != 0 || params.btime != 0 || params.winc != 0 || params.binc != 0) {
        tournament_limit_exists = true;
        if (us == chess::Color::WHITE) {
            ls.push_back(limits::tournament{
                params.wtime,
                params.btime,
                params.winc,
                params.binc,
                std::nullopt
            });
        } else {
            ls.push_back(limits::tournament{
                params.btime,
                params.wtime,
                params.binc,
                params.winc,
                std::nullopt
            });
        }
    }
    if (params.max_depth != 0) {
        depth_limit = params.max_depth;
    }

    last_move = std::nullopt;
    last_score = 0;
    last_move_stability = 0;

    start_time = std::chrono::steady_clock::now();
    compute_potential_bounds();
}

// this is silly but its the only way to get decent looking switch code
template<class... Ts> struct match : Ts... { using Ts::operator()...; };
template<class... Ts> match(Ts...) -> match<Ts...>;

bool time_manager::hit_hard_limit(uint64_t cur_nodes, std::chrono::steady_clock::time_point cur_time) const { 
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        cur_time - start_time
    ).count();

    for (const auto& limit : ls) {
        const bool hit = std::visit(match {
            [cur_nodes](const limits::nodes& l) {
                return cur_nodes >= l.nodes;
            },
            [cur_nodes](const limits::softnodes& l) {
                return cur_nodes >= l.hard_limit;
            },
            [elapsed, this](const limits::time& l) {
                return elapsed >= static_cast<int64_t>(l.millis);
            },
            [elapsed, this](const limits::tournament& l) {
                return elapsed >= this->hard_limit_time;
            },
        }, limit);

        if (hit) {
            return true;
        }
    }

    return false;
}

bool time_manager::hit_soft_limit(uint64_t cur_nodes, std::chrono::steady_clock::time_point cur_time) const {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        cur_time - start_time
    ).count();
    for (const auto& limit : ls) {
        const bool hit = std::visit(match {
            [cur_nodes](const limits::softnodes& l) {
                return cur_nodes >= l.soft_limit;
            },
            [elapsed, this](const limits::tournament& l) {
                return elapsed >= this->soft_limit_time;
            },
            [](const limits::nodes&) {
                return false;
            },
            [](const limits::time&) {
                return false;
            }
        }, limit);

        if (hit) {
            return true;
        }
    }

    return false;
}

void time_manager::compute_potential_bounds() {
    // if we have a tournament limit, set the hard and soft limits based on the tournament time

    if (!tournament_limit_exists) {
        return;
    }

    std::optional<limits::tournament> tournament_limit;
    for (const auto& limit : ls) {
        if (const auto* t = std::get_if<limits::tournament>(&limit)) {
            tournament_limit = *t;
            break;
        }
    }
    
    if (tournament_limit.has_value()) {
        hard_limit_time = (tournament_limit->our_time * 60) / 100;

        opt_limit_time = tournament_limit->our_time / 20 + tournament_limit->our_inc / 2;
        opt_limit_time = std::min(opt_limit_time, hard_limit_time);

        soft_limit_time = opt_limit_time;
    }
}

void time_manager::update_soft_time_limit(chess::Move best_move, int16_t best_score) {

    if(last_move.has_value()) {
        last_move_stability = (last_move.value() == best_move) ? last_move_stability + 1 : 0;
    } else {
        last_move_stability = 0;
    }

    const uint64_t stability = std::clamp(last_move_stability, 0ULL, 4ULL);

    // from viridithas
    const double STABILITY_MULTIPLIERS[5] = {2.50, 1.20, 0.90, 0.80, 0.75};
    const double stability_multiplier = STABILITY_MULTIPLIERS[stability];

    soft_limit_time = static_cast<uint64_t>(opt_limit_time * stability_multiplier);

    soft_limit_time = std::min(soft_limit_time, hard_limit_time);

    last_move = best_move;
    last_score = best_score;
}
