/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  v4l2_capture.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/23/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 08:00:06 PM"
 *                 
 ********************************************************************************/

#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define BUFFER_COUNT 4

typedef struct
{
	void *addr;
	int length;
}Buffer;

static Buffer buffers[BUFFER_COUNT];

static int xioctl(int fd,int request,void *arg)
{
	int ret;
	do
	{
		ret=ioctl(fd,request,arg);
	}while(ret<0);
	return ret;
}

int v4l2_capture_init(V4L2Capture *cap, const char *device, int width, int height)
{
	memset(cap,0,sizeof(V4L2Capture));
	cap->fd=open(device,O_RDWR);
	if(cap->fd<0)
	{
		perror("open video");
		return -1;
	}

	struct v4l2_format fmt;
	memset(&fmt,0,sizeof(fmt));
	fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width=width;
	fmt.fmt.pix_mp.height=height;
	fmt.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;
	fmt.fmt.pix_mp.field=V4L2_FIELD_NONE;

	if(xioctl(cap->fd,VIDIOC_S_FMT,&fmt)<0)
	{
		perror("VIDIOC_S_FMT");
		return -1;
	}

	cap->width = fmt.fmt.pix_mp.width;
	cap->height = fmt.fmt.pix_mp.height;
	cap->nplanes = fmt.fmt.pix_mp.num_planes;
	printf("capture format %dx%d planes=%d\n",
			cap->width,
			cap->height,
			cap->nplanes);

	struct v4l2_requestbuffers req;
	memset(&req,0,sizeof(req));
	req.count=BUFFER_COUNT;
	req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory=V4L2_MEMORY_MMAP;

	if(xioctl(cap->fd,VIDIOC_REQBUFS,&req)<0)
	{
		perror("REQBUFS");
		return -1;
	}

	for(int i=0;i<req.count;i++)
	{
		struct v4l2_buffer buf;
		struct v4l2_plane planes[VIDEO_MAX_PLANES];
		memset(&buf,0,sizeof(buf));
		memset(planes,0,sizeof(planes));

		buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory=V4L2_MEMORY_MMAP;
		buf.index=i;
		buf.length=cap->nplanes;
		buf.m.planes=planes;

		xioctl(cap->fd,VIDIOC_QUERYBUF,&buf);

		buffers[i].length=planes[0].length;
		buffers[i].addr=mmap(
				NULL,
				planes[0].length,
				PROT_READ|PROT_WRITE,
				MAP_SHARED,
				cap->fd,
				planes[0].m.mem_offset);

		if(buffers[i].addr==MAP_FAILED)
		{
			perror("mmap");
			return -1;
		}

		xioctl(cap->fd,VIDIOC_QBUF,&buf);
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	xioctl(cap->fd,VIDIOC_STREAMON,&type);

	return 0;
}

int v4l2_capture_get_frame(V4L2Capture *cap, uint8_t **data, int *size)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	memset(&buf,0,sizeof(buf));
	memset(planes,0,sizeof(planes));
	buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory=V4L2_MEMORY_MMAP;
	buf.length=cap->nplanes;
	buf.m.planes=planes;

	if(xioctl(cap->fd,VIDIOC_DQBUF,&buf)<0)
	{
		perror("DQBUF");
		return -1;
	}

	*data=buffers[buf.index].addr;
	*size=planes[0].bytesused;

	xioctl(cap->fd,VIDIOC_QBUF,&buf);

	return 0;
}

void v4l2_capture_close(V4L2Capture *cap)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ioctl(cap->fd,VIDIOC_STREAMOFF,&type);
	for(int i=0;i<BUFFER_COUNT;i++)
	{
		if(buffers[i].addr)
		{
			munmap(buffers[i].addr, buffers[i].length);
		}
	}
	close(cap->fd);
}

