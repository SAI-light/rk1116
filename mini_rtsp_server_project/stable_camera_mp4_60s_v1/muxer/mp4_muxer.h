/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mp4_muxer.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/28/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/28/2026 10:05:13 PM"
 *                 
 ********************************************************************************/

#ifndef MP4_MUXER_H
#define MP4_MUXER_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
	int id;
	int width;
	int height;
	int frame_rate;
	int bit_rate;

	int initialized;
	uint64_t frame_count;
	uint64_t key_frame_count;
	int64_t last_pts_us;
} Mp4Muxer;

int mp4_muxer_init(Mp4Muxer *muxer, int muxer_id, const char *output_path, int width, int height, int frame_rate, int bit_rate);

int mp4_muxer_write_h264(Mp4Muxer *muxer, const uint8_t *h264_data, size_t h264_size, int64_t pts_us, int *key_frame_out);

int mp4_muxer_close(Mp4Muxer *muxer);

#endif
