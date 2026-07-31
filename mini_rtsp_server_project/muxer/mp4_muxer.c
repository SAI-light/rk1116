/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: mp4_muxer.c
 * Description: Annex-B H.264 to MP4 wrapper around Rockchip librkmuxer.
 ********************************************************************************/

#include "mp4_muxer.h"
#include "log.h"
#include "rkmuxer.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MODULE_NAME           "muxer"
#define H264_PROFILE_BASELINE 66
#define H264_LEVEL_5_2        52

#define H264_NAL_SLICE 1
#define H264_NAL_IDR   5
#define H264_NAL_SPS   7
#define H264_NAL_PPS   8

typedef struct
{
    int has_annexb_start_code;
    int has_vcl;
    int has_idr;
    int has_sps;
    int has_pps;
} H264PacketInfo;

static int path_has_mp4_extension(const char *path)
{
    const char *dot;

    if (path == NULL)
        return 0;

    dot = strrchr(path, '.');
    if (dot == NULL)
        return 0;

    return strcmp(dot, ".mp4") == 0 || strcmp(dot, ".MP4") == 0;
}

static size_t annexb_start_code_size(const uint8_t *data,
                                     size_t size,
                                     size_t offset)
{
    if (data == NULL || offset >= size)
        return 0U;

    if (offset + 4U <= size &&
        data[offset] == 0x00U &&
        data[offset + 1U] == 0x00U &&
        data[offset + 2U] == 0x00U &&
        data[offset + 3U] == 0x01U)
    {
        return 4U;
    }

    if (offset + 3U <= size &&
        data[offset] == 0x00U &&
        data[offset + 1U] == 0x00U &&
        data[offset + 2U] == 0x01U)
    {
        return 3U;
    }

    return 0U;
}

static H264PacketInfo inspect_annexb_h264(const uint8_t *data,
                                          size_t size)
{
    H264PacketInfo info;
    size_t offset = 0U;

    memset(&info, 0, sizeof(info));

    while (offset < size)
    {
        size_t start_code_size = annexb_start_code_size(data, size, offset);

        if (start_code_size == 0U)
        {
            ++offset;
            continue;
        }

        info.has_annexb_start_code = 1;
        offset += start_code_size;

        if (offset >= size)
            break;

        {
            unsigned int nal_type = data[offset] & 0x1fU;

            if (nal_type >= H264_NAL_SLICE && nal_type <= H264_NAL_IDR)
                info.has_vcl = 1;

            if (nal_type == H264_NAL_IDR)
                info.has_idr = 1;
            else if (nal_type == H264_NAL_SPS)
                info.has_sps = 1;
            else if (nal_type == H264_NAL_PPS)
                info.has_pps = 1;
        }

        ++offset;
    }

    return info;
}

int mp4_muxer_init(Mp4Muxer *muxer,
                   int muxer_id,
                   const char *output_path,
                   int width,
                   int height,
                   int frame_rate,
                   int bit_rate)
{
    VideoParam video_param;
    int ret;

    if (muxer == NULL || output_path == NULL ||
        width <= 0 || height <= 0 ||
        frame_rate <= 0 || bit_rate <= 0 || muxer_id < 0)
    {
        LOG_ERROR(MODULE_NAME, "invalid MP4 muxer parameter");
        return -1;
    }

    if (!path_has_mp4_extension(output_path))
    {
        LOG_ERROR(MODULE_NAME,
                  "MP4 output path must end in .mp4: %s",
                  output_path);
        return -1;
    }

    memset(muxer, 0, sizeof(*muxer));
    muxer->id = muxer_id;
    muxer->width = width;
    muxer->height = height;
    muxer->frame_rate = frame_rate;
    muxer->bit_rate = bit_rate;
    muxer->last_pts_us = -1;

    memset(&video_param, 0, sizeof(video_param));
    snprintf(video_param.format, sizeof(video_param.format), "%s", "NV12");
    snprintf(video_param.codec, sizeof(video_param.codec), "%s", "H.264");

    video_param.width = width;
    video_param.height = height;
    video_param.bit_rate = bit_rate;
    video_param.profile = H264_PROFILE_BASELINE;
    video_param.level = H264_LEVEL_5_2;
    video_param.frame_rate_den = 1;
    video_param.frame_rate_num = frame_rate;

    ret = rkmuxer_init(muxer_id, NULL, output_path, &video_param, NULL);
    if (ret != 0)
    {
        LOG_ERROR(MODULE_NAME,
                  "rkmuxer_init failed: id=%d ret=%d output=%s",
                  muxer_id,
                  ret,
                  output_path);
        return -1;
    }

    muxer->initialized = 1;

    LOG_INFO(MODULE_NAME,
             "initialized: id=%d output=%s size=%dx%d fps=%d bitrate=%d profile=%d level=%d",
             muxer_id,
             output_path,
             width,
             height,
             frame_rate,
             bit_rate,
             H264_PROFILE_BASELINE,
             H264_LEVEL_5_2);

    return 0;
}

int mp4_muxer_write_h264(Mp4Muxer *muxer,
                         const uint8_t *h264_data,
                         size_t h264_size,
                         int64_t pts_us,
                         int *key_frame_out)
{
    H264PacketInfo info;
    int key_frame;
    int ret;

    if (key_frame_out != NULL)
        *key_frame_out = 0;

    if (muxer == NULL || !muxer->initialized ||
        h264_data == NULL || h264_size == 0U ||
        h264_size > UINT_MAX || pts_us < 0)
    {
        LOG_ERROR(MODULE_NAME, "invalid MP4 video frame parameter");
        return -1;
    }

    if (muxer->frame_count > 0U && pts_us <= muxer->last_pts_us)
    {
        LOG_ERROR(MODULE_NAME,
                  "non-increasing PTS: current=%lld previous=%lld",
                  (long long)pts_us,
                  (long long)muxer->last_pts_us);
        return -1;
    }

    info = inspect_annexb_h264(h264_data, h264_size);

    if (!info.has_annexb_start_code || !info.has_vcl)
    {
        LOG_ERROR(MODULE_NAME,
                  "invalid Annex-B access unit: size=%zu start_code=%d vcl=%d",
                  h264_size,
                  info.has_annexb_start_code,
                  info.has_vcl);
        return -1;
    }

    key_frame = info.has_idr ? 1 : 0;

    if (muxer->frame_count == 0U &&
        (!info.has_idr || !info.has_sps || !info.has_pps))
    {
        LOG_ERROR(MODULE_NAME,
                  "first packet is incomplete: IDR=%d SPS=%d PPS=%d",
                  info.has_idr,
                  info.has_sps,
                  info.has_pps);
        return -1;
    }

    ret = rkmuxer_write_video_frame(muxer->id,
                                    (unsigned char *)h264_data,
                                    (unsigned int)h264_size,
                                    pts_us,
                                    key_frame);
    if (ret != 0)
    {
        LOG_ERROR(MODULE_NAME,
                  "write frame failed: frame=%llu pts=%lld size=%zu key=%d ret=%d",
                  (unsigned long long)muxer->frame_count,
                  (long long)pts_us,
                  h264_size,
                  key_frame,
                  ret);
        return -1;
    }

    ++muxer->frame_count;
    if (key_frame)
        ++muxer->key_frame_count;
    muxer->last_pts_us = pts_us;

    if (key_frame_out != NULL)
        *key_frame_out = key_frame;

    return 0;
}

int mp4_muxer_close(Mp4Muxer *muxer)
{
    int ret;

    if (muxer == NULL)
        return -1;

    if (!muxer->initialized)
        return 0;

    ret = rkmuxer_deinit(muxer->id);
    muxer->initialized = 0;

    if (ret != 0)
    {
        LOG_ERROR(MODULE_NAME,
                  "rkmuxer_deinit failed: id=%d ret=%d",
                  muxer->id,
                  ret);
        return -1;
    }

    LOG_INFO(MODULE_NAME,
             "closed: frames=%llu key_frames=%llu last_pts=%lld us",
             (unsigned long long)muxer->frame_count,
             (unsigned long long)muxer->key_frame_count,
             (long long)muxer->last_pts_us);

    return 0;
}
