/***************************************************************
 * prof.c: a profile of a NICE program by function, on the host,
 * from niceemu's trace.
 *
 * Usage: niceemu -t [-i INPUT] PROGRAM.bin | prof PROGRAM.sym [MIN%]
 *
 * PROGRAM.sym is the file of labels that nicebuild.py --symbols
 * writes. Each line of the trace whose first field is an address
 * (8 hex digits) is one step; prof counts it for the label at or
 * below the address. It prints the total, then each label with at
 * least MIN% of the steps (0.1 by default), most steps first: its
 * steps, its share, the steps at its first address, which are its
 * calls, and the steps a call.
 *
 * A function inlined into another counts as that other one. The
 * calls count a jump back to a function's first address too, which
 * nicebuild.py's code does not make.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LABELS 4096

typedef struct
{
    unsigned           addr;
    char               name[64];
    unsigned long long steps;
    unsigned long long calls;
} label_t;

static label_t labels[MAX_LABELS];
static int     count;

static int by_addr(const void *a, const void *b)
{
    unsigned x = ((const label_t *) a)->addr, y = ((const label_t *) b)->addr;
    return x < y ? -1 : x > y;
}

static int by_steps(const void *a, const void *b)
{
    unsigned long long x = ((const label_t *) a)->steps, y = ((const label_t *) b)->steps;
    return x > y ? -1 : x < y;
}

// The label at or below addr, or -1 if addr is below the first.
static int find(unsigned addr)
{
    int lo = 0, hi = count - 1;
    if (count == 0 || addr < labels[0].addr)
        return -1;
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        if (labels[mid].addr <= addr)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Usage: niceemu -t [-i INPUT] PROGRAM.bin | prof PROGRAM.sym [MIN%%]\n");
        return 1;
    }
    double min = argc == 3 ? atof(argv[2]) : 0.1;

    FILE *f = fopen(argv[1], "r");
    if (!f)
    {
        perror(argv[1]);
        return 1;
    }
    while (count < MAX_LABELS
           && fscanf(f, "%x %63s", &labels[count].addr, labels[count].name) == 2)
        ++count;
    if (!feof(f) && count == MAX_LABELS)
    {
        fprintf(stderr, "%s: more than %d labels\n", argv[1], MAX_LABELS);
        return 1;
    }
    fclose(f);
    qsort(labels, count, sizeof labels[0], by_addr);

    char line[512];
    unsigned long long total = 0, outside = 0;
    while (fgets(line, sizeof line, stdin))
    {
        char *end;
        unsigned addr = strtoul(line, &end, 16);
        if (end != line + 8 || *end != ' ')
            continue; // Not a step: the registers after the halt
        ++total;
        int i = find(addr);
        if (i < 0)
        {
            ++outside;
            continue;
        }
        ++labels[i].steps;
        if (addr == labels[i].addr)
            ++labels[i].calls;
    }

    qsort(labels, count, sizeof labels[0], by_steps);
    printf("%-28s %12llu\n", "total", total);
    if (outside)
        printf("%-28s %12llu\n", "(below the first label)", outside);
    for (int i = 0; i < count; ++i)
    {
        double share = total ? 100.0 * labels[i].steps / total : 0;
        if (labels[i].steps == 0 || share < min)
            continue;
        printf("%-28s %12llu %5.1f%% %10llu calls", labels[i].name, labels[i].steps,
               share, labels[i].calls);
        if (labels[i].calls)
            printf(" %9.1f a call", (double) labels[i].steps / labels[i].calls);
        printf("\n");
    }
    return 0;
} // end of main
