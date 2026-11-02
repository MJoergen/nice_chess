/***************************************************************
 * game.c: the game, as the front ends see it.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

int engine_piece_at(int file, int rank)
{
    return pos.board[square(file, rank)];
}

int engine_side_to_move(void)
{
    return pos.side;
}

int engine_in_check(void)
{
    return is_attacked(king_of(pos.side), pos.side);
}

int engine_legal_moves(move_t *out)
{
    return gen_legal(out);
}

uint32_t engine_hash(void)
{
    return pos.hash;
}

uint32_t engine_hash_full(void)
{
    return board_compute_hash();
}

/***************************************************************
 * engine_make_move
 * Keeps room in the history for the plies of a search.
 ***************************************************************/
#define SEARCH_PLIES 64

int engine_make_move(move_t move)
{
    if (pos.ply >= MAX_HISTORY - SEARCH_PLIES)
        return -1;
    make_move(move);
    return 0;
}

int engine_undo(void)
{
    if (pos.ply == 0)
        return -1;
    undo_move();
    return 0;
}

/***************************************************************
 * engine_result
 ***************************************************************/
int engine_result(void)
{
    move_t *list = move_stack + move_sp;
    if (gen_legal(list) == 0)
        return engine_in_check() ? ENGINE_CHECKMATE : ENGINE_STALEMATE;

    if (pos.halfmove >= 100)
        return ENGINE_FIFTY;

    // The same position twice before, with the same side to move. Only
    // the plies since the last capture or pawn move can repeat, and only
    // those in the history.
    int back = pos.halfmove < pos.ply ? pos.halfmove : pos.ply;
    int seen = 0;
    for (int i = 2; i <= back; i += 2)
        if (hist[pos.ply - i].hash == pos.hash && ++seen == 2)
            return ENGINE_REPETITION;

    return ENGINE_PLAYING;
} // end of engine_result
