/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_request.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 10:26:38 PM"
 *                 
 ********************************************************************************/

#include "rtsp_request.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int rtsp_request_complete(char *buffer)
{
	printf("check request\n");

	if(strstr(buffer,"CSeq:")==NULL)
	{
		printf("no CSeq\n");
		return 0;
	}

	if(strstr(buffer,"\r\n\r\n"))
	{
		printf("find CRLF\n");
		return 1;
	}

	if(strstr(buffer,"\n\n"))
	{
		printf("find LF\n");
		return 1;
	}

	printf("no end\n");

	return 1;
}

int rtsp_request_get_cseq(char *request)
{
	char *p;
	p=strstr(request,"CSeq:");
	if(p==NULL)
	{
		return -1;
	}
	return atoi(p+5);
}
