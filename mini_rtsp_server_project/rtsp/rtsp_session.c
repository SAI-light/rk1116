/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: rtsp_session.c
 * Description: Single-client RTSP session state.
 ********************************************************************************/

#include "rtsp_session.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MODULE_NAME "rtsp"

void rtsp_session_init(RTSPSession *session)
{
    if (session == NULL)
        return;

    memset(session, 0, sizeof(*session));
    session->client_fd = -1;
    session->session_id =
        (uint32_t)time(NULL) ^ (uint32_t)getpid() ^ 0x13572468U;
    session->server_rtp_port = 5000;
    session->server_rtcp_port = 5001;
    session->playing = 0;
}

int rtsp_session_parse_transport(RTSPSession *session,
                                 const char *request)
{
    const char *p;
    int rtp_port;
    int rtcp_port;
    int parsed;

    if (session == NULL || request == NULL)
        return -1;

    if (strstr(request, "RTP/AVP/TCP") != NULL ||
        strstr(request, "interleaved=") != NULL)
    {
        LOG_WARN(MODULE_NAME,
                 "RTP over RTSP/TCP is not supported; use RTP/UDP");
        return -1;
    }

    p = strstr(request, "client_port=");
    if (p == NULL)
    {
        LOG_WARN(MODULE_NAME, "SETUP request has no client_port");
        return -1;
    }

    parsed = sscanf(p, "client_port=%d-%d", &rtp_port, &rtcp_port);
    if (parsed == 1)
        rtcp_port = rtp_port + 1;
    else if (parsed != 2)
        return -1;

    if (rtp_port <= 0 || rtp_port > 65535 ||
        rtcp_port <= 0 || rtcp_port > 65535)
    {
        LOG_WARN(MODULE_NAME,
                 "invalid client RTP ports: %d-%d",
                 rtp_port,
                 rtcp_port);
        return -1;
    }

    session->client_rtp_port = rtp_port;
    session->client_rtcp_port = rtcp_port;

    LOG_INFO(MODULE_NAME,
             "client transport: RTP=%d RTCP=%d",
             rtp_port,
             rtcp_port);

    return 0;
}
