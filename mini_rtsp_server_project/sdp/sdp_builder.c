/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  sdp_builder.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 04:46:04 PM"
 *                 
 ********************************************************************************/

#include "sdp_builder.h"
#include "base64.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int sdp_builder_build(uint8_t *sps, int sps_size, uint8_t *pps, int pps_size, char *sdp, int sdp_size)
{
	char sps_base64[512];
	char pps_base64[512];
	memset(sps_base64, 0, sizeof(sps_base64));
	memset(pps_base64, 0, sizeof(pps_base64));

	if(base64_encode(sps, sps_size, sps_base64, sizeof(sps_base64)) < 0)
	{
		return -1;
	}

	if(base64_encode(pps, pps_size, pps_base64, sizeof(pps_base64)) < 0)
	{
		return -1;
	}

	int len = snprintf(
			sdp,
			sdp_size,
			"v=0\r\n"
			"o=- 0 0 IN IP4 0.0.0.0\r\n"
			"s=Mini RTSP Server\r\n"
			"t=0 0\r\n"
			"m=video 0 RTP/AVP 96\r\n"
			"a=rtpmap:96 H264/90000\r\n"
			"a=fmtp:96 packetization-mode=1;"
			"sprop-parameter-sets=%s,%s\r\n"
			"a=control:track1\r\n",
			sps_base64,
			pps_base64
			);

	if(len < 0 || len >= sdp_size)
	{
		return -1;
	}

	return len;
}
