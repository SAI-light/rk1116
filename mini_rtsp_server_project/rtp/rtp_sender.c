/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: rtp_sender.c
 * Description: UDP RTP sender with optional fixed local port.
 ********************************************************************************/

#include "rtp_sender.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MODULE_NAME "rtp"

int rtp_sender_init(RTPSender *sender,
                    const char *ip,
                    int port)
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
        LOG_ERROR(MODULE_NAME, "invalid RTP sender parameter");
        return -1;
    }

    memset(sender, 0, sizeof(*sender));
    sender->sockfd = -1;

    sender->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sender->sockfd < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "create RTP socket failed");
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
            LOG_ERROR_ERRNO(MODULE_NAME,
                            errno,
                            "bind RTP local port %d failed",
                            local_port);
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
        LOG_ERROR(MODULE_NAME, "invalid RTP destination IP: %s", ip);
        rtp_sender_close(sender);
        return -1;
    }

    snprintf(sender->ip, sizeof(sender->ip), "%s", ip);
    sender->port = port;
    sender->local_port = local_port;

    LOG_INFO(MODULE_NAME,
             "sender initialized: local=%d remote=%s:%d socket=%d",
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
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "send RTP packet failed");
        return -1;
    }

    if (ret != size)
    {
        LOG_ERROR(MODULE_NAME,
                  "short RTP send: sent=%d expected=%d",
                  ret,
                  size);
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
