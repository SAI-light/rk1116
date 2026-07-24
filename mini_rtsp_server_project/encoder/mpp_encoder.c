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
#include "rockchip/rk_venc_cfg.h"
#include "rockchip/rk_venc_rc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
	printf("encoder ptr=%p\n",encoder);

	memset(encoder,0,sizeof(*encoder));

	printf("before mpp_create\n");

	encoder->width = width;
	encoder->height = height;

	MPP_RET ret;
	ret=mpp_create(&encoder->ctx,&encoder->mpi);
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
	mpp_enc_cfg_set_s32(cfg, "codec:profile", 100);
	mpp_enc_cfg_set_s32(cfg, "codec:level", 40);

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

int mpp_encoder_encode(MppEncoder *encoder, uint8_t *nv12, int size, uint8_t **out)
{
	MPP_RET ret;
	MppBuffer buffer;
	ret = mpp_buffer_get(encoder->group, &buffer, size);
	printf("encode_put_frame ret=%d\n", ret);
	if(ret != MPP_OK)
	{
		printf("mpp_buffer_get failed\n");
		return -1;
	}

	void *ptr = mpp_buffer_get_ptr(buffer);
	memcpy(ptr,nv12,size);

	MppFrame frame;
	mpp_frame_init(&frame);

	mpp_frame_set_width(frame, encoder->width);
	mpp_frame_set_height(frame, encoder->height);
	mpp_frame_set_hor_stride(frame, encoder->width);
	mpp_frame_set_ver_stride(frame, encoder->height);
	mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
	mpp_frame_set_buffer(frame, buffer);
	mpp_frame_set_pts(frame, 0);
	mpp_frame_set_eos(frame, 0);

	ret = encoder->mpi->encode_put_frame(encoder->ctx, frame);
	if(ret != MPP_OK)
	{
		printf("encode_put_frame failed\n");
		return -1;
	}

	MppPacket packet=NULL;
	for(int i=0;i<100;i++)
	{
		ret = encoder->mpi->encode_get_packet(encoder->ctx, &packet);
		printf("encode_get_packet try=%d ret=%d packet=%p\n", i, ret, packet);
		if(packet)
			break;

		usleep(50000);
	}
	if(packet==NULL)
	{
		printf("no packet output\n");
		return -1;
	}


	if(ret != MPP_OK || packet==NULL)
	{
		printf("encode_get_packet failed\n");
		return -1;
	}

	void *data = mpp_packet_get_data(packet);
	int len = mpp_packet_get_length(packet);
	*out = malloc(len);
	memcpy(*out,data,len);
	printf("encode h264 size=%d\n",len);
	mpp_packet_deinit(&packet);
	mpp_buffer_put(buffer);
	mpp_frame_deinit(&frame);
	return len;
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
