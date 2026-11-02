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
 *   uci             switch to UCI (see below)
 *   help
 *   quit
 *
 * The limits of go are those of UCI: depth N, nodes N, movetime MS,
 * wtime MS, btime MS, winc MS, binc MS, movestogo N, and infinite.
 * A number alone is a depth. With none, the engine takes
 * platform_move_time.
 *
 * After uci, the commands are those of the UCI protocol: uci,
 * isready, ucinewgame, position, go, and quit. There is no prompt,
 * go does not make the move, and stop is not heard during a search.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/engine.h"
#include "../platform/platform.h"

static const char piece_chars[] = "kqrbnp.PNBRQK";

static int uci_mode = 0;

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
    return hard_time && elapsed() >= hard_time;
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
        // A share of the time left, and most of the increment, but
        // never more than half of the time left.
        int white = engine_side_to_move() > 0;
        unsigned long left = white ? wtime : btime;
        unsigned long inc  = white ? winc : binc;
        unsigned long t = left / (movestogo ? movestogo : 30) + inc * 3 / 4;
        if (t > left / 2)
            t = left / 2;
        hard_time = ms_to_us(t ? t : 1);
        soft_time = hard_time / 2;
    }
    else if (!limits.depth && !limits.nodes && !infinite)
    {
        hard_time = platform_move_time;
        soft_time = hard_time / 2;
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
    char line[1024];

    engine_new_game();
    printf("nice_chess. Type help for the commands.\n");

    while (1)
    {
        if (!uci_mode)
        {
            printf("> ");
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        line[strcspn(line, "\r\n")] = '\0';

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
            printf("id name nice_chess\nid author Michael Jorgensen\nuciok\n");
            fflush(stdout);
        }
        else if (!strcmp(cmd, "isready"))
        {
            printf("readyok\n");
            fflush(stdout);
        }
        else if (!strcmp(cmd, "new") || !strcmp(cmd, "ucinewgame"))
            engine_new_game();
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
        else if (uci_mode)
            continue; // stop, setoption, ponderhit, and the rest
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
        else if (!strcmp(cmd, "help"))
            printf("Commands: new, fen FEN, show, moves, move M (or just M), undo,\n"
                   "go [depth N | nodes N | movetime MS | ...], perft N, divide N,\n"
                   "uci, help, quit\n");
        else if (looks_like_move(cmd))
            do_move(cmd);
        else
            printf("Unknown command: %s. Type help for the commands.\n", cmd);
    }

    return 0;
}
