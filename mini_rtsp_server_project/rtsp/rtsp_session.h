/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_session.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/19/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/19/2026 01:12:24 AM"
 *                 
 ********************************************************************************/

#ifndef RTSP_SESSION_H
#define RTSP_SESSION_H

#include <stdint.h>

typedef struct
{
	int client_fd;
	char client_ip[64];
	int client_rtp_port;
	int client_rtcp_port;
	int server_rtp_port;
	int server_rtcp_port;
	uint32_t session_id;
}RTSPSession;

void rtsp_session_init(RTSPSession *session);

int rtsp_session_parse_transport(RTSPSession *session, const char *request);

#endif
