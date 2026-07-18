/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_h264_rtp.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/18/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/18/2026 12:02:44 AM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include "h264_rtp.h"

int main()
{
	uint8_t sps[] = {0x67, 0x64, 0x10, 0x28};
	uint8_t packet[1500];
	int len = h264_rtp_packet(sps, sizeof(sps), packet, 1, 9000);

	printf("RTP size=%d\n", len);

	for(int i=0;i<len;i++)
	{
		printf("%02X ", packet[i]);
	}
	printf("\n");

	return 0;
}
