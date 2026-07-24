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

#include <stdint.h>

#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_packet.h"

typedef struct
{
	MppCtx ctx;
	MppApi *mpi;
	MppBufferGroup group;
	int width;
	int height;
}MppEncoder;

int mpp_encoder_init(MppEncoder *encoder, int width, int height);

int mpp_encoder_encode(MppEncoder *encoder, void *nv12, int size, void **out, int *out_size);

void mpp_encoder_close(MppEncoder *encoder);

#endif
