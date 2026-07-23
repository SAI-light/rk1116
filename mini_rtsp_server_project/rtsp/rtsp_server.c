/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/19/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/19/2026 12:34:38 AM"
 *                 
 ********************************************************************************/

#include "rtsp_server.h"
#include "rtsp_session.h"
#include "rtsp_media.h"
#include "sdp_builder.h"
#include "rtsp_request.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 4096

static int check_request_complete(char *buffer)
{
	if(strstr(buffer,"\r\n\r\n") || strstr(buffer,"\n\n"))
	{
		if(strstr(buffer,"CSeq:"))
			return 1;
	}
	return 0;
}

static void send_response(int client_fd, const char *response)
{
	printf("====== SEND RESPONSE ======\n");
	printf("%s\n", response);
	printf("===========================\n");
	send(client_fd, response, strlen(response),0);
}

static void handle_options(int client_fd, char *request)
{
	char response[512];
	int cseq = rtsp_request_get_cseq(request);
	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
			"\r\n",
			cseq
			);

	send_response(client_fd,response);
}

static void handle_describe(int client_fd, char *request, RTSPMedia *media)
{
	char response[2048];

	char sdp[2048];
	if(sdp_builder_build(
				media->reader->sps,
				media->reader->sps_size,
				media->reader->pps,
				media->reader->pps_size,
				sdp,
				sizeof(sdp)) < 0)
	{
		printf("build sdp failed\n");
		return;
	}
	printf("Generated SDP:\n%s\n", sdp);

	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Content-Type: application/sdp\r\n"
			"Content-Length: %ld\r\n"
			"\r\n"
			"%s",
			rtsp_request_get_cseq(request),
			strlen(sdp),
			sdp);

	send_response(client_fd,response);
}

static void handle_setup(int client_fd, RTSPSession *session, char *request)
{
	char response[512];
	rtsp_session_parse_transport(session, request);
	strcpy(session->client_ip,"127.0.0.1");

	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Transport: RTP/AVP;unicast;"
			"client_port=%d-%d;"
			"server_port=%d-%d\r\n"
			"Session: %u\r\n"
			"\r\n",
			rtsp_request_get_cseq(request),
			session->client_rtp_port,
			session->client_rtcp_port,
			session->server_rtp_port,
			session->server_rtcp_port,
			session->session_id
			);

	send_response(client_fd,response);
}

static void handle_play(int client_fd, RTSPMedia *media, RTSPSession *session, char *request)
{
	rtsp_media_start(media, session);
	char response[512];
	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: %d\r\n"
			"Session: %d\r\n"
			"\r\n",
			rtsp_request_get_cseq(request),
			session->session_id
			);

	send_response(client_fd,response);
	printf("PLAY received\n");
}

static void process_request(int client_fd,char *request, RTSPSession *session, RTSPMedia *media)
{
	printf("process_request enter\n");
	if(strncmp(request,"OPTIONS",7)==0)
	{
		printf("OPTIONS\n");
		handle_options(client_fd, request);
	}
	else if(strncmp(request,"DESCRIBE",8)==0)
	{
		printf("DESCRIBE\n");
		handle_describe(client_fd, request, media);
	}
	else if(strncmp(request,"SETUP",5)==0)
	{
		printf("SETUP\n");
		handle_setup(client_fd, session, request);
	}
	else if(strncmp(request,"PLAY",4)==0)
	{
		printf("PLAY\n");
		handle_play(client_fd, media, session, request);
	}
}

int rtsp_server_start(int port)
{
	int server_fd;
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(server_fd <0)
	{
		perror("socket");
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr))<0)
	{
		perror("bind");
		return -1;
	}
	listen(server_fd,5);
	printf("RTSP server listen %d\n",port);

	RTSPMedia media;
	if(rtsp_media_init(&media)<0)
	{
		return -1;
	}

	while(1)
	{
		int client_fd;
		client_fd = accept(server_fd, NULL, NULL);
		if(client_fd<0)
			continue;
		printf("client connected\n");

		RTSPSession session;
		rtsp_session_init(&session);
		session.client_fd = client_fd;

		char buffer[BUFFER_SIZE];
		int offset = 0;

		while(1)
		{
			memset(buffer,0,sizeof(buffer));

			while (1)
			{
				int len = recv(client_fd, buffer + offset, sizeof(buffer)-offset-1, 0);
				printf("recv len=%d\n",len);

				if(len <=0)
				{
					 perror("recv");
					 break;
				}

				offset += len;
				buffer[offset]=0;

				printf("DATA:\n%s\n",buffer);

				//if(check_request_complete(buffer))
				if(rtsp_request_complete(buffer))
				{	
					process_request(client_fd, buffer, &session, &media);

					memset(buffer,0,sizeof(buffer));
					offset=0;
				}
			}
		}
		close(client_fd);
	}
	return 0;
}
