/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.h
 *    Description:  This file 
 *
 *        Version:  1.1.0(07/29/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 09:31:01 PM"
 *                  2, Add configurable fps/gop/bitrate initializer on "07/29/2026"
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
	int fps;
	int gop;
	int bit_rate;
	int bit_rate_min;
	int bit_rate_max;
	size_t frame_size;
	int64_t frame_index;
	int initialized;


	uint8_t *header_data;
	size_t header_len;
	int header_pending;
}MppEncoder;

/*
 * Compatibility initializer. It preserves the verified v8 defaults:
 * 30 fps, GOP 30, target bitrate 4,000,000 bit/s.
 */
int mpp_encoder_init(MppEncoder *encoder, int width, int height);

/*
 * Configurable initializer used by the unified live-camera pipeline.
 * The encoder frame rate, GOP and MP4 frame rate can now use one setting.
 */
int mpp_encoder_init_ex(MppEncoder *encoder,
                        int width,
                        int height,
                        int fps,
                        int gop,
                        int bit_rate);

int mpp_encoder_encode(MppEncoder *encoder, const uint8_t *nv12, int size, uint8_t **out);

void mpp_encoder_close(MppEncoder *encoder);

#endif
