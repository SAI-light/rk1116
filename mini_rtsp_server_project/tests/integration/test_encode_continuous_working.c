/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_encode_continuous.c
 *    Description:  Continuous NV12 -> H264 isolation test.
 *
 *                  The input NV12 file is read exactly once into memory. The
 *                  same in-memory frame is then submitted repeatedly to the
 *                  already verified RV1106 vendor-packet encoder.
 *
 *                  No raw NV12 frame is written to disk inside the loop.
 *                  Only the final compressed H264 stream is written, so this
 *                  test can verify:
 *
 *                  1. One encoder instance can encode many frames continuously.
 *                  2. Every vendor packet is copied and released correctly.
 *                  3. The generated Annex-B packets form one playable H264 stream.
 *                  4. Encoding time and compressed-output write time can be
 *                     measured separately.
 *
 *        Version:  1.0.0 (2026-07-27)
 ********************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "mpp_encoder.h"

#define DEFAULT_INPUT_FILE   "frame_nv12.yuv"
#define DEFAULT_OUTPUT_FILE  "output_100.h264"
#define DEFAULT_FRAME_COUNT  100
#define VIDEO_WIDTH          2304
#define VIDEO_HEIGHT         1296

static int64_t get_time_us(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
        return -1;

    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
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
    printf("Usage: %s [input_nv12] [output_h264] [frame_count]\n", program);
    printf("\n");
    printf("Defaults:\n");
    printf("  input_nv12 : %s\n", DEFAULT_INPUT_FILE);
    printf("  output_h264: %s\n", DEFAULT_OUTPUT_FILE);
    printf("  frame_count: %d\n", DEFAULT_FRAME_COUNT);
    printf("\n");
    printf("Examples:\n");
    printf("  %s\n", program);
    printf("  %s frame_nv12.yuv output_30.h264 30\n", program);
    printf("  %s frame_nv12.yuv /dev/null 300\n", program);
}

int main(int argc, char *argv[])
{
    const char *input_path = DEFAULT_INPUT_FILE;
    const char *output_path = DEFAULT_OUTPUT_FILE;
    int frame_count = DEFAULT_FRAME_COUNT;

    const int width = VIDEO_WIDTH;
    const int height = VIDEO_HEIGHT;
    const size_t frame_size =
        (size_t)VIDEO_WIDTH * (size_t)VIDEO_HEIGHT * 3U / 2U;

    FILE *input_file = NULL;
    FILE *output_file = NULL;
    uint8_t *nv12_data = NULL;
    MppEncoder encoder;
    int encoder_initialized = 0;

    int encoded_frames = 0;
    uint64_t total_h264_bytes = 0;
    int64_t total_encode_us = 0;
    int64_t total_write_us = 0;
    int64_t wall_start_us = -1;
    int64_t wall_end_us = -1;

    int exit_code = EXIT_FAILURE;
    int i;

    memset(&encoder, 0, sizeof(encoder));

    if (argc > 4)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2)
        input_path = argv[1];

    if (argc >= 3)
        output_path = argv[2];

    if (argc >= 4 && parse_positive_int(argv[3], &frame_count) != 0)
    {
        printf("invalid frame_count: %s\n", argv[3]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("continuous encode test\n");
    printf("input       : %s\n", input_path);
    printf("output      : %s\n", output_path);
    printf("resolution  : %dx%d NV12\n", width, height);
    printf("frame size  : %zu bytes\n", frame_size);
    printf("frame count : %d\n", frame_count);
    printf("\n");
    printf("The NV12 file will be read once, then reused from memory.\n");
    printf("Only compressed H264 packets are written in the loop.\n");

    input_file = fopen(input_path, "rb");
    if (input_file == NULL)
    {
        printf("open input file failed: %s: %s\n",
               input_path, strerror(errno));
        goto CLEANUP;
    }

    nv12_data = (uint8_t *)malloc(frame_size);
    if (nv12_data == NULL)
    {
        printf("malloc NV12 buffer failed, size=%zu\n", frame_size);
        goto CLEANUP;
    }

    if (fread(nv12_data, 1, frame_size, input_file) != frame_size)
    {
        if (ferror(input_file))
        {
            printf("read input file failed: %s\n", strerror(errno));
        }
        else
        {
            printf("input file is too small: expected %zu bytes\n",
                   frame_size);
        }
        goto CLEANUP;
    }

    fclose(input_file);
    input_file = NULL;

    printf("input NV12 loaded into memory once\n");

    if (mpp_encoder_init(&encoder, width, height) < 0)
    {
        printf("encoder init failed\n");
        goto CLEANUP;
    }
    encoder_initialized = 1;

    output_file = fopen(output_path, "wb");
    if (output_file == NULL)
    {
        printf("open output file failed: %s: %s\n",
               output_path, strerror(errno));
        goto CLEANUP;
    }

    wall_start_us = get_time_us();
    if (wall_start_us < 0)
    {
        printf("get start time failed\n");
        goto CLEANUP;
    }

    for (i = 0; i < frame_count; ++i)
    {
        uint8_t *h264_data = NULL;
        int h264_len;
        int64_t encode_start_us;
        int64_t encode_end_us;
        int64_t write_start_us;
        int64_t write_end_us;
        int64_t frame_encode_us;

        encode_start_us = get_time_us();

        h264_len = mpp_encoder_encode(&encoder,
                                      nv12_data,
                                      (int)frame_size,
                                      &h264_data);

        encode_end_us = get_time_us();

        if (encode_start_us < 0 || encode_end_us < 0)
        {
            printf("frame %d: get encode timestamp failed\n", i);
            free(h264_data);
            goto CLEANUP;
        }

        frame_encode_us = encode_end_us - encode_start_us;
        total_encode_us += frame_encode_us;

        if (h264_len <= 0 || h264_data == NULL)
        {
            printf("frame %d: encode failed, len=%d data=%p\n",
                   i, h264_len, h264_data);
            free(h264_data);
            goto CLEANUP;
        }

        write_start_us = get_time_us();

        if (fwrite(h264_data, 1, (size_t)h264_len, output_file) !=
            (size_t)h264_len)
        {
            printf("frame %d: write H264 failed: %s\n",
                   i, strerror(errno));
            free(h264_data);
            goto CLEANUP;
        }

        write_end_us = get_time_us();

        if (write_start_us < 0 || write_end_us < 0)
        {
            printf("frame %d: get write timestamp failed\n", i);
            free(h264_data);
            goto CLEANUP;
        }

        total_write_us += write_end_us - write_start_us;
        total_h264_bytes += (uint64_t)h264_len;
        encoded_frames++;

        printf("continuous frame %d/%d:"
               " h264=%d bytes"
               " encode=%.3f ms"
               " write=%.3f ms\n",
               encoded_frames,
               frame_count,
               h264_len,
               (double)frame_encode_us / 1000.0,
               (double)(write_end_us - write_start_us) / 1000.0);

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

    exit_code = EXIT_SUCCESS;

CLEANUP:
    if (output_file != NULL)
    {
        if (fclose(output_file) != 0)
        {
            printf("close output file failed: %s\n", strerror(errno));
            exit_code = EXIT_FAILURE;
        }
        output_file = NULL;
    }

    if (encoder_initialized)
    {
        mpp_encoder_close(&encoder);
        encoder_initialized = 0;
    }

    if (input_file != NULL)
    {
        fclose(input_file);
        input_file = NULL;
    }

    free(nv12_data);
    nv12_data = NULL;

    printf("\n");
    printf("continuous encode summary\n");
    printf("encoded frames     : %d/%d\n", encoded_frames, frame_count);
    printf("total H264 bytes   : %" PRIu64 "\n", total_h264_bytes);

    if (encoded_frames > 0 && total_encode_us > 0)
    {
        printf("average encode time: %.3f ms/frame\n",
               (double)total_encode_us /
               (double)encoded_frames /
               1000.0);

        printf("encode-only speed  : %.2f fps\n",
               (double)encoded_frames * 1000000.0 /
               (double)total_encode_us);
    }

    if (encoded_frames > 0)
    {
        printf("average write time : %.3f ms/frame\n",
               (double)total_write_us /
               (double)encoded_frames /
               1000.0);

        printf("average packet size: %.2f bytes/frame\n",
               (double)total_h264_bytes /
               (double)encoded_frames);
    }

    if (wall_start_us >= 0 &&
        wall_end_us > wall_start_us &&
        encoded_frames > 0)
    {
        int64_t wall_total_us = wall_end_us - wall_start_us;

        printf("wall-clock time    : %.3f s\n",
               (double)wall_total_us / 1000000.0);

        printf("overall speed      : %.2f fps\n",
               (double)encoded_frames * 1000000.0 /
               (double)wall_total_us);
    }

    if (exit_code == EXIT_SUCCESS)
    {
        printf("continuous H264 saved to: %s\n", output_path);
    }
    else
    {
        printf("continuous encode test failed\n");
    }

    return exit_code;
}
