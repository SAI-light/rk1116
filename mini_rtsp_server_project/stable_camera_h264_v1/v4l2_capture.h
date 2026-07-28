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
#include <stddef.h>

#define V4L2_CAPTURE_MAX_PLANES   2
#define V4L2_CAPTURE_MAX_BUFFERS  4

typedef struct
{
	void *addr;
	size_t length;
} V4L2CaptureBuffer;

typedef struct
{
	uint8_t *data;
	size_t size;
	unsigned int index;
	unsigned int sequence;
	int64_t timestamp_us;
	int valid;
} V4L2Frame;

typedef struct
{
	int fd;
	int width;
	int height;
	int nplanes;
	int bytesperline;
	int sizeimage;
	uint32_t pixelformat;

	unsigned int buffer_count;
	int streaming;

	unsigned int fps_numerator;
	unsigned int fps_denominator;

	V4L2CaptureBuffer buffers[V4L2_CAPTURE_MAX_BUFFERS];

	/*  Used only by the legacy single-frame compatibility API. */
	uint8_t *legacy_copy;
	size_t legacy_copy_capacity;
} V4L2Capture;

int v4l2_capture_init(V4L2Capture *cap, const char *device, int width, int height);

/*
 *   Acquire one MMAP frame with VIDIOC_DQBUF.
 *  
 *   On success, frame->data remains owned by the application until
 *   v4l2_capture_release_frame() is called.
 * 
 *   timeout_ms:
 *      > 0  wait at most timeout_ms milliseconds
 *      = 0  poll without waiting
 *      < 0  wait indefinitely
 *  
 */
int v4l2_capture_acquire_frame(V4L2Capture *cap, V4L2Frame *frame, int timeout_ms);

/*  Return a previously acquired frame to the V4L2 driver with VIDIOC_QBUF. */
int v4l2_capture_release_frame(V4L2Capture *cap, V4L2Frame *frame);

/* 
 * Compatibility wrapper for the old single-frame test.
 * It copies one acquired frame to cap-owned memory before QBUF, so callers may
 * continue using the returned pointer until the next call or close().
 * The real-time encoder must use acquire_frame()/release_frame() directly.
 * */
int v4l2_capture_get_frame(V4L2Capture *cap, uint8_t **data, int *size);

void v4l2_capture_close(V4L2Capture *cap);

#endif
