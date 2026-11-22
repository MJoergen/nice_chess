/***************************************************************
 * nice.c: the platform, on NICE: niceemu, or a board such as the
 * MEGA65 under NiceMonMEGA65's monitor.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <nice.h>
#include "platform.h"

uint32_t platform_micros(void)
{
    return (uint32_t) nice_micros();
}

// In niceemu with --step-time the cycle counter and the microsecond
// counter both count steps (nice_cpu's SYSTEM.md §3). On a board both
// count real time, the cycles at the clock's rate, 100 MHz on the
// MEGA65. So the ratio of the two over a millisecond tells them apart,
// once, at the first call. Without --step-time niceemu counts the
// host's time, and is taken for a board.
int platform_on_board(void)
{
    static int known, board;
    if (!known)
    {
        uint32_t c0 = (uint32_t) nice_cycles();
        uint32_t u0 = (uint32_t) nice_micros();
        while ((uint32_t) nice_micros() - u0 < 1000)
            ;
        uint32_t cycles = (uint32_t) nice_cycles() - c0;
        uint32_t us = (uint32_t) nice_micros() - u0;
        board = cycles / us >= 10;
        known = 1;
    }
    return board;
}

// 300 million steps in niceemu, about 7 seconds there and 5.5 on the
// MEGA65, which runs about 54 million steps a second of this program.
// On a board, where the clock is real, 5 seconds.
uint32_t platform_move_time(void)
{
    return platform_on_board() ? 5000000 : 300000000;
}

// The MEGA65 runs about 54 million steps a second of this program.
uint32_t platform_clock_second(void)
{
    return platform_on_board() ? 1000000 : 54000000;
}

// On a board, the clock and the cycle counter at the first call, which
// the time the player took to load and start the program sets.
uint32_t platform_seed(void)
{
    return platform_on_board() ? (uint32_t) nice_micros() ^ (uint32_t) nice_cycles() : 1;
}

int platform_pollc(void)
{
    return nice_pollc();
}

int platform_getc(void)
{
    return nice_getc();
}

void platform_putc(int c)
{
    nice_putc(c);
}

void platform_wait(int console)
{
    (void) console;
}

// The network: the UDP entries of NiceMonMEGA65's monitor, through the
// stubs of net.nasm, in a build with -D NICE_NET. The monitor's
// dispatch table is a JMP to an address at each even address, whose
// first word is 0x0003D800; nice_cpu's stand-in for a monitor in the
// tests in niceemu has none, and then the network is not there.
#ifdef NICE_NET

int nc_udp_open(unsigned port);
int nc_udp_send(unsigned ip, unsigned port, unsigned buf, unsigned n);
int nc_udp_recv(unsigned buf, unsigned n, unsigned src);
int nc_udp_close(void);
unsigned nc_words(const unsigned *p);

#define JMP_WORD 0x0003D800u
#define NET_MAX  192

static unsigned net_buf[NET_MAX];
static unsigned net_src[2];
static unsigned net_port;
static int net_open;

int platform_net_open(unsigned port)
{
    // Entries 0 and 16 to 19: the reset and the UDP entries.
    if (nice_peek(0) != JMP_WORD)
        return -1;
    for (unsigned a = 0x20; a <= 0x26; a += 2)
        if (nice_peek(a) != JMP_WORD)
            return -1;
    int r = nc_udp_open(port);
    if (r < 0)
        return r;
    net_port = port;
    net_open = 1;
    return 0;
}

int platform_net_send(uint32_t ip, const char *buf, int n)
{
    if (!net_open)
        return -1;
    if (n > NET_MAX)
        n = NET_MAX;
    for (int i = 0; i < n; i++)
        net_buf[i] = (unsigned char) buf[i];
    return nc_udp_send(ip, net_port, nc_words(net_buf), (unsigned) n);
}

int platform_net_recv(char *buf, int n, uint32_t *ip)
{
    if (!net_open)
        return -1;
    if (n > NET_MAX)
        n = NET_MAX;
    int r = nc_udp_recv(nc_words(net_buf), (unsigned) n, nc_words(net_src));
    for (int i = 0; i < r; i++)
        buf[i] = (char) net_buf[i];
    if (r > 0)
        *ip = net_src[0];
    return r;
}

void platform_net_close(void)
{
    if (net_open)
        nc_udp_close();
    net_open = 0;
}

#else

int platform_net_open(unsigned port)
{
    (void) port;
    return -1;
}

int platform_net_send(uint32_t ip, const char *buf, int n)
{
    (void) ip, (void) buf, (void) n;
    return -1;
}

int platform_net_recv(char *buf, int n, uint32_t *ip)
{
    (void) buf, (void) n, (void) ip;
    return -1;
}

void platform_net_close(void)
{
}

#endif
