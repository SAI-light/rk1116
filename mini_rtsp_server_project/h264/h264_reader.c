#include "h264_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *h264_fp = NULL;

static int find_start_code(uint8_t *buf, int size)
{
	int i;

	for(i = 0; i < size - 3; i++)
	{
		if(buf[i] == 0x00 && buf[i+1] == 0x00)
		{
			if(buf[i+2] == 0x01)
			{
				return 3;
			}

			if(i < size-4 && buf[i+2] == 0x00 && buf[i+3] == 0x01)
			{
				return 4;
			}
		}
	}
	return 0;
}

int h264_reader_open(const char *filename)
{
	h264_fp = fopen(filename,"rb");

	if(h264_fp == NULL)
	{
		perror("fopen h264");
		return -1;
	}

	return 0;
}

int h264_reader_read(H264NALU *nalu)
{
	static uint8_t buffer[MAX_NALU_SIZE];

	int len;

	if(h264_fp == NULL)
		return -1;

	/* 读取一块数据 */
	len = fread(buffer,1,sizeof(buffer),h264_fp);

	if(len <= 0)
	{
		return -1;
	}

	/* 第一个start code */
	int start_len = find_start_code(buffer,len);

	if(start_len == 0)
		return -1;

	int nalu_start = start_len;

	/* 找下一个NALU */
	int next = find_start_code(buffer+nalu_start, len-nalu_start);

	int nalu_size;

	if(next > 0)
	{
		nalu_size = next;
	}
	else
	{
		nalu_size = len-nalu_start;
	}

	/* 保存NALU */
	nalu->data = malloc(nalu_size);

	memcpy(nalu->data, buffer+nalu_start, nalu_size);

	nalu->size = nalu_size;

	nalu->type = nalu->data[0]&0x1f;

	return 0;
}

void h264_reader_close()
{
	if(h264_fp)
	{
		fclose(h264_fp);
		h264_fp=NULL;
	}
}
