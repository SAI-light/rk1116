/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  h264_rtp.h
 *    Description:  H264 RTP packetization with single-NAL and FU-A support.
 *                  The extended API lets the caller control the RTP marker bit
 *                  and pass a callback context. Legacy APIs are kept for the
 *                  existing unit tests.
 ********************************************************************************/

#ifndef H264_RTP_H
#define H264_RTP_H

#include <stdint.h>

#define RTP_MAX_PACKET_SIZE 1500
#define RTP_PAYLOAD_MAX     1400

typedef int (*rtp_send_callback)(uint8_t *packet, int size);
typedef int (*rtp_send_callback_ctx)(void *opaque,
                                     const uint8_t *packet,
                                     int size);

int h264_rtp_packet(uint8_t *nalu,
                    int nalu_size,
                    uint8_t *packet,
                    uint16_t seq,
                    uint32_t timestamp);

int h264_rtp_packet_ex(uint8_t *nalu,
                       int nalu_size,
                       uint8_t *packet,
                       uint16_t seq,
                       uint32_t timestamp,
                       int marker);

int h264_rtp_fu_a(uint8_t *nalu,
                  int nalu_size,
                  uint16_t *seq,
                  uint32_t timestamp,
                  rtp_send_callback send);

int h264_rtp_send_nalu(uint8_t *nalu,
                       int nalu_size,
                       uint16_t *seq,
                       uint32_t timestamp,
                       rtp_send_callback send);

/*
 * Send one NAL unit without an Annex-B start code.
 * marker_for_nalu must be 1 only when this NAL is the final NAL of the access
 * unit. For FU-A it is applied only to the final fragment.
 */
int h264_rtp_send_nalu_ctx(const uint8_t *nalu,
                           int nalu_size,
                           uint16_t *seq,
                           uint32_t timestamp,
                           int marker_for_nalu,
                           rtp_send_callback_ctx send,
                           void *opaque);

#endif
