/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_request.c
 *    Description:  Minimal RTSP request helpers.
 ********************************************************************************/

#include "rtsp_request.h"

#include <stdlib.h>
#include <string.h>

int rtsp_request_complete(char *buffer)
{
    if (buffer == NULL)
        return 0;

    if (strstr(buffer, "CSeq:") == NULL)
        return 0;

    if (strstr(buffer, "\r\n\r\n") != NULL ||
        strstr(buffer, "\n\n") != NULL)
    {
        return 1;
    }

    return 0;
}

int rtsp_request_get_cseq(char *request)
{
    char *p;

    if (request == NULL)
        return -1;

    p = strstr(request, "CSeq:");
    if (p == NULL)
        return -1;

    return atoi(p + 5);
}
