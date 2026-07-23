/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/23/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 11:06:04 PM"
 *                 
 ********************************************************************************/

#include "mpp_encoder.h"

#include <stdio.h>
#include <string.h>


int mpp_encoder_init(MPPEncoder *encoder, int width, int height)
{
	memset(encoder,0,sizeof(MPPEncoder));
	encoder->width = width;
	encoder->height = height;

	MPP_RET ret;
	ret = mpp_create(&encoder->ctx, &encoder->mpi);
	if(ret != MPP_OK)
	{
		printf("mpp_create failed\n");
		return -1;
	}

	ret = mpp_init(encoder->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
	if(ret != MPP_OK)
	{
		printf("mpp_init failed\n");
		return -1;
	}
	printf("MPP encoder init success\n");

	return 0;
}

void mpp_encoder_close(MPPEncoder *encoder)
{
	if(encoder->ctx)
	{
		mpp_destroy(encoder->ctx);
	}
}
