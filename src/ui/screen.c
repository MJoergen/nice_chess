/***************************************************************
 * screen.c: the windowed front end, on the 80x25 character screen.
 *
 * The board takes the left half of the screen: 8x8 squares of 5
 * columns and 3 rows each, with the ranks in column 0 and the files
 * in row 24. The right half has the players and their clocks, the
 * state of the game,
 * what the engine is thinking, the moves, the move being typed, and
 * the keys.
 *
 * The keys:
 *   Arrows          move the cursor on the board
 *   Enter, Space    pick up the piece under the cursor, or put it
 *                   down on a square it can go to
 *   e2e4, e7e8n     type a move, then Enter. Backspace erases.
 *   Esc             drop the piece or the typed move; while the
 *                   engine thinks, make it move now
 *   U               take back your last move (and the engine's reply)
 *   S               switch sides: the engine plays the side to move
 *   R               rotate the board
 *   N               a new game
 *   + -             with no time control, make the engine stronger
 *                   or weaker, levels 1 to 5, by its time and noise;
 *                   under a time control, strengths 1 to 5
 *   T               the next time control, and a new game
 *   O               the opening book on or off (on at first)
 *   P               pondering on or off (on at first)
 *   Q               quit, and clear the screen
 * The letters of commands act only when no move is being typed.
 *
 * The pieces are glyphs of 3x3 characters, drawn by tools/mkpieces.py
 * (pieces.h): a white piece as its outline, and a black piece filled,
 * in one color on the square's. The program copies the character ROM
 * into the font RAM, with Spleen's letters, writes the pieces over the
 * codes 0x80 to 0xEB, and shows the font RAM (nice_cpu's SYSTEM.md
 * §1.8). On Q it shows the ROM again.
 *
 * Each move is also written to the console, as "move e2e4", the end of
 * the game as "result ...", and the position at the end as "fen ...".
 * The program ends on Q, or at the end of the console's input, which
 * leaves the screen as it is.
 *
 * T steps through the time controls: none, Blitz 3+2, Rapid 15+10,
 * and Classical 90+30, and starts a new game. With none, each side's
 * clock counts up the time it has taken, and the level sets the time
 * of the engine's move and the noise in its evaluation
 * (engine_set_noise), none at level 5. With a time control, each
 * clock counts down from the base time and gains the increment after
 * each of its side's moves; a side whose clock reaches 0 loses,
 * whatever the material. The engine takes a share of the time on its
 * clock (engine_time_share), at its strength (engine_set_strength),
 * 1, the weakest, at first. A new game has a new seed of the noise,
 * of the level or of the strength, the same in niceemu each time. U
 * takes back moves, not time. N sets the clocks as at the start. In niceemu the clocks count steps, at 54
 * million a second, the MEGA65's rate, so the engine's time there is
 * what it would take on the board.
 *
 * The engine polls the keyboard during its search on a board, and in
 * niceemu only once a read of the console has found no byte waiting:
 * that is, once the input is a keyboard. Input from a file (niceemu
 * -i, in the tests) is then read only between moves, so a run is the
 * same every time.
 *
 * Pondering: after its move, the engine makes the move it expects of
 * the player, the second of its principal variation, and searches the
 * position after it, on the player's time. The search reads a key
 * every 1024 nodes, from a keyboard or a file alike, so a run from a
 * file is still the same every time. The keys that need the engine's
 * board (U, S, N, T, Q, the end of the input) stop it; they then wait
 * in ahead, and the expected move is taken back. So does a move of
 * the player other than the expected one, which is then played. The
 * expected move is a hit: it is played on the screen only, and the
 * search goes on as the engine's, with its time from then. Meanwhile
 * the screen draws the position, and checks the player's moves, from
 * view, a copy taken before the search. There is no pondering after a
 * move of the book, when the principal variation has no second move,
 * or when the expected move ends the game, and none with P off.
 *
 * This front end is for NICE only. It does not use printf.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdint.h>
#include <nice.h>
#include "../engine/engine.h"
#include "../platform/platform.h"
#include "pieces.h"
#include "version.h"

/***************************************************************
 * Colors: a screen word has the background in bits 31..20, the
 * foreground in bits 19..8, and the character in bits 7..0.
 ***************************************************************/
#define COLOR(bg, fg) ((uint32_t) (bg) << 20 | (uint32_t) (fg) << 8)

enum
{
    SQ_LIGHT    = 0xCA7, SQ_DARK    = 0x863,  // The squares
    LAST_LIGHT  = 0xDC5, LAST_DARK  = 0xAA3,  // The last move
    SEL_LIGHT   = 0x7C7, SEL_DARK   = 0x5A5,  // The piece picked up
    TGT_LIGHT   = 0x9BD, TGT_DARK   = 0x69B,  // Where it can go
    INK         = 0x111,                      // The pieces
    CHECK_LIGHT = 0xE76, CHECK_DARK = 0xC54,  // A king in check
    MARK        = 0xF11,                      // The cursor's brackets
    PANEL       = 0x123,                      // The background of the rest
    TEXT        = 0xCCC, BRIGHT = 0xFFF, DIM = 0x89A, WARN = 0xFA4
};

// The first column of the right half.
#define PX 42

// The rows of the right half.
enum
{
    ROW_TITLE = 0, ROW_PLAYERS = 1,           // Players: white, then black
    ROW_STATUS = 3, ROW_LEVEL = 4, ROW_PONDER = 5,
    ROW_INFO = 6, ROW_PV = 7,                 // PV: two rows
    ROW_MOVES = 10, MOVE_ROWS = 9,            // Two columns of 9 moves
    ROW_INPUT = 20, ROW_MSG = 21, ROW_HELP = 22 // Help: three rows
};

/***************************************************************
 * The keys
 ***************************************************************/
enum
{
    K_END = -1, K_NONE = -2,
    K_ENTER = 13, K_ESC = 27, K_BS = 127,
    K_UP = 256, K_DOWN, K_RIGHT, K_LEFT, K_OTHER
};

/***************************************************************
 * The state of the front end
 ***************************************************************/
#define MAX_PLAYED 512

static move_t played[MAX_PLAYED];  // The moves of the game
static int    n_played;
static int    human;               // The side the player has: 1 white, -1 black
static int    flipped;             // Black at the bottom
static int    cur_file, cur_rank;  // The cursor
static int    selected = -1;       // The square of the piece picked up, or -1
static int32_t targets[64];        // The squares it can go to
static char   typed[6];            // The move being typed
static int    n_typed;
static const char *message = "";   // A line for the last thing that went wrong
static char   msg_buf[24];

// The levels: the time of a move, as a share of platform_move_time,
// and the noise in the evaluation, in centipawns (LOG.md, 2026-11-11).
// tools/rate.sh, tools/screen_uci.py, and README.md have copies of the
// table, which tools/test_levels.py compares with it.
static const uint8_t level_num[5] = {1, 1, 1, 3, 9};
static const uint8_t level_den[5] = {9, 3, 1, 1, 1};
static const int16_t level_noise[5] = {800, 600, 320, 170, 0};
static int    level = 1;
static int    book = 1;            // The engine plays from its opening book

// The time controls: a base time and an increment, in seconds. The
// first is none: the clocks count up, and the levels apply.
static const struct
{
    const char *name;
    uint32_t    base, inc;
} controls[] =
{
    {"",                   0,  0},
    {"Blitz 3+2",        180,  2},
    {"Rapid 15+10",      900, 10},
    {"Classical 90+30", 5400, 30},
};
#define N_CONTROLS ((int) (sizeof controls / sizeof controls[0]))
static int    control;             // The time control: an index of controls
static int    strength = 1;       // Under a time control
static uint32_t game_seed;         // Of the noise of a level or a strength

// The result of a game lost on time, beside the engine's results.
enum { RESULT_TIME = 16 };

// The position the screen shows, and its legal moves. While the engine
// ponders, its own board is in the search, so what is drawn, and the
// player's moves, come from here. snapshot takes it from the engine.
// play, new_game, and undo call it when they change the position, and
// a ponder hit takes view_next, so the main loop does not.
typedef struct
{
    int    piece[64];
    int    side;                   // 1 if white is to move, -1 if black
    int    in_check;
    int    result;                 // engine_result
    int    n_moves;
    move_t moves[ENGINE_MAX_MOVES];
} view_t;
static view_t view;
static view_t view_next;           // After the move the engine expects

static void snapshot(view_t *v)
{
    for (int sq = 0; sq < 64; ++sq)
        v->piece[sq] = engine_piece_at(sq % 8, sq / 8);
    v->side = engine_side_to_move();
    v->in_check = engine_in_check();
    v->result = engine_result();
    v->n_moves = engine_legal_moves(v->moves);
}

// The legal move of the view that s, such as "e2e4" or "e7e8n", names,
// or 0. A promotion with no piece given is to a queen.
static move_t view_move(const char *s)
{
    int len = 0;
    while (s[len])
        ++len;
    for (int i = 0; i < view.n_moves; ++i)
    {
        char m[8];
        engine_move_to_str(view.moves[i], m);
        int j = 0;
        while (j < len && m[j] == s[j])
            ++j;
        if (j == len && (m[j] == '\0' || (len == 4 && m[4] == 'q')))
            return view.moves[i];
    }
    return 0;
}

// Pondering: the engine searches the position after the move it
// expects of the player, while the player thinks (see the top).
static int    ponder_on = 1;       // P turns it off
static move_t expected;            // The player's move the engine expects, or 0
static int    pondering;           // The search runs on the player's time
static move_t ponder_miss;         // The player's move, if not the expected one
static int    ponder_stop;         // A key that needs the engine's board
static engine_info_t ponder_info;  // Of the last iteration of the ponder
static int    have_ponder_info;

// What the engine thought about its last move.
static engine_info_t info;
static int    have_info;           // 1 if info is set, 2 if the move was the book's
static int    thinking;

// The clocks: for white [0] and black [1], the seconds taken, and the
// counts of platform_micros beyond them.
static uint32_t clock_secs[2], clock_rest[2];
static uint32_t clock_last;        // platform_micros at the last tick
static int    clock_side;          // The side the clock runs for: 0 white, 1 black
static int    clock_on;            // The game goes on, and so does the clock
static uint32_t clock_bonus[2];    // The seconds of increment each side has gained
static int    flagged;             // 1 + the side whose time ran out, or 0

static void log_result(void);

/***************************************************************
 * Writing to the screen
 ***************************************************************/
static void put(int col, int row, uint32_t color, int c)
{
    nice_screen_put(col, row, (int) (color | (uint8_t) c));
}

// Writes s at col, row, and returns the column after it.
static int put_str(int col, int row, uint32_t color, const char *s)
{
    while (*s && col < 80)
        put(col++, row, color, *s++);
    return col;
}

// Fills a row of the right half from col to its end with blanks.
static void clear_to_end(int col, int row)
{
    while (col < 80)
        put(col++, row, COLOR(PANEL, TEXT), ' ');
}

// Writes the decimal digits of n to buf, and returns the end.
static char *fmt_uint(char *buf, uint32_t n)
{
    char tmp[10];
    int i = 0;
    do
        tmp[i++] = (char) ('0' + n % 10);
    while ((n /= 10) != 0);
    while (i > 0)
        *buf++ = tmp[--i];
    *buf = '\0';
    return buf;
}

static char *fmt_str(char *buf, const char *s)
{
    while (*s)
        *buf++ = *s++;
    *buf = '\0';
    return buf;
}

static void set_cursor(int show, int col, int row)
{
    nice_poke(0xFFFFFF81u, (unsigned) col);
    nice_poke(0xFFFFFF82u, (unsigned) row);
    nice_poke(0xFFFFFF80u, show ? 0x60u : 0u); // Shown, blinking
}

/***************************************************************
 * The font
 ***************************************************************/
#define FONT_RAM  0xFFFE0000u
#define FONT_ROM  0xFFFE2000u
#define FONT_BASE 0xFFFFFF86u
#define GLYPH_PIECES 0x80          // The code of the first glyph of a piece

// The ROM's glyphs, and the pieces over them from GLYPH_PIECES on, into
// the font RAM, and the RAM shown.
static void font_init(void)
{
    for (unsigned i = 0; i < 256 * 32; ++i)
        nice_poke(FONT_RAM + i, nice_peek(FONT_ROM + i));
    for (unsigned g = 0; g < 2 * 6 * 9; ++g)
        for (unsigned r = 0; r < 32; ++r)
            nice_poke(FONT_RAM + 32 * (GLYPH_PIECES + g) + r, piece_glyphs[g][r]);
    nice_poke(FONT_BASE, FONT_RAM);
}

/***************************************************************
 * The board
 ***************************************************************/
static int sq_index(int file, int rank) { return file + 8 * rank; }

// The screen column and row of the top left cell of a square.
static int sq_col(int file) { return 1 + 5 * (flipped ? 7 - file : file); }
static int sq_row(int rank) { return 3 * (flipped ? rank : 7 - rank); }

static void draw_square(int file, int rank)
{
    int sq = sq_index(file, rank);
    int light = (file + rank) & 1;
    int bg = light ? SQ_LIGHT : SQ_DARK;
    if (n_played)
    {
        move_t last = played[n_played - 1];
        if (sq == engine_move_from(last) || sq == engine_move_to(last))
            bg = light ? LAST_LIGHT : LAST_DARK;
    }
    int piece = view.piece[sq];
    if ((piece == WK || piece == BK) && piece * view.side > 0 && view.in_check)
        bg = light ? CHECK_LIGHT : CHECK_DARK;
    if (sq == selected)
        bg = light ? SEL_LIGHT : SEL_DARK;
    else if (targets[sq])
        bg = light ? TGT_LIGHT : TGT_DARK;

    int col = sq_col(file), row = sq_row(rank);
    uint32_t blank = COLOR(bg, 0);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 5; ++c)
            put(col + c, row + r, blank, ' ');

    // A piece is 3x3 glyphs, in the middle columns of the square.
    if (piece != EM)
    {
        int g = GLYPH_PIECES + 9 * (piece > 0 ? piece - 1 : 5 - piece);
        uint32_t ink = COLOR(bg, INK);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                put(col + 1 + c, row + r, ink, g++);
    }
    if (file == cur_file && rank == cur_rank && !thinking)
    {
        put(col, row + 1, COLOR(bg, MARK), '[');
        put(col + 4, row + 1, COLOR(bg, MARK), ']');
    }
}

static void draw_board(void)
{
    uint32_t label = COLOR(PANEL, DIM);
    for (int rank = 0; rank < 8; ++rank)
    {
        int row = sq_row(rank);
        put(0, row, label, ' ');
        put(0, row + 1, label, '1' + rank);
        put(0, row + 2, label, ' ');
        for (int file = 0; file < 8; ++file)
            draw_square(file, rank);
    }
    for (int col = 0; col <= 40; ++col)
        put(col, 24, label, ' ');
    for (int file = 0; file < 8; ++file)
        put(sq_col(file) + 2, 24, label, 'a' + file);
    put(41, 24, label, ' ');
    for (int row = 0; row < 24; ++row)
        put(41, row, label, ' ');
}

/***************************************************************
 * The right half
 ***************************************************************/
static void draw_line(int row, uint32_t color, const char *s)
{
    clear_to_end(put_str(PX, row, color, s), row);
}

// The seconds on a side's clock under a time control, from which its
// time taken counts.
static uint32_t clock_limit(int side)
{
    return controls[control].base + clock_bonus[side];
}

// The game's result: the engine's, or RESULT_TIME.
static int game_result(void)
{
    return flagged ? RESULT_TIME : view.result;
}

// A player and their clock: "White  you          0:12", the side whose
// clock runs bright. The clock shows the time taken, or under a time
// control the whole seconds left.
static void draw_player(int side)
{
    char buf[40];
    char *p = fmt_str(buf, side ? "Black  " : "White  ");
    p = fmt_str(p, (human > 0) == !side ? "you" : "nice_chess");
    while (p < buf + 20)
        *p++ = ' ';
    uint32_t s = clock_secs[side];
    if (control)
    {
        uint32_t limit = clock_limit(side);
        s = s >= limit ? 0 : limit - s - (clock_rest[side] != 0);
    }
    if (s >= 3600)
    {
        p = fmt_uint(p, s / 3600);
        *p++ = ':';
        *p++ = (char) ('0' + s / 600 % 6);
        *p++ = (char) ('0' + s / 60 % 10);
    }
    else
        p = fmt_uint(p, s / 60);
    *p++ = ':';
    *p++ = (char) ('0' + s / 10 % 6);
    *p++ = (char) ('0' + s % 10);
    *p = '\0';
    draw_line(ROW_PLAYERS + side, COLOR(PANEL, clock_on && side == clock_side ? BRIGHT : TEXT), buf);
}

static void draw_players(void)
{
    draw_player(0);
    draw_player(1);
}

// Charges the time since the last tick to the side the clock runs
// for, and draws its clock when a second has passed. Under a time
// control, a side whose time has run out loses. The engine's search
// calls it too, so it does not ask the engine whose move it is.
static void clock_tick(void)
{
    uint32_t now = platform_micros();
    uint32_t d = now - clock_last;
    clock_last = now;
    if (!clock_on)
        return;
    uint32_t second = platform_clock_second();
    uint32_t rest = clock_rest[clock_side] + d;
    if (rest >= second)
    {
        clock_secs[clock_side] += rest / second;
        rest %= second;
        if (control && clock_secs[clock_side] >= clock_limit(clock_side))
        {
            clock_secs[clock_side] = clock_limit(clock_side);
            rest = 0;
            flagged = 1 + clock_side;
            clock_on = 0;
            log_result();
        }
        draw_player(clock_side);
    }
    clock_rest[clock_side] = rest;
}

// Charges the clock, then runs it for the side to move, if the game
// goes on. Called after each change to the game.
static void clock_switch(void)
{
    clock_tick();
    clock_side = view.side < 0;
    clock_on = game_result() == ENGINE_PLAYING;
}

static const char *status_text(int result)
{
    int white = view.side > 0;
    switch (result)
    {
        case ENGINE_CHECKMATE:  return white ? "Checkmate. Black wins." : "Checkmate. White wins.";
        case ENGINE_STALEMATE:  return "Stalemate. A draw.";
        case ENGINE_FIFTY:      return "A draw by the 50-move rule.";
        case ENGINE_REPETITION: return "A draw by repetition.";
        case RESULT_TIME:       return flagged == 1 ? "White's time is up. Black wins."
                                                    : "Black's time is up. White wins.";
        default: break;
    }
    if (thinking)
        return "Thinking... Esc: move now";
    if (view.in_check)
        return white ? "White to move, in check" : "Black to move, in check";
    return white ? "White to move" : "Black to move";
}

static void draw_status(void)
{
    int result = game_result();
    draw_line(ROW_STATUS, COLOR(PANEL, result != ENGINE_PLAYING || thinking ? WARN : BRIGHT),
              status_text(result));
}

static void draw_ponder(void)
{
    draw_line(ROW_PONDER, COLOR(PANEL, DIM), !ponder_on ? "Ponder off"
              : pondering ? "Ponder on: thinking on your time" : "Ponder on");
}

static void draw_level(void)
{
    char buf[48];
    char *p = control ? fmt_uint(fmt_str(fmt_str(buf, controls[control].name), ", strength "),
                                 (uint32_t) strength)
                      : fmt_str(fmt_uint(fmt_str(buf, "Level "), (uint32_t) level), " of 5");
    fmt_str(p, book ? ", book on" : ", book off");
    draw_line(ROW_LEVEL, COLOR(PANEL, DIM), buf);
}

// The score, for the engine, as +1.25, or "mate 3", or "mated 2".
static char *fmt_score(char *p, int score)
{
    if (score > ENGINE_MATE - ENGINE_MAX_PLY)
        return fmt_uint(fmt_str(p, "mate "), (uint32_t) (ENGINE_MATE - score + 1) / 2);
    if (score < -ENGINE_MATE + ENGINE_MAX_PLY)
        return fmt_uint(fmt_str(p, "mated "), (uint32_t) (ENGINE_MATE + score) / 2);
    *p++ = score < 0 ? '-' : '+';
    uint32_t a = (uint32_t) (score < 0 ? -score : score);
    p = fmt_uint(p, a / 100);
    *p++ = '.';
    *p++ = (char) ('0' + a / 10 % 10);
    *p++ = (char) ('0' + a % 10);
    *p = '\0';
    return p;
}

static void draw_info(void)
{
    char buf[48];
    char *p = buf;
    *p = '\0';
    if (have_info == 2)
        fmt_str(p, "A move of the book.");
    if (have_info == 1)
    {
        p = fmt_uint(fmt_str(p, "Depth "), (uint32_t) info.depth);
        p = fmt_score(fmt_str(p, "  Score "), info.score);
        fmt_uint(fmt_str(p, "  Nodes "), info.nodes);
    }
    draw_line(ROW_INFO, COLOR(PANEL, TEXT), buf);

    // The principal variation, in as many moves as fit on two rows.
    int col = PX, row = ROW_PV;
    if (have_info == 1)
        col = put_str(col, row, COLOR(PANEL, DIM), "PV");
    for (int i = 0; have_info == 1 && i < info.pv_len; ++i)
    {
        char m[8];
        engine_move_to_str(info.pv[i], m);
        int len = 1;
        while (m[len - 1])
            ++len; // The move and a space
        if (col + len > 80)
        {
            clear_to_end(col, row);
            if (++row > ROW_PV + 1)
                break;
            col = PX + 2;
        }
        col = put_str(put_str(col, row, COLOR(PANEL, DIM), " "), row, COLOR(PANEL, DIM), m);
    }
    for (; row <= ROW_PV + 1; ++row, col = PX)
        clear_to_end(col, row);
}

// Two columns of moves: "12. e2e4  e7e5". The last ones that fit.
static void draw_moves(void)
{
    draw_line(ROW_MOVES - 1, COLOR(PANEL, DIM), "Moves");
    int full = (n_played + 1) / 2;               // Full moves, the last maybe half
    int first = full > 2 * MOVE_ROWS ? full - 2 * MOVE_ROWS : 0;
    for (int row = 0; row < MOVE_ROWS; ++row)
        clear_to_end(PX, ROW_MOVES + row);
    for (int k = first; k < full; ++k)
    {
        int slot = k - first;
        int col = PX + (slot / MOVE_ROWS) * 19;
        int row = ROW_MOVES + slot % MOVE_ROWS;
        char buf[20];
        char *p = fmt_uint(buf, (uint32_t) k + 1);
        p = fmt_str(p, ". ");
        int c = col + (k + 1 < 10 ? 2 : k + 1 < 100 ? 1 : 0);
        c = put_str(c, row, COLOR(PANEL, DIM), buf);
        for (int h = 0; h < 2 && 2 * k + h < n_played; ++h)
        {
            char m[8];
            engine_move_to_str(played[2 * k + h], m);
            put_str(c + 6 * h, row, COLOR(PANEL, TEXT), m);
        }
    }
}

static void draw_input(void)
{
    int col = put_str(PX, ROW_INPUT, COLOR(PANEL, DIM), "Your move: ");
    typed[n_typed] = '\0';
    int end = put_str(col, ROW_INPUT, COLOR(PANEL, BRIGHT), typed);
    clear_to_end(end, ROW_INPUT);
    set_cursor(!thinking, end, ROW_INPUT);
    draw_line(ROW_MSG, COLOR(PANEL, WARN), message);
}

static void draw_all(void)
{
    draw_board();
    int col = put_str(PX, ROW_TITLE, COLOR(PANEL, BRIGHT), "nice_chess ");
    clear_to_end(put_str(col, ROW_TITLE, COLOR(PANEL, BRIGHT), nice_chess_version), ROW_TITLE);
    draw_players();
    draw_status();
    draw_level();
    draw_ponder();
    draw_info();
    draw_moves();
    draw_line(ROW_INPUT - 1, COLOR(PANEL, TEXT), "");
    draw_input();
    draw_line(ROW_HELP,     COLOR(PANEL, DIM), "Arrows+Enter or e2e4: move  Q quit");
    draw_line(ROW_HELP + 1, COLOR(PANEL, DIM), "U undo  S switch  R rotate  N new");
    draw_line(ROW_HELP + 2, COLOR(PANEL, DIM), "+/- level  T time  O book  P ponder");
}

/***************************************************************
 * The console: the keys, and the log of the moves
 ***************************************************************/
static int pushed_back = K_NONE;
static int live;                   // The input is a keyboard (see the top)

// The log goes to the console's register itself, not through
// nice_putc: under the monitor on the board, nice_putc prints on the
// screen too, where the board is (NiceMonMEGA65's monitor/README.md).
static void console_putc(int c)
{
    nice_poke(NICE_CONSOLE, (unsigned) c);
}

static void console_print(const char *s)
{
    while (*s)
        console_putc(*s++);
}

static int read_byte(void)
{
    int c = pushed_back;
    if (c != K_NONE)
    {
        pushed_back = K_NONE;
        return c;
    }
    c = platform_pollc();
    if (c == K_NONE)
        live = 1;
    return c;
}

// The byte after an ESC: the rest of a key's bytes may still be on
// their way from a keyboard, so this waits up to 20 ms for one.
static int read_byte_soon(void)
{
    uint32_t t0 = platform_micros();
    int c;
    while ((c = read_byte()) == K_NONE && platform_micros() - t0 < 20000)
        ;
    return c;
}

// The next key, K_NONE if none waits, or K_END.
static int poll_key(void)
{
    int c = read_byte();
    if (c == '\n')
        return K_ENTER;
    if (c != K_ESC)
        return c;

    // Esc alone, or the start of the bytes of a key: ESC [ A for Up,
    // ESC [ 1 5 ~ for F5, ESC O P for F1.
    int d = read_byte_soon();
    if (d != '[' && d != 'O')
    {
        pushed_back = d;
        return K_ESC;
    }
    int e = read_byte_soon();
    switch (e)
    {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default: break;
    }
    while (e >= '0' && e <= '9')
        e = read_byte_soon();
    return K_OTHER;
}

// The keys typed while the engine thought, for after its move.
#define AHEAD 16
static int ahead[AHEAD];
static int n_ahead;

static int wait_key(void)
{
    if (n_ahead)
    {
        int k = ahead[0];
        for (int i = 1; i < n_ahead; ++i)
            ahead[i - 1] = ahead[i];
        --n_ahead;
        return k;
    }
    // K_NONE when a side's time runs out, for the screen to show it.
    int k, was = flagged;
    while ((k = poll_key()) == K_NONE && flagged == was)
        clock_tick();
    return k;
}

static void log_move(move_t m)
{
    char buf[8];
    engine_move_to_str(m, buf);
    console_print("move ");
    console_print(buf);
    console_putc('\n');
}

/***************************************************************
 * The game
 ***************************************************************/
static void clear_selection(void)
{
    selected = -1;
    for (int i = 0; i < 64; ++i)
        targets[i] = 0;
}

// Writes the result of the game to the console.
static void log_result(void)
{
    console_print("result ");
    console_print(status_text(game_result()));
    console_putc('\n');
}

static void ponder_hit(void);

// Plays a move, unless the mover's time ran out first, and gives the
// mover the increment. While the engine ponders, the player's move is
// a hit, which the search goes on with, or a miss, which stops it.
static void play(move_t m)
{
    if (pondering)
    {
        if (m == expected)
            ponder_hit();
        else
            ponder_miss = m;
        return;
    }
    clock_tick();
    if (flagged)
        return;
    int side = view.side < 0;
    if (n_played >= MAX_PLAYED || engine_make_move(m))
    {
        message = "The game is too long.";
        return;
    }
    played[n_played++] = m;
    clock_bonus[side] += controls[control].inc;
    snapshot(&view);
    log_move(m);
    if (game_result() != ENGINE_PLAYING)
        log_result();
}

static void new_game(void)
{
    engine_new_game();
    n_played = 0;
    for (int i = 0; i < 2; ++i)
        clock_secs[i] = clock_rest[i] = clock_bonus[i] = 0;
    flagged = 0;
    clock_on = 0;
    expected = 0;
    snapshot(&view);
    game_seed = platform_seed();
    clock_last = platform_micros();
    have_info = 0;
    clear_selection();
}

// Takes back moves until it is the player's turn again, at least one.
static void undo(void)
{
    if (n_played == 0)
    {
        message = "Nothing to take back.";
        return;
    }
    do
    {
        engine_undo();
        --n_played;
    } while (n_played > 0 && engine_side_to_move() != human);
    snapshot(&view);
    expected = 0; // It was of the position before
    have_info = 0;
    clear_selection();
}

/***************************************************************
 * The engine's move
 ***************************************************************/
// The engine's time is that of its clock, in ms, which does not wrap
// as platform_micros does, after 79 seconds of steps in niceemu.
static uint32_t start_time, soft_time, hard_time;

static uint32_t clock_ms(int side)
{
    return clock_secs[side] * 1000 + clock_rest[side] / (platform_clock_second() / 1000);
}

static uint32_t elapsed(void)
{
    return clock_ms(clock_side) - start_time;
}

static int handle_key(int k);

static int stop_search(void)
{
    if (pondering)
    {
        // The keys are read, as between moves, a key at a time: first
        // those typed while the engine thought. A key that stops the
        // search stays in ahead, or goes there, for after it.
        clock_tick();
        if (flagged)
            return 1;
        int queued = n_ahead > 0;
        int k = queued ? wait_key() : poll_key();
        if (k == K_NONE)
            return 0;
        if (k == K_END)
            ponder_stop = 1;
        else
        {
            handle_key(k);
            if (!ponder_stop && !ponder_miss)
                draw_all();
        }
        if (ponder_stop && queued)
        {
            for (int i = n_ahead; i > 0; --i)
                ahead[i] = ahead[i - 1];
            ahead[0] = k;
            ++n_ahead;
        }
        else if (ponder_stop && n_ahead < AHEAD)
            ahead[n_ahead++] = k;
        return ponder_stop || ponder_miss || !ponder_on;
    }
    clock_tick();
    if (flagged || elapsed() >= hard_time)
        return 1;
    if (!live)
        return 0;
    int k = poll_key();
    if (k == K_ESC)
        return 1;
    if (k != K_NONE && n_ahead < AHEAD)
        ahead[n_ahead++] = k;
    return 0;
}

static int on_info(const engine_info_t *in)
{
    if (pondering)
    {
        // The info of the engine's last move stays on the screen.
        ponder_info = *in;
        have_ponder_info = 1;
        return 0;
    }
    info = *in;
    have_info = 1;
    draw_info();
    clock_tick();
    return elapsed() >= soft_time;
}

// The limits of the time of the engine's move, from now.
static void set_times(void)
{
    clock_tick();
    start_time = clock_ms(clock_side);
    if (control)
    {
        uint32_t limit = clock_limit(clock_side) * 1000;
        uint32_t left = limit > start_time ? limit - start_time : 0;
        hard_time = engine_time_share(left, controls[control].inc * 1000, 0);
        if (hard_time == 0)
            hard_time = 1;
    }
    else
        hard_time = platform_move_time() / (platform_clock_second() / 1000)
                    / level_den[level - 1] * level_num[level - 1];
    hard_time = engine_strength_share(hard_time);
    soft_time = hard_time / 2;
}

// Plays the move that the search found, and keeps the reply it
// expects, the second move of its principal variation, to ponder on.
static void engine_moved(move_t m)
{
    thinking = 0;
    if (!m)
        return;
    play(m);
    if (have_info == 1 && info.pv_len >= 2 && info.pv[0] == m)
        expected = info.pv[1];
}

// The engine's strength: the strength of the time control, or with
// none, the noise of the level.
static void set_strength(void)
{
    if (control)
        engine_set_strength(strength, game_seed);
    else
        engine_set_noise(level_noise[level - 1], game_seed);
}

static void think(void)
{
    have_info = 0;
    expected = 0;
    clear_selection();
    move_t m = book ? engine_book_move() : 0;
    if (m)
    {
        have_info = 2;
        play(m);
        return;
    }
    thinking = 1;
    draw_all();

    set_strength();
    set_times();
    engine_limits_t limits = {0, 0, stop_search};
    engine_moved(engine_search(&limits, on_info));
}

// The player played the expected move: the search becomes the
// engine's, with its time from now. The engine's board has the move.
static void ponder_hit(void)
{
    clock_tick();
    if (flagged)
        return;
    played[n_played++] = expected;
    clock_bonus[view.side < 0] += controls[control].inc;
    log_move(expected);
    view = view_next;
    clock_switch();
    pondering = 0;
    thinking = 1;
    info = ponder_info;
    have_info = have_ponder_info;
    clear_selection();
    set_times();
    draw_all();
}

// Searches the position after the expected move until the player
// moves, or presses a key that needs the engine's board, which then
// waits in ahead. The player's keys are read in the search.
static void ponder(void)
{
    move_t e = expected;
    expected = 0;
    if (n_played + 1 >= MAX_PLAYED || engine_make_move(e))
        return;
    if (engine_result() != ENGINE_PLAYING)
    {
        engine_undo();
        return;
    }
    snapshot(&view_next);
    expected = e;
    set_strength();
    pondering = 1;
    have_ponder_info = 0;
    ponder_miss = 0;
    ponder_stop = 0;
    draw_ponder();
    engine_limits_t limits = {0, 0, stop_search};
    move_t m = engine_search(&limits, on_info);
    expected = 0;
    if (!pondering)
    {
        engine_moved(m); // A hit
        return;
    }
    // A miss, a key, or the end of the search with no move yet
    pondering = 0;
    engine_undo();
    if (ponder_miss)
        play(ponder_miss);
}

/***************************************************************
 * The keys
 ***************************************************************/

// Makes the typed move, if it is legal.
static void enter_typed(void)
{
    typed[n_typed] = '\0';
    move_t m = view_move(typed);
    if (m && (game_result() != ENGINE_PLAYING || view.side != human))
        m = 0;
    if (m)
        play(m);
    else
    {
        fmt_str(fmt_str(msg_buf, "Not a legal move: "), typed);
        message = msg_buf;
    }
    n_typed = 0;
}

// Enter on the board: pick up the piece under the cursor, or put
// the one picked up on the cursor's square.
static void enter_square(void)
{
    int sq = sq_index(cur_file, cur_rank);
    if (game_result() != ENGINE_PLAYING || view.side != human)
        return;

    if (selected >= 0 && targets[sq])
    {
        // A promotion by the cursor is to a queen, the first that
        // view_move finds.
        char s[6];
        s[0] = (char) ('a' + selected % 8);
        s[1] = (char) ('1' + selected / 8);
        s[2] = (char) ('a' + cur_file);
        s[3] = (char) ('1' + cur_rank);
        s[4] = '\0';
        clear_selection();
        play(view_move(s));
        return;
    }
    if (sq == selected)
    {
        clear_selection();
        return;
    }
    clear_selection();
    for (int i = 0; i < view.n_moves; ++i)
        if (engine_move_from(view.moves[i]) == sq)
        {
            selected = sq;
            targets[engine_move_to(view.moves[i])] = 1;
        }
    if (selected < 0)
        message = "No move from there.";
}

static void move_cursor(int k)
{
    int up = flipped ? -1 : 1;
    switch (k)
    {
        case K_UP:    cur_rank += up; break;
        case K_DOWN:  cur_rank -= up; break;
        case K_RIGHT: cur_file += up; break;
        case K_LEFT:  cur_file -= up; break;
        default: break;
    }
    if (cur_rank < 0) cur_rank = 0;
    if (cur_rank > 7) cur_rank = 7;
    if (cur_file < 0) cur_file = 0;
    if (cur_file > 7) cur_file = 7;
}

// Whether c can come next in the move being typed.
static int fits_typed(int c)
{
    switch (n_typed)
    {
        case 0: case 2: return c >= 'a' && c <= 'h';
        case 1: case 3: return c >= '1' && c <= '8';
        case 4:         return c == 'q' || c == 'r' || c == 'b' || c == 'n';
        default:        return 0;
    }
}

// Handles a key. Returns 0 to quit.
static int handle_key(int k)
{
    message = "";
    if (k >= 'A' && k <= 'Z')
        k += 'a' - 'A';

    if (k >= K_UP && k <= K_LEFT)
        move_cursor(k);
    else if (k == K_ESC)
    {
        n_typed = 0;
        clear_selection();
    }
    else if (k == K_BS)
    {
        if (n_typed)
            --n_typed;
    }
    else if (k == K_ENTER || k == ' ')
    {
        if (n_typed >= 4)
            enter_typed();
        else if (n_typed)
            message = "Type a move such as e2e4.";
        else
            enter_square();
    }
    else if (fits_typed(k))
    {
        typed[n_typed++] = (char) k;
        clear_selection();
    }
    else if (n_typed)
        message = "Type a move such as e2e4.";
    else if (pondering && (k == 'q' || k == 'u' || k == 'n' || k == 's' || k == 't'))
        ponder_stop = 1; // After the search, from ahead
    else switch (k)
    {
        case 'q':
            // Leave a blank screen, for the monitor on the board, in
            // its colors, white on black, as nice_screen_clear writes
            // them. The end of the input keeps the screen instead, for
            // the tests.
            nice_poke(FONT_BASE, 0);
            nice_screen_clear();
            return 0;
        case 'u': undo(); break;
        case 'n': new_game(); break;
        case 'r': flipped = !flipped; break;
        case 's':
            human = -human;
            flipped = human < 0;
            clear_selection();
            break;
        case 't':
            control = (control + 1) % N_CONTROLS;
            new_game();
            break;
        case '+':
        case '-':
            if (control)
                strength += k == '+' ? strength < ENGINE_STRENGTH_MAX : -(strength > 1);
            else
                level += k == '+' ? level < 5 : -(level > 1);
            break;
        case 'o': book = !book; break;
        case 'p': ponder_on = !ponder_on; break;
        default: break;
    }
    return 1;
}

int main(void)
{
    human = 1;
    cur_file = 4;
    cur_rank = 1;
    live = platform_on_board();
    engine_book_seed(platform_seed());
    font_init();
    new_game();

    while (1)
    {
        clock_switch();
        draw_all();
        if (game_result() == ENGINE_PLAYING && view.side != human)
        {
            think();
            continue;
        }
        if (expected && ponder_on && game_result() == ENGINE_PLAYING)
        {
            ponder();
            continue;
        }
        int k = wait_key();
        if (k == K_NONE)
            continue;
        if (k == K_END || !handle_key(k))
            break;
    }
    set_cursor(0, 0, 0);
    char fen[ENGINE_FEN_MAX];
    engine_get_fen(fen);
    console_print("fen ");
    console_print(fen);
    console_putc('\n');
    return 0;
}
