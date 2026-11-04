/***************************************************************
 * mkbook.c: makes the table of the opening book (src/engine/book.c)
 * from its lines (src/engine/book.txt), on the host.
 *
 * Usage: mkbook < book.txt > book_data.c
 *
 * It plays each line with the engine itself, so the keys are the
 * engine's. Each move must be legal, and the key that make_move keeps
 * must be the key computed from scratch. Two positions with the same
 * key are an error too. The weight of a move from a position is the
 * number of lines that play it there.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

// The table this program makes; book.c, which the engine links, needs
// one, so mkbook has an empty one.
const uint32_t book_table[2] = {0, 0};
const int book_size = 0;

#define MAX_ENTRIES 8192

typedef struct
{
    uint32_t key;
    uint32_t move;  // From, to, and the promotion, as book.c has them
    uint32_t weight;
    char fen[ENGINE_FEN_MAX];
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int n_entries;

static int cmp(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->key != y->key)
        return x->key < y->key ? -1 : 1;
    return x->move < y->move ? -1 : x->move > y->move;
}

static uint32_t encode(move_t m)
{
    char buf[8];
    engine_move_to_str(m, buf);
    const char *p = buf[4] ? strchr("qrbn", buf[4]) : NULL;
    uint32_t promo = p ? (uint32_t) (p - "qrbn") + 1 : 0;
    return (uint32_t) engine_move_from(m) | (uint32_t) engine_move_to(m) << 6 | promo << 12;
}

static void add(int line_no, move_t m)
{
    char fen[ENGINE_FEN_MAX];
    engine_get_fen(fen);
    // Up to the move counters, which do not change the key.
    char *end = strrchr(fen, ' ');
    *end = '\0';
    end = strrchr(fen, ' ');
    *end = '\0';
    uint32_t key = engine_hash(), move = encode(m);
    for (int i = 0; i < n_entries; ++i)
    {
        if (entries[i].key != key)
            continue;
        if (strcmp(entries[i].fen, fen))
        {
            fprintf(stderr, "mkbook: line %d: %s and %s have the same key\n",
                    line_no, entries[i].fen, fen);
            exit(1);
        }
        if (entries[i].move == move)
        {
            entries[i].weight++;
            return;
        }
    }
    if (n_entries == MAX_ENTRIES)
    {
        fprintf(stderr, "mkbook: more than %d entries\n", MAX_ENTRIES);
        exit(1);
    }
    entry_t *e = &entries[n_entries++];
    e->key = key;
    e->move = move;
    e->weight = 1;
    strcpy(e->fen, fen);
}

int main(void)
{
    char line[1024];
    int line_no = 0, n_lines = 0;
    while (fgets(line, sizeof line, stdin))
    {
        ++line_no;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        engine_new_game();
        int n_moves = 0;
        for (char *w = strtok(line, " \t\r\n"); w; w = strtok(NULL, " \t\r\n"))
        {
            const char *end;
            move_t m = engine_parse_move(w, &end);
            if (!m || *end)
            {
                fprintf(stderr, "mkbook: line %d: %s is not a legal move\n", line_no, w);
                return 1;
            }
            add(line_no, m);
            engine_make_move(m);
            if (engine_hash() != engine_hash_full())
            {
                fprintf(stderr, "mkbook: line %d: the key after %s is wrong\n", line_no, w);
                return 1;
            }
            ++n_moves;
        }
        n_lines += n_moves > 0;
    }

    qsort(entries, (size_t) n_entries, sizeof entries[0], cmp);
    int positions = 0;
    for (int i = 0; i < n_entries; ++i)
        positions += i == 0 || entries[i].key != entries[i - 1].key;

    printf("// The table of the opening book: %d lines, %d positions, %d moves.\n"
           "// Made by tools/mkbook.c from src/engine/book.txt; src/engine/book.c\n"
           "// says what an entry is.\n\n"
           "#include <stdint.h>\n\n"
           "const int book_size = %d;\n\n"
           "const uint32_t book_table[] =\n{\n",
           n_lines, positions, n_entries, n_entries);
    for (int i = 0; i < n_entries; ++i)
    {
        uint32_t w = entries[i].weight > 0xFFFF ? 0xFFFF : entries[i].weight;
        printf("    0x%08x, 0x%08x,\n", (unsigned) entries[i].key,
               (unsigned) (entries[i].move | w << 16));
    }
    printf("};\n");
    fprintf(stderr, "mkbook: %d lines, %d positions, %d moves\n", n_lines, positions, n_entries);
    return 0;
}
