/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_packet.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 10:29:39 PM"
 *                 
 ********************************************************************************/

#include "rtp_packet.h"
#include <string.h>
#include <arpa/inet.h>

void rtp_header_init(RTPHeader *header, uint16_t seq, uint32_t timestamp)
{
	header->version = 2;
	header->payload_type = 96;
	header->marker = 0;
	header->sequence = seq;
	header->timestamp = timestamp;
	header->ssrc = 0x12345678;
}

int rtp_packet_pack(RTPHeader *header, uint8_t *payload, int payload_size, uint8_t *packet)
{
	/* RTP Header 12 bytes */
	packet[0]=0x80;

	/* marker + payload type */
	packet[1]=(header->marker<<7) | (header->payload_type);

	uint16_t seq = htons(header->sequence);
	memcpy(packet+2, &seq, 2);

	uint32_t timestamp = htonl(header->timestamp);
	memcpy(packet+4, &timestamp, 4);

	uint32_t ssrc = htonl(header->ssrc);
	memcpy(packet+8, &ssrc, 4);

	/* copy H264 payload */
	memcpy(packet+12, payload,payload_size);

	return payload_size+12;
}
