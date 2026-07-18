#ifndef H264_READER_H
#define H264_READER_H

#include <stdint.h>

#define MAX_NALU_SIZE (1024*1024)

typedef struct
{
	uint8_t *data;     // NALU数据
	int      size;     // NALU长度
	int      type;     // NALU类型
}H264NALU;

int h264_reader_open(const char *filename);

int h264_reader_read(H264NALU *nalu);

void h264_reader_close();

#endif
