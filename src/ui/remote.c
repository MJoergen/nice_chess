/***************************************************************
 * remote.c: remote play over UDP, the protocol of remote.h.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <string.h>
#include "../platform/platform.h"
#include "remote.h"

// The longest message: MOVE with S, P, M, T of their most digits,
// and the longest FEN.
#define MSG_MAX 160
// The messages that wait for their ACK, or to be sent.
#define QUEUE 4

static const char *my_kind;
static uint32_t peer;

// The messages with an S, in order: the first is sent, and sent again
// until its ACK comes.
static char queue[QUEUE][MSG_MAX];
static uint32_t queue_seq[QUEUE];
static int queue_len;
static uint32_t seq_out;     // The S of the last message queued
static uint32_t sent_at;     // When the first was last sent
static uint32_t first_at;    // When the first was first sent
static int sent;             // Whether the first has been sent

// The last S taken in, and from where, to ignore its repeats.
static uint32_t seq_in, seq_in_ip;
static int seq_in_known;

// Whether the other board is there: when it was last heard, and when
// it was last asked, with a HELLO? whose answer is not reported.
static uint32_t heard_at, asked_at;
static int asked;
static int silent;

static remote_msg_t msg;
static int held;
static char in[MSG_MAX + 1];

char *remote_utoa(char *buf, uint32_t n)
{
    char t[10];
    int i = 0;
    do
        t[i++] = (char) ('0' + n % 10);
    while ((n /= 10));
    while (i)
        *buf++ = t[--i];
    *buf = '\0';
    return buf;
}

// The number at *s, and *s past it; *ok is set to 0 if no digit is there.
static uint32_t number(const char **s, int *ok)
{
    uint32_t n = 0;
    if (**s < '0' || **s > '9')
        *ok = 0;
    while (**s >= '0' && **s <= '9')
        n = n * 10 + (uint32_t) (*(*s)++ - '0');
    return n;
}

uint32_t remote_parse_ip(const char *s)
{
    uint32_t ip = 0;
    int ok = 1;
    for (int i = 0; i < 4; i++)
    {
        uint32_t b = number(&s, &ok);
        if (b > 255 || (i < 3 && *s++ != '.'))
            return 0;
        ip = ip << 8 | b;
    }
    return ok && (*s == '\0' || *s == ' ') ? ip : 0;
}

void remote_ip_to_str(uint32_t ip, char *buf)
{
    for (int i = 3; i >= 0; i--)
    {
        buf = remote_utoa(buf, ip >> (8 * i) & 255);
        if (i)
            *buf++ = '.';
    }
}

static void send_text(uint32_t ip, const char *s)
{
    platform_net_send(ip, s, (int) strlen(s));
}

static void send_hello(uint32_t ip, const char *word)
{
    char b[32], *p = b;
    strcpy(p, word);
    p += strlen(p);
    *p++ = ' ';
    p = remote_utoa(p, REMOTE_VERSION);
    *p++ = ' ';
    strcpy(p, my_kind);
    send_text(ip, b);
}

static void send_ack(uint32_t ip, uint32_t seq)
{
    char b[16];
    strcpy(b, "ACK ");
    remote_utoa(b + 4, seq);
    send_text(ip, b);
}

int remote_open(const char *kind)
{
    my_kind = kind;
    peer = 0;
    queue_len = 0;
    held = 0;
    seq_in_known = 0;
    asked = silent = 0;
    seq_out = platform_micros() % 1000000; // Not the same after a restart
    return platform_net_open(REMOTE_PORT);
}

void remote_close(void)
{
    platform_net_close();
}

void remote_connect(uint32_t ip)
{
    if (ip != peer)
    {
        queue_len = 0;
        heard_at = asked_at = platform_micros();
        asked = silent = 0;
    }
    peer = ip;
}

uint32_t remote_peer(void)
{
    return peer;
}

void remote_hello(uint32_t ip)
{
    asked = 0; // The answer is the front end's
    send_hello(ip, "HELLO?");
}

int remote_send(const char *word, const char *args)
{
    if (queue_len == QUEUE || !peer || strlen(word) + strlen(args) > MSG_MAX - 13)
        return -1;
    char *p = queue[queue_len];
    strcpy(p, word);
    p += strlen(p);
    *p++ = ' ';
    p = remote_utoa(p, ++seq_out);
    if (*args)
    {
        *p++ = ' ';
        strcpy(p, args);
    }
    queue_seq[queue_len] = seq_out;
    if (queue_len++ == 0)
        sent = 0;
    return 0;
}

int remote_send_new(int color, const char *tc)
{
    char b[16];
    b[0] = color > 0 ? 'w' : 'b';
    b[1] = ' ';
    strcpy(b + 2, tc);
    return remote_send("NEW", b);
}

const char *remote_result(void)
{
    int how = engine_result();
    if (how == ENGINE_CHECKMATE)
        return engine_side_to_move() > 0 ? "0-1 checkmate" : "1-0 checkmate";
    if (how == ENGINE_STALEMATE)
        return "1/2-1/2 stalemate";
    if (how == ENGINE_FIFTY)
        return "1/2-1/2 fifty moves";
    if (how == ENGINE_REPETITION)
        return "1/2-1/2 repetition";
    return NULL;
}

int remote_ply(void)
{
    char fen[ENGINE_FEN_MAX];
    engine_get_fen(fen);
    // The side to move, and the move number, the last field.
    const char *s = strchr(fen, ' ') + 1;
    int black = *s == 'b';
    s = strrchr(fen, ' ') + 1;
    int ok = 1;
    return 2 * ((int) number(&s, &ok) - 1) + black;
}

int remote_send_move(move_t move, uint32_t ms)
{
    char b[MSG_MAX - 16], *p = b;
    p = remote_utoa(p, (uint32_t) (remote_ply() - 1));
    *p++ = ' ';
    engine_move_to_str(move, p);
    p += strlen(p);
    *p++ = ' ';
    p = remote_utoa(p, ms);
    *p++ = ' ';
    engine_get_fen(p);
    return remote_send("MOVE", b);
}

int remote_pending(void)
{
    return queue_len > 0;
}

int remote_gone(void)
{
    uint32_t now = platform_micros(), limit = 30u * platform_clock_second();
    if (queue_len && sent && now - first_at >= limit)
        return 1;
    // A silent board stays so until it is heard, however the clock wraps.
    if (peer && now - heard_at >= limit)
        silent = 1;
    return silent;
}

// Asks the other board whether it is there, when nothing has come
// from it for 5 seconds, and every 5 seconds until something does.
static void ask(void)
{
    uint32_t now = platform_micros(), wait = 5u * platform_clock_second();
    if (!peer || now - heard_at < wait || now - asked_at < wait)
        return;
    asked_at = now;
    asked = 1;
    send_hello(peer, "HELLO?");
}

// Sends the first message of the queue when it is due.
static void resend(void)
{
    if (!queue_len)
        return;
    uint32_t now = platform_micros();
    if (sent && now - sent_at < platform_clock_second() / 2)
        return;
    if (!sent)
        first_at = now;
    sent = 1;
    sent_at = now;
    send_text(peer, queue[0]);
}

// The word at *s, copied to buf of n bytes, and *s past it and the
// space after it. 0 if it is empty or too long.
static int word(const char **s, char *buf, int n)
{
    int i = 0;
    while (**s && **s != ' ')
    {
        if (i == n - 1)
            return 0;
        buf[i++] = *(*s)++;
    }
    buf[i] = '\0';
    if (**s == ' ')
        (*s)++;
    return i > 0;
}

static const char *const words[] = {
    "HELLO!", "NEW", "ACCEPT", "MOVE", "RESIGN", "DRAW?", "DRAW!",
    "UNDO?", "UNDO!", "NO", "BYE"
};

// Parses the datagram in `in` from ip into msg. Returns 0 if it is not
// a message to report, after the HELLO?, the ACK, and the repeats,
// which it handles. While a message is held, a new one is dropped
// unacknowledged, and comes again.
static int parse(uint32_t ip)
{
    const char *s = in;
    char w[8];
    int ok = 1;
    if (peer && ip == peer)
    {
        heard_at = platform_micros();
        silent = 0;
    }
    if (!word(&s, w, sizeof w))
        return 0;
    if (!strcmp(w, "HELLO?"))
    {
        send_hello(ip, "HELLO!");
        return 0;
    }
    if (!strcmp(w, "HELLO!") && asked && ip == peer)
    {
        asked = 0; // The answer to ask
        return 0;
    }
    if (held && !strcmp(w, "HELLO!"))
        return 0;
    if (!strcmp(w, "HELLO!"))
    {
        memset(&msg, 0, sizeof msg);
        msg.ip = ip;
        msg.type = REMOTE_HELLO;
        msg.version = (int) number(&s, &ok);
        if (*s == ' ')
            s++;
        word(&s, msg.kind, sizeof msg.kind);
        return ok;
    }
    if (peer && ip != peer)
        return 0;
    uint32_t seq = number(&s, &ok);
    if (!ok)
        return 0;
    if (*s == ' ')
        s++;
    if (!strcmp(w, "ACK"))
    {
        if (queue_len && sent && seq == queue_seq[0])
        {
            queue_len--;
            memmove(queue[0], queue[1], sizeof queue[0] * (size_t) queue_len);
            memmove(queue_seq, queue_seq + 1, sizeof queue_seq[0] * (size_t) queue_len);
            sent = 0;
        }
        return 0;
    }
    if (seq_in_known && seq == seq_in && ip == seq_in_ip)
    {
        send_ack(ip, seq);
        return 0;
    }
    if (held)
        return 0;
    memset(&msg, 0, sizeof msg);
    msg.ip = ip;
    for (int i = 1; i < (int) (sizeof words / sizeof words[0]); i++)
        if (!strcmp(w, words[i]))
            msg.type = REMOTE_HELLO + i;
    if (msg.type == REMOTE_NEW)
    {
        char c[2];
        ok = word(&s, c, sizeof c) && (c[0] == 'w' || c[0] == 'b')
             && word(&s, msg.tc, sizeof msg.tc);
        msg.color = c[0] == 'w' ? 1 : -1;
    }
    else if (msg.type == REMOTE_MOVE)
    {
        msg.ply = (int) number(&s, &ok);
        if (*s == ' ')
            s++;
        ok = ok && word(&s, msg.move, sizeof msg.move);
        msg.ms = number(&s, &ok);
        if (*s == ' ')
            s++;
        ok = ok && strlen(s) < sizeof msg.fen;
        if (ok)
            strcpy(msg.fen, s);
    }
    if (!msg.type || !ok)
        return 0;
    // Taken in: acknowledged now, and its repeats ignored.
    send_ack(ip, seq);
    seq_in = seq;
    seq_in_ip = ip;
    seq_in_known = 1;
    return 1;
}

const remote_msg_t *remote_poll(void)
{
    resend();
    ask();
    uint32_t ip;
    // A few datagrams a call, so that a burst does not wait. While a
    // message is held, the ACKs are still taken in.
    for (int i = 0; i < 4 && !held; i++)
    {
        int n = platform_net_recv(in, MSG_MAX, &ip);
        if (n <= 0)
            break;
        in[n] = '\0';
        held = parse(ip);
    }
    for (int i = 0; i < 4 && held; i++)
    {
        int n = platform_net_recv(in, MSG_MAX, &ip);
        if (n <= 0)
            break;
        in[n] = '\0';
        parse(ip);
    }
    return held ? &msg : NULL;
}

void remote_done(void)
{
    held = 0;
}

int remote_take_move(const remote_msg_t *m)
{
    const char *end;
    move_t move = engine_parse_move(m->move, &end);
    if (!move || *end || m->ply != remote_ply() || engine_make_move(move))
        return REMOTE_ILLEGAL;
    char fen[ENGINE_FEN_MAX];
    engine_get_fen(fen);
    if (strcmp(fen, m->fen))
    {
        engine_undo();
        return REMOTE_MISMATCH;
    }
    return REMOTE_OK;
}
