/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_receiver.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 11:38:34 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main()
{
	int fd;
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	if(fd <0)
	{
		perror("socket");
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr,0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5000);
	addr.sin_addr.s_addr = INADDR_ANY;

	if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) <0)
	{
		perror("bind");
		return -1;
	}

	printf("UDP receiver listening port 5000...\n");

	uint8_t buffer[1500];

	while(1)
	{
		int len = recvfrom(fd, buffer, sizeof(buffer), 0, NULL, NULL);

		if(len>0)
		{
			printf("Received packet size=%d\n", len);

			for(int i=0;i<len;i++)
			{
				printf("%02X ", buffer[i]);
			}
			printf("\n\n");
		}
	}

	close(fd);
	return 0;
}
