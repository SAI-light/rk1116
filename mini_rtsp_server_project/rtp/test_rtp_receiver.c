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

#define RTP_HEADER_SIZE 12

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

	printf("RTP receiver listen port 5000...\n");

	uint8_t buffer[1500];
	int count = 0;

	while(1)
	{
		int size;
		size = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
		if(size <=0)
			continue;

		printf("\n====================\n");
		printf("RTP packet %d\n", count++);
		printf("packet size=%d\n", size);

		uint8_t version = buffer[0] >> 6;
		uint8_t marker = buffer[1] >> 7;
		uint8_t payload_type = buffer[1] & 0x7f;
		uint16_t seq = (buffer[2]<<8)|buffer[3];
		uint32_t timestamp =
			(buffer[4]<<24)
			|
			(buffer[5]<<16)
			|
			(buffer[6]<<8)
			|
			buffer[7];

		uint32_t ssrc =
			(buffer[8]<<24)
			|
			(buffer[9]<<16)
			|
			(buffer[10]<<8)
			|
			buffer[11];

		printf("RTP Header:\n");
		printf(" Version=%d\n", version);
		printf(" Marker=%d\n", marker);
		printf(" PayloadType=%d\n", payload_type);
		printf(" Sequence=%d\n", seq);
		printf(" Timestamp=%u\n", timestamp);
		printf(" SSRC=0x%08X\n", ssrc);

		uint8_t *payload = buffer+RTP_HEADER_SIZE;
		int payload_size = size-RTP_HEADER_SIZE;
		if(payload_size<=0)
			continue;

		uint8_t nal_type = payload[0]&0x1f;

		printf("H264:\n");
		printf(" Payload size=%d\n", payload_size);
		printf(" NAL type=%d\n", nal_type);

		if(nal_type==28)
		{
			uint8_t fu_header = payload[1];
			int start = fu_header&0x80;
			int end = fu_header&0x40;
			int type = fu_header&0x1f;
			printf(" FU-A:\n");
			printf("  start=%d\n", start?1:0);
			printf("  end=%d\n", end?1:0);
			printf("  original NAL type=%d\n", type);
		}
	}

	close(sockfd);
	return 0;
}
