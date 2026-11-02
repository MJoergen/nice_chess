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
 *   go [DEPTH]      the engine moves, after a search of DEPTH ply
 *   perft N         count the leaf nodes of the move tree of depth N
 *   divide N        perft N, for each legal move
 *   help
 *   quit
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/engine.h"

#define DEFAULT_DEPTH 4

static const char piece_chars[] = "kqrbnp.PNBRQK";

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

static void print_result(void)
{
    switch (engine_result())
    {
        case ENGINE_CHECKMATE:
            printf("Checkmate, %s wins\n", engine_side_to_move() > 0 ? "black" : "white");
            break;
        case ENGINE_STALEMATE:  printf("Stalemate\n"); break;
        case ENGINE_FIFTY:      printf("Draw by the 50-move rule\n"); break;
        case ENGINE_REPETITION: printf("Draw by repetition\n"); break;
        default: break;
    }
}

static void info(move_t move, int score)
{
    char buf[8];
    engine_move_to_str(move, buf);
    printf("info %s %d\n", buf, score);
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

int main(void)
{
    char line[256];

    engine_new_game();
    printf("nice_chess. Type help for the commands.\n");

    while (1)
    {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        line[strcspn(line, "\r\n")] = '\0';

        char *arg = line;
        while (*arg == ' ')
            arg++;
        char *cmd = arg;
        while (*arg && *arg != ' ')
            arg++;
        if (*arg)
            *arg++ = '\0';
        while (*arg == ' ')
            arg++;

        if (*cmd == '\0')
            continue;
        else if (!strcmp(cmd, "quit"))
            break;
        else if (!strcmp(cmd, "new"))
            engine_new_game();
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
        else if (!strcmp(cmd, "go"))
        {
            if (engine_result() != ENGINE_PLAYING)
            {
                print_result();
                continue;
            }
            int depth = *arg ? atoi(arg) : DEFAULT_DEPTH;
            move_t m = engine_search(depth, info);
            char buf[8];
            engine_move_to_str(m, buf);
            printf("bestmove %s\n", buf);
            if (engine_make_move(m))
                printf("The game is too long\n");
            else
                print_result();
        }
        else if (!strcmp(cmd, "perft"))
        {
            int depth = atoi(arg);
            printf("perft(%d) = %u\n", depth, (unsigned) engine_perft(depth));
        }
        else if (!strcmp(cmd, "divide"))
            do_divide(atoi(arg));
        else if (!strcmp(cmd, "help"))
            printf("Commands: new, fen FEN, show, moves, move M (or just M),\n"
                   "undo, go [DEPTH], perft N, divide N, help, quit\n");
        else if (looks_like_move(cmd))
            do_move(cmd);
        else
            printf("Unknown command: %s. Type help for the commands.\n", cmd);
    }

    return 0;
}
