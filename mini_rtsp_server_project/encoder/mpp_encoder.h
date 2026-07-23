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

typedef struct
{
	MppCtx ctx;
	MppApi *mpi;
	int width;
	int height;
}MPPEncoder;

int mpp_encoder_init(MPPEncoder *encoder, int width, int height);

int mpp_encoder_encode(MPPEncoder *encoder, uint8_t *nv12, int size, uint8_t **h264, int *h264_size);

void mpp_encoder_close(MPPEncoder *encoder);

#endif
