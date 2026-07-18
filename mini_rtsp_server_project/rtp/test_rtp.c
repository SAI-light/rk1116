/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_rtp.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 10:35:41 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include "rtp_packet.h"

int main()
{
	RTPHeader header;

	uint8_t payload[]={0x65, 0x88, 0x99};
	uint8_t packet[1500];

	rtp_header_init(&header, 100, 3600);

	int len = rtp_packet_pack(&header, payload, sizeof(payload), packet);

	printf("RTP packet size=%d\n",len);

	for(int i=0;i<len;i++)
	{
		printf("%02X ", packet[i]);
	}
	printf("\n");

	return 0;
}
