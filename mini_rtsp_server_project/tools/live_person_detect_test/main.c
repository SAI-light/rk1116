/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: main.c
 * Description: Independent live camera PERSON detection test for RV1106.
 ********************************************************************************/

#include "isp_control.h"
#include "live_person_detector.h"
#include "log.h"
#include "v4l2_capture.h"

#include <errno.h>
#include <getopt.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_DEVICE          "/dev/video11"
#define DEFAULT_IQ_DIR          "/oem/usr/share/iqfiles"
#define DEFAULT_MODEL_DIR       "/root/rockiva_model"
#define DEFAULT_WIDTH           2304
#define DEFAULT_HEIGHT          1296
#define DEFAULT_CAMERA_ID       0
#define DEFAULT_WARMUP_FRAMES   30
#define DEFAULT_DETECT_FPS      5
#define DEFAULT_THRESHOLD       60
#define CAPTURE_TIMEOUT_MS      3000
#define STATUS_INTERVAL_SECONDS 5

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) != 0)
        return -1;

    if (sigaction(SIGTERM, &action, NULL) != 0)
        return -1;

    return 0;
}

static int64_t monotonic_ms(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return -1;

    return (int64_t)value.tv_sec * 1000LL +
           (int64_t)value.tv_nsec / 1000000LL;
}

static int parse_int_range(const char *text,
                           int minimum,
                           int maximum,
                           int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL || text[0] == '\0')
        return -1;

    errno = 0;
    parsed = strtol(text, &end, 10);

    if (errno != 0 ||
        end == text ||
        *end != '\0' ||
        parsed < minimum ||
        parsed > maximum)
    {
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s [options]\n"
            "\n"
            "  -d, --device PATH        V4L2 device, default %s\n"
            "  -a, --iq-dir DIR         RKAIQ IQ directory, default %s\n"
            "  -m, --model-dir DIR      RockIVA model directory, default %s\n"
            "  -c, --camera-id ID       RKAIQ camera id, default %d\n"
            "  -w, --width WIDTH        Capture width, default %d\n"
            "  -H, --height HEIGHT      Capture height, default %d\n"
            "  -r, --detect-fps FPS     AI sampling rate, default %d\n"
            "  -t, --threshold SCORE    PERSON threshold [0,100], default %d\n"
            "  -u, --warmup-frames N    Frames discarded before AI, default %d\n"
            "  -s, --duration SEC       Stop after SEC seconds, 0 means forever\n"
            "  -h, --help               Show this help\n",
            program,
            DEFAULT_DEVICE,
            DEFAULT_IQ_DIR,
            DEFAULT_MODEL_DIR,
            DEFAULT_CAMERA_ID,
            DEFAULT_WIDTH,
            DEFAULT_HEIGHT,
            DEFAULT_DETECT_FPS,
            DEFAULT_THRESHOLD,
            DEFAULT_WARMUP_FRAMES);
}

static int rounded_camera_fps(const V4L2Capture *capture)
{
    unsigned int numerator;
    unsigned int denominator;
    unsigned int rounded;

    if (capture == NULL)
        return 30;

    numerator = capture->fps_numerator;
    denominator = capture->fps_denominator;

    if (numerator == 0U || denominator == 0U)
        return 30;

    rounded = (denominator + numerator / 2U) / numerator;
    if (rounded == 0U || rounded > 60U)
        return 30;

    return (int)rounded;
}

int main(int argc, char **argv)
{
    const char *device = DEFAULT_DEVICE;
    const char *iq_dir = DEFAULT_IQ_DIR;
    const char *model_dir = DEFAULT_MODEL_DIR;
    int camera_id = DEFAULT_CAMERA_ID;
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    int warmup_frames = DEFAULT_WARMUP_FRAMES;
    int detect_fps = DEFAULT_DETECT_FPS;
    int threshold = DEFAULT_THRESHOLD;
    int duration_seconds = 0;

    static const struct option options[] = {
        {"device", required_argument, NULL, 'd'},
        {"iq-dir", required_argument, NULL, 'a'},
        {"model-dir", required_argument, NULL, 'm'},
        {"camera-id", required_argument, NULL, 'c'},
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"detect-fps", required_argument, NULL, 'r'},
        {"threshold", required_argument, NULL, 't'},
        {"warmup-frames", required_argument, NULL, 'u'},
        {"duration", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    IspControl *isp = NULL;
    IspConfig isp_config;
    V4L2Capture capture;
    V4L2Frame frame;
    LivePersonDetector *detector = NULL;
    LivePersonDetectorConfig detector_config;
    LivePersonDetectorStats stats;

    int isp_created = 0;
    int capture_initialized = 0;
    int detector_initialized = 0;
    int frame_acquired = 0;
    int exit_code = EXIT_FAILURE;
    int option;
    int camera_fps;
    int ai_accumulator;
    int64_t start_ms;
    size_t expected_nv12_size;
    uint64_t captured_frames = 0U;
    uint64_t sequence_gaps = 0U;
    unsigned int previous_sequence = 0U;
    int have_previous_sequence = 0;
    int i;

    memset(&capture, 0, sizeof(capture));
    capture.fd = -1;
    memset(&frame, 0, sizeof(frame));
    memset(&isp_config, 0, sizeof(isp_config));
    memset(&detector_config, 0, sizeof(detector_config));
    memset(&stats, 0, sizeof(stats));

    while ((option = getopt_long(argc,
                                 argv,
                                 "d:a:m:c:w:H:r:t:u:s:h",
                                 options,
                                 NULL)) != -1)
    {
        switch (option)
        {
        case 'd':
            device = optarg;
            break;

        case 'a':
            iq_dir = optarg;
            break;

        case 'm':
            model_dir = optarg;
            break;

        case 'c':
            if (parse_int_range(optarg, 0, 7, &camera_id) != 0)
            {
                fprintf(stderr, "invalid camera id: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'w':
            if (parse_int_range(optarg, 2, 65534, &width) != 0)
            {
                fprintf(stderr, "invalid width: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'H':
            if (parse_int_range(optarg, 2, 65534, &height) != 0)
            {
                fprintf(stderr, "invalid height: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'r':
            if (parse_int_range(optarg, 1, 30, &detect_fps) != 0)
            {
                fprintf(stderr, "invalid detect fps: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 't':
            if (parse_int_range(optarg, 0, 100, &threshold) != 0)
            {
                fprintf(stderr, "invalid threshold: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'u':
            if (parse_int_range(optarg, 0, 1000, &warmup_frames) != 0)
            {
                fprintf(stderr,
                        "invalid warmup frame count: %s\n",
                        optarg);
                return EXIT_FAILURE;
            }
            break;

        case 's':
            if (parse_int_range(optarg, 0, 86400,
                                &duration_seconds) != 0)
            {
                fprintf(stderr, "invalid duration: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'h':
            print_usage(stdout, argv[0]);
            return EXIT_SUCCESS;

        default:
            print_usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (optind != argc)
    {
        fprintf(stderr,
                "unexpected positional argument: %s\n",
                argv[optind]);
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if ((width & 1) != 0 || (height & 1) != 0)
    {
        fprintf(stderr, "NV12 width and height must be even\n");
        return EXIT_FAILURE;
    }

    if (install_signal_handlers() != 0)
    {
        fprintf(stderr,
                "install signal handlers failed: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    log_set_level(LOG_LEVEL_INFO);

    printf("live PERSON detection test\n");
    printf("  device         : %s\n", device);
    printf("  capture        : %dx%d NV12\n", width, height);
    printf("  IQ directory   : %s\n", iq_dir);
    printf("  model directory: %s\n", model_dir);
    printf("  camera id      : %d\n", camera_id);
    printf("  warmup frames  : %d\n", warmup_frames);
    printf("  detect rate    : about %d fps\n", detect_fps);
    printf("  threshold      : %d\n", threshold);
    printf("  returned class : PERSON only\n");

    isp_config.camera_id = camera_id;
    isp_config.iq_dir = iq_dir;
    isp_config.hdr_mode = 0;
    isp_config.multi_cam = false;

    if (isp_control_create(&isp, &isp_config) != 0)
    {
        fprintf(stderr, "ISP create failed\n");
        goto cleanup;
    }
    isp_created = 1;

    if (isp_control_start(isp) != 0)
    {
        fprintf(stderr, "ISP start failed\n");
        goto cleanup;
    }

    if (v4l2_capture_init(&capture,
                          device,
                          width,
                          height) != 0)
    {
        fprintf(stderr, "V4L2 capture init failed\n");
        goto cleanup;
    }
    capture_initialized = 1;

    if (capture.width != width || capture.height != height)
    {
        fprintf(stderr,
                "camera adjusted resolution to %dx%d; expected %dx%d\n",
                capture.width,
                capture.height,
                width,
                height);
        goto cleanup;
    }

    if (capture.pixelformat != V4L2_PIX_FMT_NV12)
    {
        fprintf(stderr,
                "unsupported pixel format: 0x%08x; expected NV12\n",
                capture.pixelformat);
        goto cleanup;
    }

    if (capture.bytesperline != width)
    {
        fprintf(stderr,
                "unsupported stride: bytesperline=%d width=%d\n",
                capture.bytesperline,
                width);
        fprintf(stderr,
                "this first live test requires tightly packed NV12\n");
        goto cleanup;
    }

    expected_nv12_size =
        (size_t)width * (size_t)height * 3U / 2U;

    for (i = 0; i < warmup_frames && !g_stop; ++i)
    {
        if (v4l2_capture_acquire_frame(&capture,
                                       &frame,
                                       CAPTURE_TIMEOUT_MS) != 0)
        {
            fprintf(stderr,
                    "warmup frame %d acquire failed: %s\n",
                    i + 1,
                    strerror(errno));
            goto cleanup;
        }
        frame_acquired = 1;

        if (i == 0 || i + 1 == warmup_frames)
        {
            printf("warmup %d/%d: sequence=%u size=%zu\n",
                   i + 1,
                   warmup_frames,
                   frame.sequence,
                   frame.size);
        }

        if (v4l2_capture_release_frame(&capture, &frame) != 0)
        {
            fprintf(stderr,
                    "warmup frame %d release failed: %s\n",
                    i + 1,
                    strerror(errno));
            goto cleanup;
        }
        frame_acquired = 0;
    }

    if (g_stop)
    {
        exit_code = EXIT_SUCCESS;
        goto cleanup;
    }

    detector_config.model_dir = model_dir;
    detector_config.width = (unsigned int)width;
    detector_config.height = (unsigned int)height;
    detector_config.person_threshold = (uint8_t)threshold;
    detector_config.buffer_count = 2U;

    if (live_person_detector_create(&detector,
                                    &detector_config) != 0)
    {
        fprintf(stderr, "live PERSON detector init failed\n");
        goto cleanup;
    }
    detector_initialized = 1;

    camera_fps = rounded_camera_fps(&capture);
    if (detect_fps > camera_fps)
        detect_fps = camera_fps;

    /* Bresenham-style sampling; 30/5 gives exactly one AI frame per 6. */
    ai_accumulator = camera_fps - detect_fps;

    start_ms = monotonic_ms();
    if (start_ms < 0)
    {
        fprintf(stderr,
                "clock_gettime failed: %s\n",
                strerror(errno));
        goto cleanup;
    }

    printf("capture started: source_fps=%d AI_fps=%d\n",
           camera_fps,
           detect_fps);
    printf("Press Ctrl+C to stop.\n");

    while (!g_stop && !isp_control_should_quit(isp))
    {
        int submit_result;

        if (duration_seconds > 0)
        {
            int64_t now_ms = monotonic_ms();

            if (now_ms < 0)
            {
                fprintf(stderr,
                        "clock_gettime failed: %s\n",
                        strerror(errno));
                goto cleanup;
            }

            if (now_ms - start_ms >=
                (int64_t)duration_seconds * 1000LL)
            {
                break;
            }
        }

        if (v4l2_capture_acquire_frame(&capture,
                                       &frame,
                                       CAPTURE_TIMEOUT_MS) != 0)
        {
            if (errno == EINTR && g_stop)
                break;

            fprintf(stderr,
                    "capture failed: %s\n",
                    strerror(errno));
            goto cleanup;
        }
        frame_acquired = 1;
        captured_frames++;

        if (have_previous_sequence &&
            frame.sequence > previous_sequence + 1U)
        {
            sequence_gaps +=
                (uint64_t)(frame.sequence - previous_sequence - 1U);
        }

        previous_sequence = frame.sequence;
        have_previous_sequence = 1;

        if (frame.size < expected_nv12_size)
        {
            fprintf(stderr,
                    "short NV12 frame: sequence=%u actual=%zu expected=%zu\n",
                    frame.sequence,
                    frame.size,
                    expected_nv12_size);
            goto cleanup;
        }

        ai_accumulator += detect_fps;
        if (ai_accumulator >= camera_fps)
        {
            ai_accumulator -= camera_fps;

            submit_result = live_person_detector_submit(
                detector,
                frame.data,
                frame.size,
                frame.sequence,
                frame.timestamp_us);

            if (submit_result < 0)
            {
                fprintf(stderr,
                        "submit camera sequence %u to RockIVA failed\n",
                        frame.sequence);
                goto cleanup;
            }
        }

        /* AI data is already copied into its independent DMA slot. */
        if (v4l2_capture_release_frame(&capture, &frame) != 0)
        {
            fprintf(stderr,
                    "release camera frame failed: %s\n",
                    strerror(errno));
            goto cleanup;
        }
        frame_acquired = 0;

        if (captured_frames %
            ((uint64_t)camera_fps * STATUS_INTERVAL_SECONDS) == 0U)
        {
            live_person_detector_get_stats(detector, &stats);

            printf("[status] captured=%llu sequence_gaps=%llu "
                   "AI_submitted=%llu AI_busy_skips=%llu "
                   "AI_results=%llu person_frames=%llu\n",
                   (unsigned long long)captured_frames,
                   (unsigned long long)sequence_gaps,
                   (unsigned long long)stats.submitted_frames,
                   (unsigned long long)stats.busy_skips,
                   (unsigned long long)stats.result_callbacks,
                   (unsigned long long)stats.person_positive_frames);
        }
    }

    if (isp_control_should_quit(isp))
    {
        fprintf(stderr, "RKAIQ requested shutdown\n");
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if (frame_acquired)
    {
        (void)v4l2_capture_release_frame(&capture, &frame);
        frame_acquired = 0;
    }

    if (detector_initialized)
    {
        (void)live_person_detector_wait_idle(detector, 5000);
        live_person_detector_get_stats(detector, &stats);

        printf("\nAI summary\n");
        printf("  submitted_frames       : %llu\n",
               (unsigned long long)stats.submitted_frames);
        printf("  busy_skips            : %llu\n",
               (unsigned long long)stats.busy_skips);
        printf("  result_callbacks      : %llu\n",
               (unsigned long long)stats.result_callbacks);
        printf("  person_positive_frames: %llu\n",
               (unsigned long long)stats.person_positive_frames);
        printf("  released_frames       : %llu\n",
               (unsigned long long)stats.released_frames);
        printf("  failed_results        : %llu\n",
               (unsigned long long)stats.failed_results);

        live_person_detector_destroy(detector);
        detector = NULL;
    }

    if (capture_initialized)
    {
        v4l2_capture_close(&capture);
        capture_initialized = 0;
    }

    if (isp_created)
    {
        isp_control_destroy(isp);
        isp = NULL;
    }

    printf("\nCapture summary\n");
    printf("  captured_frames: %llu\n",
           (unsigned long long)captured_frames);
    printf("  sequence_gaps  : %llu\n",
           (unsigned long long)sequence_gaps);

    return exit_code;
}
