/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.c
 *    Description:  UDP RTP sender with optional fixed local port.
 ********************************************************************************/

#include "rtp_sender.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int rtp_sender_init(RTPSender *sender, const char *ip, int port)
{
    return rtp_sender_init_ex(sender, 0, ip, port);
}

int rtp_sender_init_ex(RTPSender *sender,
                       int local_port,
                       const char *ip,
                       int port)
{
    int reuse = 1;

    if (sender == NULL || ip == NULL ||
        port <= 0 || port > 65535 ||
        local_port < 0 || local_port > 65535)
    {
        return -1;
    }

    memset(sender, 0, sizeof(*sender));
    sender->sockfd = -1;

    sender->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->sockfd < 0)
    {
        perror("RTP socket");
        return -1;
    }

    (void)setsockopt(sender->sockfd,
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     &reuse,
                     sizeof(reuse));

    if (local_port > 0)
    {
        struct sockaddr_in local_addr;

        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons((uint16_t)local_port);

        if (bind(sender->sockfd,
                 (struct sockaddr *)&local_addr,
                 sizeof(local_addr)) < 0)
        {
            printf("bind RTP local port %d failed: %s\n",
                   local_port,
                   strerror(errno));
            rtp_sender_close(sender);
            return -1;
        }
    }

    memset(&sender->remote_addr, 0, sizeof(sender->remote_addr));
    sender->remote_addr.sin_family = AF_INET;
    sender->remote_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET,
                  ip,
                  &sender->remote_addr.sin_addr) != 1)
    {
        printf("invalid RTP destination IP: %s\n", ip);
        rtp_sender_close(sender);
        return -1;
    }

    snprintf(sender->ip, sizeof(sender->ip), "%s", ip);
    sender->port = port;
    sender->local_port = local_port;

    printf("RTP sender init local=%d remote=%s:%d sockfd=%d\n",
           local_port,
           sender->ip,
           sender->port,
           sender->sockfd);

    return 0;
}

int rtp_sender_send(RTPSender *sender,
                    const uint8_t *packet,
                    int size)
{
    int ret;

    if (sender == NULL || sender->sockfd < 0 ||
        packet == NULL || size <= 0)
    {
        return -1;
    }

    ret = (int)sendto(sender->sockfd,
                      packet,
                      (size_t)size,
                      0,
                      (struct sockaddr *)&sender->remote_addr,
                      sizeof(sender->remote_addr));

    if (ret < 0)
    {
        perror("RTP sendto");
        return -1;
    }

    if (ret != size)
    {
        printf("short RTP send: ret=%d expected=%d\n", ret, size);
        return -1;
    }

    return ret;
}

void rtp_sender_close(RTPSender *sender)
{
    if (sender == NULL)
        return;

    if (sender->sockfd >= 0)
        close(sender->sockfd);

    sender->sockfd = -1;
}
