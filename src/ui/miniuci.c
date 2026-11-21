/***************************************************************
 * miniuci.c: the game of the minimal build, as a subset of UCI
 * (miniuci.h). It has no printf, so that the build has no float
 * formatter, and fits in the Nexys4DDR's 26K words.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <string.h>
#include "../engine/engine.h"
#include "miniuci.h"
#include "version.h"

// The nodes of a search, where go names none: 30 to 45 million steps
// at -O z, under a second on a board.
#ifndef MINI_NODES
#define MINI_NODES 20000
#endif

static char reply[128];

static char *skip_space(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

// If s starts with the word w, the text after it and the spaces that
// follow, and NULL if not.
static char *word(char *s, const char *w)
{
    size_t n = strlen(w);
    if (strncmp(s, w, n) || (s[n] != '\0' && s[n] != ' ' && s[n] != '\t'))
        return NULL;
    return skip_space(s + n);
}

static uint32_t number(const char *s)
{
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (uint32_t) (*s++ - '0');
    return n;
}

void mini_init(uint32_t seed)
{
    engine_new_game();
    engine_book_seed(seed);
}

static const char *position(char *s)
{
    const char *end;
    char *r;
    if ((r = word(s, "startpos")))
    {
        engine_set_fen(ENGINE_START_FEN, NULL);
        s = r;
    }
    else if ((r = word(s, "fen")))
    {
        if (engine_set_fen(r, &end))
            return "info string invalid FEN\n";
        s = skip_space((char *) end);
    }
    else
        return "";

    if (!(r = word(s, "moves")))
        return "";
    for (s = r; *s; s = skip_space((char *) end))
    {
        move_t m = engine_parse_move(s, &end);
        if (!m || (*end != '\0' && *end != ' ' && *end != '\t') || engine_make_move(m))
            return "info string illegal move\n";
    }
    return "";
}

// The result of a game that has ended, as "R REASON".
static const char *result(int how)
{
    if (how == ENGINE_CHECKMATE)
        return engine_side_to_move() > 0 ? "0-1 checkmate" : "1-0 checkmate";
    if (how == ENGINE_STALEMATE)
        return "1/2-1/2 stalemate";
    if (how == ENGINE_FIFTY)
        return "1/2-1/2 fifty moves";
    return "1/2-1/2 repetition";
}

static const char *go(char *s)
{
    engine_limits_t limits = {0, MINI_NODES, 0};
    char *r;
    while (*s)
    {
        if ((r = word(s, "nodes")))
            limits.nodes = number(s = r);
        while (*s && *s != ' ' && *s != '\t')
            s++;
        s = skip_space(s);
    }

    int how = engine_result();
    if (how != ENGINE_PLAYING)
    {
        strcpy(reply, "info string result ");
        strcat(reply, result(how));
        strcat(reply, "\nbestmove 0000\n");
        return reply;
    }
    move_t m = engine_book_move();
    if (!m)
        m = engine_search(&limits, NULL);
    strcpy(reply, "bestmove ");
    engine_move_to_str(m, reply + strlen(reply));
    strcat(reply, "\n");
    return reply;
}

const char *mini_command(char *line)
{
    char *s = skip_space(line), *r;
    if ((r = word(s, "position")))
        return position(r);
    if ((r = word(s, "go")))
        return go(r);
    if (word(s, "isready"))
        return "readyok\n";
    if (word(s, "ucinewgame"))
    {
        engine_new_game();
        return "";
    }
    if (word(s, "uci"))
    {
        strcpy(reply, "id name nice_chess ");
        strcat(reply, nice_chess_version);
        strcat(reply, " mini\nid author Michael Jorgensen\nuciok\n");
        return reply;
    }
    if (word(s, "quit"))
        return NULL;
    return "";
}
