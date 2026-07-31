/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  h264_rtp.c
 *    Description:  H264 RTP packetization with correct access-unit marker
 *                  semantics for single-NAL and FU-A packets.
 ********************************************************************************/

#include "h264_rtp.h"
#include "log.h"
#include "rtp_packet.h"

#include <string.h>

typedef struct
{
    rtp_send_callback callback;
} LegacyCallbackAdapter;

static int legacy_send_adapter(void *opaque,
                               const uint8_t *packet,
                               int size)
{
    LegacyCallbackAdapter *adapter = (LegacyCallbackAdapter *)opaque;

    if (adapter == NULL || adapter->callback == NULL)
        return -1;

    return adapter->callback((uint8_t *)packet, size);
}

static int send_single_nalu(const uint8_t *nalu,
                            int nalu_size,
                            uint16_t *seq,
                            uint32_t timestamp,
                            int marker,
                            rtp_send_callback_ctx send,
                            void *opaque)
{
    RTPHeader header;
    uint8_t packet[RTP_MAX_PACKET_SIZE];
    int packet_len;

    rtp_header_init(&header, *seq, timestamp);
    header.marker = marker ? 1U : 0U;

    packet_len = rtp_packet_pack(&header,
                                 (uint8_t *)nalu,
                                 nalu_size,
                                 packet);
    if (packet_len < 0)
        return -1;

    if (send(opaque, packet, packet_len) < 0)
        return -1;

    ++(*seq);
    return 1;
}

static int send_fu_a(const uint8_t *nalu,
                     int nalu_size,
                     uint16_t *seq,
                     uint32_t timestamp,
                     int marker_for_nalu,
                     rtp_send_callback_ctx send,
                     void *opaque)
{
    uint8_t packet[RTP_MAX_PACKET_SIZE];
    uint8_t nal_header;
    uint8_t fu_indicator;
    uint8_t nal_type;
    int offset;
    int remain;
    int count = 0;

    if (nalu_size <= 1)
        return -1;

    nal_header = nalu[0];
    fu_indicator = (uint8_t)((nal_header & 0xe0U) | 28U);
    nal_type = (uint8_t)(nal_header & 0x1fU);
    offset = 1;
    remain = nalu_size - 1;

    while (remain > 0)
    {
        uint8_t fu_payload[RTP_PAYLOAD_MAX + 2];
        uint8_t fu_header = nal_type;
        RTPHeader header;
        int payload_size = remain > RTP_PAYLOAD_MAX
                         ? RTP_PAYLOAD_MAX
                         : remain;
        int is_first = offset == 1;
        int is_last = remain <= RTP_PAYLOAD_MAX;
        int packet_len;

        if (is_first)
            fu_header |= 0x80U;
        if (is_last)
            fu_header |= 0x40U;

        fu_payload[0] = fu_indicator;
        fu_payload[1] = fu_header;
        memcpy(fu_payload + 2, nalu + offset, (size_t)payload_size);

        rtp_header_init(&header, *seq, timestamp);
        header.marker = (is_last && marker_for_nalu) ? 1U : 0U;

        packet_len = rtp_packet_pack(&header,
                                     fu_payload,
                                     payload_size + 2,
                                     packet);
        if (packet_len < 0)
            return -1;

        if (send(opaque, packet, packet_len) < 0)
            return -1;

        ++(*seq);
        offset += payload_size;
        remain -= payload_size;
        ++count;
    }

    return count;
}

int h264_rtp_send_nalu_ctx(const uint8_t *nalu,
                           int nalu_size,
                           uint16_t *seq,
                           uint32_t timestamp,
                           int marker_for_nalu,
                           rtp_send_callback_ctx send,
                           void *opaque)
{
    if (nalu == NULL || nalu_size <= 0 ||
        seq == NULL || send == NULL)
    {
        return -1;
    }

    if (nalu_size <= RTP_PAYLOAD_MAX)
    {
        return send_single_nalu(nalu,
                                nalu_size,
                                seq,
                                timestamp,
                                marker_for_nalu,
                                send,
                                opaque);
    }

    return send_fu_a(nalu,
                     nalu_size,
                     seq,
                     timestamp,
                     marker_for_nalu,
                     send,
                     opaque);
}

int h264_rtp_packet(uint8_t *nalu,
                    int nalu_size,
                    uint8_t *packet,
                    uint16_t seq,
                    uint32_t timestamp)
{
    return h264_rtp_packet_ex(nalu,
                              nalu_size,
                              packet,
                              seq,
                              timestamp,
                              0);
}

int h264_rtp_packet_ex(uint8_t *nalu,
                       int nalu_size,
                       uint8_t *packet,
                       uint16_t seq,
                       uint32_t timestamp,
                       int marker)
{
    RTPHeader header;

    if (nalu == NULL || packet == NULL ||
        nalu_size <= 0 || nalu_size > RTP_PAYLOAD_MAX)
    {
        if (nalu_size > RTP_PAYLOAD_MAX)
            LOG_DEBUG("rtp", "NALU too large for a single RTP packet; FU-A required");
        return -1;
    }

    rtp_header_init(&header, seq, timestamp);
    header.marker = marker ? 1U : 0U;

    return rtp_packet_pack(&header,
                           nalu,
                           nalu_size,
                           packet);
}

int h264_rtp_fu_a(uint8_t *nalu,
                  int nalu_size,
                  uint16_t *seq,
                  uint32_t timestamp,
                  rtp_send_callback send)
{
    LegacyCallbackAdapter adapter;

    adapter.callback = send;
    return h264_rtp_send_nalu_ctx(nalu,
                                  nalu_size,
                                  seq,
                                  timestamp,
                                  1,
                                  legacy_send_adapter,
                                  &adapter);
}

int h264_rtp_send_nalu(uint8_t *nalu,
                       int nalu_size,
                       uint16_t *seq,
                       uint32_t timestamp,
                       rtp_send_callback send)
{
    LegacyCallbackAdapter adapter;

    adapter.callback = send;
    return h264_rtp_send_nalu_ctx(nalu,
                                  nalu_size,
                                  seq,
                                  timestamp,
                                  1,
                                  legacy_send_adapter,
                                  &adapter);
}
