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

static int mpp_encoder_get_header(MppEncoder *encoder)
{
	MPP_RET ret;
	MppPacket packet = NULL;
	uint8_t *packet_mem = NULL;
	void *pos;
	size_t len;
	int result = -1;

	/*  packet_mem = malloc(encoder->frame_size);
	if (packet_mem == NULL)
	{
		printf("malloc H264 header packet memory failed, size=%zu\n", encoder->frame_size);
		return -1;
	}

	ret = mpp_packet_init(&packet, packet_mem, encoder->frame_size);
	if (ret != MPP_OK || packet == NULL)
	{
		printf("mpp_packet_init(header) failed ret=%d\n", ret);
		goto CLEANUP;
	}*/

	ret = mpp_packet_init_with_buffer(&packet, encoder->pkt_buf);
	printf("header packet init ret=%d packet=%p\n", ret, packet);

	if (ret != MPP_OK || packet == NULL)
	{
		printf("mpp_packet_init_with_buffer header failed ret=%d\n", ret);
		return -1;
	}

	mpp_packet_set_length(packet, 0);

	ret = encoder->mpi->control(encoder->ctx, MPP_ENC_GET_HDR_SYNC, packet);
	printf("MPP_ENC_GET_HDR_SYNC ret=%d\n", ret);
	if (ret != MPP_OK)
	{
		printf("MPP_ENC_GET_HDR_SYNC failed ret=%d\n", ret);
		goto CLEANUP;
	}

	pos = mpp_packet_get_pos(packet);
	len = mpp_packet_get_length(packet);
	printf("H264 header packet pos=%p len=%zu size=%zu\n", pos, len, mpp_packet_get_size(packet));
	if (pos == NULL || len == 0)
	{
		printf("H264 header empty, continue first frame\n");

		encoder->header_data = NULL;
		encoder->header_len = 0;
		encoder->header_pending = 0;

		goto CLEANUP;
	}

	encoder->header_data = malloc(len);
	if (encoder->header_data == NULL)
	{
		printf("malloc H264 header failed, size=%zu\n", len);
		goto CLEANUP;
	}

	memcpy(encoder->header_data, pos, len);
	encoder->header_len = len;
	encoder->header_pending = 1;

	printf("MPP H264 header size=%zu\n", len);

	result = 0;

CLEANUP:
	if (packet != NULL)
		mpp_packet_deinit(&packet);

	free(packet_mem);

	return result;
}

int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
	MPP_RET ret;
	MppPollType timeout = MPP_POLL_BLOCK;
	MppEncCfg cfg = NULL;
	//MppBufferType buffer_type = MPP_BUFFER_TYPE_ION;
	MppBufferType buffer_type = MPP_BUFFER_TYPE_DRM;

	if (encoder == NULL || width <= 0 || height <= 0)
	{
		printf("invalid encoder parameter\n");
		return -1;
	}

	memset(encoder, 0, sizeof(*encoder));
	encoder->width = width;
	encoder->height = height;
	encoder->frame_size = (size_t)width * (size_t)height * 3U / 2U;

	printf("before mpp_create\n");

	ret=mpp_create(&encoder->ctx,&encoder->mpi);
	printf("after mpp_create: ret=%d ctx=%p mpi=%p\n", ret, encoder->ctx, encoder->mpi);

	if(ret != MPP_OK || encoder->ctx == NULL || encoder->mpi == NULL)
	{
		printf("mpp_create failed ret=%d\n", ret);
		goto FAIL;
	}

	ret = mpp_init(encoder->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
	printf("after mpp_init: ret=%d\n", ret);

	if(ret != MPP_OK)
	{
		printf("mpp_init failed ret=%d\n", ret);
		goto FAIL;
	}
	encoder->initialized = 1;

	ret = encoder->mpi->control(encoder->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
	printf("MPP_SET_OUTPUT_TIMEOUT ret=%d\n", ret);
	if(ret != MPP_OK)
	{
		printf("MPP_SET_OUTPUT_TIMEOUT failed ret=%d\n", ret);
		goto FAIL;
	}

	ret = mpp_buffer_group_get_internal(&encoder->group, buffer_type);
	if(ret != MPP_OK)
	{
		printf("mpp_buffer_group_get_internal failed ret=%d\n", ret);
		goto FAIL;
	}

	ret = mpp_buffer_get(encoder->group, &encoder->frm_buf, encoder->frame_size);
	if(ret != MPP_OK)
	{
		printf("mpp_buffer_get input frame failed ret=%d\n", ret);
		goto FAIL;
	}

	ret = mpp_buffer_get(encoder->group, &encoder->pkt_buf, encoder->frame_size);
	if(ret != MPP_OK)
	{
		printf("mpp_buffer_get output packet failed ret=%d\n", ret);
		goto FAIL;
	}

	printf("frm_buf=%p size=%zu ptr=%p\n", encoder->frm_buf, mpp_buffer_get_size(encoder->frm_buf), mpp_buffer_get_ptr(encoder->frm_buf));
	printf("pkt_buf=%p size=%zu ptr=%p\n", encoder->pkt_buf, mpp_buffer_get_size(encoder->pkt_buf), mpp_buffer_get_ptr(encoder->pkt_buf));

	ret = mpp_enc_cfg_init(&cfg);
	if (ret != MPP_OK || cfg == NULL)
	{
		printf("mpp_enc_cfg_init failed ret=%d\n", ret);
		goto FAIL;
	}

	ret = encoder->mpi->control(encoder->ctx, MPP_ENC_GET_CFG, cfg);
	if (ret != MPP_OK)
	{
		printf("MPP_ENC_GET_CFG failed ret=%d\n", ret);
		goto FAIL;
	}

	mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);

	mpp_enc_cfg_set_s32(cfg, "prep:width", width);
	mpp_enc_cfg_set_s32(cfg, "prep:height", height);
	mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", width);
	mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", height);
	mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

	mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
	mpp_enc_cfg_set_s32(cfg, "rc:bps_target", 4000000);
	mpp_enc_cfg_set_s32(cfg, "rc:bps_max", 4500000);
	mpp_enc_cfg_set_s32(cfg, "rc:bps_min",3000000);
	mpp_enc_cfg_set_s32(cfg, "rc:gop", 30);

	mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", 30);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", 30);
	mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

	ret = encoder->mpi->control(encoder->ctx, MPP_ENC_SET_CFG, cfg);
	if(ret != MPP_OK)
	{
		printf("MPP_ENC_SET_CFG failed ret=%d\n", ret);
		goto FAIL;
	}

	mpp_enc_cfg_deinit(cfg);
	cfg = NULL;

	ret = mpp_encoder_get_header(encoder);
	printf("mpp_encoder_get_header ret=%d\n", ret);
	if (ret != 0)
	{
		printf("warning: H264 header unavailable, continue encoding first frame\n");

		if (encoder->header_data != NULL)
		{
			free(encoder->header_data);
			encoder->header_data = NULL;
		}
		encoder->header_len = 0;
		encoder->header_pending = 0;
	}

	printf("MPP encoder init success, frame_size=%zu\n", encoder->frame_size); 
	return 0;

FAIL:
	if (cfg != NULL)
		mpp_enc_cfg_deinit(cfg);

	mpp_encoder_close(encoder);
	return -1;
}

int mpp_encoder_encode(MppEncoder *encoder, const uint8_t *nv12, int size, uint8_t **out)
{
	printf("start encode NV12, input=%d expected=%zu\n", size, encoder->frame_size);

	MPP_RET ret;
	MppFrame frame = NULL;
	MppPacket packet = NULL;
	MppMeta meta = NULL;
	//uint8_t *packet_mem = NULL;
	void *frm_ptr;
	void *pkt_pos;
	size_t pkt_len;
	size_t total_len;
	size_t prefix_len;

	int result = -1;

	if (encoder == NULL || encoder->ctx == NULL || encoder->mpi == NULL ||
			encoder->frm_buf == NULL ||	nv12 == NULL || out == NULL)
	{
		printf("mpp_encoder_encode invalid parameter\n");
		return -1;
	}

	*out = NULL;

	if (size < 0 || (size_t)size < encoder->frame_size)
	{
		printf("NV12 size too small: input=%d expected=%zu\n", size, encoder->frame_size);
		return -1;
	}

	frm_ptr = mpp_buffer_get_ptr(encoder->frm_buf);
	if (frm_ptr == NULL)
	{
		printf("mpp_buffer_get_ptr input frame failed\n");
		return -1;
	}

	/*  ret = mpp_buffer_sync_begin(encoder->frm_buf);
	if (ret != MPP_OK)
	{
		printf("mpp_buffer_sync_begin failed ret=%d\n", ret);
		return -1;
	}*/

	memcpy(frm_ptr, nv12, encoder->frame_size);

	/*  ret = mpp_buffer_sync_end(encoder->frm_buf);
	if (ret != MPP_OK)
	{
		printf("mpp_buffer_sync_end failed ret=%d\n", ret);
		return -1;
	}*/

	ret = mpp_frame_init(&frame);
	if (ret != MPP_OK || frame == NULL)
	{
		printf("mpp_frame_init failed ret=%d\n", ret);
		goto CLEANUP;
	}

	mpp_frame_set_width(frame, encoder->width);
	mpp_frame_set_height(frame, encoder->height);
	mpp_frame_set_hor_stride(frame, encoder->width);
	mpp_frame_set_ver_stride(frame, encoder->height);
	mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);

	mpp_frame_set_buf_size(frame, encoder->frame_size);

	mpp_frame_set_pts(frame, encoder->frame_index++);
	mpp_frame_set_eos(frame, 0);
	mpp_frame_set_buffer(frame, encoder->frm_buf);

	/*  packet_mem = malloc(encoder->frame_size);
	if (packet_mem == NULL)
	{
		printf("malloc output packet memory failed, size=%zu\n", encoder->frame_size);
		goto CLEANUP;
	}

	ret = mpp_packet_init(&packet, packet_mem, encoder->frame_size);
	if (ret != MPP_OK || packet == NULL)
	{
		printf("mpp_packet_init output failed ret=%d\n", ret);
		goto CLEANUP;
	}*/

 	ret = mpp_packet_init_with_buffer(&packet, encoder->pkt_buf);
	printf("output packet init ret=%d packet=%p\n", ret, packet);
	if (ret != MPP_OK || packet == NULL)
	{
		printf("mpp_packet_init_with_buffer failed ret=%d\n", ret);
		goto CLEANUP;
	}

	mpp_packet_set_length(packet, 0);

	meta = mpp_frame_get_meta(frame);
	if (meta == NULL)
	{
		printf("mpp_frame_get_meta failed\n");
		goto CLEANUP;
	}

	ret = mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);
	if (ret != MPP_OK)
	{
		printf("mpp_meta_set_packet KEY_OUTPUT_PACKET failed ret=%d\n", ret);
		goto CLEANUP;
	}

	ret = encoder->mpi->encode_put_frame(encoder->ctx, frame);
	printf("encode_put_frame ret=%d\n", ret);

	if (ret != MPP_OK)
		goto CLEANUP;

	mpp_frame_deinit(&frame);

	ret = encoder->mpi->encode_get_packet(encoder->ctx, &packet);
	printf("encode_get_packet ret=%d packet=%p\n", ret, packet);
	if (ret != MPP_OK || packet == NULL)
		goto CLEANUP;

	pkt_pos = mpp_packet_get_pos(packet);
	pkt_len = mpp_packet_get_length(packet);
	printf("encoded packet pos=%p len=%zu size=%zu\n", pkt_pos, pkt_len,
			mpp_packet_get_size(packet));

	if (pkt_pos == NULL || pkt_len == 0)
	{
		printf("encoded packet is empty\n");
		goto CLEANUP;
	}

	prefix_len = encoder->header_pending ? encoder->header_len : 0;
	total_len = prefix_len + pkt_len;

	*out = malloc(total_len);
	if (*out == NULL)
	{
		printf("malloc H264 output failed, size=%zu\n", total_len);
		goto CLEANUP;
	}

	if (prefix_len > 0)
	{
		memcpy(*out, encoder->header_data, prefix_len);
		encoder->header_pending = 0;
	}

	memcpy(*out + prefix_len, pkt_pos, pkt_len);

	printf("encode H264 packet=%zu total_output=%zu\n", pkt_len, total_len);
	printf("encode_get_packet ret=%d packet=%p\n", ret, packet);

	MppPacket submitted_packet = packet;
	printf("packet before encode=%p\n", submitted_packet);

	result = (int)total_len;

CLEANUP:
	if (frame != NULL)
		mpp_frame_deinit(&frame);

	if (packet != NULL)
		mpp_packet_deinit(&packet);

	//free(packet_mem);

	if (result < 0 && *out != NULL)
	{
		free(*out);
		*out = NULL;
	}

	return result;
}

void mpp_encoder_close(MppEncoder *encoder)
{
	if(encoder == NULL)
		return;

	if (encoder->initialized && encoder->ctx != NULL && encoder->mpi != NULL)
		encoder->mpi->reset(encoder->ctx);

	if (encoder->frm_buf != NULL)
	{
		mpp_buffer_put(encoder->frm_buf);
		encoder->frm_buf = NULL;
	}

	if (encoder->pkt_buf != NULL)
	{
		mpp_buffer_put(encoder->pkt_buf);
		encoder->pkt_buf = NULL;
	}

	if (encoder->group != NULL)
	{
		mpp_buffer_group_put(encoder->group);
		encoder->group = NULL;
	}

	free(encoder->header_data);
	encoder->header_data = NULL;
	encoder->header_len = 0;
	encoder->header_pending = 0;

	if (encoder->ctx != NULL)
	{
		mpp_destroy(encoder->ctx);
		encoder->ctx = NULL;
	}

	encoder->mpi = NULL;
	encoder->initialized = 0;
}
