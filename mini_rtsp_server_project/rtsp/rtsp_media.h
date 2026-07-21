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

int rtsp_media_start(RTSPSession *session);

int rtsp_media_stop(RTSPSession *session);

#endif
