/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  h264_rtp.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 11:54:22 PM"
 *                 
 ********************************************************************************/

#ifndef H264_RTP_H
#define H264_RTP_H
#include <stdint.h>

#define RTP_MAX_PACKET_SIZE 1500 /* 表示整个RTP包大小*/

#define RTP_PAYLOAD_MAX 1400 /*  表示H264 payload最大大小*/

typedef int (*rtp_send_callback)(uint8_t *packet, int size);

int h264_rtp_packet(uint8_t *nalu, int nalu_size, uint8_t *packet, uint16_t seq, uint32_t timestamp);

int h264_rtp_packet_ex(uint8_t *nalu, int nalu_size, uint8_t *packet, uint16_t seq, uint32_t timestamp, int marker);

int h264_rtp_fu_a(uint8_t *nalu, int nalu_size, uint16_t *seq, uint32_t timestamp, rtp_send_callback send);

int h264_rtp_send_nalu(uint8_t *nalu, int nalu_size, uint16_t *seq, uint32_t timestamp, rtp_send_callback send);

#endif
