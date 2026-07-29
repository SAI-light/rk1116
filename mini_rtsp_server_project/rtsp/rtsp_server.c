/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.c
 *    Description:  Single-client RTSP/UDP server for the live camera pipeline.
 ********************************************************************************/

#include "rtsp_server.h"
#include "rtsp_media.h"
#include "rtsp_request.h"
#include "rtsp_session.h"
#include "sdp_builder.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

static int send_all(int client_fd,
                    const char *data,
                    size_t size)
{
    size_t sent = 0U;

    while (sent < size)
    {
        ssize_t ret = send(client_fd,
                           data + sent,
                           size - sent,
                           0);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;

            perror("RTSP send");
            return -1;
        }

        if (ret == 0)
            return -1;

        sent += (size_t)ret;
    }

    return 0;
}

static int send_response(int client_fd,
                         const char *response)
{
    printf("====== SEND RESPONSE ======\n");
    printf("%s\n", response);
    printf("===========================\n");

    return send_all(client_fd,
                    response,
                    strlen(response));
}

static int send_simple_ok(int client_fd,
                          int cseq,
                          const RTSPSession *session)
{
    char response[512];

    if (session != NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "RTSP/1.0 200 OK\r\n"
                 "CSeq: %d\r\n"
                 "Session: %u\r\n"
                 "\r\n",
                 cseq,
                 session->session_id);
    }
    else
    {
        snprintf(response,
                 sizeof(response),
                 "RTSP/1.0 200 OK\r\n"
                 "CSeq: %d\r\n"
                 "\r\n",
                 cseq);
    }

    return send_response(client_fd, response);
}

static int send_unsupported_transport(int client_fd,
                                      int cseq)
{
    char response[256];

    snprintf(response,
             sizeof(response),
             "RTSP/1.0 461 Unsupported Transport\r\n"
             "CSeq: %d\r\n"
             "\r\n",
             cseq);

    return send_response(client_fd, response);
}

static int handle_options(int client_fd,
                          char *request)
{
    char response[512];
    int cseq = rtsp_request_get_cseq(request);

    snprintf(response,
             sizeof(response),
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "Public: OPTIONS, DESCRIBE, SETUP, PLAY, "
             "GET_PARAMETER, TEARDOWN\r\n"
             "\r\n",
             cseq);

    return send_response(client_fd, response);
}

static int handle_describe(int client_fd,
                           char *request,
                           RTSPMedia *media)
{
    char response[3072];
    char sdp[2048];
    int sdp_len;
    int response_len;

    if (media->sps == NULL || media->pps == NULL)
    {
        printf("live SPS/PPS is not ready\n");
        return -1;
    }

    sdp_len = sdp_builder_build(media->sps,
                                (int)media->sps_size,
                                media->pps,
                                (int)media->pps_size,
                                sdp,
                                sizeof(sdp));
    if (sdp_len < 0)
    {
        printf("build live SDP failed\n");
        return -1;
    }

    printf("Generated live SDP:\n%s\n", sdp);

    response_len = snprintf(response,
                            sizeof(response),
                            "RTSP/1.0 200 OK\r\n"
                            "CSeq: %d\r\n"
                            "Content-Type: application/sdp\r\n"
                            "Content-Length: %d\r\n"
                            "\r\n"
                            "%s",
                            rtsp_request_get_cseq(request),
                            sdp_len,
                            sdp);

    if (response_len < 0 || response_len >= (int)sizeof(response))
        return -1;

    return send_response(client_fd, response);
}

static int handle_setup(int client_fd,
                        RTSPSession *session,
                        char *request)
{
    char response[512];
    int cseq = rtsp_request_get_cseq(request);

    if (rtsp_session_parse_transport(session, request) != 0)
        return send_unsupported_transport(client_fd, cseq);

    snprintf(response,
             sizeof(response),
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "Transport: RTP/AVP;unicast;"
             "client_port=%d-%d;"
             "server_port=%d-%d;"
             "ssrc=12345678\r\n"
             "Session: %u\r\n"
             "\r\n",
             cseq,
             session->client_rtp_port,
             session->client_rtcp_port,
             session->server_rtp_port,
             session->server_rtcp_port,
             session->session_id);

    return send_response(client_fd, response);
}

static int handle_play(int client_fd,
                       RTSPMedia *media,
                       RTSPSession *session,
                       char *request)
{
    char response[512];
    int cseq = rtsp_request_get_cseq(request);

    if (session->client_rtp_port <= 0)
    {
        printf("PLAY received before successful SETUP\n");
        return -1;
    }

    snprintf(response,
             sizeof(response),
             "RTSP/1.0 200 OK\r\n"
             "CSeq: %d\r\n"
             "Session: %u\r\n"
             "Range: npt=0.000-\r\n"
             "\r\n",
             cseq,
             session->session_id);

    /* Send PLAY response before the first RTP packet. */
    if (send_response(client_fd, response) != 0)
        return -1;

    if (rtsp_media_start(media, session) != 0)
    {
        printf("start live media failed\n");
        return -1;
    }

    printf("PLAY accepted: RTP %s:%d\n",
           session->client_ip,
           session->client_rtp_port);
    return 0;
}

static int process_request(int client_fd,
                           char *request,
                           RTSPSession *session,
                           RTSPMedia *media,
                           int *close_session)
{
    int cseq = rtsp_request_get_cseq(request);

    *close_session = 0;

    printf("====== RTSP REQUEST ======\n%s\n==========================\n",
           request);

    if (strncmp(request, "OPTIONS", 7) == 0)
        return handle_options(client_fd, request);

    if (strncmp(request, "DESCRIBE", 8) == 0)
        return handle_describe(client_fd, request, media);

    if (strncmp(request, "SETUP", 5) == 0)
        return handle_setup(client_fd, session, request);

    if (strncmp(request, "PLAY", 4) == 0)
        return handle_play(client_fd, media, session, request);

    if (strncmp(request, "GET_PARAMETER", 13) == 0)
        return send_simple_ok(client_fd, cseq, session);

    if (strncmp(request, "TEARDOWN", 8) == 0)
    {
        int ret = send_simple_ok(client_fd, cseq, session);
        (void)rtsp_media_stop(media, session);
        *close_session = 1;
        return ret;
    }

    {
        char response[256];

        snprintf(response,
                 sizeof(response),
                 "RTSP/1.0 405 Method Not Allowed\r\n"
                 "CSeq: %d\r\n"
                 "\r\n",
                 cseq);
        return send_response(client_fd, response);
    }
}

int rtsp_server_start(int port,
                      const char *record_path)
{
    int server_fd = -1;
    int reuse = 1;
    struct sockaddr_in addr;
    RTSPMedia media;

    if (port <= 0 || port > 65535)
        return -1;

    signal(SIGPIPE, SIG_IGN);

    memset(&media, 0, sizeof(media));

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("RTSP socket");
        return -1;
    }

    (void)setsockopt(server_fd,
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     &reuse,
                     sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd,
             (struct sockaddr *)&addr,
             sizeof(addr)) < 0)
    {
        perror("RTSP bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("RTSP listen");
        close(server_fd);
        return -1;
    }

    if (rtsp_media_init(&media, record_path) < 0)
    {
        close(server_fd);
        return -1;
    }

    printf("RTSP live server listening on port %d\n", port);
    printf("Open VLC: rtsp://<board-ip>:%d/live\n", port);
    if (media.recording_enabled)
        printf("Record while PLAY is active: %s\n", media.record_path);
    else
        printf("MP4 recording is disabled\n");

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        RTSPSession session;
        char buffer[BUFFER_SIZE];
        int offset = 0;
        int client_fd;

        memset(&client_addr, 0, sizeof(client_addr));

        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_addr_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;

            perror("RTSP accept");
            break;
        }

        rtsp_session_init(&session);
        session.client_fd = client_fd;

        if (inet_ntop(AF_INET,
                      &client_addr.sin_addr,
                      session.client_ip,
                      sizeof(session.client_ip)) == NULL)
        {
            snprintf(session.client_ip,
                     sizeof(session.client_ip),
                     "%s",
                     "0.0.0.0");
        }

        printf("RTSP client connected: %s:%u\n",
               session.client_ip,
               (unsigned int)ntohs(client_addr.sin_port));

        memset(buffer, 0, sizeof(buffer));

        while (1)
        {
            ssize_t len;
            int close_session = 0;

            if (offset >= (int)sizeof(buffer) - 1)
            {
                printf("RTSP request is too large\n");
                break;
            }

            len = recv(client_fd,
                       buffer + offset,
                       sizeof(buffer) - (size_t)offset - 1U,
                       0);
            if (len < 0)
            {
                if (errno == EINTR)
                    continue;

                perror("RTSP recv");
                break;
            }

            if (len == 0)
            {
                printf("RTSP client disconnected\n");
                break;
            }

            offset += (int)len;
            buffer[offset] = '\0';

            if (!rtsp_request_complete(buffer))
                continue;

            if (process_request(client_fd,
                                buffer,
                                &session,
                                &media,
                                &close_session) != 0)
            {
                printf("process RTSP request failed\n");
                break;
            }

            memset(buffer, 0, sizeof(buffer));
            offset = 0;

            if (close_session)
                break;
        }

        (void)rtsp_media_stop(&media, &session);
        close(client_fd);
        printf("RTSP session closed\n");
    }

    rtsp_media_close(&media);
    close(server_fd);
    return -1;
}
