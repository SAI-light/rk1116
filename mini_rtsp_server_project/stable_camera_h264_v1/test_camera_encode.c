/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_camera_encode.c
 *    Description:  Capture live NV12 frames from /dev/video11 and encode them
 *                  directly to one Annex-B H264 stream using the already
 *                  verified RV1106 vendor-packet MPP encoder (v8).
 *
 *                  No raw NV12 frame is written to disk. The live path is:
 *
 *                    VIDIOC_DQBUF (V4L2 MMAP NV12)
 *                              -> mpp_encoder_encode()
 *                              -> VIDIOC_QBUF
 *                              -> append compressed H264 packet
 *
 *        Version:  1.0.0 (2026-07-27)
 ********************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "mpp_encoder.h"
#include "v4l2_capture.h"

#define DEFAULT_DEVICE         "/dev/video11"
#define DEFAULT_OUTPUT_FILE    "output_camera_100.h264"
#define DEFAULT_FRAME_COUNT    100
#define DEFAULT_WARMUP_FRAMES  5
#define CAPTURE_TIMEOUT_MS     3000
#define VIDEO_WIDTH            2304
#define VIDEO_HEIGHT           1296

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static int64_t get_time_us(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
        return -1;

    return (int64_t)tv.tv_sec * 1000000LL +
           (int64_t)tv.tv_usec;
}

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL || *text == '\0')
        return -1;

    errno = 0;
    parsed = strtol(text, &end, 10);

    if (errno != 0 ||
        end == text ||
        *end != '\0' ||
        parsed <= 0 ||
        parsed > 1000000L)
    {
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static void print_usage(const char *program)
{
    printf("Usage: %s [device] [output_h264] [frame_count]\n", program);
    printf("\n");
    printf("Defaults:\n");
    printf("  device      : %s\n", DEFAULT_DEVICE);
    printf("  output_h264 : %s\n", DEFAULT_OUTPUT_FILE);
    printf("  frame_count : %d\n", DEFAULT_FRAME_COUNT);
    printf("\n");
    printf("Example:\n");
    printf("  %s /dev/video11 output_camera_100.h264 100\n",
           program);
}

int main(int argc, char *argv[])
{
    const char *device = DEFAULT_DEVICE;
    const char *output_path = DEFAULT_OUTPUT_FILE;
    int frame_count = DEFAULT_FRAME_COUNT;
    const size_t expected_nv12_size =
        (size_t)VIDEO_WIDTH * (size_t)VIDEO_HEIGHT * 3U / 2U;

    V4L2Capture capture;
    V4L2Frame frame;
    MppEncoder encoder;
    FILE *output_file = NULL;

    int capture_initialized = 0;
    int encoder_initialized = 0;
    int frame_acquired = 0;
    int encoded_frames = 0;
    uint64_t total_h264_bytes = 0;
    uint64_t dropped_capture_frames = 0;
    unsigned int previous_sequence = 0;
    int have_previous_sequence = 0;
    int64_t total_acquire_us = 0;
    int64_t total_encode_us = 0;
    int64_t total_write_us = 0;
    int64_t wall_start_us = -1;
    int64_t wall_end_us = -1;
    int exit_code = EXIT_FAILURE;
    int i;

    memset(&capture, 0, sizeof(capture));
    capture.fd = -1;
    memset(&frame, 0, sizeof(frame));
    memset(&encoder, 0, sizeof(encoder));

    if (argc > 4)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2)
        device = argv[1];

    if (argc >= 3)
        output_path = argv[2];

    if (argc >= 4 && parse_positive_int(argv[3], &frame_count) != 0)
    {
        printf("invalid frame_count: %s\n", argv[3]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("live camera encode test\n");
    printf("device       : %s\n", device);
    printf("output       : %s\n", output_path);
    printf("resolution   : %dx%d NV12\n",
           VIDEO_WIDTH,
           VIDEO_HEIGHT);
    printf("expected NV12: %zu bytes/frame\n",
           expected_nv12_size);
    printf("frame count  : %d\n", frame_count);
    printf("warmup frames: %d\n", DEFAULT_WARMUP_FRAMES);
    printf("raw NV12 intermediate files: disabled\n");

    if (v4l2_capture_init(&capture,
                          device,
                          VIDEO_WIDTH,
                          VIDEO_HEIGHT) < 0)
    {
        printf("capture init failed\n");
        goto CLEANUP;
    }
    capture_initialized = 1;

    if (capture.width != VIDEO_WIDTH ||
        capture.height != VIDEO_HEIGHT)
    {
        printf("camera adjusted resolution to %dx%d; "
               "current MPP configuration requires %dx%d\n",
               capture.width,
               capture.height,
               VIDEO_WIDTH,
               VIDEO_HEIGHT);
        goto CLEANUP;
    }

    if (capture.bytesperline != VIDEO_WIDTH)
    {
        printf("unsupported camera stride: bytesperline=%d width=%d\n",
               capture.bytesperline,
               VIDEO_WIDTH);
        printf("current test expects tightly packed NV12; "
               "do not encode until row repacking is added\n");
        goto CLEANUP;
    }

    /* Drop a few initial frames so auto-exposure and buffering can settle. */
    for (i = 0; i < DEFAULT_WARMUP_FRAMES && !g_stop; ++i)
    {
        if (v4l2_capture_acquire_frame(&capture,
                                       &frame,
                                       CAPTURE_TIMEOUT_MS) < 0)
        {
            printf("warmup frame %d acquire failed: %s\n",
                   i,
                   strerror(errno));
            goto CLEANUP;
        }
        frame_acquired = 1;

        printf("warmup frame %d/%d: sequence=%u size=%zu\n",
               i + 1,
               DEFAULT_WARMUP_FRAMES,
               frame.sequence,
               frame.size);

        if (v4l2_capture_release_frame(&capture, &frame) < 0)
        {
            printf("warmup frame %d release failed\n", i);
            goto CLEANUP;
        }
        frame_acquired = 0;
    }

    if (g_stop)
    {
        printf("stopped during warmup\n");
        goto CLEANUP;
    }

    if (mpp_encoder_init(&encoder,
                         VIDEO_WIDTH,
                         VIDEO_HEIGHT) < 0)
    {
        printf("encoder init failed\n");
        goto CLEANUP;
    }
    encoder_initialized = 1;

    output_file = fopen(output_path, "wb");
    if (output_file == NULL)
    {
        printf("open output file failed: %s: %s\n",
               output_path,
               strerror(errno));
        goto CLEANUP;
    }

    wall_start_us = get_time_us();
    if (wall_start_us < 0)
    {
        printf("get start time failed\n");
        goto CLEANUP;
    }

    while (encoded_frames < frame_count && !g_stop)
    {
        uint8_t *h264_data = NULL;
        int h264_len;
        int64_t acquire_start_us;
        int64_t acquire_end_us;
        int64_t encode_start_us;
        int64_t encode_end_us;
        int64_t write_start_us;
        int64_t write_end_us;
        int64_t acquire_us;
        int64_t encode_us;
        int64_t write_us;
        unsigned int captured_sequence;
        size_t captured_size;

        acquire_start_us = get_time_us();

        if (v4l2_capture_acquire_frame(&capture,
                                       &frame,
                                       CAPTURE_TIMEOUT_MS) < 0)
        {
            printf("frame %d acquire failed: %s\n",
                   encoded_frames,
                   strerror(errno));
            goto CLEANUP;
        }
        frame_acquired = 1;

        acquire_end_us = get_time_us();

        if (frame.size < expected_nv12_size)
        {
            printf("frame %d is too small: sequence=%u "
                   "size=%zu expected=%zu\n",
                   encoded_frames,
                   frame.sequence,
                   frame.size,
                   expected_nv12_size);
            goto CLEANUP;
        }

        captured_sequence = frame.sequence;
        captured_size = frame.size;

        if (have_previous_sequence &&
            captured_sequence > previous_sequence + 1U)
        {
            dropped_capture_frames +=
                (uint64_t)(captured_sequence - previous_sequence - 1U);
        }

        previous_sequence = captured_sequence;
        have_previous_sequence = 1;

        encode_start_us = get_time_us();

        /*
         * mpp_encoder_encode() copies the live NV12 image from this MMAP
         * address into its MPP input DMA buffer before it returns. Therefore,
         * the V4L2 buffer can be safely QBUF'ed immediately after this call.
         */
        h264_len = mpp_encoder_encode(&encoder,
                                      frame.data,
                                      (int)expected_nv12_size,
                                      &h264_data);

        encode_end_us = get_time_us();

        if (v4l2_capture_release_frame(&capture, &frame) < 0)
        {
            printf("frame %d release failed\n", encoded_frames);
            free(h264_data);
            goto CLEANUP;
        }
        frame_acquired = 0;

        if (h264_len <= 0 || h264_data == NULL)
        {
            printf("frame %d encode failed: len=%d data=%p\n",
                   encoded_frames,
                   h264_len,
                   h264_data);
            free(h264_data);
            goto CLEANUP;
        }

        write_start_us = get_time_us();

        if (fwrite(h264_data,
                   1,
                   (size_t)h264_len,
                   output_file) != (size_t)h264_len)
        {
            printf("frame %d write H264 failed: %s\n",
                   encoded_frames,
                   strerror(errno));
            free(h264_data);
            goto CLEANUP;
        }

        write_end_us = get_time_us();

        acquire_us = acquire_end_us - acquire_start_us;
        encode_us = encode_end_us - encode_start_us;
        write_us = write_end_us - write_start_us;

        total_acquire_us += acquire_us;
        total_encode_us += encode_us;
        total_write_us += write_us;
        total_h264_bytes += (uint64_t)h264_len;
        encoded_frames++;

        printf("camera frame %d/%d: sequence=%u "
               "nv12=%zu h264=%d "
               "acquire=%.3f ms encode=%.3f ms write=%.3f ms\n",
               encoded_frames,
               frame_count,
               captured_sequence,
               captured_size,
               h264_len,
               (double)acquire_us / 1000.0,
               (double)encode_us / 1000.0,
               (double)write_us / 1000.0);

        free(h264_data);
        h264_data = NULL;
    }

    if (fflush(output_file) != 0)
    {
        printf("flush output failed: %s\n", strerror(errno));
        goto CLEANUP;
    }

    wall_end_us = get_time_us();
    if (wall_end_us < 0)
    {
        printf("get end time failed\n");
        goto CLEANUP;
    }

    if (g_stop)
    {
        printf("capture stopped by signal after %d frames\n",
               encoded_frames);
    }

    if (encoded_frames > 0)
        exit_code = EXIT_SUCCESS;

CLEANUP:
    if (frame_acquired)
    {
        if (v4l2_capture_release_frame(&capture, &frame) < 0)
            printf("cleanup: release held camera frame failed\n");
        frame_acquired = 0;
    }

    if (output_file != NULL)
    {
        if (fclose(output_file) != 0)
        {
            printf("close output file failed: %s\n",
                   strerror(errno));
            exit_code = EXIT_FAILURE;
        }
        output_file = NULL;
    }

    if (encoder_initialized)
    {
        mpp_encoder_close(&encoder);
        encoder_initialized = 0;
    }

    if (capture_initialized)
    {
        v4l2_capture_close(&capture);
        capture_initialized = 0;
    }

    printf("\n");
    printf("live camera encode summary\n");
    printf("encoded frames      : %d/%d\n",
           encoded_frames,
           frame_count);
    printf("total H264 bytes    : %" PRIu64 "\n",
           total_h264_bytes);
    printf("sequence gaps       : %" PRIu64 " frame(s)\n",
           dropped_capture_frames);

    if (encoded_frames > 0)
    {
        printf("average acquire wait: %.3f ms/frame\n",
               (double)total_acquire_us /
               (double)encoded_frames /
               1000.0);

        printf("average encode time : %.3f ms/frame\n",
               (double)total_encode_us /
               (double)encoded_frames /
               1000.0);

        printf("average write time  : %.3f ms/frame\n",
               (double)total_write_us /
               (double)encoded_frames /
               1000.0);

        printf("average packet size : %.2f bytes/frame\n",
               (double)total_h264_bytes /
               (double)encoded_frames);
    }

    if (total_encode_us > 0 && encoded_frames > 0)
    {
        printf("encode-only speed   : %.2f fps\n",
               (double)encoded_frames * 1000000.0 /
               (double)total_encode_us);
    }

    if (wall_start_us >= 0 &&
        wall_end_us > wall_start_us &&
        encoded_frames > 0)
    {
        int64_t wall_us = wall_end_us - wall_start_us;

        printf("wall-clock time     : %.3f s\n",
               (double)wall_us / 1000000.0);

        printf("end-to-end speed    : %.2f fps\n",
               (double)encoded_frames * 1000000.0 /
               (double)wall_us);
    }

    if (exit_code == EXIT_SUCCESS)
    {
        printf("live H264 saved to  : %s\n", output_path);
    }
    else
    {
        printf("live camera encode test failed\n");
    }

    return exit_code;
}
