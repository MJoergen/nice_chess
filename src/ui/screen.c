/***************************************************************
 * screen.c: the windowed front end, on the 80x25 character screen.
 *
 * The board takes the left half of the screen: 8x8 squares of 5
 * columns and 3 rows each, with the ranks in column 0 and the files
 * in row 24. The right half has the players, the state of the game,
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
 *   + -             give the engine more or less time, levels 1 to 5
 *   Q               quit
 * The letters of commands act only when no move is being typed.
 *
 * Each move is also written to the console, as "move e2e4", and the
 * end of the game as "result ...". The program halts on Q, or at the
 * end of the console's input.
 *
 * The engine polls the keyboard during its search only once a read
 * of the console has found no byte waiting: that is, once the input
 * is a keyboard. Input from a file (niceemu -i, in the tests) is
 * then read only between moves, so a run is the same every time.
 *
 * This front end is for NICE only. It does not use printf.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdint.h>
#include <nice.h>
#include "../engine/engine.h"
#include "../platform/platform.h"

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
    PC_WHITE    = 0xEEE, PC_BLACK   = 0x111,  // The pieces: letters on chips
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
    ROW_TITLE = 0, ROW_PLAYERS = 1, ROW_STATUS = 3, ROW_LEVEL = 4,
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

// The levels: the time of a move, as a share of platform_move_time.
static const uint8_t level_num[5] = {1, 1, 1, 3, 9};
static const uint8_t level_den[5] = {9, 3, 1, 1, 1};
static int    level = 3;

// What the engine thought about its last move.
static engine_info_t info;
static int    have_info;
static int    thinking;

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
    int piece = engine_piece_at(file, rank);
    if ((piece == WK || piece == BK) && piece * engine_side_to_move() > 0 && engine_in_check())
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

    // A piece is its letter on a chip of three cells, in its color.
    if (piece != EM)
    {
        uint32_t chip = piece > 0 ? COLOR(PC_WHITE, PC_BLACK) : COLOR(PC_BLACK, PC_WHITE);
        put(col + 1, row + 1, chip, ' ');
        put(col + 2, row + 1, chip, "PNBRQK"[(piece > 0 ? piece : -piece) - 1]);
        put(col + 3, row + 1, chip, ' ');
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

static void draw_players(void)
{
    draw_line(ROW_PLAYERS, COLOR(PANEL, TEXT),
              human > 0 ? "White: you      Black: nice_chess"
                        : "White: nice_chess   Black: you");
}

static const char *status_text(int result)
{
    int white = engine_side_to_move() > 0;
    switch (result)
    {
        case ENGINE_CHECKMATE:  return white ? "Checkmate. Black wins." : "Checkmate. White wins.";
        case ENGINE_STALEMATE:  return "Stalemate. A draw.";
        case ENGINE_FIFTY:      return "A draw by the 50-move rule.";
        case ENGINE_REPETITION: return "A draw by repetition.";
        default: break;
    }
    if (thinking)
        return "Thinking... Esc: move now";
    if (engine_in_check())
        return white ? "White to move, in check" : "Black to move, in check";
    return white ? "White to move" : "Black to move";
}

static void draw_status(void)
{
    int result = engine_result();
    draw_line(ROW_STATUS, COLOR(PANEL, result != ENGINE_PLAYING || thinking ? WARN : BRIGHT),
              status_text(result));
}

static void draw_level(void)
{
    char buf[16];
    fmt_str(fmt_uint(fmt_str(buf, "Level "), (uint32_t) level), " of 5");
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
    if (have_info)
    {
        p = fmt_uint(fmt_str(p, "Depth "), (uint32_t) info.depth);
        p = fmt_score(fmt_str(p, "  Score "), info.score);
        fmt_uint(fmt_str(p, "  Nodes "), info.nodes);
    }
    draw_line(ROW_INFO, COLOR(PANEL, TEXT), buf);

    // The principal variation, in as many moves as fit on two rows.
    int col = PX, row = ROW_PV;
    if (have_info)
        col = put_str(col, row, COLOR(PANEL, DIM), "PV");
    for (int i = 0; have_info && i < info.pv_len; ++i)
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
    draw_line(ROW_TITLE, COLOR(PANEL, BRIGHT), "nice_chess");
    draw_players();
    draw_line(2, COLOR(PANEL, TEXT), "");
    draw_status();
    draw_level();
    draw_line(5, COLOR(PANEL, TEXT), "");
    draw_info();
    draw_moves();
    draw_line(ROW_INPUT - 1, COLOR(PANEL, TEXT), "");
    draw_input();
    draw_line(ROW_HELP,     COLOR(PANEL, DIM), "Arrows, Enter: move. Or type e2e4.");
    draw_line(ROW_HELP + 1, COLOR(PANEL, DIM), "U undo  S switch  R rotate  N new");
    draw_line(ROW_HELP + 2, COLOR(PANEL, DIM), "+/- level  Q quit  Esc cancel/stop");
}

/***************************************************************
 * The console: the keys, and the log of the moves
 ***************************************************************/
static int pushed_back = K_NONE;
static int live;                   // The input is a keyboard (see the top)

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

// The next key, K_NONE if none waits, or K_END.
static int poll_key(void)
{
    int c = read_byte();
    if (c == '\n')
        return K_ENTER;
    if (c != K_ESC)
        return c;

    // Esc alone, or the start of the bytes of a key, which all come
    // at once: ESC [ A for Up, ESC [ 1 5 ~ for F5, ESC O P for F1.
    int d = read_byte();
    if (d != '[' && d != 'O')
    {
        pushed_back = d;
        return K_ESC;
    }
    int e = read_byte();
    switch (e)
    {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default: break;
    }
    while (e >= '0' && e <= '9')
        e = read_byte();
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
    int k;
    while ((k = poll_key()) == K_NONE)
        ;
    return k;
}

static void log_move(move_t m)
{
    char buf[8];
    engine_move_to_str(m, buf);
    nice_print("move ");
    nice_print(buf);
    nice_putc('\n');
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

static void play(move_t m)
{
    if (n_played >= MAX_PLAYED || engine_make_move(m))
    {
        message = "The game is too long.";
        return;
    }
    played[n_played++] = m;
    log_move(m);
    int result = engine_result();
    if (result != ENGINE_PLAYING)
    {
        nice_print("result ");
        nice_print(status_text(result));
        nice_putc('\n');
    }
}

static void new_game(void)
{
    engine_new_game();
    n_played = 0;
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
    have_info = 0;
    clear_selection();
}

/***************************************************************
 * The engine's move
 ***************************************************************/
static uint32_t start_time, soft_time, hard_time;

static uint32_t elapsed(void)
{
    return platform_micros() - start_time;
}

static int stop_search(void)
{
    if (elapsed() >= hard_time)
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
    info = *in;
    have_info = 1;
    draw_info();
    return elapsed() >= soft_time;
}

static void think(void)
{
    thinking = 1;
    have_info = 0;
    clear_selection();
    draw_all();

    hard_time = platform_move_time / level_den[level - 1] * level_num[level - 1];
    soft_time = hard_time / 2;
    engine_limits_t limits = {0, 0, stop_search};
    start_time = platform_micros();
    move_t m = engine_search(&limits, on_info);

    thinking = 0;
    if (m)
        play(m);
}

/***************************************************************
 * The keys
 ***************************************************************/

// Makes the typed move, if it is legal.
static void enter_typed(void)
{
    typed[n_typed] = '\0';
    move_t m = engine_parse_move(typed, 0);
    if (m && (engine_result() != ENGINE_PLAYING || engine_side_to_move() != human))
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
    if (engine_result() != ENGINE_PLAYING || engine_side_to_move() != human)
        return;

    move_t moves[ENGINE_MAX_MOVES];
    int n = engine_legal_moves(moves);
    if (selected >= 0 && targets[sq])
    {
        // A promotion by the cursor is to a queen, the first that
        // engine_parse_move finds.
        char s[6];
        s[0] = (char) ('a' + selected % 8);
        s[1] = (char) ('1' + selected / 8);
        s[2] = (char) ('a' + cur_file);
        s[3] = (char) ('1' + cur_rank);
        s[4] = '\0';
        clear_selection();
        play(engine_parse_move(s, 0));
        return;
    }
    if (sq == selected)
    {
        clear_selection();
        return;
    }
    clear_selection();
    for (int i = 0; i < n; ++i)
        if (engine_move_from(moves[i]) == sq)
        {
            selected = sq;
            targets[engine_move_to(moves[i])] = 1;
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
    else switch (k)
    {
        case 'q': return 0;
        case 'u': undo(); break;
        case 'n': new_game(); break;
        case 'r': flipped = !flipped; break;
        case 's':
            human = -human;
            flipped = human < 0;
            clear_selection();
            break;
        case '+': level += level < 5; break;
        case '-': level -= level > 1; break;
        default: break;
    }
    return 1;
}

int main(void)
{
    human = 1;
    cur_file = 4;
    cur_rank = 1;
    new_game();

    while (1)
    {
        draw_all();
        if (engine_result() == ENGINE_PLAYING && engine_side_to_move() != human)
        {
            think();
            continue;
        }
        int k = wait_key();
        if (k == K_END || !handle_key(k))
            break;
    }
    draw_all();
    set_cursor(0, 0, 0);
    return 0;
}
