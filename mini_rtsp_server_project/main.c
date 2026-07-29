/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  Live camera RTSP server entry with optional MP4 recording.
 ********************************************************************************/

#include "rtsp_server.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_RTSP_PORT    8554
#define DEFAULT_RECORD_PATH  "/root/live_record.mp4"

static int parse_port(const char *text,
                      int *port)
{
    char *end = NULL;
    long value;

    if (text == NULL || port == NULL || text[0] == '\0')
        return -1;

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' ||
        value <= 0L || value > 65535L || value > INT_MAX)
    {
        return -1;
    }

    *port = (int)value;
    return 0;
}

int main(int argc,
         char **argv)
{
    int port = DEFAULT_RTSP_PORT;
    const char *record_path = DEFAULT_RECORD_PATH;

    if (argc > 3)
    {
        fprintf(stderr,
                "Usage: %s [rtsp_port] [record_path|-]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2 && parse_port(argv[1], &port) != 0)
    {
        fprintf(stderr, "invalid RTSP port: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (argc >= 3)
        record_path = argv[2];

    return rtsp_server_start(port, record_path) == 0
         ? EXIT_SUCCESS
         : EXIT_FAILURE;
}
