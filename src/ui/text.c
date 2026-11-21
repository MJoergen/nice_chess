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
 *   remote [IP]     remote play: look for boards, or connect to one
 *   remote auto [LIMITS]  the engine plays this side of a remote game,
 *                   with the limits of go; remote auto off stops it
 *   remote off      leave remote play
 *   uci             switch to UCI (see below)
 *   help
 *   quit
 *
 * In a remote game (see "Remote play" below), new [w|b], move M, go,
 * undo, draw, no, and resign speak to the other board.
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
 * options are OwnBook, the book, false at first; Strength, 1 to 5,
 * 5 at first (engine_set_strength): below 5, go gets a share of its
 * time and of its nodes, but all of its depth; and Noise, 0 to 1000,
 * 0 at first, the noise of the windowed front end's levels in
 * centipawns (engine_set_noise), which counts only at Strength 5.
 * There is no prompt, and go does not make the move. setoption works
 * before uci too.
 *
 * During a search, in both modes, the line stop stops it, and the
 * line isready is answered with readyok. Any other line waits until
 * the search ends.
 *
 * go ponder searches the position, which ends with the move the
 * engine expects of its opponent, with no limit of time, until
 * ponderhit or stop. ponderhit, which says the opponent played that
 * move, turns it into the search that go would have made without
 * ponder, its time counted from the ponderhit. bestmove waits for
 * ponderhit or stop. In UCI, bestmove names the move the engine
 * expects in reply, the second of the principal variation, as
 * "ponder M", when it has one. The option Ponder is there for the
 * GUIs, which ask for it before they send go ponder; its value
 * changes nothing.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/engine.h"
#include "../platform/platform.h"
#include "remote.h"
#include "version.h"

static const char piece_chars[] = "kqrbnp.PNBRQK";

static int uci_mode = 0;
static int own_book = 0;
static int strength = ENGINE_STRENGTH_MAX;
static int noise;                  // At full strength (engine_set_noise)
static uint32_t game_seed;         // Of the noise, new for each game

// Remote play (below): whether the network is open, the work it does
// while the console has no line, and whether it still has work after
// the end of the input.
static int net_open;
static void remote_service(void);
static int remote_busy(void);

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
static int in_ended;       // The input has ended

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
        // With the network open, it is served while no byte waits, and
        // after the end of the input, until the game and its messages
        // are done.
        int c = net_open ? platform_pollc() : platform_getc();
        if (c == -2 || (c == -1 && net_open && remote_busy()))
        {
            remote_service();
            continue;
        }
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
 * the end of a line. Returns 1 for the line stop, and 2 for the line
 * ponderhit, which it drops. It answers isready, and drops it too.
 * Any other line it leaves for read_line, and then reads no more.
 * From a file, where a read waits, the next line is read at once.
 ***************************************************************/
static int poll_input(void)
{
    while (!in_ready)
    {
        int c = platform_pollc();
        if (c < 0)
        {
            in_ended = c == -1;
            return 0;
        }
        if (!add_byte(c))
            continue;
        char *w = in_line;
        while (*w == ' ' || *w == '\t')
            w++;
        int stop = !strncmp(w, "stop", 4);
        int hit = !strncmp(w, "ponderhit", 9);
        int ready = !strncmp(w, "isready", 7);
        if (!stop && !hit && !ready)
            return 0;
        for (w += stop ? 4 : hit ? 9 : 7; *w == ' ' || *w == '\t'; w++)
            ;
        if (*w)
            return 0; // Such as "stopper"
        in_len = in_ready = 0;
        if (!ready)
            return hit ? 2 : 1;
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

// Sets the strength, or at full strength, the noise.
static void set_strength(void)
{
    if (strength < ENGINE_STRENGTH_MAX)
        engine_set_strength(strength, game_seed);
    else
        engine_set_noise(noise, game_seed);
}

// UCI: setoption name OwnBook value true|false, name Strength value N,
// or name Noise value N
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
        if (strength < 1)
            strength = 1;
        if (strength > ENGINE_STRENGTH_MAX)
            strength = ENGINE_STRENGTH_MAX;
        set_strength();
    }
    else if (!strcmp(name, "Noise"))
    {
        noise = atoi(value);
        if (noise < 0)
            noise = 0;
        if (noise > 1000)
            noise = 1000;
        set_strength();
    }
}

static void new_game(void)
{
    engine_new_game();
    game_seed = game_seed * 1664525u + 1013904223u;
    set_strength();
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
 * stops at hard_time. While it ponders, it has neither, and keeps
 * them in ponder_soft and ponder_hard for the ponderhit.
 *
 * A search with no limit, that of go infinite, or of go ponder
 * before its ponderhit, ends on stop. It also ends when a line
 * that poll_input does not hear waits, such as quit, and at the
 * end of the input: poll_input reads no more then, so no stop
 * would come.
 ***************************************************************/
static uint32_t start_time, info_time; // info_time: the start, for info
static uint32_t soft_time, hard_time; // In microseconds; 0 for no limit
static int bounded;                  // The search has a depth or a count of nodes
static int pondering;
static uint32_t ponder_soft, ponder_hard;
static engine_info_t last_info;      // Of the last iteration
static int have_info;

static uint32_t elapsed(void)
{
    return platform_micros() - start_time;
}

// Handles a line that poll_input found: ponderhit starts the clock of
// the search. Returns 1 to stop the search.
static int heard(int r)
{
    if (r == 2 && pondering)
    {
        pondering = 0;
        start_time = platform_micros();
        soft_time = ponder_soft;
        hard_time = ponder_hard;
        return 0;
    }
    if (r == 1)
        pondering = 0;
    return r == 1;
}

static int stop_search(void)
{
    // The network's ACKs and resends; a message that comes waits.
    if (net_open)
        remote_poll();
    if ((hard_time && elapsed() >= hard_time) || heard(poll_input()))
        return 1;
    // With no limit, a line that waits, or the end of the input (see
    // above).
    return !hard_time && !bounded && (in_ready || in_ended);
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
    printf(" nodes %u time %u pv", (unsigned) in->nodes,
           (unsigned) ((platform_micros() - info_time) / 1000));
    for (int i = 0; i < in->pv_len; ++i)
    {
        char buf[8];
        engine_move_to_str(in->pv[i], buf);
        printf(" %s", buf);
    }
    printf("\n");
    fflush(stdout);
    last_info = *in;
    have_info = 1;
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
 * Reads the limits, searches, and returns the best move, or 0. With
 * ponder, it returns only after ponderhit or stop.
 ***************************************************************/
static move_t do_go(char *args)
{
    engine_limits_t limits = {0, 0, stop_search};
    unsigned long movetime = 0, wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
    int timed = 0, infinite = 0;
    pondering = 0;

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
        unsigned long v = 0;
        if (strcmp(w, "infinite") && strcmp(w, "ponder"))
            v = strtoul(next_word(&args), NULL, 10);
        if      (!strcmp(w, "depth"))     limits.depth = (int) v;
        else if (!strcmp(w, "nodes"))     limits.nodes = (uint32_t) v;
        else if (!strcmp(w, "movetime"))  movetime = v, timed = 1;
        else if (!strcmp(w, "wtime"))     wtime = v, timed = 1;
        else if (!strcmp(w, "btime"))     btime = v, timed = 1;
        else if (!strcmp(w, "winc"))      winc = v;
        else if (!strcmp(w, "binc"))      binc = v;
        else if (!strcmp(w, "movestogo")) movestogo = v;
        else if (!strcmp(w, "infinite"))  infinite = 1;
        else if (!strcmp(w, "ponder"))    pondering = 1;
    }

    bounded = limits.depth || limits.nodes;
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

    if (pondering)
    {
        ponder_soft = soft_time;
        ponder_hard = hard_time;
        soft_time = hard_time = 0;
    }
    have_info = 0;
    move_t m = own_book ? engine_book_move() : 0;
    if (m)
        printf("info string book move\n");
    else
    {
        start_time = info_time = platform_micros();
        m = engine_search(&limits, print_info);
    }

    // A search that ended by itself while it pondered: bestmove waits,
    // until a line other than isready, or the end of the input.
    while (pondering && !in_ready && !in_ended)
        heard(poll_input());
    pondering = 0;
    return m;
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
    printf("bestmove %s", buf);
    if (uci_mode && m && have_info && last_info.pv_len >= 2 && last_info.pv[0] == m)
    {
        engine_move_to_str(last_info.pv[1], buf);
        printf(" ponder %s", buf);
    }
    printf("\n");
    fflush(stdout);
}

/***************************************************************
 * Remote play (remote.h): a game against another board, which
 * plays the other side. In it, new, move, go, undo, draw, no, and
 * resign speak to the other board, and the other board's moves are
 * printed as they come. The lines "game white|black", "move M" for
 * each move of the game, and "result R REASON" are those of
 * src/ui/peer.c too.
 ***************************************************************/
static int r_color;        // The local side: 1 white, -1 black
static int r_playing;      // A game is on
static int r_asked;        // NEW is sent, and the ACCEPT not yet in
static int r_undo_in;      // The other board asks to take back
static int r_draw_in;      // The other board offers a draw
static int r_undo_out;     // This side asked to take back
static int r_draw_out;     // This side offered a draw
static int r_auto;         // The engine plays the local side
static char r_auto_go[128]; // With these limits of go
static int r_gone;
static uint32_t r_heard;   // When the last message came

static void r_over(const char *how)
{
    printf("result %s\n", how);
    fflush(stdout);
    r_playing = r_undo_in = r_draw_in = r_undo_out = r_draw_out = 0;
}

static void r_check_end(void)
{
    const char *how = remote_result();
    if (how)
        r_over(how);
}

static void r_start(int color)
{
    r_color = color;
    r_playing = 1;
    r_asked = r_undo_in = r_draw_in = r_undo_out = r_draw_out = 0;
    printf("game %s\n", color > 0 ? "white" : "black");
    fflush(stdout);
}

// Makes the local side's move, and sends it.
static void r_move(move_t m)
{
    char buf[8];
    if (engine_make_move(m))
    {
        r_over("* too long");
        return;
    }
    engine_move_to_str(m, buf);
    printf("move %s\n", buf);
    remote_send_move(m, 0);
    if (r_draw_in)
    {
        remote_send("NO", ""); // A move declines the draw
        r_draw_in = 0;
    }
    r_undo_in = 0; // And refuses to take back: the other board reads the move so
    r_check_end();
}

static int my_turn(void)
{
    return r_playing && engine_side_to_move() == r_color;
}

// Waits for the local side's turn, while the game goes on. Returns 0
// if the wait ended without it: the game ended, the other board is
// gone, or a line came at the console, which runs next. The line stop
// only ends the wait.
static int wait_turn(void)
{
    while (r_playing && !my_turn())
    {
        remote_service();
        if (r_gone || poll_input() == 1 || in_ready)
            break;
    }
    return my_turn();
}

static void r_handle(const remote_msg_t *m)
{
    char ip[16];
    remote_ip_to_str(m->ip, ip);
    switch (m->type)
    {
    case REMOTE_HELLO:
        if (m->version != REMOTE_VERSION)
        {
            // No game with a board of another version of the protocol.
            printf("%s has version %d of the protocol, not %d\n", ip, m->version, REMOTE_VERSION);
            if (m->ip == remote_peer() && !r_playing)
            {
                remote_connect(0);
                r_asked = 0;
                printf("Not connected\n");
            }
            break;
        }
        printf("hello %s %s\n", ip, m->kind);
        break;
    case REMOTE_NEW:
        remote_connect(m->ip);
        new_game();
        remote_send("ACCEPT", "");
        printf("New game from %s\n", ip);
        r_start(-m->color);
        break;
    case REMOTE_ACCEPT:
        if (r_asked)
        {
            new_game();
            r_start(r_color);
        }
        break;
    case REMOTE_MOVE:
        if (!r_playing || my_turn())
            break;
        {
            int r = remote_take_move(m);
            if (r != REMOTE_OK)
            {
                printf("error %s %s\n", r == REMOTE_ILLEGAL ? "illegal move" : "FEN after", m->move);
                remote_send("BYE", "");
                r_playing = 0;
                break;
            }
            printf("move %s\n", m->move);
            r_undo_out = 0; // A move after UNDO? refuses it
            if (!r_auto)
                print_board();
            r_check_end();
        }
        break;
    case REMOTE_RESIGN:
        if (r_playing)
            r_over(r_color > 0 ? "1-0 resigns" : "0-1 resigns");
        break;
    case REMOTE_DRAW_OFFER:
        if (!r_playing)
            break;
        if (r_auto)
            remote_send("NO", "");
        else
        {
            r_draw_in = 1;
            printf("%s offers a draw: draw accepts, no refuses\n", ip);
        }
        break;
    case REMOTE_DRAW_ACCEPT:
        if (r_playing && r_draw_out)
            r_over("1/2-1/2 agreed");
        break;
    case REMOTE_UNDO_ASK:
        // Only right after the other board's move, before the reply.
        if (!my_turn() || r_auto)
            remote_send("NO", "");
        else
        {
            r_undo_in = 1;
            printf("%s asks to take back its move: undo agrees, no refuses\n", ip);
        }
        break;
    case REMOTE_UNDO_AGREE:
        if (r_playing && r_undo_out)
        {
            engine_undo();
            r_undo_out = 0;
            printf("Taken back\n");
            print_board();
        }
        break;
    case REMOTE_NO:
        if (r_undo_out || r_draw_out)
            printf("Refused\n");
        r_undo_out = r_draw_out = 0;
        break;
    case REMOTE_BYE:
        if (r_playing)
        {
            printf("bye\n");
            r_playing = 0;
        }
        r_asked = 0;
        break;
    default:
        break;
    }
    fflush(stdout);
}

static void remote_service(void)
{
    const remote_msg_t *m = remote_poll();
    if (m)
    {
        r_heard = platform_micros();
        r_handle(m);
        remote_done();
    }
    if (remote_gone() != r_gone)
    {
        r_gone = !r_gone;
        printf(r_gone ? "gone\n" : "back\n");
        fflush(stdout);
    }
    if (r_auto && my_turn() && !r_undo_out)
    {
        char args[sizeof r_auto_go];
        strcpy(args, r_auto_go);
        r_move(do_go(args));
    }
}

// Whether the network has work after the end of the input: a game in
// which the other board is to move, or the engine is for this side,
// messages that wait for their ACK, or a message in the last 2
// seconds, whose repeats it answers. It has none once the other board
// is gone.
static int remote_busy(void)
{
    if (remote_gone())
        return 0;
    return (r_playing && (r_auto || !my_turn())) || r_asked || remote_pending()
        || platform_micros() - r_heard < 2 * platform_clock_second();
}

// remote, remote IP, remote auto [LIMITS], remote auto off, remote off
static void do_remote(char *arg)
{
    if (uci_mode)
        return;
    if (!strcmp(arg, "off"))
    {
        if (!net_open)
            return;
        if (r_playing)
            remote_send("BYE", "");
        r_playing = r_asked = r_auto = 0;
        uint32_t t = platform_micros();
        while (remote_pending() && !remote_gone()
               && platform_micros() - t < 2 * platform_clock_second())
            remote_service();
        remote_close();
        net_open = 0;
        return;
    }
    if (!strncmp(arg, "auto", 4) && (arg[4] == '\0' || arg[4] == ' '))
    {
        arg += 4;
        while (*arg == ' ')
            arg++;
        r_auto = strcmp(arg, "off") != 0;
        if (r_auto)
            snprintf(r_auto_go, sizeof r_auto_go, "%s", arg);
        return;
    }
    if (!net_open)
    {
        if (remote_open("full"))
        {
            printf("No network\n");
            return;
        }
        net_open = 1;
        r_heard = platform_micros();
    }
    if (*arg)
    {
        uint32_t ip = remote_parse_ip(arg);
        if (!ip)
        {
            printf("Not an IP address: %s\n", arg);
            return;
        }
        remote_connect(ip);
        remote_hello(ip);
        printf("Connected to %s\n", arg);
    }
    else
    {
        remote_hello(REMOTE_BROADCAST);
        printf("Looking for boards\n");
    }
}

// The commands of a remote game. Returns 0 for a command that it does
// not take, which then runs as usual.
static int remote_command(const char *cmd, char *arg)
{
    if (!net_open || uci_mode || !remote_peer())
        return 0;
    if (!strcmp(cmd, "new"))
    {
        r_color = *arg == 'b' ? -1 : 1;
        r_playing = 0;
        r_asked = remote_send_new(r_color, "none") == 0;
        if (!r_asked)
        {
            printf("The queue is full\n");
            return 1;
        }
        // The game starts at the ACCEPT, before the next command. The
        // wait ends too when the other board is gone, and at stop.
        while (r_asked && !remote_gone() && poll_input() != 1)
            remote_service();
        if (r_asked)
        {
            printf("No answer\n");
            r_asked = 0;
        }
        return 1;
    }
    if (!r_playing)
        return 0;
    if (!strcmp(cmd, "move") || looks_like_move(cmd) || !strcmp(cmd, "go"))
    {
        if (!wait_turn())
        {
            if (r_playing)
                printf("Not played\n");
            return 1;
        }
        if (!strcmp(cmd, "go"))
        {
            move_t m = do_go(arg);
            print_bestmove(m);
            r_move(m);
            return 1;
        }
        move_t m = engine_parse_move(strcmp(cmd, "move") ? cmd : arg, NULL);
        if (m)
            r_move(m);
        else
            printf("Illegal move: %s\n", strcmp(cmd, "move") ? cmd : arg);
        return 1;
    }
    if (!strcmp(cmd, "undo"))
    {
        if (r_undo_in)
        {
            engine_undo();
            remote_send("UNDO!", "");
            r_undo_in = 0;
            print_board();
        }
        else if (!my_turn() && remote_ply() > 0)
        {
            remote_send("UNDO?", "");
            r_undo_out = 1;
        }
        else
            printf("Only your last move, before the reply\n");
        return 1;
    }
    if (!strcmp(cmd, "draw"))
    {
        if (r_draw_in)
        {
            remote_send("DRAW!", "");
            r_over("1/2-1/2 agreed");
        }
        else
        {
            remote_send("DRAW?", "");
            r_draw_out = 1;
        }
        return 1;
    }
    if (!strcmp(cmd, "no"))
    {
        if (r_undo_in || r_draw_in)
            remote_send("NO", "");
        r_undo_in = r_draw_in = 0;
        return 1;
    }
    if (!strcmp(cmd, "resign"))
    {
        remote_send("RESIGN", "");
        r_over(r_color > 0 ? "0-1 resigns" : "1-0 resigns");
        return 1;
    }
    return 0;
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
        else if (remote_command(cmd, arg))
            continue;
        else if (!strcmp(cmd, "remote"))
            do_remote(arg);
        else if (!strcmp(cmd, "quit"))
            break;
        else if (!strcmp(cmd, "uci"))
        {
            uci_mode = 1;
            printf("id name nice_chess %s\nid author Michael Jorgensen\n"
                   "option name OwnBook type check default false\n"
                   "option name Ponder type check default false\n"
                   "option name Strength type spin default 5 min 1 max 5\n"
                   "option name Noise type spin default 0 min 0 max 1000\nuciok\n", nice_chess_version);
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
                   "book [on | off], remote [IP | auto [LIMITS] | auto off | off],\n"
                   "uci, help, quit\n"
                   "In a remote game: new [w | b], move M, go, undo, draw, no, resign\n");
        else if (looks_like_move(cmd))
            do_move(cmd);
        else
            printf("Unknown command: %s. Type help for the commands.\n", cmd);
    }

    if (net_open)
        do_remote("off");
    return 0;
}
