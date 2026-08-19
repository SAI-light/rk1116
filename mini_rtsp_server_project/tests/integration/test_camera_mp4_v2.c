/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_camera_mp4.c
 *    Description:  Capture live NV12 frames from /dev/video11, encode them with
 *                  the verified RV1106 MPP vendor-packet encoder with configurable timing, and pass
 *                  each in-memory Annex-B H264 packet directly to Rockchip
 *                  librkmuxer to create one video-only MP4 file.
 *
 *                  No raw NV12 file and no intermediate H264 file are written.
 *
 *                    V4L2 MMAP NV12
 *                          -> MPP v9 configurable H264
 *                          -> rkmuxer_write_video_frame()
 *                          -> MP4
 *
 *        Version:  1.1.0 (2026-07-29)
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
#include "mp4_muxer.h"
#include "v4l2_capture.h"

#define DEFAULT_DEVICE         "/dev/video11"
#define DEFAULT_OUTPUT_FILE    "output_camera_100.mp4"
#define DEFAULT_FRAME_COUNT    100
#define DEFAULT_WARMUP_FRAMES  5
#define CAPTURE_TIMEOUT_MS     3000

#define VIDEO_WIDTH            2304
#define VIDEO_HEIGHT           1296
#define VIDEO_FRAME_RATE       25
#define VIDEO_GOP              25
#define VIDEO_BIT_RATE         4000000
#define MP4_MUXER_ID           0

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
    printf("Usage: %s [device] [output_mp4] [frame_count]\n", program);
    printf("\n");
    printf("Defaults:\n");
    printf("  device      : %s\n", DEFAULT_DEVICE);
    printf("  output_mp4  : %s\n", DEFAULT_OUTPUT_FILE);
    printf("  frame_count : %d\n", DEFAULT_FRAME_COUNT);
    printf("\n");
    printf("Example:\n");
    printf("  %s /dev/video11 output_camera_100.mp4 100\n", program);
}

int main(int argc, char *argv[])
{
    const char *device = DEFAULT_DEVICE;
    const char *output_path = DEFAULT_OUTPUT_FILE;
    int requested_frames = DEFAULT_FRAME_COUNT;
    const size_t expected_nv12_size =
        (size_t)VIDEO_WIDTH * (size_t)VIDEO_HEIGHT * 3U / 2U;

    V4L2Capture capture;
    V4L2Frame frame;
    MppEncoder encoder;
    Mp4Muxer muxer;

    int capture_initialized = 0;
    int encoder_initialized = 0;
    int muxer_initialized = 0;
    int frame_acquired = 0;
    int encoded_frames = 0;
    int key_frames = 0;
    uint64_t total_h264_bytes = 0U;
    uint64_t dropped_capture_frames = 0U;
    unsigned int previous_sequence = 0U;
    int have_previous_sequence = 0;

    int64_t total_acquire_us = 0;
    int64_t total_encode_us = 0;
    int64_t total_mux_us = 0;
    int64_t wall_start_us = -1;
    int64_t wall_end_us = -1;

    int exit_code = EXIT_FAILURE;
    int i;

    memset(&capture, 0, sizeof(capture));
    capture.fd = -1;
    memset(&frame, 0, sizeof(frame));
    memset(&encoder, 0, sizeof(encoder));
    memset(&muxer, 0, sizeof(muxer));

    if (argc > 4)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2)
        device = argv[1];

    if (argc >= 3)
        output_path = argv[2];

    if (argc >= 4 &&
        parse_positive_int(argv[3], &requested_frames) != 0)
    {
        printf("invalid frame_count: %s\n", argv[3]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("live camera MP4 test\n");
    printf("device       : %s\n", device);
    printf("output       : %s\n", output_path);
    printf("resolution   : %dx%d NV12\n", VIDEO_WIDTH, VIDEO_HEIGHT);
    printf("frame rate   : %d fps\n", VIDEO_FRAME_RATE);
    printf("GOP          : %d frames\n", VIDEO_GOP);
    printf("bit rate     : %d bps\n", VIDEO_BIT_RATE);
    printf("expected NV12: %zu bytes/frame\n", expected_nv12_size);
    printf("frame count  : %d\n", requested_frames);
    printf("warmup frames: %d\n", DEFAULT_WARMUP_FRAMES);
    printf("raw NV12 intermediate files: disabled\n");
    printf("raw H264 intermediate files: disabled\n");

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
        goto CLEANUP;
    }

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

    if (mpp_encoder_init_ex(&encoder,
                            VIDEO_WIDTH,
                            VIDEO_HEIGHT,
                            VIDEO_FRAME_RATE,
                            VIDEO_GOP,
                            VIDEO_BIT_RATE) < 0)
    {
        printf("encoder init failed\n");
        goto CLEANUP;
    }
    encoder_initialized = 1;

    /* Do not leave an old file behind when this isolated test is repeated. */
    if (remove(output_path) != 0 && errno != ENOENT)
    {
        printf("remove old output failed: %s: %s\n",
               output_path,
               strerror(errno));
        goto CLEANUP;
    }

    if (mp4_muxer_init(&muxer,
                       MP4_MUXER_ID,
                       output_path,
                       VIDEO_WIDTH,
                       VIDEO_HEIGHT,
                       VIDEO_FRAME_RATE,
                       VIDEO_BIT_RATE) < 0)
    {
        printf("MP4 muxer init failed\n");
        goto CLEANUP;
    }
    muxer_initialized = 1;

    wall_start_us = get_time_us();
    if (wall_start_us < 0)
    {
        printf("get start time failed\n");
        goto CLEANUP;
    }

    while (encoded_frames < requested_frames && !g_stop)
    {
        uint8_t *h264_data = NULL;
        int h264_len;
        int key_frame = 0;
        int64_t pts_us;
        int64_t acquire_start_us;
        int64_t acquire_end_us;
        int64_t encode_start_us;
        int64_t encode_end_us;
        int64_t mux_start_us;
        int64_t mux_end_us;
        int64_t acquire_us;
        int64_t encode_us;
        int64_t mux_us;
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

        h264_len = mpp_encoder_encode(&encoder,
                                      frame.data,
                                      (int)expected_nv12_size,
                                      &h264_data);

        encode_end_us = get_time_us();

        /*
         * The v8 encoder has copied the complete NV12 frame into its own MPP
         * input buffer before returning, so the camera buffer may be returned
         * to V4L2 before the compressed packet is passed to librkmuxer.
         */
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

        /* Fixed 25 fps timeline, expressed in microseconds. */
        pts_us = (int64_t)encoded_frames * 1000000LL /
                 VIDEO_FRAME_RATE;

        mux_start_us = get_time_us();

        if (mp4_muxer_write_h264(&muxer,
                                 h264_data,
                                 (size_t)h264_len,
                                 pts_us,
                                 &key_frame) < 0)
        {
            printf("frame %d MP4 write failed\n", encoded_frames);
            free(h264_data);
            goto CLEANUP;
        }

        mux_end_us = get_time_us();

        acquire_us = acquire_end_us - acquire_start_us;
        encode_us = encode_end_us - encode_start_us;
        mux_us = mux_end_us - mux_start_us;

        total_acquire_us += acquire_us;
        total_encode_us += encode_us;
        total_mux_us += mux_us;
        total_h264_bytes += (uint64_t)h264_len;
        if (key_frame)
            key_frames++;
        encoded_frames++;

        printf("MP4 frame %d/%d: sequence=%u pts=%lld us "
               "key=%d nv12=%zu h264=%d "
               "acquire=%.3f ms encode=%.3f ms mux=%.3f ms\n",
               encoded_frames,
               requested_frames,
               captured_sequence,
               (long long)pts_us,
               key_frame,
               captured_size,
               h264_len,
               (double)acquire_us / 1000.0,
               (double)encode_us / 1000.0,
               (double)mux_us / 1000.0);

        free(h264_data);
        h264_data = NULL;
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

    /* Finalize MP4 before destroying the encoder and capture objects. */
    if (muxer_initialized)
    {
        if (mp4_muxer_close(&muxer) < 0)
            exit_code = EXIT_FAILURE;
        muxer_initialized = 0;
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
    printf("live camera MP4 summary\n");
    printf("encoded frames      : %d/%d\n",
           encoded_frames,
           requested_frames);
    printf("key frames          : %d\n", key_frames);
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

        printf("average mux time    : %.3f ms/frame\n",
               (double)total_mux_us /
               (double)encoded_frames /
               1000.0);

        printf("average packet size : %.2f bytes/frame\n",
               (double)total_h264_bytes /
               (double)encoded_frames);

        printf("timeline duration   : %.3f s\n",
               (double)encoded_frames /
               (double)VIDEO_FRAME_RATE);
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
        printf("live MP4 saved to   : %s\n", output_path);
    }
    else
    {
        printf("live camera MP4 test failed\n");
    }

    return exit_code;
}
