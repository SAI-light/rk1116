/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_h264_udp.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/18/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/18/2026 10:19:56 PM"
 *                 
 ********************************************************************************/

#include "h264_reader.h"
#include "h264_rtp.h"
#include "rtp_sender.h"

#include <stdio.h>
#include <stdlib.h>

static RTPSender sender;

int send_rtp_packet(uint8_t *packet, int size)
{
	return rtp_sender_send(&sender, packet, size);
}

int main()
{
	printf("H264 RTP UDP test start\n");

	if(rtp_sender_init(&sender, "127.0.0.1", 5000)<0)
	{
		return -1;
	}

	H264Reader *reader;
	reader = h264_reader_open("test.h264");
	if(reader==NULL)
	{
		printf("open h264 failed\n");
		return -1;
	}

	uint16_t seq=100;
	uint32_t timestamp=0;
	H264NALU nalu;

	while(h264_reader_read(reader, &nalu)>0)
	{
		printf("send NALU type=%d size=%d\n", nalu.type, nalu.size);
		h264_rtp_send_nalu(nalu.data, nalu.size, &seq, timestamp, send_rtp_packet);
		free(nalu.data);
		timestamp+=3000;
	}

	h264_reader_close(reader);
	rtp_sender_close(&sender);
	printf("test end\n");

	return 0;
}
