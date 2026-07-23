/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_request.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 10:26:16 PM"
 *                 
 ********************************************************************************/

#ifndef RTSP_REQUEST_H
#define RTSP_REQUEST_H

int rtsp_request_complete(char *buffer);
int rtsp_request_get_cseq(char *request);

#endif
