/***************************************************************
 * peer.c: a player of remote play (remote.h) whose engine plays its
 * side: the program of the Nexys4DDR in a game against a MEGA65, and
 * the partner of the host's tests. It refuses DRAW? and UNDO?, and
 * ignores the clocks, as a player of the kind mini.
 *
 * It answers any board's NEW, and plays. It prints its name once its
 * network is open, each move of the game as "move M", both sides',
 * and the end as "result R REASON". It lets go of the other board
 * once the game has ended and its last message has its ACK, at the
 * other board's BYE, and when the other board is gone, so that
 * another board can have the next game.
 * Its console takes these lines, which it reads without waiting, and
 * which end at LF or CR, or both:
 *
 *   hello [IP]     sends HELLO? to IP, or to everyone
 *   connect IP     chooses the other board
 *   new w|b        a new game against it, with this side playing w or b
 *   nodes N        the nodes of a search, 1 or more (MINI_NODES by
 *                  default)
 *   seed N         the seed of the opening book's choices
 *   quit
 *
 * Once its input has ended, and a game has, it quits when nothing
 * has come for 2 seconds and every message has its ACK.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <string.h>
#include "../engine/engine.h"
#include "../platform/platform.h"
#include "remote.h"
#include "version.h"

#ifndef MINI_NODES
#define MINI_NODES 20000
#endif

#define LINE_MAX 64

static char line[LINE_MAX];
static int line_len;
static int line_last;  // The byte before
static int input_ended;

static int my_color;   // 1 white, -1 black
static int playing;    // A game is on
static int asked;      // NEW is sent, and the ACCEPT not yet in
static int done;       // The game is over: let go of the board
static int games;      // Games that have ended
static uint32_t nodes = MINI_NODES;
static uint32_t last_heard;

static void out(const char *s)
{
    while (*s)
        platform_putc(*s++);
}

static void out_ip(uint32_t ip)
{
    char b[16];
    remote_ip_to_str(ip, b);
    out(b);
}

static uint32_t number(const char *s)
{
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (uint32_t) (*s++ - '0');
    return n;
}

// If s starts with the word w, the text after it and a space, else NULL.
static const char *word(const char *s, const char *w)
{
    size_t n = strlen(w);
    if (strncmp(s, w, n) || (s[n] != '\0' && s[n] != ' '))
        return NULL;
    return s[n] ? s + n + 1 : s + n;
}

static void game_over(const char *how)
{
    out("result ");
    out(how);
    out("\n");
    playing = 0;
    done = 1;
    games++;
}

// Ends the game if the position has ended it.
static void check_end(void)
{
    const char *r = remote_result();
    if (r)
        game_over(r);
}

static void print_move(move_t m)
{
    char b[8];
    engine_move_to_str(m, b);
    out("move ");
    out(b);
    out("\n");
}

static void start(int color)
{
    my_color = color;
    playing = 1;
    asked = done = 0;
    out(color > 0 ? "game white\n" : "game black\n");
}

static void command(const char *s)
{
    const char *r;
    if ((r = word(s, "hello")))
        remote_hello(*r ? remote_parse_ip(r) : REMOTE_BROADCAST);
    else if ((r = word(s, "connect")))
    {
        done = 0;
        remote_connect(remote_parse_ip(r));
    }
    else if ((r = word(s, "new")))
    {
        done = 0;
        engine_new_game();
        my_color = *r == 'b' ? -1 : 1;
        playing = 0;
        asked = remote_send_new(my_color, "none") == 0;
        if (!asked)
            out("error no board\n");
    }
    else if ((r = word(s, "nodes")))
    {
        // 0, or no number, would be a search with no limit.
        uint32_t n = number(r);
        if (n)
            nodes = n;
    }
    else if ((r = word(s, "seed")))
        engine_book_seed(number(r));
    else if (word(s, "quit"))
        input_ended = 2;
}

// Reads the console without waiting, and runs each line.
static void console(void)
{
    int c;
    while (input_ended == 0 && (c = platform_pollc()) != -2)
    {
        int prev = line_last;
        line_last = c;
        if (c == -1)
            input_ended = 1;
        else if (c == '\n' && prev == '\r')
            continue; // The LF of a CR LF
        else if (c == '\n' || c == '\r')
        {
            line[line_len] = '\0';
            line_len = 0;
            command(line);
        }
        else if (line_len < LINE_MAX - 1)
            line[line_len++] = (char) c;
    }
}

static void network(void)
{
    const remote_msg_t *m = remote_poll();
    if (!m)
        return;
    last_heard = platform_micros();
    switch (m->type)
    {
    case REMOTE_HELLO:
        out("hello ");
        out_ip(m->ip);
        out(" ");
        out(m->kind);
        if (m->version != REMOTE_VERSION)
            out(" of another version");
        out("\n");
        break;
    case REMOTE_NEW:
        remote_connect(m->ip);
        engine_new_game();
        remote_send("ACCEPT", "");
        start(-m->color);
        break;
    case REMOTE_ACCEPT:
        if (asked)
            start(my_color);
        break;
    case REMOTE_MOVE:
        if (!playing || engine_side_to_move() == my_color)
            break;
        {
            int r = remote_take_move(m);
            if (r == REMOTE_OK)
            {
                out("move ");
                out(m->move);
                out("\n");
                check_end();
            }
            else
            {
                out(r == REMOTE_ILLEGAL ? "error illegal move " : "error FEN after ");
                out(m->move);
                out("\n");
                playing = 0;
                done = 1;
                remote_send("BYE", "");
            }
        }
        break;
    case REMOTE_RESIGN:
        if (playing)
            game_over(my_color > 0 ? "1-0 resigns" : "0-1 resigns");
        break;
    case REMOTE_DRAW_OFFER:
    case REMOTE_UNDO_ASK:
        remote_send("NO", "");
        break;
    case REMOTE_BYE:
        if (playing)
        {
            out("bye\n");
            playing = 0;
        }
        done = 1;
        break;
    default:
        break;
    }
    remote_done();
}

// Plays this side's move, if it is to move.
static void play(void)
{
    if (!playing || engine_side_to_move() != my_color)
        return;
    move_t m = engine_book_move();
    if (!m)
    {
        engine_limits_t limits = {0, nodes, 0};
        m = engine_search(&limits, NULL);
    }
    if (!m || engine_make_move(m))
    {
        game_over("* too long");
        return;
    }
    print_move(m);
    remote_send_move(m, 0);
    check_end();
}

int main(void)
{
    engine_new_game();
    engine_book_seed(platform_seed());
    // The name comes once the network is open, so that whoever waits
    // for it can send a HELLO?, which goes once.
    int err = remote_open("mini");
    out("nice_chess ");
    out(nice_chess_version);
    out(" peer\n");
    if (err)
    {
        out("error network\n");
        return 1;
    }
    last_heard = platform_micros();
    while (input_ended < 2)
    {
        console();
        network();
        play();
        // remote_connect(0) drops the queue, so only once it is empty,
        // unless the other board is gone.
        if (remote_gone())
        {
            out("gone\n");
            if (playing)
                games++;
            playing = asked = 0;
            remote_connect(0);
        }
        if (done && !remote_pending())
        {
            done = 0;
            remote_connect(0);
        }
        platform_wait(!input_ended);
        if (input_ended && games && !playing && !remote_pending()
            && platform_micros() - last_heard >= 2 * platform_clock_second())
            break;
    }
    remote_close();
    return 0;
}
