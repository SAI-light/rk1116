/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_server.h
 *    Description:  Single-client live-camera RTSP server interface.
 ********************************************************************************/

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

/*
 * record_path:
 *   normal .mp4 path  enable recording while PLAY is active
 *   NULL, empty, "-"  disable MP4 recording
 */
int rtsp_server_start(int port,
                      const char *record_path);

#endif
