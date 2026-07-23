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

int h264_rtp_packet_ex(uint8_t *nalu, int nalu_size, uint8_t *packet, uint16_t seq, uint32_t timestamp, int marker)
{
	RTPHeader header;
	rtp_header_init(&header, seq, timestamp);

	header.marker = marker;

	return rtp_packet_pack(&header, nalu, nalu_size, packet);
}

int h264_rtp_fu_a(uint8_t *nalu, int nalu_size, uint16_t *seq, uint32_t timestamp, rtp_send_callback send)
{
	uint8_t packet[RTP_MAX_PACKET_SIZE];
	uint8_t nal_header = nalu[0];
	uint8_t fu_indicator = (nal_header & 0xE0) | 28;
	uint8_t nal_type = nal_header & 0x1F;
	int offset = 1;
	int remain = nalu_size - 1;
	int count = 0;

	while(remain > 0)
	{
		int payload_size;
		int marker = 0;

		if(remain <= RTP_PAYLOAD_MAX)
		{
			marker = 1;
		}

		if(remain > RTP_PAYLOAD_MAX)
		{
			payload_size = RTP_PAYLOAD_MAX;
		}
		else
		{
			payload_size = remain;
		}

		uint8_t fu_payload[RTP_PAYLOAD_MAX+2];
		fu_payload[0] = fu_indicator;
		uint8_t fu_header = nal_type;

		if(offset == 1)
		{
			fu_header |= 0x80;
		}

		if(remain <= RTP_PAYLOAD_MAX)
		{
			fu_header |= 0x40;
		}

		fu_payload[1] = fu_header;
		memcpy(fu_payload+2, nalu+offset, payload_size);

		RTPHeader header;
		rtp_header_init(&header, (*seq)++, timestamp);
		header.marker = marker;

		int packet_len = rtp_packet_pack(&header, fu_payload, payload_size+2, packet);
		if(send(packet,packet_len)<0)
		{
			return -1;
		}
		offset += payload_size;
		remain -= payload_size;
		count++;
	}
	return count;
}

int h264_rtp_send_nalu(uint8_t *nalu, int nalu_size, uint16_t *seq, uint32_t timestamp, rtp_send_callback send)
{
	uint8_t packet[RTP_MAX_PACKET_SIZE];

	if(nalu_size <= RTP_PAYLOAD_MAX)
	{
		int len = h264_rtp_packet_ex(nalu, nalu_size, packet, *seq, timestamp, 1);
		if(len < 0)
		{
			return -1;
		}

		send(packet,len);

		(*seq)++;

		return 1;
	}

	return h264_rtp_fu_a(nalu, nalu_size, seq, timestamp, send);
}
