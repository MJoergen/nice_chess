/***************************************************************
 * platform.h: what a front end needs from the machine it runs on,
 * beyond the C library. nice.c is for NICE, and host.c for a PC.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// A clock in microseconds. It wraps after 71 minutes, so only the
// difference of two readings means anything, as uint32_t.
//
// In niceemu, a microsecond is one step (one instruction), which the
// pipelined core on the MEGA65 runs in about 1/70 of a microsecond.
uint32_t platform_micros(void);

// The time a move takes by default, in microseconds of
// platform_micros: real time on a PC and on a board, and a number of
// steps in niceemu, chosen so that the engine does about the same work
// in both.
uint32_t platform_move_time(void);

// The counts of platform_micros in a second of the players' clocks: a
// million on a PC and on a board, where the clock is real, and in
// niceemu 54 million steps, which the MEGA65 runs in a second, so that
// the clocks there show the time the board would take.
uint32_t platform_clock_second(void);

// Whether the program runs on a board, such as the MEGA65, where the
// clock is real and a read of the console returns at once when no
// byte waits, and not in niceemu, where the clock counts steps and a
// read from a file waits. On a PC, 0.
int platform_on_board(void);

// A seed for a choice at random, such as that of the opening book:
// from the clock on a PC and on a board, and always the same in
// niceemu, so that a run there is the same every time.
uint32_t platform_seed(void);

// The next byte of the console's input, if one waits: 0 to 255, or
// -2 if none waits yet, or -1 once the input has ended. From a file,
// or from stdin in niceemu, it waits for the next byte, and never
// returns -2. From a keyboard (niceemu's window, or the board's UART
// and keyboard) it does not wait.
int platform_pollc(void);

// The next byte of the console's input: 0 to 255, or -1 once the
// input has ended. It waits for the byte. It reads no more than the
// byte, so that platform_pollc sees the bytes after it.
int platform_getc(void);

// Writes a byte to the console. On a PC it is written at once.
void platform_putc(int c);

// The network: one UDP endpoint, at one local port, for remote play
// (ui/remote.c). An IP address is a uint32_t, A.B.C.D as A in bits
// 31..24, and 255.255.255.255 sends to everyone on the network. The
// errors are negative. On NICE, nice.c built with -D NICE_NET calls
// the UDP entries of NiceMonMEGA65's monitor, and without it, or
// without the monitor, the endpoint does not open. On a PC, host.c
// binds the endpoint to the address in the environment variable
// NICE_CHESS_IP, or to every address of the PC without it, so that two
// programs on one PC can have the same port at 127.0.0.1 and 127.0.0.2.

// Opens the endpoint at the port. Returns 0, or an error.
int platform_net_open(unsigned port);

// Sends n bytes as a datagram to the IP address, at the port of the
// endpoint. Returns 0, or an error.
int platform_net_send(uint32_t ip, const char *buf, int n);

// Copies up to n bytes of the next datagram to buf, and its source's
// IP address to *ip. Does not wait. Returns the number of bytes, 0 if
// none waits, or an error.
int platform_net_recv(char *buf, int n, uint32_t *ip);

// Closes the endpoint.
void platform_net_close(void);

#endif // PLATFORM_H
