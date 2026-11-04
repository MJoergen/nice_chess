/***************************************************************
 * book.c: the opening book.
 *
 * The book is a table that tools/mkbook.c makes from
 * src/engine/book.txt, sorted by the Zobrist key: two words an entry,
 * the key of a position, and a move from it with its weight, as
 *   bits 0..5   the square the move goes from (0 is a1)
 *   bits 6..11  the square it goes to
 *   bits 12..14 the piece of a promotion: 0 none, 1 q, 2 r, 3 b, 4 n
 *   bits 16..31 the weight, the number of lines that play the move
 * A move of the book is played only if it is legal, so a key that two
 * positions share cannot make the engine play an illegal move.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "engine.h"

extern const uint32_t book_table[];
extern const int book_size;

static uint32_t rng = 1;

void engine_book_seed(uint32_t seed)
{
    rng = seed ? seed : 1;
}

// Whether a legal move is the one an entry names.
static int same_move(move_t m, uint32_t e)
{
    if (engine_move_from(m) != (int) (e & 63) || engine_move_to(m) != (int) (e >> 6 & 63))
        return 0;
    char buf[8];
    engine_move_to_str(m, buf);
    static const char promo[] = "\0qrbn";
    return buf[4] == promo[e >> 12 & 7];
}

int engine_book_moves(move_t *moves, uint32_t *weights)
{
    uint32_t key = engine_hash();
    int lo = 0, hi = book_size;
    while (lo < hi) // The first entry whose key is not less than key
    {
        int mid = (lo + hi) / 2;
        if (book_table[2 * mid] < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == book_size || book_table[2 * lo] != key)
        return 0;

    move_t legal[ENGINE_MAX_MOVES];
    int n_legal = engine_legal_moves(legal);
    int n = 0;
    for (int i = lo; i < book_size && book_table[2 * i] == key; ++i)
    {
        uint32_t e = book_table[2 * i + 1];
        for (int j = 0; j < n_legal; ++j)
            if (same_move(legal[j], e))
            {
                moves[n] = legal[j];
                weights[n++] = e >> 16;
                break;
            }
    }
    return n;
}

move_t engine_book_move(void)
{
    move_t moves[ENGINE_MAX_MOVES];
    uint32_t weights[ENGINE_MAX_MOVES];
    int n = engine_book_moves(moves, weights);
    uint32_t total = 0;
    for (int i = 0; i < n; ++i)
        total += weights[i];
    if (!total)
        return 0;

    rng ^= rng << 13; // xorshift32
    rng ^= rng >> 17;
    rng ^= rng << 5;
    uint32_t r = rng % total;
    int i = 0;
    while (r >= weights[i])
        r -= weights[i++];
    return moves[i];
}
