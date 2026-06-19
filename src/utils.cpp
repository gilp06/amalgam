
#include "utils.h"
#include "chess.hpp"

using namespace chess;
Bitboard BETWEEN_MASK[64][64];



void initLookup() {
    auto att = [](PieceType pt, Square sq, Bitboard occ) {
        return (pt == PieceType::BISHOP) ? attacks::bishop(sq, occ) : attacks::rook(sq, occ);
    };

    for (int sq1 = 0; sq1 < 64; ++sq1) {
        for (PieceType pt : {PieceType::BISHOP, PieceType::ROOK}) {
            for (int sq2 = 0; sq2 < 64; ++sq2) {
                if (att(pt, sq1, 0).check(sq2)) {
                    BETWEEN_MASK[sq1][sq2] =
                        att(pt, sq1, Bitboard::fromSquare(sq2)) & att(pt, sq2, Bitboard::fromSquare(sq1));
                }
                // BETWEEN_MASK[sq1][sq2].set(sq2);
            }
        }
    }
}

Bitboard attackers_to_square(const Board& board, Square square, Bitboard occupied) {
    const auto queens   = board.pieces(PieceType::QUEEN);

    auto atks = (attacks::pawn(Color::WHITE, square) & board.pieces(PieceType::PAWN, Color::WHITE));
    atks |= (attacks::pawn(Color::BLACK, square) & board.pieces(PieceType::PAWN, Color::BLACK));
    atks |= (attacks::knight(square) & board.pieces(PieceType::KNIGHT));
    atks |= (attacks::bishop(square, occupied) & (board.pieces(PieceType::BISHOP) | queens));
    atks |= (attacks::rook(square, occupied) & (board.pieces(PieceType::ROOK) | queens));
    atks |= (attacks::king(square) & board.pieces(PieceType::KING));

    return atks & occupied;
}


// generate pinners belonging to us that pin their pieces to their king, and return them as a bitboard
PinnersBlockersStruct fetchPinnersBlockers(const Board& board, Color side) {


    Bitboard us = board.us(side);
    Bitboard them = board.them(side);

    Square kingSq = board.kingSq(!side);

    Bitboard pinners = 0;
    Bitboard blockers = 0;

    Bitboard rq = board.pieces(PieceType::ROOK, side) | board.pieces(PieceType::QUEEN, side);
    Bitboard bq = board.pieces(PieceType::BISHOP, side) | board.pieces(PieceType::QUEEN, side);

    // check for rooks or queens on the same rank or file as the king, and if there is exactly one piece between them, then that piece is pinned
    Bitboard rq_candidates = rq & (attacks::rook(kingSq, 1ULL << kingSq.index()));
    while (rq_candidates) {
        Square sq = rq_candidates.pop();

        Bitboard between = BETWEEN_MASK[kingSq.index()][sq.index()];

        Bitboard our_blockers = between & us;
        Bitboard their_blockers = between & them;

        if(!our_blockers && their_blockers.count() == 1) {
            pinners |= (1ULL << sq.index());
            blockers |= their_blockers;
        }
    }

    Bitboard bq_candidates = bq & (attacks::queen(kingSq, 1ULL << kingSq.index()));
    while (bq_candidates) {
        Square sq = bq_candidates.pop();

        Bitboard between = BETWEEN_MASK[kingSq.index()][sq.index()];

        Bitboard our_blockers = between & us;
        Bitboard their_blockers = between & them;

        if(!our_blockers && their_blockers.count() == 1) {
            pinners |= (1ULL << sq.index());
            blockers |= their_blockers;
        }
    }

    return {pinners, blockers};
}
