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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 4096

static void send_response(int client_fd, const char *response)
{
	send(client_fd, response, strlen(response),0);
}

static void handle_options(int client_fd)
{
	char response[512];
	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: 1\r\n"
			"Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
			"\r\n");

	send_response(client_fd,response);
}

static void handle_describe(int client_fd)
{
	char response[2048];
	const char *sdp =
		"v=0\r\n"
		"o=- 0 0 IN IP4 0.0.0.0\r\n"
		"s=Mini RTSP Server\r\n"
		"t=0 0\r\n"
		"m=video 0 RTP/AVP 96\r\n"
		"a=rtpmap:96 H264/90000\r\n"
		"a=control:track1\r\n";

	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: 2\r\n"
			"Content-Type: application/sdp\r\n"
			"Content-Length: %ld\r\n"
			"\r\n"
			"%s",
			strlen(sdp),
			sdp);

	send_response(client_fd,response);
}

static void handle_setup(int client_fd, RTSPSession *session, char *request)
{
	char response[512];
	rtsp_session_parse_transport(session, request);

	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: 3\r\n"
			"Transport: RTP/AVP;unicast;"
			"client_port=%d-%d;"
			"server_port=%d-%d\r\n"
			"Session: %u\r\n"
			"\r\n",
			session->client_rtp_port,
			session->client_rtcp_port,
			session->server_rtp_port,
			session->server_rtcp_port,
			session->session_id
			);

	send_response(client_fd,response);
}

static void handle_play(int client_fd)
{
	char response[512];
	snprintf(response,
			sizeof(response),
			"RTSP/1.0 200 OK\r\n"
			"CSeq: 4\r\n"
			"Session: 12345678\r\n"
			"\r\n");

	send_response(client_fd,response);
	printf("PLAY received\n");
}

static void process_request(int client_fd,char *request, RTSPSession *session)
{
	if(strncmp(request,"OPTIONS",7)==0)
	{
		printf("OPTIONS\n");
		handle_options(client_fd);
	}
	else if(strncmp(request,"DESCRIBE",8)==0)
	{
		printf("DESCRIBE\n");
		handle_describe(client_fd);
	}
	else if(strncmp(request,"SETUP",5)==0)
	{
		printf("SETUP\n");
		handle_setup(client_fd, session, request);
	}
	else if(strncmp(request,"PLAY",4)==0)
	{
		printf("PLAY\n");
		handle_play(client_fd);
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

		while(1)
		{
			char buffer[BUFFER_SIZE];
			int offset = 0;
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

				if(strstr(buffer,"\r\n\r\n"))
				{	
					process_request(client_fd, buffer, &session);

					memset(buffer,0,sizeof(buffer));
					offset=0;
				}
			}
		}
		close(client_fd);
	}
	return 0;
}
