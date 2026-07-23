/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  v4l2_capture.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/23/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 05:33:48 PM"
 *                 
 ********************************************************************************/

#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <stdint.h>

#define VIDEO_MAX_PLANES 2

typedef struct
{
	int fd;
	int width;
	int height;
	int nplanes;
	uint8_t *planes[VIDEO_MAX_PLANES];
	int plane_size[VIDEO_MAX_PLANES];
}V4L2Capture;

int v4l2_capture_init(V4L2Capture *cap, const char *device, int width, int height);

int v4l2_capture_get_frame(V4L2Capture *cap, uint8_t **data, int *size);

void v4l2_capture_close(V4L2Capture *cap);

#endif
