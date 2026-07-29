/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  Live camera RTSP server entry.
 ********************************************************************************/

#include "rtsp_server.h"

#include <stdlib.h>

int main(void)
{
    return rtsp_server_start(8554) == 0
         ? EXIT_SUCCESS
         : EXIT_FAILURE;
}
