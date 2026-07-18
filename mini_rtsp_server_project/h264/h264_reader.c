#include "h264_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_start_code(uint8_t *buf, int size)
{
	int i;

	for(i = 0; i < size - 3; i++)
	{
		if(buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x01)
		{
			return i;
		}

		if(i < size-4 && buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x00 && buf[i+3] == 0x01)
		{
			return i;
		}
	}
	return -1;
}

H264Reader *h264_reader_open(const char *filename)
{
	FILE *fp=fopen(filename,"rb");

	if(fp == NULL)
	{
		perror("fopen h264");
		return NULL;
	}

	fseek(fp,0,SEEK_END);
	int size=ftell(fp);
	fseek(fp,0,SEEK_SET);

	H264Reader *reader= malloc(sizeof(H264Reader));

	if(reader==NULL)
	{
		fclose(fp);
		return NULL;
	}

	reader->buffer = malloc(size);
	if(reader->buffer==NULL)
	{
		free(reader);
		fclose(fp);
		return NULL;
	}

	fread(reader->buffer,1,size,fp);
	fclose(fp);

	reader->size=size;
	reader->pos=0;

	return reader;
}

int h264_reader_read(H264Reader *reader, H264NALU *nalu)
{
	if(reader == NULL)
		return -1;

	if(reader->pos >= reader->size)
		return 0;

	uint8_t *buffer= reader->buffer;

	int remain = reader->size-reader->pos;

	/* 第一个start code */
	int start = find_start_code(buffer+reader->pos, remain);

	if(start < 0)
		return 0;

	start += reader->pos;

	int start_len;

	if(buffer[start+2] == 0x01)
	{
		start_len = 3;
	}
	else
	{
		start_len = 4;
	}

	int nalu_start = start + start_len;

	/* 找下一个NALU */
	int next = find_start_code(buffer+nalu_start, reader->size-nalu_start);

	int nalu_end;

	if(next >= 0)
	{
		nalu_end=nalu_start+next;
	}
	else
	{
		nalu_end=reader->size;
	}

	int nalu_size = nalu_end-nalu_start;
	nalu->data = malloc(nalu_size);
	memcpy(nalu->data, buffer+nalu_start, nalu_size);
	nalu->size = nalu_size;
	nalu->type = nalu->data[0]&0x1f;

	reader->pos=nalu_end;


	return 1;
}

void h264_reader_close(H264Reader *reader)
{
	if(reader)
	{
		if(reader->buffer)
			free(reader->buffer);

		free(reader);
	}
}
