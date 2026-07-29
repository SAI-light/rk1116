/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.h
 *    Description:  UDP RTP sender.
 ********************************************************************************/

#ifndef RTP_SENDER_H
#define RTP_SENDER_H

#include <netinet/in.h>
#include <stdint.h>

typedef struct
{
    int sockfd;
    char ip[64];
    int port;
    int local_port;
    struct sockaddr_in remote_addr;
} RTPSender;

/* Compatibility API: use an ephemeral local UDP port. */
int rtp_sender_init(RTPSender *sender, const char *ip, int port);

/* Bind the sender to the RTSP-advertised server RTP port. */
int rtp_sender_init_ex(RTPSender *sender,
                       int local_port,
                       const char *ip,
                       int port);

int rtp_sender_send(RTPSender *sender,
                    const uint8_t *packet,
                    int size);

void rtp_sender_close(RTPSender *sender);

#endif
