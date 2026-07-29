/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/23/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 09:31:01 PM"
 *                 
 ********************************************************************************/

#ifndef MPP_ENCODER_H
#define MPP_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_packet.h"
#include "rockchip/mpp_meta.h"

typedef struct
{
	MppCtx ctx;
	MppApi *mpi;
	MppBufferGroup group;

	MppBuffer frm_buf;
	MppBuffer pkt_buf;

	int width;
	int height;
	size_t frame_size;
	int64_t frame_index;
	int initialized;


	uint8_t *header_data;
	size_t header_len;
	int header_pending;
}MppEncoder;

int mpp_encoder_init(MppEncoder *encoder, int width, int height);

int mpp_encoder_encode(MppEncoder *encoder, const uint8_t *nv12, int size, uint8_t **out);

void mpp_encoder_close(MppEncoder *encoder);

#endif
