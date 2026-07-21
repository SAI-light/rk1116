/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 11:11:21 PM"
 *                 
 ********************************************************************************/

#include "rtp_sender.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int rtp_sender_init(RTPSender *sender, const char *ip, int port)
{
	memset(sender,0,sizeof(RTPSender));
	sender->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	printf("sockfd=%d\n",sender->sockfd);
	if(sender->sockfd <0)
	{
		perror("socket");
		return -1;
	}

	strcpy(sender->ip,ip);
	sender->port=port;
	printf("RTP sender init %s:%d\n", ip, port);

	return 0;
}

int rtp_sender_send(RTPSender *sender, uint8_t *packet, int size)
{
	printf("rtp_sender_send enter\n");
	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(sender->port);
	addr.sin_addr.s_addr = inet_addr(sender->ip);

	int ret = sendto(sender->sockfd, packet, size, 0, (struct sockaddr*)&addr, sizeof(addr));
	printf(
			"sendto ret=%d size=%d ip=%s port=%d\n",
			ret,
			size,
			sender->ip,
			sender->port
		  );

	if(ret <0)
	{
		perror("sendto");
		return -1;
	}

	return ret;
}

void rtp_sender_close(RTPSender *sender)
{
	if(sender->sockfd>0)
	{
		close(sender->sockfd);
	}
}
