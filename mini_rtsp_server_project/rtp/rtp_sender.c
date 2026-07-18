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

static int udp_fd = -1;

static struct sockaddr_in dest_addr;

int rtp_sender_init(const char *ip, int port)
{
	udp_fd = socket(AF_INET, SOCK_DGRAM, 0);

	if(udp_fd < 0)
	{
		perror("socket");
		return -1;
	}

	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(port);

	if(inet_pton(AF_INET, ip, &dest_addr.sin_addr) <= 0)
	{
		perror("inet_pton");
		return -1;
	}

	printf("RTP sender init %s:%d\n", ip, port);

	return 0;
}

int rtp_sender_send(uint8_t *packet, int size)
{
	if(udp_fd <0)
	{
		return -1;
	}

	int ret = sendto(udp_fd, packet, size, 0,
			(struct sockaddr*)&dest_addr,
			sizeof(dest_addr));

	if(ret <0)
	{
		perror("sendto");
		return -1;
	}

	return ret;
}

void rtp_sender_close()
{
	if(udp_fd >=0)
	{
		close(udp_fd);
		udp_fd=-1;
	}
}

