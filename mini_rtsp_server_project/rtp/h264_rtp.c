/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  h264_rtp.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 11:58:26 PM"
 *                 
 ********************************************************************************/

#include "h264_rtp.h"
#include "rtp_packet.h"
#include <string.h>
#include <stdio.h>

#define RTP_MAX_PACKET_SIZE 1500

int h264_rtp_packet(uint8_t *nalu, int nalu_size, uint8_t *packet, uint16_t seq, uint32_t timestamp)
{
	if(nalu_size > 1400)
	{
		printf("NALU too large, need FU-A\n");
		return -1;
	}

	RTPHeader header;
	rtp_header_init(&header, seq, timestamp);

	int len = rtp_packet_pack(&header, nalu, nalu_size, packet);

	return len;
}
