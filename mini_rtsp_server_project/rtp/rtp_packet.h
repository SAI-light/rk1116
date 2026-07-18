/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_packet.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 09:33:53 PM"
 *                 
 ********************************************************************************/

#ifndef RTP_PACKET_H
#define RTP_PACKET_H

#include <stdint.h>

#define RTP_HEADER_SIZE 12

typedef struct
{
	uint8_t version;
	uint8_t payload_type;
	uint8_t marker;
	uint16_t sequence;
	uint32_t timestamp;
	uint32_t ssrc;
}RTPHeader;

/* 创建RTP头 */
void rtp_header_init(RTPHeader *header, uint16_t seq, uint32_t timestamp);

/* 打包RTP */
int rtp_packet_pack(RTPHeader *header, uint8_t *payload, int payload_size, uint8_t *packet);

#endif
