/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_media.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/21/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/21/2026 03:15:09 PM"
 *                 
 ********************************************************************************/

#ifndef RTSP_MEDIA_H
#define RTSP_MEDIA_H

#include "rtsp_session.h"
#include "h264_reader.h"

typedef struct
{
	H264Reader *reader;
}RTSPMedia;

int rtsp_media_init(RTSPMedia *media);

int rtsp_media_start(RTSPMedia *media, RTSPSession *session);

int rtsp_media_stop(RTSPMedia *media, RTSPSession *session);

#endif
