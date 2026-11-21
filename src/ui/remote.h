/***************************************************************
 * remote.h: remote play, a game against another board over UDP
 * (#27). It speaks the protocol below through the platform's network
 * (platform.h), and keeps no game of its own: the front end keeps
 * the game in the engine, and remote.c checks the other side's moves
 * against it. It has no printf, so that it fits the Nexys4DDR.
 *
 * The protocol, version 1. A datagram to UDP port 4511 at both ends
 * is one message, of plain ASCII words separated by single spaces.
 *
 *   HELLO? V K        Is a board there? V is the version of the
 *                     protocol, and K the kind of player: full, or
 *                     mini, which refuses DRAW? and UNDO? and
 *                     ignores the clocks. Sent to one address, or
 *                     to 255.255.255.255.
 *   HELLO! V K        The answer to a HELLO?. A front end does not
 *                     list, or connect to, a board whose V is not
 *                     its own.
 *   NEW S C TC        A new game from the opening position: the
 *                     sender plays C, w or b, and TC is the time
 *                     control, such as none or 3+2.
 *   ACCEPT S          The game of the last NEW starts.
 *   MOVE S P M T FEN  The sender's move M, in coordinate notation
 *                     such as e2e4 or e7e8n, at ply P (0 is white's
 *                     first move); T the milliseconds left on the
 *                     sender's clock after it, 0 without a clock;
 *                     FEN the position after the move.
 *   RESIGN S          The sender resigns.
 *   DRAW? S, DRAW! S  Offers a draw; accepts the offer.
 *   UNDO? S, UNDO! S  Asks to take back the last move; agrees.
 *   NO S              Refuses the last DRAW? or UNDO?.
 *   BYE S             The sender leaves the game.
 *   ACK S             Message S has arrived.
 *
 * S is the sender's sequence number, one more for each message.
 * The messages with an S are sent in order, one at a time: each is
 * sent again every 500 ms until its ACK comes. A message with the S
 * of the last one from the same address is a repeat, which is
 * acknowledged again and otherwise ignored. HELLO?, HELLO!, and ACK
 * are sent once. During a game only the other board's address is
 * heard. The receiver of a MOVE makes the move, and compares the FEN
 * with its own: a move that is not legal, or a FEN that differs,
 * ends the game with an error.
 *
 * A board that has heard nothing from the other board for 5 seconds
 * sends it a HELLO?, and again every 5 seconds. The other board is
 * gone when nothing has come from it for 30 seconds, or a message
 * has waited that long for its ACK.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef REMOTE_H
#define REMOTE_H

#include <stdint.h>
#include "../engine/engine.h"

#define REMOTE_PORT    4511
#define REMOTE_VERSION 1
#define REMOTE_BROADCAST 0xFFFFFFFFu

// The kinds of message that remote_poll returns.
enum
{
    REMOTE_HELLO = 1,   // HELLO!: ip, version (to compare with REMOTE_VERSION), and kind
    REMOTE_NEW,         // color (the sender's: 1 white, -1 black), tc
    REMOTE_ACCEPT,
    REMOTE_MOVE,        // ply, move, ms, fen
    REMOTE_RESIGN,
    REMOTE_DRAW_OFFER,
    REMOTE_DRAW_ACCEPT,
    REMOTE_UNDO_ASK,
    REMOTE_UNDO_AGREE,
    REMOTE_NO,
    REMOTE_BYE
};

// The results of remote_take_move.
enum
{
    REMOTE_OK       = 0,
    REMOTE_ILLEGAL  = -1, // Not a legal move, or not at this ply
    REMOTE_MISMATCH = -2  // The FEN after the move differs
};

typedef struct
{
    int      type;
    uint32_t ip;        // The sender
    int      version;   // HELLO!
    char     kind[8];   // HELLO!: full or mini
    int      color;     // NEW: the sender's, 1 white, -1 black
    char     tc[8];     // NEW
    int      ply;       // MOVE
    char     move[8];   // MOVE
    uint32_t ms;        // MOVE
    char     fen[ENGINE_FEN_MAX]; // MOVE
} remote_msg_t;

// Opens the network at REMOTE_PORT. kind, full or mini, is what this
// side answers to HELLO?. Returns 0, or the platform's error.
int remote_open(const char *kind);

void remote_close(void);

// The other board, whose messages alone are heard after this: 0
// hears everyone, until the front end chooses one.
void remote_connect(uint32_t ip);
uint32_t remote_peer(void);

// Sends HELLO? to an address, or to REMOTE_BROADCAST.
void remote_hello(uint32_t ip);

// Sends a message with an S to the other board, after those before
// it: word is the first word, such as "RESIGN", and args the words
// after S, or "". Returns 0, or -1 if the queue is full or no board
// is chosen.
int remote_send(const char *word, const char *args);

// Sends NEW, with color the sender's (1 white, -1 black), and tc.
int remote_send_new(int color, const char *tc);

// Sends MOVE for the move just made in the engine, with ms on the
// clock.
int remote_send_move(move_t move, uint32_t ms);

// Whether messages wait for their ACK.
int remote_pending(void);

// Whether the other board is gone, for now: nothing has come from it
// for 30 seconds, though remote_poll asks it with a HELLO? after 5
// seconds of silence, or the oldest of the messages has waited 30
// seconds for its ACK. It is back when something comes from it.
int remote_gone(void);

// Receives and sends: the ACKs, the answers to HELLO?, the messages
// that are due again, and the HELLO? to a silent board, whose answer
// it keeps. Does not wait. Returns the next
// message from the other board, or NULL. The message stays valid
// until remote_done; until then the ACKs are taken in, and a new
// message is dropped without its ACK, so that it comes again.
const remote_msg_t *remote_poll(void);
void remote_done(void);

// Makes the move of a MOVE in the engine, and checks its ply and
// FEN: REMOTE_OK, or REMOTE_ILLEGAL or REMOTE_MISMATCH, with the
// position unchanged.
int remote_take_move(const remote_msg_t *m);

// The result of the game in the engine, as "R REASON", such as
// "1-0 checkmate" or "1/2-1/2 repetition", or NULL while it goes on.
const char *remote_result(void);

// The ply of the position in the engine, from its FEN: 0 at the start.
int remote_ply(void);

// Parses A.B.C.D into an IP address, or returns 0. Writes an IP
// address as A.B.C.D to buf, at least 16 bytes.
uint32_t remote_parse_ip(const char *s);
void remote_ip_to_str(uint32_t ip, char *buf);

// Writes n in decimal to buf, and returns the end of what it wrote.
char *remote_utoa(char *buf, uint32_t n);

#endif
