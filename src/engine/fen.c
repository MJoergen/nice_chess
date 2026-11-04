/***************************************************************
 * fen.c: FEN strings and moves in coordinate notation.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

static const char piece_chars[] = "kqrbnp.PNBRQK"; // By piece + 6

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

// Reads a number, and returns -1 if there is none.
static int read_number(const char **ps)
{
    const char *s = *ps;
    if (*s < '0' || *s > '9')
        return -1;
    int v = 0;
    while (*s >= '0' && *s <= '9' && v < 100000)
        v = v*10 + (*s++ - '0');
    *ps = s;
    return v;
}

// Reads a square such as "e3", and returns it, or 0 if there is none.
static int read_square(const char *s)
{
    unsigned f = (unsigned) (s[0] - 'a');
    unsigned r = (unsigned) (s[1] - '1');
    if (f >= 8 || r >= 8)
        return 0;
    return square(f, r);
}

/***************************************************************
 * engine_set_fen
 ***************************************************************/
int engine_set_fen(const char *fen, const char **end)
{
    position_t saved = pos;
    const char *s = skip_spaces(fen);

    board_clear();

    // The pieces, from a8 to h1
    int file = 0, rank = 7;
    while (1)
    {
        char c = *s++;
        if (c >= '1' && c <= '8')
        {
            file += c - '0';
            if (file > 8) goto error;
        }
        else if (c == '/')
        {
            if (file != 8 || rank == 0) goto error;
            file = 0;
            rank--;
        }
        else if (c == ' ')
        {
            if (file != 8 || rank != 0) goto error;
            break;
        }
        else
        {
            int p = 0;
            while (piece_chars[p] && piece_chars[p] != c)
                p++;
            if (!piece_chars[p] || p == 6 || file >= 8) goto error;
            p -= 6;
            if ((p == WK && pos.king[1]) || (p == BK && pos.king[0])) goto error;
            if ((p == WP || p == BP) && (rank == 0 || rank == 7)) goto error;
            if (pos.count[p > 0] == 16) goto error;
            board_put(square(file, rank), p);
            file++;
        }
    }
    if (!pos.king[0] || !pos.king[1]) goto error;

    // The side to move
    s = skip_spaces(s);
    if (*s == 'w') pos.side = 1;
    else if (*s == 'b') pos.side = -1;
    else goto error;
    s++;

    // Castling rights. Those that the pieces contradict are dropped.
    s = skip_spaces(s);
    if (*s == '-')
        s++;
    else
    {
        const char *start = s;
        while (1)
        {
            if      (*s == 'K') pos.castle |= CASTLE_WK;
            else if (*s == 'Q') pos.castle |= CASTLE_WQ;
            else if (*s == 'k') pos.castle |= CASTLE_BK;
            else if (*s == 'q') pos.castle |= CASTLE_BQ;
            else break;
            s++;
        }
        if (s == start) goto error;
    }
    if (pos.board[E1] != WK) pos.castle &= ~(CASTLE_WK | CASTLE_WQ);
    if (pos.board[E8] != BK) pos.castle &= ~(CASTLE_BK | CASTLE_BQ);
    if (pos.board[H1] != WR) pos.castle &= ~CASTLE_WK;
    if (pos.board[A1] != WR) pos.castle &= ~CASTLE_WQ;
    if (pos.board[H8] != BR) pos.castle &= ~CASTLE_BK;
    if (pos.board[A8] != BR) pos.castle &= ~CASTLE_BQ;

    // The en passant square. One that no pawn could have passed is dropped.
    s = skip_spaces(s);
    if (*s == '-')
        s++;
    else
    {
        int sq = read_square(s);
        if (!sq) goto error;
        s += 2;
        int behind = sq - N*pos.side; // The pawn that moved two
        if (rank_of(sq) == ((pos.side > 0) ? 5 : 2)
                && pos.board[behind] == -pos.side*WP
                && pos.board[sq] == EM && pos.board[sq + N*pos.side] == EM)
            pos.ep = sq;
    }

    // The half-move clock and the move number, both optional
    {
        const char *t = skip_spaces(s);
        int v = read_number(&t);
        if (v >= 0)
        {
            pos.halfmove = v;
            s = t;
            t = skip_spaces(s);
            v = read_number(&t);
            if (v >= 0)
            {
                pos.fullmove = (v > 0) ? v : 1;
                s = t;
            }
        }
    }

    // The side not to move may not be in check.
    if (is_attacked(king_of(-pos.side), -pos.side)) goto error;

    pos.hash = board_compute_hash();
    if (end)
        *end = s;
    return 0;

error:
    pos = saved;
    return -1;
} // end of engine_set_fen

/***************************************************************
 * engine_new_game
 ***************************************************************/
void engine_new_game(void)
{
    engine_set_fen(ENGINE_START_FEN, 0);
    tt_clear();
}

// Writes a number, and returns the end of it.
static char *write_number(char *p, int v)
{
    char tmp[12];
    int n = 0;
    do
    {
        tmp[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (n > 0)
        *p++ = tmp[--n];
    return p;
}

/***************************************************************
 * engine_get_fen
 ***************************************************************/
void engine_get_fen(char *buf)
{
    char *p = buf;
    for (int rank = 7; rank >= 0; --rank)
    {
        int empty = 0;
        for (int file = 0; file < 8; ++file)
        {
            int piece = pos.board[square(file, rank)];
            if (piece == EM)
            {
                empty++;
                continue;
            }
            if (empty)
                *p++ = (char) ('0' + empty);
            empty = 0;
            *p++ = piece_chars[piece + 6];
        }
        if (empty)
            *p++ = (char) ('0' + empty);
        if (rank)
            *p++ = '/';
    }

    *p++ = ' ';
    *p++ = (pos.side > 0) ? 'w' : 'b';
    *p++ = ' ';
    if (!pos.castle)
        *p++ = '-';
    if (pos.castle & CASTLE_WK) *p++ = 'K';
    if (pos.castle & CASTLE_WQ) *p++ = 'Q';
    if (pos.castle & CASTLE_BK) *p++ = 'k';
    if (pos.castle & CASTLE_BQ) *p++ = 'q';
    *p++ = ' ';
    if (pos.ep)
    {
        *p++ = (char) ('a' + file_of(pos.ep));
        *p++ = (char) ('1' + rank_of(pos.ep));
    }
    else
        *p++ = '-';
    *p++ = ' ';
    p = write_number(p, pos.halfmove);
    *p++ = ' ';
    p = write_number(p, pos.fullmove);
    *p = '\0';
} // end of engine_get_fen

/***************************************************************
 * engine_move_to_str
 ***************************************************************/
void engine_move_to_str(move_t move, char *buf)
{
    int from = move_from(move), to = move_to(move);
    buf[0] = (char) ('a' + file_of(from));
    buf[1] = (char) ('1' + rank_of(from));
    buf[2] = (char) ('a' + file_of(to));
    buf[3] = (char) ('1' + rank_of(to));
    buf[4] = '\0';
    int promoted = move_promoted(move);
    if (promoted != EM)
    {
        buf[4] = piece_chars[6 - (promoted > 0 ? promoted : -promoted)]; // Lower case
        buf[5] = '\0';
    }
} // end of engine_move_to_str

/***************************************************************
 * engine_parse_move
 ***************************************************************/
move_t engine_parse_move(const char *s, const char **end)
{
    s = skip_spaces(s);
    int from = read_square(s);
    if (!from) return 0;
    int to = read_square(s + 2);
    if (!to) return 0;
    s += 4;

    int wanted = WQ; // Promotion piece, when the move promotes
    switch (*s)
    {
        case 'q' : case 'Q' : wanted = WQ; s++; break;
        case 'r' : case 'R' : wanted = WR; s++; break;
        case 'b' : case 'B' : wanted = WB; s++; break;
        case 'n' : case 'N' : wanted = WN; s++; break;
        default  : break;
    }

    move_t *list = move_stack + move_sp; // Free space, as nothing recurses
    assert(move_stack_room());
    int n = gen_legal(list);
    for (int i = 0; i < n; ++i)
    {
        move_t m = list[i];
        int promoted = move_promoted(m);
        if (move_from(m) == from && move_to(m) == to
                && (promoted == EM || promoted*pos.side == wanted))
        {
            if (end)
                *end = s;
            return m;
        }
    }
    return 0;
} // end of engine_parse_move
