/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_session.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/19/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/19/2026 01:14:18 AM"
 *                 
 ********************************************************************************/

#include "rtsp_session.h"
#include <stdio.h>
#include <string.h>

void rtsp_session_init(RTSPSession *session)
{
	memset(session,0,sizeof(RTSPSession));
	session->session_id = 12345678;

	session->server_rtp_port = 5000;
	session->server_rtcp_port = 5001;
}

int rtsp_session_parse_transport(RTSPSession *session, const char *request)
{
	const char *p;

	p = strstr(request,"client_port=");
	if(p == NULL)
	{
		printf("client_port not found\n");
		return -1;
	}

	int rtp_port;
	int rtcp_port;
	sscanf(p, "client_port=%d-%d", &rtp_port, &rtcp_port);
	session->client_rtp_port = rtp_port;
	session->client_rtcp_port = rtcp_port;

	printf("Parsed client RTP port : %d\n", rtp_port);
	printf("Parsed client RTCP port: %d\n", rtcp_port);

	return 0;
}
