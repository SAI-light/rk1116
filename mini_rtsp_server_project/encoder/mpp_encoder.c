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
#include <stdlib.h>

int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
	memset(encoder,0,sizeof(*encoder));
	encoder->width = width;
	encoder->height = height;

	MPP_RET ret;
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

	ret = mpp_buffer_group_get_internal(&encoder->group, MPP_BUFFER_TYPE_NORMAL);
	if(ret != MPP_OK)
	{
		printf("mpp buffer group init failed\n");
		return -1;
	}

	MppEncCfg cfg;
	mpp_enc_cfg_init(&cfg);

	mpp_enc_cfg_set_s32(cfg, "prep:width", width);
	mpp_enc_cfg_set_s32(cfg, "prep:height", height);
	mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", width);
	mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", height);
	mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

	mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
	mpp_enc_cfg_set_s32(cfg, "rc:bps_target", 4000000);
	mpp_enc_cfg_set_s32(cfg, "rc:bps_max", 4000000);
	mpp_enc_cfg_set_s32(cfg, "rc:bps_min",1000000);

	mpp_enc_cfg_set_s32(cfg, "rc:gop", 30);

	ret = encoder->mpi->control(encoder->ctx, MPP_ENC_SET_CFG, cfg);
	if(ret != MPP_OK)
	{
		printf("control cfg failed\n");
		return -1;
	}

	mpp_enc_cfg_deinit(cfg);
	printf("MPP encoder init success\n");
	return 0;
}

void mpp_encoder_close(MppEncoder *encoder)
{
	if(encoder == NULL)
		return;

	if(encoder->group)
	{
		mpp_buffer_group_put(encoder->group);
		encoder->group=NULL;
	}

	if(encoder->ctx)
	{
		mpp_destroy(encoder->ctx);
		encoder->ctx = NULL;
	}

	encoder->mpi = NULL;
}
