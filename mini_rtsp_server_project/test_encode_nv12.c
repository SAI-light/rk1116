/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_encode_nv12.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/24/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/24/2026 05:18:15 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "mpp_encoder.h"

int main()
{
	FILE *fp=fopen("frame_nv12.yuv", "rb");
	if(!fp)
	{
		printf("open yuv failed\n");
		return -1;
	}

	int width = 2304;
	int height = 1296;
	int size = width*height*3/2;

	uint8_t *buf = malloc(size);
	fread(buf,1,size,fp);
	fclose(fp);

	MppEncoder encoder;
	if(mpp_encoder_init(&encoder, width, height)<0)
	{
		printf("encoder init failed\n");
		return -1;
	}

	uint8_t *h264=NULL;
	int len = mpp_encoder_encode(&encoder, buf, size, &h264);
	if(len>0)
	{
		FILE *out=fopen("output.h264", "wb");
		fwrite(h264, 1, len, out);
		fclose(out);
		printf("save output.h264 size=%d\n", len);
	}

	free(h264);
	free(buf);
	mpp_encoder_close(&encoder);
	return 0;
}
