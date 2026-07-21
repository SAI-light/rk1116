/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_media.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/21/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/21/2026 03:15:26 PM"
 *                 
 ********************************************************************************/

#include "rtsp_media.h"
#include <stdio.h>

int rtsp_media_start(RTSPSession *session)
{
	if(session->playing)
	{
		return 0;
	}
	printf("start media stream\n");
	session->playing=1;
	return 0;
}

int rtsp_media_stop(RTSPSession *session)
{
	session->playing=0;
	printf("stop media stream\n");
	return 0;
}
