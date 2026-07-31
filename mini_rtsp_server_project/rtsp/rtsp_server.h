/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: rtsp_server.h
 * Description: Single-client live-camera RTSP server interface.
 ********************************************************************************/

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <signal.h>

typedef struct
{
    int port;
    const char *device_path;

    /* NULL disables recording. A normal path must end in .mp4. */
    const char *record_path;

    /* Optional process-wide graceful-shutdown integration. */
    const volatile sig_atomic_t *stop_flag;
    int stop_fd;
} RTSPServerConfig;

int rtsp_server_run(const RTSPServerConfig *config);

#endif
