/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  sdp_builder.c
 *    Description:  Build H264 SDP from runtime SPS/PPS.
 ********************************************************************************/

#include "sdp_builder.h"
#include "base64.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int sdp_builder_build(uint8_t *sps,
                      int sps_size,
                      uint8_t *pps,
                      int pps_size,
                      char *sdp,
                      int sdp_size)
{
    char sps_base64[512];
    char pps_base64[512];
    unsigned int profile_idc;
    unsigned int profile_iop;
    unsigned int level_idc;
    int len;

    if (sps == NULL || pps == NULL || sdp == NULL ||
        sps_size < 4 || pps_size <= 0 || sdp_size <= 0)
    {
        return -1;
    }

    memset(sps_base64, 0, sizeof(sps_base64));
    memset(pps_base64, 0, sizeof(pps_base64));

    if (base64_encode(sps,
                      sps_size,
                      sps_base64,
                      sizeof(sps_base64)) < 0)
    {
        return -1;
    }

    if (base64_encode(pps,
                      pps_size,
                      pps_base64,
                      sizeof(pps_base64)) < 0)
    {
        return -1;
    }

    /* SPS includes the NAL header at byte 0. */
    profile_idc = sps[1];
    profile_iop = sps[2];
    level_idc = sps[3];

    len = snprintf(
        sdp,
        (size_t)sdp_size,
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=Mini RTSP Live Camera\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=framerate:25\r\n"
        "a=fmtp:96 packetization-mode=1;"
        "profile-level-id=%02X%02X%02X;"
        "sprop-parameter-sets=%s,%s\r\n"
        "a=control:track1\r\n",
        profile_idc,
        profile_iop,
        level_idc,
        sps_base64,
        pps_base64);

    if (len < 0 || len >= sdp_size)
        return -1;

    return len;
}
