#include "h264_rtp.h"
#include "../h264/h264_reader.h"

#include <stdio.h>
#include <stdint.h>

int test_send(uint8_t *packet, int size)
{
	static int count = 0;

	printf("\n");
	printf("RTP packet %d\n", count++);
	printf("size=%d\n", size);
	printf("header:");
	for(int i=0;i<12;i++)
	{
		printf(" %02X",packet[i]);
	}
	printf("\n");

	printf("payload:");
	for(int i=12;i<size && i<16;i++)
	{
		printf(" %02X",packet[i]);
	}
	printf("\n");

	uint8_t nal_type = packet[12] & 0x1F;
	if(nal_type == 28)
	{
		uint8_t fu_header = packet[13];
		printf("FU-A header=0x%02X\n", fu_header);

		if(fu_header & 0x80)
			printf("Start fragment\n");

		if(fu_header & 0x40)
			printf("End fragment\n");
	}
	else
	{
		printf("Single NALU type=%d\n", nal_type);
	}

	return size;
}

int main()
{
	printf("test start\n");

	if(h264_reader_open("test.h264") < 0)
	{
		printf("open h264 failed\n");
		return -1;
	}
	printf("open h264 success\n");

	H264NALU nalu;
	uint16_t seq = 100;
	uint32_t timestamp = 90000;

	while(h264_reader_read(&nalu) > 0)
	{
		printf("\n====================\n");
		printf("NALU type=%d size=%d\n", nalu.type, nalu.size);
		h264_rtp_send_nalu(nalu.data, nalu.size, &seq, timestamp, test_send);
		timestamp += 3600;
	}

	h264_reader_close();
	return 0;
}
