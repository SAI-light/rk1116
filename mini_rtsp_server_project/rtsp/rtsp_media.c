/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtsp_media.c
 *    Description:  Live camera media source for the RTSP server.
 *
 *                  /dev/video11 NV12
 *                        -> RV1106 MPP H264 access unit in memory
 *                        -> Annex-B NAL split
 *                        -> RTP single NAL / FU-A
 *                        -> UDP client negotiated by RTSP SETUP
 ********************************************************************************/

#include "rtsp_media.h"

#include "h264_annexb.h"
#include "h264_rtp.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAMERA_DEVICE          "/dev/video11"
#define CAPTURE_TIMEOUT_MS     3000
#define WARMUP_FRAME_COUNT     5
#define H264_NAL_IDR           5U
#define H264_NAL_SPS           7U
#define H264_NAL_PPS           8U

typedef struct
{
    RTSPMedia *media;
    RTSPSession *session;
} MediaThreadArgs;

static int media_should_run(RTSPMedia *media,
                            RTSPSession *session)
{
    int should_run;

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
        printf("invalid Annex-B H264 access unit, size=%zu\n",
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
                              unsigned int *sequence)
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
        printf("live capture failed: %s\n", strerror(errno));
        return -1;
    }
    acquired = 1;

    if (frame.size < expected_size)
    {
        printf("live frame too small: sequence=%u size=%zu expected=%zu\n",
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

    if (*h264_len <= 0 || *h264_data == NULL)
    {
        printf("live encode failed: len=%d data=%p\n",
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
            printf("release live camera frame failed\n");
            free(*h264_data);
            *h264_data = NULL;
            *h264_len = -1;
            result = -1;
        }
    }

    return result;
}

static int prepare_bootstrap_access_unit(RTSPMedia *media)
{
    uint8_t *h264_data = NULL;
    int h264_len = -1;
    unsigned int sequence = 0U;

    if (capture_and_encode(media,
                           &h264_data,
                           &h264_len,
                           &sequence) != 0)
    {
        return -1;
    }

    if (!h264_annexb_contains_type(h264_data,
                                   (size_t)h264_len,
                                   H264_NAL_IDR))
    {
        printf("first live H264 access unit has no IDR\n");
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
        printf("extract live SPS/PPS failed\n");
        free(h264_data);
        return -1;
    }

    media->bootstrap_au = h264_data;
    media->bootstrap_au_size = (size_t)h264_len;
    media->bootstrap_consumed = 0;

    printf("live bootstrap ready: sequence=%u au=%d sps=%zu pps=%zu\n",
           sequence,
           h264_len,
           media->sps_size,
           media->pps_size);

    return 0;
}

int rtsp_media_init(RTSPMedia *media)
{
    V4L2Frame frame;
    int i;

    if (media == NULL)
        return -1;

    memset(media, 0, sizeof(*media));
    media->capture.fd = -1;
    media->sender.sockfd = -1;

    if (pthread_mutex_init(&media->lock, NULL) != 0)
    {
        printf("media mutex init failed\n");
        return -1;
    }
    media->lock_initialized = 1;

    if (v4l2_capture_init(&media->capture,
                          CAMERA_DEVICE,
                          RTSP_MEDIA_WIDTH,
                          RTSP_MEDIA_HEIGHT) < 0)
    {
        printf("live capture init failed\n");
        goto FAIL;
    }
    media->capture_initialized = 1;

    if (media->capture.width != RTSP_MEDIA_WIDTH ||
        media->capture.height != RTSP_MEDIA_HEIGHT ||
        media->capture.bytesperline != RTSP_MEDIA_WIDTH)
    {
        printf("unsupported camera format: size=%dx%d stride=%d\n",
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
            printf("warmup frame %d failed\n", i);
            goto FAIL;
        }

        printf("RTSP warmup frame %d/%d sequence=%u size=%zu\n",
               i + 1,
               WARMUP_FRAME_COUNT,
               frame.sequence,
               frame.size);

        if (v4l2_capture_release_frame(&media->capture, &frame) < 0)
        {
            printf("warmup frame %d release failed\n", i);
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
        printf("live MPP encoder init failed\n");
        goto FAIL;
    }
    media->encoder_initialized = 1;

    if (prepare_bootstrap_access_unit(media) != 0)
        goto FAIL;

    media->initialized = 1;

    printf("RTSP live media init success: %dx%d %dfps GOP=%d bitrate=%d\n",
           RTSP_MEDIA_WIDTH,
           RTSP_MEDIA_HEIGHT,
           RTSP_MEDIA_FPS,
           RTSP_MEDIA_GOP,
           RTSP_MEDIA_BIT_RATE);

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
    uint16_t seq = 100U;
    uint32_t timestamp = 0U;
    const uint32_t timestamp_step =
        (uint32_t)(RTSP_MEDIA_RTP_CLOCK / RTSP_MEDIA_FPS);
    unsigned int frame_count = 0U;
    int waiting_for_idr;

    free(args);

    printf("live media thread start: client=%s:%d local_rtp=%d ts_step=%u\n",
           session->client_ip,
           session->client_rtp_port,
           session->server_rtp_port,
           timestamp_step);

    if (rtp_sender_init_ex(&media->sender,
                           session->server_rtp_port,
                           session->client_ip,
                           session->client_rtp_port) < 0)
    {
        printf("live RTP sender init failed\n");
        goto EXIT;
    }

    waiting_for_idr = 1;

    pthread_mutex_lock(&media->lock);
    if (!media->bootstrap_consumed &&
        media->bootstrap_au != NULL &&
        media->bootstrap_au_size > 0U)
    {
        pthread_mutex_unlock(&media->lock);

        if (send_access_unit(media,
                             media->bootstrap_au,
                             media->bootstrap_au_size,
                             &seq,
                             timestamp,
                             0) < 0)
        {
            printf("send bootstrap access unit failed\n");
            goto EXIT;
        }

        pthread_mutex_lock(&media->lock);
        media->bootstrap_consumed = 1;
        pthread_mutex_unlock(&media->lock);

        waiting_for_idr = 0;
        timestamp += timestamp_step;
        ++frame_count;
        printf("sent bootstrap IDR: frame=%u timestamp=%u seq=%u\n",
               frame_count,
               timestamp - timestamp_step,
               seq);
    }
    else
    {
        pthread_mutex_unlock(&media->lock);
    }

    while (media_should_run(media, session))
    {
        uint8_t *h264_data = NULL;
        int h264_len = -1;
        unsigned int capture_sequence = 0U;
        int is_idr;
        int prepend_parameter_sets = 0;
        int packet_count;

        if (capture_and_encode(media,
                               &h264_data,
                               &h264_len,
                               &capture_sequence) != 0)
        {
            break;
        }

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
            prepend_parameter_sets = 1;
            waiting_for_idr = 0;
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
            printf("send live access unit failed\n");
            break;
        }

        ++frame_count;

        if (is_idr || frame_count == 1U || frame_count % 25U == 0U)
        {
            printf("RTP live frame=%u capture_sequence=%u idr=%d "
                   "timestamp=%u packets=%d next_seq=%u\n",
                   frame_count,
                   capture_sequence,
                   is_idr,
                   timestamp,
                   packet_count,
                   seq);
        }

        timestamp += timestamp_step;
    }

EXIT:
    rtp_sender_close(&media->sender);

    pthread_mutex_lock(&media->lock);
    session->playing = 0;
    pthread_mutex_unlock(&media->lock);

    printf("live media thread exit: frames=%u\n", frame_count);
    return NULL;
}

int rtsp_media_start(RTSPMedia *media, RTSPSession *session)
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
        printf("media thread is already running\n");
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
        printf("create media thread failed: %s\n", strerror(ret));
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

int rtsp_media_stop(RTSPMedia *media, RTSPSession *session)
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
