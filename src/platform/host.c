/***************************************************************
 * host.c: the platform, on a PC.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "platform.h"

static int net_sock = -1;
static unsigned net_port;

uint32_t platform_micros(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ts.tv_sec * 1000000u + (uint32_t) (ts.tv_nsec / 1000);
}

uint32_t platform_move_time(void)
{
    return 1000000; // 1 second
}

uint32_t platform_clock_second(void)
{
    return 1000000;
}

int platform_on_board(void)
{
    return 0;
}

uint32_t platform_seed(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t) ts.tv_sec ^ (uint32_t) ts.tv_nsec;
}

int platform_pollc(void)
{
    struct pollfd p = {0, POLLIN, 0};
    if (poll(&p, 1, 0) <= 0)
        return -2;
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

int platform_getc(void)
{
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

void platform_putc(int c)
{
    unsigned char b = (unsigned char) c;
    if (write(1, &b, 1) != 1)
        return;
}

void platform_wait(int console)
{
    struct pollfd p[2] = {{net_sock, POLLIN, 0}, {0, POLLIN, 0}};
    poll(p, console ? 2 : 1, 5);
}

int platform_net_open(unsigned port)
{
    platform_net_close();
    net_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (net_sock < 0)
        return -1;
    int one = 1;
    setsockopt(net_sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t) port);
    const char *ip = getenv("NICE_CHESS_IP");
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (ip && inet_pton(AF_INET, ip, &a.sin_addr) != 1)
    {
        platform_net_close();
        return -2;
    }
    if (bind(net_sock, (struct sockaddr *) &a, sizeof a) < 0)
    {
        platform_net_close();
        return -3;
    }
    net_port = port;
    return 0;
}

int platform_net_send(uint32_t ip, const char *buf, int n)
{
    if (net_sock < 0)
        return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t) net_port);
    a.sin_addr.s_addr = htonl(ip);
    return sendto(net_sock, buf, (size_t) n, 0, (struct sockaddr *) &a, sizeof a) == n ? 0 : -4;
}

int platform_net_recv(char *buf, int n, uint32_t *ip)
{
    if (net_sock < 0)
        return -1;
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    ssize_t r = recvfrom(net_sock, buf, (size_t) n, MSG_DONTWAIT, (struct sockaddr *) &a, &len);
    if (r < 0)
        return 0;
    *ip = ntohl(a.sin_addr.s_addr);
    return (int) r;
}

void platform_net_close(void)
{
    if (net_sock >= 0)
        close(net_sock);
    net_sock = -1;
}
