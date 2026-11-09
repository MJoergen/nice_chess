/***************************************************************
 * text.c: the text front end, on the console.
 *
 * Commands, one per line:
 *   new             a new game
 *   fen FEN         set up a position
 *   show            the board, and the position as FEN
 *   moves           the legal moves
 *   move M, or M    make a move in coordinate notation, such as e2e4
 *   undo            take back the last move
 *   go [LIMITS]     the engine moves (see below)
 *   perft N         count the leaf nodes of the move tree of depth N
 *   divide N        perft N, for each legal move
 *   book            the moves of the opening book for the position
 *   book on|off     whether go plays a move of the book (off at first)
 *   uci             switch to UCI (see below)
 *   help
 *   quit
 *
 * The limits of go are those of UCI: depth N, nodes N, movetime MS,
 * wtime MS, btime MS, winc MS, binc MS, movestogo N, and infinite.
 * A number alone is a depth. With none, the engine takes
 * platform_move_time.
 *
 * With the book on, go plays a move of the book when the position is
 * in it, chosen at random by the weights, and prints "info string book
 * move" before it.
 *
 * After uci, the commands are those of the UCI protocol: uci,
 * isready, ucinewgame, position, go, setoption, and quit. The
 * options are OwnBook, the book, false at first, and Strength, 1 to 5,
 * 5 at first (engine_set_strength): below 5, go gets a share of its
 * time and of its nodes, but all of its depth. There is no prompt, and go does
 * not make the move. setoption works before uci too.
 *
 * During a search, in both modes, the line stop stops it, and the
 * line isready is answered with readyok. Any other line waits until
 * the search ends.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/engine.h"
#include "../platform/platform.h"
#include "version.h"

static const char piece_chars[] = "kqrbnp.PNBRQK";

static int uci_mode = 0;
static int own_book = 0;
static int strength = ENGINE_STRENGTH_MAX;
static uint32_t game_seed;         // Of the noise below full strength, new for each game

/***************************************************************
 * next_word
 * Returns the next word of *ps, which it ends with a 0, and moves
 * *ps past it. Returns "" at the end of the line.
 ***************************************************************/
static char *next_word(char **ps)
{
    char *s = *ps;
    while (*s == ' ' || *s == '\t')
        s++;
    char *w = s;
    while (*s && *s != ' ' && *s != '\t')
        s++;
    if (*s)
        *s++ = '\0';
    *ps = s;
    return w;
}

/***************************************************************
 * The console's input, by lines
 *
 * A line ends at LF or CR, as a terminal on the board's line, and the
 * board's keyboard, send CR, and a file LF; an LF right after a CR is
 * the same end. A line longer than the buffer loses the rest.
 *
 * The search reads the input too, through poll_input, to hear stop.
 * The bytes it reads of another line wait in in_line for read_line.
 ***************************************************************/
static char in_line[2048]; // position startpos moves ... of a long game
static int in_len;         // The bytes in in_line
static int in_ready;       // Whether in_line holds a whole line
static int in_last;        // The byte before

// Adds the byte c to in_line. Returns 1 when it ends the line.
static int add_byte(int c)
{
    int prev = in_last;
    in_last = c;
    if (c == '\n' && prev == '\r')
        return 0;
    if (c == '\n' || c == '\r')
    {
        in_line[in_len] = '\0';
        in_ready = 1;
        return 1;
    }
    if (in_len < (int) sizeof in_line - 1)
        in_line[in_len++] = (char) c;
    return 0;
}

/***************************************************************
 * read_line
 * Reads a line of the console into s, which has the size of in_line.
 * Returns 0 at the end of the input.
 ***************************************************************/
static int read_line(char *s)
{
    while (!in_ready)
    {
        int c = platform_getc();
        if (c < 0)
        {
            if (!in_len)
                return 0;
            in_line[in_len] = '\0';
            break;
        }
        add_byte(c);
    }
    strcpy(s, in_line);
    in_len = in_ready = 0;
    return 1;
}

/***************************************************************
 * poll_input
 * Reads the bytes of the console that wait, during a search, up to
 * the end of a line. Returns 1 for the line stop, which it drops. It
 * answers isready, and drops it too. Any other line it leaves for
 * read_line, and then reads no more. From a file, where a read waits,
 * the next line is read at once.
 ***************************************************************/
static int poll_input(void)
{
    while (!in_ready)
    {
        int c = platform_pollc();
        if (c < 0)
            return 0;
        if (!add_byte(c))
            continue;
        char *w = in_line;
        while (*w == ' ' || *w == '\t')
            w++;
        int stop = !strncmp(w, "stop", 4);
        int ready = !strncmp(w, "isready", 7);
        if (!stop && !ready)
            return 0;
        for (w += stop ? 4 : 7; *w == ' ' || *w == '\t'; w++)
            ;
        if (*w)
            return 0; // Such as "stopper"
        in_len = in_ready = 0;
        if (stop)
            return 1;
        printf("readyok\n");
        fflush(stdout);
    }
    return 0;
}

static void print_board(void)
{
    for (int rank = 7; rank >= 0; --rank)
    {
        printf("%d ", rank + 1);
        for (int file = 0; file < 8; ++file)
        {
            putchar(piece_chars[engine_piece_at(file, rank) + 6]);
            putchar(' ');
        }
        putchar('\n');
    }
    printf("  a b c d e f g h\n");

    char fen[ENGINE_FEN_MAX];
    engine_get_fen(fen);
    printf("%s\n", fen);
    printf("%s to move%s\n", engine_side_to_move() > 0 ? "White" : "Black",
            engine_in_check() ? ", in check" : "");
}

static void print_moves(void)
{
    move_t moves[ENGINE_MAX_MOVES];
    int n = engine_legal_moves(moves);
    for (int i = 0; i < n; ++i)
    {
        char buf[8];
        engine_move_to_str(moves[i], buf);
        printf("%s%s", i ? " " : "", buf);
    }
    printf("\n");
}

// Prints the result of the game, if it has ended, and returns it.
static int print_result(void)
{
    int result = engine_result();
    switch (result)
    {
        case ENGINE_CHECKMATE:
            printf("Checkmate, %s wins\n", engine_side_to_move() > 0 ? "black" : "white");
            break;
        case ENGINE_STALEMATE:  printf("Stalemate\n"); break;
        case ENGINE_FIFTY:      printf("Draw by the 50-move rule\n"); break;
        case ENGINE_REPETITION: printf("Draw by repetition\n"); break;
        default: break;
    }
    return result;
}

static void do_move(const char *s)
{
    move_t m = engine_parse_move(s, NULL);
    if (!m)
    {
        printf("Illegal move: %s\n", s);
        return;
    }
    if (engine_make_move(m))
    {
        printf("The game is too long\n");
        return;
    }
    print_result();
}

// True for a word such as e2e4 or e7e8q, legal or not.
static int looks_like_move(const char *s)
{
    return s[0] >= 'a' && s[0] <= 'h' && s[1] >= '1' && s[1] <= '8'
        && s[2] >= 'a' && s[2] <= 'h' && s[3] >= '1' && s[3] <= '8'
        && (s[4] == '\0' || (strchr("qrbnQRBN", s[4]) && s[5] == '\0'));
}

static void print_book(void)
{
    move_t moves[ENGINE_MAX_MOVES];
    uint32_t weights[ENGINE_MAX_MOVES];
    int n = engine_book_moves(moves, weights);
    if (!n)
    {
        printf("Book: none\n");
        return;
    }
    printf("Book:");
    for (int i = 0; i < n; ++i)
    {
        char buf[8];
        engine_move_to_str(moves[i], buf);
        printf(" %s %u", buf, (unsigned) weights[i]);
    }
    printf("\n");
}

// UCI: setoption name OwnBook value true|false, or name Strength value N
static void do_setoption(char *args)
{
    if (strcmp(next_word(&args), "name"))
        return;
    char *name = next_word(&args);
    if (strcmp(next_word(&args), "value"))
        return;
    char *value = next_word(&args);
    if (!strcmp(name, "OwnBook"))
        own_book = !strcmp(value, "true");
    else if (!strcmp(name, "Strength"))
    {
        strength = atoi(value);
        engine_set_strength(strength, game_seed);
    }
}

static void new_game(void)
{
    engine_new_game();
    game_seed = game_seed * 1664525u + 1013904223u;
    engine_set_strength(strength, game_seed);
}

static void do_divide(int depth)
{
    move_t moves[ENGINE_MAX_MOVES];
    int n = engine_legal_moves(moves);
    unsigned total = 0;
    for (int i = 0; i < n; ++i)
    {
        char buf[8];
        engine_move_to_str(moves[i], buf);
        engine_make_move(moves[i]);
        unsigned count = engine_perft(depth - 1);
        engine_undo();
        printf("%s %u\n", buf, count);
        total += count;
    }
    printf("divide(%d) = %u\n", depth, total);
}

/***************************************************************
 * The search, and its time
 *
 * A search with a time has two limits: it starts no new iteration
 * after soft_time, as the next would likely not finish, and it
 * stops at hard_time.
 ***************************************************************/
static uint32_t start_time;
static uint32_t soft_time, hard_time; // In microseconds; 0 for no limit

static uint32_t elapsed(void)
{
    return platform_micros() - start_time;
}

static int stop_search(void)
{
    return (hard_time && elapsed() >= hard_time) || poll_input();
}

static int print_info(const engine_info_t *in)
{
    printf("info depth %d score ", in->depth);
    if (in->score > ENGINE_MATE - ENGINE_MAX_PLY)
        printf("mate %d", (ENGINE_MATE - in->score + 1) / 2);
    else if (in->score < -ENGINE_MATE + ENGINE_MAX_PLY)
        printf("mate %d", -(ENGINE_MATE + in->score) / 2);
    else
        printf("cp %d", in->score);
    printf(" nodes %u time %u pv", (unsigned) in->nodes, (unsigned) (elapsed() / 1000));
    for (int i = 0; i < in->pv_len; ++i)
    {
        char buf[8];
        engine_move_to_str(in->pv[i], buf);
        printf(" %s", buf);
    }
    printf("\n");
    fflush(stdout);
    return soft_time && elapsed() >= soft_time;
}

// Milliseconds to microseconds, at most an hour.
static uint32_t ms_to_us(unsigned long ms)
{
    if (ms > 3600000)
        ms = 3600000;
    return (uint32_t) ms * 1000u;
}

/***************************************************************
 * do_go
 * Reads the limits, searches, and returns the best move, or 0.
 ***************************************************************/
static move_t do_go(char *args)
{
    engine_limits_t limits = {0, 0, stop_search};
    unsigned long movetime = 0, wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
    int timed = 0, infinite = 0;

    while (1)
    {
        char *w = next_word(&args);
        if (*w == '\0')
            break;
        if (*w >= '0' && *w <= '9')
        {
            limits.depth = atoi(w);
            continue;
        }
        unsigned long v = strtoul(next_word(&args), NULL, 10);
        if      (!strcmp(w, "depth"))     limits.depth = (int) v;
        else if (!strcmp(w, "nodes"))     limits.nodes = (uint32_t) v;
        else if (!strcmp(w, "movetime"))  movetime = v, timed = 1;
        else if (!strcmp(w, "wtime"))     wtime = v, timed = 1;
        else if (!strcmp(w, "btime"))     btime = v, timed = 1;
        else if (!strcmp(w, "winc"))      winc = v;
        else if (!strcmp(w, "binc"))      binc = v;
        else if (!strcmp(w, "movestogo")) movestogo = v;
        else if (!strcmp(w, "infinite"))  infinite = 1;
    }

    soft_time = hard_time = 0;
    if (movetime)
        soft_time = hard_time = ms_to_us(movetime);
    else if (timed)
    {
        int white = engine_side_to_move() > 0;
        unsigned long t = engine_time_share((uint32_t) (white ? wtime : btime),
                                            (uint32_t) (white ? winc : binc),
                                            (uint32_t) movestogo);
        hard_time = ms_to_us(t ? t : 1);
        soft_time = hard_time / 2;
    }
    else if (!limits.depth && !limits.nodes && !infinite)
    {
        hard_time = platform_move_time();
        soft_time = hard_time / 2;
    }

    // Below full strength, a share of the time and of the nodes.
    hard_time = engine_strength_share(hard_time);
    soft_time = engine_strength_share(soft_time);
    limits.nodes = engine_strength_share(limits.nodes);

    if (own_book)
    {
        move_t m = engine_book_move();
        if (m)
        {
            printf("info string book move\n");
            return m;
        }
    }

    start_time = platform_micros();
    return engine_search(&limits, print_info);
}

/***************************************************************
 * do_position
 * UCI: position startpos|fen FEN [moves M...]
 ***************************************************************/
static void do_position(char *args)
{
    char *w = next_word(&args);
    if (!strcmp(w, "startpos"))
        engine_set_fen(ENGINE_START_FEN, NULL);
    else if (!strcmp(w, "fen"))
    {
        const char *end;
        if (engine_set_fen(args, &end))
        {
            printf("info string invalid FEN\n");
            return;
        }
        args = (char *) end;
    }
    else
        return;

    w = next_word(&args);
    if (strcmp(w, "moves"))
        return;
    while (1)
    {
        w = next_word(&args);
        if (*w == '\0')
            break;
        move_t m = engine_parse_move(w, NULL);
        if (!m || engine_make_move(m))
        {
            printf("info string illegal move %s\n", w);
            return;
        }
    }
}

static void print_bestmove(move_t m)
{
    char buf[8];
    if (m)
        engine_move_to_str(m, buf);
    else
        strcpy(buf, "0000");
    printf("bestmove %s\n", buf);
    fflush(stdout);
}

int main(void)
{
    static char line[sizeof in_line];

    game_seed = platform_seed();
    new_game();
    engine_book_seed(platform_seed());
    printf("nice_chess %s. Type help for the commands.\n", nice_chess_version);

    while (1)
    {
        if (!uci_mode)
        {
            printf("> ");
            fflush(stdout);
        }
        if (!read_line(line))
            break;

        char *arg = line;
        char *cmd = next_word(&arg);
        while (*arg == ' ')
            arg++;

        if (*cmd == '\0')
            continue;
        else if (!strcmp(cmd, "quit"))
            break;
        else if (!strcmp(cmd, "uci"))
        {
            uci_mode = 1;
            printf("id name nice_chess %s\nid author Michael Jorgensen\n"
                   "option name OwnBook type check default false\n"
                   "option name Strength type spin default 5 min 1 max 5\nuciok\n", nice_chess_version);
            fflush(stdout);
        }
        else if (!strcmp(cmd, "isready"))
        {
            printf("readyok\n");
            fflush(stdout);
        }
        else if (!strcmp(cmd, "new") || !strcmp(cmd, "ucinewgame"))
            new_game();
        else if (!strcmp(cmd, "position"))
            do_position(arg);
        else if (!strcmp(cmd, "go"))
        {
            if (uci_mode)
            {
                print_bestmove(do_go(arg));
                continue;
            }
            if (print_result() != ENGINE_PLAYING)
                continue;
            move_t m = do_go(arg);
            print_bestmove(m);
            if (engine_make_move(m))
                printf("The game is too long\n");
            else
            {
                print_board();
                print_result();
            }
        }
        else if (!strcmp(cmd, "setoption"))
            do_setoption(arg);
        else if (uci_mode || !strcmp(cmd, "stop"))
            continue; // stop after a search, ponderhit, and the rest
        else if (!strcmp(cmd, "fen"))
        {
            if (engine_set_fen(arg, NULL))
                printf("Invalid FEN\n");
        }
        else if (!strcmp(cmd, "show"))
            print_board();
        else if (!strcmp(cmd, "moves"))
            print_moves();
        else if (!strcmp(cmd, "move"))
            do_move(arg);
        else if (!strcmp(cmd, "undo"))
        {
            if (engine_undo())
                printf("Nothing to undo\n");
        }
        else if (!strcmp(cmd, "perft"))
        {
            int depth = atoi(arg);
            printf("perft(%d) = %u\n", depth, (unsigned) engine_perft(depth));
        }
        else if (!strcmp(cmd, "divide"))
            do_divide(atoi(arg));
        else if (!strcmp(cmd, "book"))
        {
            if (!strcmp(arg, "on") || !strcmp(arg, "off"))
                own_book = !strcmp(arg, "on");
            else
                print_book();
        }
        else if (!strcmp(cmd, "help"))
            printf("Commands: new, fen FEN, show, moves, move M (or just M), undo,\n"
                   "go [depth N | nodes N | movetime MS | ...], perft N, divide N,\n"
                   "book [on | off], uci, help, quit\n");
        else if (looks_like_move(cmd))
            do_move(cmd);
        else
            printf("Unknown command: %s. Type help for the commands.\n", cmd);
    }

    return 0;
}
