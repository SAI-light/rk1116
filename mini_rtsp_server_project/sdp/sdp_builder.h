/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  sdp_builder.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 04:39:57 PM"
 *                 
 ********************************************************************************/

#ifndef SDP_BUILDER_H
#define SDP_BUILDER_H

#include <stdint.h>

int sdp_builder_build(
		uint8_t *sps,
		int sps_size,
		uint8_t *pps,
		int pps_size,
		char *sdp,
		int sdp_size
		);

#endif
