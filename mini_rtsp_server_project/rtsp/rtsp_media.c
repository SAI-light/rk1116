/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_media.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/21/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/21/2026 03:15:26 PM"
 *                 
 ********************************************************************************/

#include "rtsp_media.h"
#include "h264_reader.h"
#include "h264_rtp.h"
#include "rtp_sender.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

static RTPSender sender;

static int media_send_callback(uint8_t *packet, int size)
{
	return rtp_sender_send(&sender, packet, size);
}

static void *media_thread(void *arg)
{
	RTSPSession *session = (RTSPSession *)arg;
	printf("media thread start\n");

	if(rtp_sender_init(&sender, session->client_ip, session->client_rtp_port) < 0)
	{
		return NULL;
	}

	H264Reader *reader;
	reader=h264_reader_open( "test.h264");
	if(!reader)
	{
		printf("open h264 failed\n");
		return NULL;
	}

	uint16_t seq=100;
	uint32_t timestamp=0;

	while(session->playing)
	{
		H264NALU nalu;
		if(h264_reader_read(reader, &nalu)<=0)
		{
			printf("end h264 file\n");
			break;
		}
		printf("send nalu type=%d size=%d\n", nalu.type, nalu.size);

		h264_rtp_send_nalu(nalu.data, nalu.size, &seq, timestamp, media_send_callback);

		timestamp += 3000;
		free(nalu.data);
		usleep(40000);
	}

	h264_reader_close(reader);
	rtp_sender_close(&sender);
	return NULL;
}

int rtsp_media_start(RTSPSession *session)
{
	pthread_t tid;
	session->playing=1;

	pthread_create(&tid, NULL, media_thread, session);
	pthread_detach(tid);

	return 0;
}

int rtsp_media_stop(RTSPSession *session)
{
	session->playing=0;
	return 0;
}
