/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: main.c
 * Description: Command-line entry for the live RTSP and MP4 server.
 ********************************************************************************/

#include "log.h"
#include "rtsp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "dev"
#endif

#define DEFAULT_RTSP_PORT    8554
#define DEFAULT_DEVICE_PATH  "/dev/video11"
#define DEFAULT_RECORD_PATH  "/root/live_record.mp4"

static volatile sig_atomic_t g_stop_signal = 0;
static int g_signal_pipe[2] = {-1, -1};

static void handle_stop_signal(int signal_number)
{
    int saved_errno = errno;
    unsigned char value = (unsigned char)signal_number;

    g_stop_signal = signal_number;

    if (g_signal_pipe[1] >= 0)
        (void)write(g_signal_pipe[1], &value, sizeof(value));

    errno = saved_errno;
}

static int set_fd_flags(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return -1;

    return 0;
}

static int install_signal_handlers(void)
{
    struct sigaction action;
    struct sigaction ignore_action;

    if (pipe(g_signal_pipe) != 0)
        return -1;

    if (set_fd_flags(g_signal_pipe[0]) != 0 ||
        set_fd_flags(g_signal_pipe[1]) != 0)
    {
        return -1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0)
    {
        return -1;
    }

    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);

    if (sigaction(SIGPIPE, &ignore_action, NULL) != 0)
        return -1;

    return 0;
}

static void close_signal_pipe(void)
{
    if (g_signal_pipe[0] >= 0)
        close(g_signal_pipe[0]);
    if (g_signal_pipe[1] >= 0)
        close(g_signal_pipe[1]);

    g_signal_pipe[0] = -1;
    g_signal_pipe[1] = -1;
}

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

static void print_usage(FILE *stream,
                        const char *program)
{
    fprintf(stream,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  -p, --port PORT         RTSP TCP port (default: %d)\n"
            "  -d, --device PATH       V4L2 device (default: %s)\n"
            "  -o, --record PATH       MP4 output path (default: %s)\n"
            "  -n, --no-record         Disable MP4 recording\n"
            "  -l, --log-level LEVEL   error, warn, info, or debug\n"
            "  -h, --help              Show this help text\n"
            "  -V, --version           Show program version\n"
            "\n"
            "Examples:\n"
            "  %s --port 8554 --record /root/live.mp4\n"
            "  %s --no-record --log-level debug\n",
            program,
            DEFAULT_RTSP_PORT,
            DEFAULT_DEVICE_PATH,
            DEFAULT_RECORD_PATH,
            program,
            program);
}

int main(int argc,
         char **argv)
{
    static const struct option long_options[] = {
        {"port", required_argument, NULL, 'p'},
        {"device", required_argument, NULL, 'd'},
        {"record", required_argument, NULL, 'o'},
        {"no-record", no_argument, NULL, 'n'},
        {"log-level", required_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}
    };

    RTSPServerConfig config;
    LogLevel log_level = LOG_LEVEL_INFO;
    const char *record_path = DEFAULT_RECORD_PATH;
    int option;
    int result;

    memset(&config, 0, sizeof(config));
    config.port = DEFAULT_RTSP_PORT;
    config.device_path = DEFAULT_DEVICE_PATH;

    while ((option = getopt_long(argc,
                                 argv,
                                 "p:d:o:nl:hV",
                                 long_options,
                                 NULL)) != -1)
    {
        switch (option)
        {
        case 'p':
            if (parse_port(optarg, &config.port) != 0)
            {
                fprintf(stderr, "invalid RTSP port: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'd':
            if (optarg[0] == '\0')
            {
                fprintf(stderr, "camera device path cannot be empty\n");
                return EXIT_FAILURE;
            }
            config.device_path = optarg;
            break;

        case 'o':
            if (optarg[0] == '\0')
            {
                fprintf(stderr, "record path cannot be empty\n");
                return EXIT_FAILURE;
            }
            record_path = optarg;
            break;

        case 'n':
            record_path = NULL;
            break;

        case 'l':
            if (log_level_parse(optarg, &log_level) != 0)
            {
                fprintf(stderr, "invalid log level: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'h':
            print_usage(stdout, argv[0]);
            return EXIT_SUCCESS;

        case 'V':
            printf("mini_rtsp_server %s\n", PROJECT_VERSION);
            return EXIT_SUCCESS;

        default:
            print_usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind != argc)
    {
        fprintf(stderr, "unexpected positional argument: %s\n", argv[optind]);
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    log_set_level(log_level);

    if (install_signal_handlers() != 0)
    {
        LOG_ERROR_ERRNO("main", errno, "install signal handlers failed");
        close_signal_pipe();
        return EXIT_FAILURE;
    }

    config.record_path = record_path;
    config.stop_flag = &g_stop_signal;
    config.stop_fd = g_signal_pipe[0];

    LOG_INFO("main",
             "mini_rtsp_server %s starting: port=%d device=%s record=%s log=%s",
             PROJECT_VERSION,
             config.port,
             config.device_path,
             config.record_path != NULL ? config.record_path : "disabled",
             log_level_name(log_level));

    result = rtsp_server_run(&config);

    if (g_stop_signal != 0)
    {
        LOG_INFO("main",
                 "shutdown completed after signal %d",
                 (int)g_stop_signal);
    }

    close_signal_pipe();
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
