/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_sdp.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 07:57:58 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdint.h>

#include "sdp_builder.h"

int main()
{
	uint8_t sps[] =
	{
		0x67,0x42,0x00,0x1f,0xe5
	};

	uint8_t pps[] =
	{
		0x68,0xce,0x06,0xe2
	};

	char sdp[2048];

	int ret = sdp_builder_build(
			sps,
			sizeof(sps),
			pps,
			sizeof(pps),
			sdp,
			sizeof(sdp));

	if(ret < 0)
	{
		printf("build sdp failed\n");
		return -1;
	}

	printf("%s\n",sdp);
	return 0;
}
