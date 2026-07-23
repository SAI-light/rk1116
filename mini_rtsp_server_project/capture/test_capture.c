/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_capture.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/23/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 08:37:28 PM"
 *                 
 ********************************************************************************/

#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{
	V4L2Capture cap;
	if(v4l2_capture_init(&cap, "/dev/video11", 2304, 1296) < 0)
	{
		printf("capture init failed\n");
		return -1;
	}
	printf("capture init success\n");
	fflush(stdout);

	uint8_t *data;
	int size;

	printf("before get frame\n");
	fflush(stdout);

	if(v4l2_capture_get_frame(&cap, &data, &size)<0)
	{
		printf("get frame failed\n");
		return -1;
	}
	printf("frame size=%d\n",size);

	FILE *fp=fopen("frame_nv12.yuv","wb");
	if(fp==NULL)
	{
		perror("fopen");
		return -1;
	}

	fwrite(data,1,size,fp);
	fclose(fp);

	printf("save frame_nv12.yuv success\n");

	sleep(1);
	v4l2_capture_close(&cap);

	return 0;
}
