/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_media.c
 *    Description:  Live camera media source for the RTSP server.
 *
 *                  /dev/video11 NV12
 *                        -> RV1106 MPP H264 access unit in memory
 *                        -> RTP single NAL / FU-A -> VLC
 *                        -> Rockchip MP4 muxer    -> local recording
 ********************************************************************************/

#include "rtsp_media.h"

#include "h264_annexb.h"
#include "log.h"
#include "h264_rtp.h"
#include "mp4_muxer.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RTSP_MEDIA_BUILD_TAG   "live-rtsp-mp4-v2.4-rc1-engineering-cleanup"
#define MODULE_NAME            "media"
#define CAPTURE_TIMEOUT_MS     3000
#define WARMUP_FRAME_COUNT     5
#define LIVE_MUXER_ID          0
#define H264_NAL_IDR           5U
#define H264_NAL_SPS           7U
#define H264_NAL_PPS           8U

static const uint8_t g_annexb_start_code[4] = {
    0x00U, 0x00U, 0x00U, 0x01U
};

static int64_t monotonic_time_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;

    return (int64_t)ts.tv_sec * 1000000LL +
           (int64_t)ts.tv_nsec / 1000LL;
}

typedef struct
{
    RTSPMedia *media;
    RTSPSession *session;
} MediaThreadArgs;

static int record_path_disabled(const char *record_path)
{
    return record_path == NULL ||
           record_path[0] == '\0' ||
           strcmp(record_path, "-") == 0;
}

static int media_should_run(RTSPMedia *media,
                            RTSPSession *session)
{
    int should_run;

    if (media->external_stop_flag != NULL &&
        *media->external_stop_flag != 0)
    {
        return 0;
    }

    pthread_mutex_lock(&media->lock);
    should_run = !media->stop_requested && session->playing;
    pthread_mutex_unlock(&media->lock);

    return should_run;
}

static int media_rtp_send(void *opaque,
                          const uint8_t *packet,
                          int size)
{
    RTSPMedia *media = (RTSPMedia *)opaque;

    return rtp_sender_send(&media->sender, packet, size);
}

static int send_parameter_set(RTSPMedia *media,
                              const uint8_t *nalu,
                              size_t nalu_size,
                              uint16_t *seq,
                              uint32_t timestamp)
{
    if (nalu == NULL || nalu_size == 0U || nalu_size > (size_t)INT_MAX)
        return -1;

    return h264_rtp_send_nalu_ctx(nalu,
                                  (int)nalu_size,
                                  seq,
                                  timestamp,
                                  0,
                                  media_rtp_send,
                                  media);
}

static int send_access_unit(RTSPMedia *media,
                            const uint8_t *access_unit,
                            size_t access_unit_size,
                            uint16_t *seq,
                            uint32_t timestamp,
                            int prepend_parameter_sets)
{
    H264NaluView nalus[H264_ANNEXB_MAX_NALUS];
    size_t count;
    size_t i;
    int packet_count = 0;
    int has_sps;
    int has_pps;

    if (h264_annexb_collect(access_unit,
                            access_unit_size,
                            nalus,
                            H264_ANNEXB_MAX_NALUS,
                            &count) != 0)
    {
        LOG_ERROR(MODULE_NAME,
                  "invalid Annex-B H264 access unit, size=%zu",
                  access_unit_size);
        return -1;
    }

    has_sps = h264_annexb_contains_type(access_unit,
                                        access_unit_size,
                                        H264_NAL_SPS);
    has_pps = h264_annexb_contains_type(access_unit,
                                        access_unit_size,
                                        H264_NAL_PPS);

    if (prepend_parameter_sets && !has_sps)
    {
        int ret = send_parameter_set(media,
                                     media->sps,
                                     media->sps_size,
                                     seq,
                                     timestamp);
        if (ret < 0)
            return -1;
        packet_count += ret;
    }

    if (prepend_parameter_sets && !has_pps)
    {
        int ret = send_parameter_set(media,
                                     media->pps,
                                     media->pps_size,
                                     seq,
                                     timestamp);
        if (ret < 0)
            return -1;
        packet_count += ret;
    }

    for (i = 0U; i < count; ++i)
    {
        int marker = (i + 1U == count) ? 1 : 0;
        int ret;

        if (nalus[i].size > (size_t)INT_MAX)
            return -1;

        ret = h264_rtp_send_nalu_ctx(nalus[i].data,
                                     (int)nalus[i].size,
                                     seq,
                                     timestamp,
                                     marker,
                                     media_rtp_send,
                                     media);
        if (ret < 0)
            return -1;

        packet_count += ret;
    }

    return packet_count;
}

static int capture_and_encode(RTSPMedia *media,
                              uint8_t **h264_data,
                              int *h264_len,
                              unsigned int *sequence,
                              int64_t *capture_timestamp_us)
{
    V4L2Frame frame;
    const size_t expected_size =
        (size_t)RTSP_MEDIA_WIDTH * (size_t)RTSP_MEDIA_HEIGHT * 3U / 2U;
    int acquired = 0;
    int result = -1;

    memset(&frame, 0, sizeof(frame));
    *h264_data = NULL;
    *h264_len = -1;

    if (v4l2_capture_acquire_frame(&media->capture,
                                   &frame,
                                   CAPTURE_TIMEOUT_MS) < 0)
    {
        LOG_ERROR_ERRNO(MODULE_NAME, errno, "live capture failed");
        return -1;
    }
    acquired = 1;

    if (frame.size < expected_size)
    {
        LOG_ERROR(MODULE_NAME,
                  "live frame too small: sequence=%u size=%zu expected=%zu",
                  frame.sequence,
                  frame.size,
                  expected_size);
        goto CLEANUP;
    }

    *h264_len = mpp_encoder_encode(&media->encoder,
                                   frame.data,
                                   (int)expected_size,
                                   h264_data);

    if (sequence != NULL)
        *sequence = frame.sequence;

    if (capture_timestamp_us != NULL)
        *capture_timestamp_us = frame.timestamp_us;

    if (*h264_len <= 0 || *h264_data == NULL)
    {
        LOG_ERROR(MODULE_NAME,
                  "live encode failed: len=%d data=%p",
                  *h264_len,
                  (void *)*h264_data);
        free(*h264_data);
        *h264_data = NULL;
        *h264_len = -1;
        goto CLEANUP;
    }

    result = 0;

CLEANUP:
    if (acquired)
    {
        if (v4l2_capture_release_frame(&media->capture, &frame) < 0)
        {
            LOG_ERROR(MODULE_NAME, "release live camera frame failed");
            free(*h264_data);
            *h264_data = NULL;
            *h264_len = -1;
            result = -1;
        }
    }

    return result;
}


static int drain_stale_capture_buffers(RTSPMedia *media,
                                       unsigned int *drained_count,
                                       unsigned int *first_sequence,
                                       unsigned int *last_sequence)
{
    unsigned int drained = 0U;
    unsigned int first = 0U;
    unsigned int last = 0U;
    unsigned int i;

    if (media == NULL)
        return -1;

    /*
     * While the RTSP server waits for PLAY, the four MMAP buffers can already
     * contain old frames. Drain at most the configured buffer count so the
     * first frame used by the live session is captured after PLAY.
     */
    for (i = 0U; i < media->capture.buffer_count; ++i)
    {
        V4L2Frame frame;

        memset(&frame, 0, sizeof(frame));

        if (v4l2_capture_acquire_frame(&media->capture,
                                       &frame,
                                       0) < 0)
        {
            if (errno == EAGAIN || errno == ETIMEDOUT)
                break;

            LOG_ERROR_ERRNO(MODULE_NAME,
                            errno,
                            "drain stale camera buffer failed");
            return -1;
        }

        if (drained == 0U)
            first = frame.sequence;

        last = frame.sequence;
        ++drained;

        if (v4l2_capture_release_frame(&media->capture, &frame) < 0)
        {
            LOG_ERROR(MODULE_NAME, "release stale camera buffer failed");
            return -1;
        }
    }

    if (drained_count != NULL)
        *drained_count = drained;

    if (first_sequence != NULL)
        *first_sequence = first;

    if (last_sequence != NULL)
        *last_sequence = last;

    return 0;
}

static int prepare_bootstrap_access_unit(RTSPMedia *media)
{
    uint8_t *h264_data = NULL;
    int h264_len = -1;
    unsigned int sequence = 0U;

    if (capture_and_encode(media,
                           &h264_data,
                           &h264_len,
                           &sequence,
                           NULL) != 0)
    {
        return -1;
    }

    if (!h264_annexb_contains_type(h264_data,
                                   (size_t)h264_len,
                                   H264_NAL_IDR))
    {
        LOG_ERROR(MODULE_NAME, "first live H264 access unit has no IDR");
        free(h264_data);
        return -1;
    }

    if (h264_annexb_copy_parameter_sets(h264_data,
                                        (size_t)h264_len,
                                        &media->sps,
                                        &media->sps_size,
                                        &media->pps,
                                        &media->pps_size) != 0)
    {
        LOG_ERROR(MODULE_NAME, "extract live SPS/PPS failed");
        free(h264_data);
        return -1;
    }

    media->bootstrap_au = h264_data;
    media->bootstrap_au_size = (size_t)h264_len;
    media->bootstrap_sequence = sequence;
    media->bootstrap_consumed = 0;

    LOG_INFO(MODULE_NAME,
             "live bootstrap ready: sequence=%u au=%d sps=%zu pps=%zu",
             sequence,
             h264_len,
             media->sps_size,
             media->pps_size);

    return 0;
}

static int build_complete_mp4_start_au(RTSPMedia *media,
                                       const uint8_t *access_unit,
                                       size_t access_unit_size,
                                       uint8_t **owned_buffer,
                                       const uint8_t **write_data,
                                       size_t *write_size)
{
    size_t total_size;
    size_t offset = 0U;
    uint8_t *buffer;
    int has_sps;
    int has_pps;

    if (media == NULL || access_unit == NULL || access_unit_size == 0U ||
        owned_buffer == NULL || write_data == NULL || write_size == NULL)
    {
        return -1;
    }

    *owned_buffer = NULL;
    *write_data = access_unit;
    *write_size = access_unit_size;

    if (!h264_annexb_contains_type(access_unit,
                                   access_unit_size,
                                   H264_NAL_IDR))
    {
        LOG_ERROR(MODULE_NAME, "first recording access unit has no IDR");
        return -1;
    }

    has_sps = h264_annexb_contains_type(access_unit,
                                        access_unit_size,
                                        H264_NAL_SPS);
    has_pps = h264_annexb_contains_type(access_unit,
                                        access_unit_size,
                                        H264_NAL_PPS);

    if (has_sps && has_pps)
        return 0;

    if (media->sps == NULL || media->sps_size == 0U ||
        media->pps == NULL || media->pps_size == 0U)
    {
        LOG_ERROR(MODULE_NAME,
                  "cannot prepend SPS/PPS to first recording frame");
        return -1;
    }

    if (media->sps_size > SIZE_MAX - sizeof(g_annexb_start_code) ||
        media->pps_size > SIZE_MAX - sizeof(g_annexb_start_code))
    {
        return -1;
    }

    total_size = sizeof(g_annexb_start_code) + media->sps_size;
    if (total_size > SIZE_MAX - sizeof(g_annexb_start_code) -
                     media->pps_size)
    {
        return -1;
    }
    total_size += sizeof(g_annexb_start_code) + media->pps_size;

    if (total_size > SIZE_MAX - access_unit_size)
        return -1;
    total_size += access_unit_size;

    buffer = (uint8_t *)malloc(total_size);
    if (buffer == NULL)
        return -1;

    memcpy(buffer + offset,
           g_annexb_start_code,
           sizeof(g_annexb_start_code));
    offset += sizeof(g_annexb_start_code);

    memcpy(buffer + offset, media->sps, media->sps_size);
    offset += media->sps_size;

    memcpy(buffer + offset,
           g_annexb_start_code,
           sizeof(g_annexb_start_code));
    offset += sizeof(g_annexb_start_code);

    memcpy(buffer + offset, media->pps, media->pps_size);
    offset += media->pps_size;

    memcpy(buffer + offset, access_unit, access_unit_size);

    *owned_buffer = buffer;
    *write_data = buffer;
    *write_size = total_size;

    LOG_DEBUG(MODULE_NAME,
              "prepended SPS/PPS for first MP4 frame: original=%zu complete=%zu",
              access_unit_size,
              total_size);

    return 0;
}

static int write_mp4_access_unit(RTSPMedia *media,
                                 Mp4Muxer *muxer,
                                 const uint8_t *access_unit,
                                 size_t access_unit_size,
                                 uint64_t recording_frame_index)
{
    uint8_t *owned_buffer = NULL;
    const uint8_t *write_data = access_unit;
    size_t write_size = access_unit_size;
    int64_t pts_us;
    int ret;

    if (recording_frame_index == 0U)
    {
        if (build_complete_mp4_start_au(media,
                                        access_unit,
                                        access_unit_size,
                                        &owned_buffer,
                                        &write_data,
                                        &write_size) != 0)
        {
            return -1;
        }
    }

    pts_us = (int64_t)(recording_frame_index * 1000000ULL /
                       (uint64_t)RTSP_MEDIA_FPS);

    ret = mp4_muxer_write_h264(muxer,
                               write_data,
                               write_size,
                               pts_us,
                               NULL);

    free(owned_buffer);
    return ret;
}

int rtsp_media_init(RTSPMedia *media,
                    const char *device_path,
                    const char *record_path,
                    const volatile sig_atomic_t *external_stop_flag)
{
    V4L2Frame frame;
    int i;

    if (media == NULL)
        return -1;

    memset(media, 0, sizeof(*media));
    media->capture.fd = -1;
    media->sender.sockfd = -1;
    media->external_stop_flag = external_stop_flag;

    if (device_path == NULL || device_path[0] == '\0')
    {
        LOG_ERROR(MODULE_NAME, "camera device path is empty");
        return -1;
    }

    if (snprintf(media->device_path,
                 sizeof(media->device_path),
                 "%s",
                 device_path) >= (int)sizeof(media->device_path))
    {
        LOG_ERROR(MODULE_NAME, "camera device path is too long");
        return -1;
    }

    if (!record_path_disabled(record_path))
    {
        int len = snprintf(media->record_path,
                           sizeof(media->record_path),
                           "%s",
                           record_path);

        if (len < 0 || len >= (int)sizeof(media->record_path))
        {
            LOG_ERROR(MODULE_NAME, "record path is too long");
            return -1;
        }

        media->recording_enabled = 1;
    }

    if (pthread_mutex_init(&media->lock, NULL) != 0)
    {
        LOG_ERROR(MODULE_NAME, "media mutex init failed");
        return -1;
    }
    media->lock_initialized = 1;

    if (v4l2_capture_init(&media->capture,
                          media->device_path,
                          RTSP_MEDIA_WIDTH,
                          RTSP_MEDIA_HEIGHT) < 0)
    {
        LOG_ERROR(MODULE_NAME, "live capture init failed");
        goto FAIL;
    }
    media->capture_initialized = 1;

    if (media->capture.width != RTSP_MEDIA_WIDTH ||
        media->capture.height != RTSP_MEDIA_HEIGHT ||
        media->capture.bytesperline != RTSP_MEDIA_WIDTH)
    {
        LOG_ERROR(MODULE_NAME,
                  "unsupported camera format: size=%dx%d stride=%d",
                  media->capture.width,
                  media->capture.height,
                  media->capture.bytesperline);
        goto FAIL;
    }

    memset(&frame, 0, sizeof(frame));
    for (i = 0; i < WARMUP_FRAME_COUNT; ++i)
    {
        if (v4l2_capture_acquire_frame(&media->capture,
                                       &frame,
                                       CAPTURE_TIMEOUT_MS) < 0)
        {
            LOG_ERROR(MODULE_NAME, "warmup frame %d failed", i);
            goto FAIL;
        }

        LOG_DEBUG(MODULE_NAME,
                  "warmup frame %d/%d sequence=%u size=%zu",
                  i + 1,
                  WARMUP_FRAME_COUNT,
                  frame.sequence,
                  frame.size);

        if (v4l2_capture_release_frame(&media->capture, &frame) < 0)
        {
            LOG_ERROR(MODULE_NAME, "warmup frame %d release failed", i);
            goto FAIL;
        }
    }

    if (mpp_encoder_init_ex(&media->encoder,
                            RTSP_MEDIA_WIDTH,
                            RTSP_MEDIA_HEIGHT,
                            RTSP_MEDIA_FPS,
                            RTSP_MEDIA_GOP,
                            RTSP_MEDIA_BIT_RATE) < 0)
    {
        LOG_ERROR(MODULE_NAME, "live MPP encoder init failed");
        goto FAIL;
    }
    media->encoder_initialized = 1;

    if (prepare_bootstrap_access_unit(media) != 0)
        goto FAIL;

    media->initialized = 1;

    LOG_INFO(MODULE_NAME, "RTSP media build: %s", RTSP_MEDIA_BUILD_TAG);
    LOG_INFO(MODULE_NAME,
             "live media initialized: device=%s size=%dx%d fps=%d GOP=%d bitrate=%d",
             media->device_path,
             RTSP_MEDIA_WIDTH,
             RTSP_MEDIA_HEIGHT,
             RTSP_MEDIA_FPS,
             RTSP_MEDIA_GOP,
             RTSP_MEDIA_BIT_RATE);

    if (media->recording_enabled)
        LOG_INFO(MODULE_NAME, "MP4 recording enabled: %s", media->record_path);
    else
        LOG_INFO(MODULE_NAME, "MP4 recording disabled");

    return 0;

FAIL:
    rtsp_media_close(media);
    return -1;
}

static void *media_thread(void *arg)
{
    MediaThreadArgs *args = (MediaThreadArgs *)arg;
    RTSPMedia *media = args->media;
    RTSPSession *session = args->session;
    Mp4Muxer muxer;
    uint16_t seq = 100U;
    uint32_t timestamp = 0U;
    const uint32_t timestamp_step =
        (uint32_t)(RTSP_MEDIA_RTP_CLOCK / RTSP_MEDIA_FPS);
    uint64_t recording_frame_count = 0U;
    unsigned int frame_count = 0U;
    unsigned int captured_frame_count = 0U;
    unsigned int sequence_gap_count = 0U;
    unsigned int last_capture_sequence = 0U;
    unsigned int first_live_sequence = 0U;
    unsigned int last_live_sequence = 0U;
    int64_t first_live_capture_timestamp_us = -1;
    int64_t last_live_capture_timestamp_us = -1;
    int64_t play_start_us = -1;
    int64_t stream_start_us = -1;
    int64_t stream_end_us = -1;
    int sequence_initialized = 0;
    int muxer_initialized = 0;
    int waiting_for_idr;

    free(args);
    memset(&muxer, 0, sizeof(muxer));

    LOG_INFO(MODULE_NAME,
             "media thread start: client=%s:%d local_rtp=%d ts_step=%u",
             session->client_ip,
             session->client_rtp_port,
             session->server_rtp_port,
             timestamp_step);

    if (rtp_sender_init_ex(&media->sender,
                           session->server_rtp_port,
                           session->client_ip,
                           session->client_rtp_port) < 0)
    {
        LOG_ERROR(MODULE_NAME, "live RTP sender init failed");
        goto EXIT;
    }

    if (media->recording_enabled)
    {
        if (mp4_muxer_init(&muxer,
                           LIVE_MUXER_ID,
                           media->record_path,
                           RTSP_MEDIA_WIDTH,
                           RTSP_MEDIA_HEIGHT,
                           RTSP_MEDIA_FPS,
                           RTSP_MEDIA_BIT_RATE) != 0)
        {
            LOG_ERROR(MODULE_NAME,
                      "live MP4 muxer init failed: %s",
                      media->record_path);
            goto EXIT;
        }

        muxer_initialized = 1;
        LOG_INFO(MODULE_NAME,
                 "live MP4 recording started: %s",
                 media->record_path);
    }

    waiting_for_idr = 1;

    {
        unsigned int drained_count = 0U;
        unsigned int drained_first = 0U;
        unsigned int drained_last = 0U;

        if (drain_stale_capture_buffers(media,
                                        &drained_count,
                                        &drained_first,
                                        &drained_last) != 0)
        {
            goto EXIT;
        }

        if (drained_count > 0U)
        {
            LOG_INFO(MODULE_NAME,
                     "discarded stale camera buffers at PLAY: count=%u sequence=%u -> %u",
                     drained_count,
                     drained_first,
                     drained_last);
        }
        else
        {
            LOG_DEBUG(MODULE_NAME, "no stale camera buffer queued at PLAY");
        }
    }

    /*
     * The bootstrap access unit is captured before the RTSP server starts
     * accepting clients. Its SPS/PPS remain valid for SDP, but the old picture
     * itself must not be sent or recorded after a long wait for PLAY.
     *
     * Start both RTP and MP4 from the next fresh IDR generated after PLAY.
     */
    pthread_mutex_lock(&media->lock);
    media->bootstrap_consumed = 1;
    pthread_mutex_unlock(&media->lock);

    LOG_INFO(MODULE_NAME,
             "bootstrap access unit used for SDP only; waiting for a fresh IDR");

    play_start_us = monotonic_time_us();

    while (media_should_run(media, session))
    {
        uint8_t *h264_data = NULL;
        int h264_len = -1;
        unsigned int capture_sequence = 0U;
        int64_t capture_timestamp_us = -1;
        int is_idr;
        int prepend_parameter_sets = 0;
        int packet_count;

        if (capture_and_encode(media,
                               &h264_data,
                               &h264_len,
                               &capture_sequence,
                               &capture_timestamp_us) != 0)
        {
            break;
        }

        ++captured_frame_count;

        if (!sequence_initialized)
        {
            unsigned int idle_skipped = 0U;

            if (capture_sequence > media->bootstrap_sequence)
            {
                idle_skipped = capture_sequence -
                               media->bootstrap_sequence - 1U;
            }

            LOG_INFO(MODULE_NAME,
                     "fresh sequence baseline: bootstrap=%u first_live=%u preplay_skipped=%u capture_ts=%lld us",
                     media->bootstrap_sequence,
                     capture_sequence,
                     idle_skipped,
                     (long long)capture_timestamp_us);

            first_live_sequence = capture_sequence;
            first_live_capture_timestamp_us = capture_timestamp_us;
            sequence_initialized = 1;
        }
        else
        {
            unsigned int expected = last_capture_sequence + 1U;

            if (capture_sequence != expected)
            {
                if (capture_sequence > expected)
                {
                    unsigned int gap = capture_sequence - expected;
                    sequence_gap_count += gap;
                    LOG_WARN(MODULE_NAME,
                             "capture sequence gap: previous=%u current=%u missing=%u",
                             last_capture_sequence,
                             capture_sequence,
                             gap);
                }
                else
                {
                    LOG_WARN(MODULE_NAME,
                             "capture sequence discontinuity: previous=%u current=%u expected=%u",
                             last_capture_sequence,
                             capture_sequence,
                             expected);
                }
            }
        }
        last_capture_sequence = capture_sequence;
        last_live_sequence = capture_sequence;
        last_live_capture_timestamp_us = capture_timestamp_us;

        is_idr = h264_annexb_contains_type(h264_data,
                                           (size_t)h264_len,
                                           H264_NAL_IDR);

        if (waiting_for_idr && !is_idr)
        {
            free(h264_data);
            continue;
        }

        if (waiting_for_idr && is_idr)
        {
            int64_t now_us = monotonic_time_us();
            double wait_seconds = 0.0;

            prepend_parameter_sets = 1;
            waiting_for_idr = 0;
            stream_start_us = now_us;

            if (play_start_us >= 0 && now_us >= play_start_us)
                wait_seconds = (double)(now_us - play_start_us) / 1000000.0;

            LOG_INFO(MODULE_NAME,
                     "fresh IDR accepted: capture_sequence=%u wait=%.3fs",
                     capture_sequence,
                     wait_seconds);
        }

        if (muxer_initialized &&
            write_mp4_access_unit(media,
                                  &muxer,
                                  h264_data,
                                  (size_t)h264_len,
                                  recording_frame_count) != 0)
        {
            LOG_ERROR(MODULE_NAME,
                      "write access unit to MP4 failed: frame=%llu",
                      (unsigned long long)recording_frame_count);
            free(h264_data);
            break;
        }

        packet_count = send_access_unit(media,
                                        h264_data,
                                        (size_t)h264_len,
                                        &seq,
                                        timestamp,
                                        prepend_parameter_sets);
        free(h264_data);

        if (packet_count < 0)
        {
            LOG_ERROR(MODULE_NAME, "send live access unit failed");
            break;
        }

        ++frame_count;
        if (muxer_initialized)
            ++recording_frame_count;

        if (is_idr || frame_count == 1U || frame_count % 25U == 0U)
        {
            LOG_DEBUG(MODULE_NAME,
                      "frame=%u capture_sequence=%u idr=%d timestamp=%u packets=%d next_seq=%u recorded=%llu",
                      frame_count,
                      capture_sequence,
                      is_idr,
                      timestamp,
                      packet_count,
                      seq,
                      (unsigned long long)recording_frame_count);
        }

        timestamp += timestamp_step;
    }

EXIT:
    stream_end_us = monotonic_time_us();
    rtp_sender_close(&media->sender);

    if (muxer_initialized)
    {
        if (mp4_muxer_close(&muxer) != 0)
            LOG_ERROR(MODULE_NAME, "close live MP4 muxer failed");
        else
            LOG_INFO(MODULE_NAME,
                     "live MP4 recording finalized: %s",
                     media->record_path);
    }

    pthread_mutex_lock(&media->lock);
    session->playing = 0;
    pthread_mutex_unlock(&media->lock);

    {
        double session_seconds = 0.0;
        double media_seconds = 0.0;
        double idr_wait_seconds = 0.0;
        double pipeline_fps = 0.0;
        double timeline_seconds =
            (double)recording_frame_count / (double)RTSP_MEDIA_FPS;
        double capture_seconds = 0.0;
        double source_sequence_rate = 0.0;

        if (play_start_us >= 0 && stream_end_us >= play_start_us)
            session_seconds =
                (double)(stream_end_us - play_start_us) / 1000000.0;

        if (stream_start_us >= 0 && stream_end_us >= stream_start_us)
            media_seconds =
                (double)(stream_end_us - stream_start_us) / 1000000.0;

        if (play_start_us >= 0 && stream_start_us >= play_start_us)
            idr_wait_seconds =
                (double)(stream_start_us - play_start_us) / 1000000.0;

        if (media_seconds > 0.0)
            pipeline_fps = (double)frame_count / media_seconds;

        if (first_live_capture_timestamp_us >= 0 &&
            last_live_capture_timestamp_us >
                first_live_capture_timestamp_us)
        {
            capture_seconds =
                (double)(last_live_capture_timestamp_us -
                         first_live_capture_timestamp_us) / 1000000.0;

            source_sequence_rate =
                (double)(last_live_sequence - first_live_sequence) /
                capture_seconds;
        }

        LOG_INFO(MODULE_NAME,
                 "media summary: processed=%u recorded=%llu captured=%u gaps=%u sequence=%u->%u",
                 frame_count,
                 (unsigned long long)recording_frame_count,
                 captured_frame_count,
                 sequence_gap_count,
                 first_live_sequence,
                 last_live_sequence);
        LOG_INFO(MODULE_NAME,
                 "timing summary: mp4=%.3fs media=%.3fs session=%.3fs idr_wait=%.3fs throughput=%.2ffps",
                 timeline_seconds,
                 media_seconds,
                 session_seconds,
                 idr_wait_seconds,
                 pipeline_fps);

        if (capture_seconds > 0.0)
        {
            LOG_INFO(MODULE_NAME,
                     "capture timing: span=%.3fs source_rate=%.2f sequence/s",
                     capture_seconds,
                     source_sequence_rate);
        }

        LOG_INFO(MODULE_NAME,
                 "media thread exit: frames=%u recorded=%llu sequence_gaps=%u",
                 frame_count,
                 (unsigned long long)recording_frame_count,
                 sequence_gap_count);
    }
    return NULL;
}

int rtsp_media_start(RTSPMedia *media,
                     RTSPSession *session)
{
    MediaThreadArgs *args;
    int ret;

    if (media == NULL || session == NULL || !media->initialized ||
        session->client_ip[0] == '\0' ||
        session->client_rtp_port <= 0)
    {
        return -1;
    }

    pthread_mutex_lock(&media->lock);

    if (media->thread_running)
    {
        pthread_mutex_unlock(&media->lock);
        LOG_WARN(MODULE_NAME, "media thread is already running");
        return 0;
    }

    media->stop_requested = 0;
    session->playing = 1;
    media->thread_running = 1;

    pthread_mutex_unlock(&media->lock);

    args = (MediaThreadArgs *)malloc(sizeof(*args));
    if (args == NULL)
        goto FAIL;

    args->media = media;
    args->session = session;

    ret = pthread_create(&media->thread, NULL, media_thread, args);
    if (ret != 0)
    {
        LOG_ERROR(MODULE_NAME,
                  "create media thread failed: %s",
                  strerror(ret));
        free(args);
        goto FAIL;
    }

    return 0;

FAIL:
    pthread_mutex_lock(&media->lock);
    media->thread_running = 0;
    media->stop_requested = 1;
    session->playing = 0;
    pthread_mutex_unlock(&media->lock);
    return -1;
}

int rtsp_media_stop(RTSPMedia *media,
                    RTSPSession *session)
{
    int should_join;

    if (media == NULL)
        return -1;

    pthread_mutex_lock(&media->lock);
    media->stop_requested = 1;
    if (session != NULL)
        session->playing = 0;
    should_join = media->thread_running;
    pthread_mutex_unlock(&media->lock);

    if (should_join)
    {
        pthread_join(media->thread, NULL);

        pthread_mutex_lock(&media->lock);
        media->thread_running = 0;
        pthread_mutex_unlock(&media->lock);
    }

    return 0;
}

void rtsp_media_close(RTSPMedia *media)
{
    if (media == NULL)
        return;

    if (media->lock_initialized)
        (void)rtsp_media_stop(media, NULL);

    rtp_sender_close(&media->sender);

    free(media->bootstrap_au);
    media->bootstrap_au = NULL;
    media->bootstrap_au_size = 0U;

    free(media->sps);
    media->sps = NULL;
    media->sps_size = 0U;

    free(media->pps);
    media->pps = NULL;
    media->pps_size = 0U;

    if (media->encoder_initialized)
    {
        mpp_encoder_close(&media->encoder);
        media->encoder_initialized = 0;
    }

    if (media->capture_initialized)
    {
        v4l2_capture_close(&media->capture);
        media->capture_initialized = 0;
    }

    media->initialized = 0;

    if (media->lock_initialized)
    {
        pthread_mutex_destroy(&media->lock);
        media->lock_initialized = 0;
    }
}
