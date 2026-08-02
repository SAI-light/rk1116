/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: main.c
 * Description: Command-line entry for the live RTSP and MP4 server.
 ********************************************************************************/

#include "isp_control.h"
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
#define DEFAULT_IQ_DIR       "/oem/usr/share/iqfiles"
#define DEFAULT_CAMERA_ID    0
#define DEFAULT_WARMUP_FRAMES 30

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

static int parse_nonnegative_int(const char *text,
                                 int *value_out)
{
    char *end = NULL;
    long value;

    if (text == NULL || value_out == NULL || text[0] == '\0')
        return -1;

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' ||
        value < 0L || value > INT_MAX)
    {
        return -1;
    }

    *value_out = (int)value;
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
            "  -a, --iq-dir DIR        RKAIQ IQ directory (default: %s)\n"
            "  -c, --camera-id ID      RKAIQ camera id (default: %d)\n"
            "  -w, --warmup-frames N   Discard N frames before bootstrap (default: %d)\n"
            "      --no-aiq            Disable RKAIQ for comparison testing\n"
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
            DEFAULT_IQ_DIR,
            DEFAULT_CAMERA_ID,
            DEFAULT_WARMUP_FRAMES,
            program,
            program);
}

enum
{
    OPTION_NO_AIQ = 1000
};

int main(int argc,
         char **argv)
{
    static const struct option long_options[] = {
        {"port", required_argument, NULL, 'p'},
        {"device", required_argument, NULL, 'd'},
        {"record", required_argument, NULL, 'o'},
        {"no-record", no_argument, NULL, 'n'},
        {"log-level", required_argument, NULL, 'l'},
        {"iq-dir", required_argument, NULL, 'a'},
        {"camera-id", required_argument, NULL, 'c'},
        {"warmup-frames", required_argument, NULL, 'w'},
        {"no-aiq", no_argument, NULL, OPTION_NO_AIQ},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}
    };

    RTSPServerConfig config;
    IspConfig isp_config;
    IspControl *isp = NULL;
    LogLevel log_level = LOG_LEVEL_INFO;
    const char *record_path = DEFAULT_RECORD_PATH;
    int aiq_enabled = 1;
    int option;
    int result;

    memset(&config, 0, sizeof(config));
    config.port = DEFAULT_RTSP_PORT;
    config.device_path = DEFAULT_DEVICE_PATH;
    config.warmup_frame_count = DEFAULT_WARMUP_FRAMES;

    memset(&isp_config, 0, sizeof(isp_config));
    isp_config.camera_id = DEFAULT_CAMERA_ID;
    isp_config.iq_dir = DEFAULT_IQ_DIR;
    isp_config.hdr_mode = 0;
    isp_config.multi_cam = false;

    while ((option = getopt_long(argc,
                                 argv,
                                 "p:d:o:nl:a:c:w:hV",
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

        case 'a':
            if (optarg[0] == '\0')
            {
                fprintf(stderr, "IQ directory cannot be empty\n");
                return EXIT_FAILURE;
            }
            isp_config.iq_dir = optarg;
            break;

        case 'c':
            if (parse_nonnegative_int(optarg,
                                      &isp_config.camera_id) != 0)
            {
                fprintf(stderr, "invalid camera id: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'w':
            if (parse_nonnegative_int(optarg,
                                      &config.warmup_frame_count) != 0)
            {
                fprintf(stderr, "invalid warmup frame count: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case OPTION_NO_AIQ:
            aiq_enabled = 0;
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

    if (aiq_enabled)
    {
        if (isp_control_create(&isp, &isp_config) != 0)
        {
            LOG_ERROR("main", "initialize RKAIQ context failed");
            close_signal_pipe();
            return EXIT_FAILURE;
        }

        if (isp_control_start(isp) != 0)
        {
            LOG_ERROR("main", "start RKAIQ failed");
            isp_control_destroy(isp);
            close_signal_pipe();
            return EXIT_FAILURE;
        }
    }

    LOG_INFO("main",
             "mini_rtsp_server %s starting: port=%d device=%s record=%s log=%s aiq=%s iq_dir=%s camera_id=%d warmup=%d",
             PROJECT_VERSION,
             config.port,
             config.device_path,
             config.record_path != NULL ? config.record_path : "disabled",
             log_level_name(log_level),
             aiq_enabled ? "enabled" : "disabled",
             aiq_enabled ? isp_config.iq_dir : "disabled",
             isp_config.camera_id,
             config.warmup_frame_count);

    result = rtsp_server_run(&config);

    /*
     * rtsp_server_run() closes V4L2, MPP and the RTSP media thread before
     * returning. RKAIQ is stopped only after the media pipeline is closed.
     */
    isp_control_destroy(isp);
    isp = NULL;

    if (g_stop_signal != 0)
    {
        LOG_INFO("main",
                 "shutdown completed after signal %d",
                 (int)g_stop_signal);
    }

    close_signal_pipe();
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
