#pragma once

#include <chess.hpp>
#include <cstdint>
#include <chrono>

#include "nnue.h"


#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define CLAMP(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))

inline uint64_t perft(chess::Board& board, int depth) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if(depth == 1){
        return moves.size();
    }

    uint64_t nodes = 0;
    for (int i = 0; i < moves.size(); i++) {
        const auto move = moves[i];
        board.makeMove(move);
        nodes += perft(board, depth-1);
        board.unmakeMove(move);
    }

    return nodes;
}

inline uint64_t perft_top(chess::Board& board, int depth) {
    auto start = std::chrono::steady_clock::now();

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if(depth == 1){
        return moves.size();
    }

    uint64_t total_nodes = 0;
    for (int i = 0; i < moves.size(); i++) {
        const auto move = moves[i];
        uint64_t nodes = 0;

        board.makeMove(move);
        nodes = perft(board, depth-1);
        board.unmakeMove(move);
        total_nodes += nodes;

        std::cout << chess::uci::moveToUci(move) << ": " << nodes << std::endl;
    }

    std::cout << "nodes: " << total_nodes << std::endl;
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "nps: " << std::fixed << (total_nodes * 1000.0) / duration.count() << std::endl;

    return total_nodes;
}

inline int calculate_nnue_index(chess::Color perspective, chess::Square sq, chess::Piece piece) {
    int square = sq.index();
    int side = piece.color();
    if (perspective == chess::Color::BLACK) {
        side = 1 - side;
        square = square ^ 0b111000;
    }
    return side * 64 * 6 + piece.type() * 64 + square;
}

inline void populateAccums(Accumulator accums[2], const chess::Board& board) {

    for (int i = 0; i < 64; i++) {
        chess::Piece piece_at = board.at(i);
        if (piece_at == chess::Piece::NONE) continue;
        NNUE::accum_add_feat(calculate_nnue_index(chess::Color::WHITE, i, piece_at), accums[0]);
        NNUE::accum_add_feat(calculate_nnue_index(chess::Color::BLACK, i, piece_at), accums[1]);
    }
}

inline void nnue_add_piece(Accumulator accums[2], chess::Square sq, chess::Piece piece) {
    NNUE::accum_add_feat(calculate_nnue_index(chess::Color::WHITE, sq, piece), accums[0]);
    NNUE::accum_add_feat(calculate_nnue_index(chess::Color::BLACK, sq, piece), accums[1]);
};

inline void nnue_remove_piece(Accumulator accums[2], chess::Square sq, chess::Piece piece) {
    NNUE::accum_remove_feat(calculate_nnue_index(chess::Color::WHITE, sq, piece), accums[0]);
    NNUE::accum_remove_feat(calculate_nnue_index(chess::Color::BLACK, sq, piece), accums[1]);
};


struct PinnersBlockersStruct {
    chess::Bitboard our_pinners;
    chess::Bitboard their_blockers;
};

chess::Bitboard attackers_to_square(const chess::Board& board, chess::Square square, chess::Bitboard occupied);
PinnersBlockersStruct fetchPinnersBlockers(const chess::Board& board, chess::Color us);

extern chess::Bitboard BETWEEN_MASK[64][64];

void initLookup();
