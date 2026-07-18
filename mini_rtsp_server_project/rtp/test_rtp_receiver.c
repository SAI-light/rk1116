/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_rtp_receiver.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/18/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/18/2026 09:55:58 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
	int sockfd;
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd <0)
	{
		perror("socket");
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(5000);

	if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr))<0)
	{
		perror("bind");
		return -1;
	}

	printf("UDP receiver listen 5000...\n");

	uint8_t buf[1500];
	int len;
	len = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
	printf("recv size=%d\n", len);

	for(int i=0;i<len;i++)
	{
		printf("%02X ", buf[i]);
	}
	printf("\n");

	close(sockfd);
	return 0;
}
