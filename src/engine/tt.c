/***************************************************************
 * tt.c: the transposition table.
 *
 * The table keeps, for positions the search has seen, the best move
 * it found, its value, and the depth of that search. It is indexed by
 * the low TT_BITS bits of the Zobrist key, and an entry is two words:
 *
 *   key    the Zobrist key, but for its low 8 bits, which hold the
 *          depth (6 bits) and the kind of bound (2 bits, TT_ values).
 *          The low bits of the key are those of the index, so they
 *          need no check as long as TT_BITS is at least 8.
 *   data   bits  0-15: the move, short: from | to << 7 | promo << 14,
 *                      where promo is 0 to 3 for Q, R, B, N
 *          bits 16-31: the value
 *
 * A new entry always replaces the old one.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

#if TT_BITS < 8
#error "TT_BITS must be at least 8"
#endif

typedef struct
{
    uint32_t key;
    int32_t  data;
} tt_entry_t;

static tt_entry_t tt[1 << TT_BITS];

#define TT_MASK ((1u << TT_BITS) - 1)

void tt_clear(void)
{
    for (uint32_t i = 0; i <= TT_MASK; ++i)
    {
        tt[i].key  = 0;
        tt[i].data = 0;
    }
}

/***************************************************************
 * tt_short_move
 * The move in 16 bits, which tt_match compares with the moves of a
 * list.
 ***************************************************************/
static int tt_short_move(move_t m)
{
    int promo = 0;
    switch (move_promoted(m))
    {
        case WR: case BR: promo = 1; break;
        case WB: case BB: promo = 2; break;
        case WN: case BN: promo = 3; break;
        default: break;
    }
    return (m & 0x3FFF) | promo << 14;
}

// The values of a mate are stored as distances from the node, not
// from the root, so that they hold wherever the position comes up.
static int value_to_tt(int v, int ply)
{
    if (v >  ENGINE_MATE - ENGINE_MAX_PLY) return v + ply;
    if (v < -ENGINE_MATE + ENGINE_MAX_PLY) return v - ply;
    return v;
}

static int value_from_tt(int v, int ply)
{
    if (v >  ENGINE_MATE - ENGINE_MAX_PLY) return v - ply;
    if (v < -ENGINE_MATE + ENGINE_MAX_PLY) return v + ply;
    return v;
}

/***************************************************************
 * tt_store
 ***************************************************************/
void tt_store(int depth, int value, int bound, move_t move, int ply)
{
    tt_entry_t *e = &tt[pos.hash & TT_MASK];
    if (depth > 63)
        depth = 63;
    e->key  = (pos.hash & ~0xFFu) | (uint32_t) depth << 2 | (uint32_t) bound;
    e->data = (int32_t) ((uint32_t) value_to_tt(value, ply) << 16)
            | (move ? tt_short_move(move) : 0);
} // end of tt_store

/***************************************************************
 * tt_probe
 * Returns 1 if there is an entry for the position, and fills in its
 * depth, value, bound, and short move. Otherwise returns 0.
 ***************************************************************/
int tt_probe(int *depth, int *value, int *bound, int *move, int ply)
{
    const tt_entry_t *e = &tt[pos.hash & TT_MASK];
    if (e->key == 0 || ((e->key ^ pos.hash) & ~0xFFu) != 0)
        return 0;
    *depth = (e->key >> 2) & 63;
    *bound = e->key & 3;
    *value = value_from_tt(e->data >> 16, ply);
    *move  = e->data & 0xFFFF;
    return 1;
} // end of tt_probe

/***************************************************************
 * tt_match
 * The index in list of the move whose short form is short_move, or
 * -1 if there is none.
 ***************************************************************/
int tt_match(const move_t *list, int n, int short_move)
{
    if (short_move == 0)
        return -1;
    for (int i = 0; i < n; ++i)
        if (tt_short_move(list[i]) == short_move)
            return i;
    return -1;
} // end of tt_match
