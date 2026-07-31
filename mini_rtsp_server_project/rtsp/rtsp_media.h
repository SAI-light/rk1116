/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: rtsp_media.h
 * Description: Live V4L2 -> MPP H264 -> RTP + optional MP4 pipeline.
 ********************************************************************************/

#ifndef RTSP_MEDIA_H
#define RTSP_MEDIA_H

#include "mpp_encoder.h"
#include "rtp_sender.h"
#include "rtsp_session.h"
#include "v4l2_capture.h"

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#define RTSP_MEDIA_WIDTH             2304
#define RTSP_MEDIA_HEIGHT            1296
#define RTSP_MEDIA_FPS               25
#define RTSP_MEDIA_GOP               25
#define RTSP_MEDIA_BIT_RATE          4000000
#define RTSP_MEDIA_RTP_CLOCK         90000
#define RTSP_MEDIA_DEVICE_PATH_MAX   128
#define RTSP_MEDIA_RECORD_PATH_MAX   256

typedef struct
{
    V4L2Capture capture;
    MppEncoder encoder;
    RTPSender sender;

    uint8_t *sps;
    size_t sps_size;
    uint8_t *pps;
    size_t pps_size;

    uint8_t *bootstrap_au;
    size_t bootstrap_au_size;
    unsigned int bootstrap_sequence;
    int bootstrap_consumed;

    char device_path[RTSP_MEDIA_DEVICE_PATH_MAX];
    char record_path[RTSP_MEDIA_RECORD_PATH_MAX];
    int recording_enabled;

    const volatile sig_atomic_t *external_stop_flag;

    int capture_initialized;
    int encoder_initialized;
    int initialized;

    pthread_t thread;
    pthread_mutex_t lock;
    int lock_initialized;
    int thread_running;
    int stop_requested;
} RTSPMedia;

int rtsp_media_init(RTSPMedia *media,
                    const char *device_path,
                    const char *record_path,
                    const volatile sig_atomic_t *external_stop_flag);
int rtsp_media_start(RTSPMedia *media,
                     RTSPSession *session);
int rtsp_media_stop(RTSPMedia *media,
                    RTSPSession *session);
void rtsp_media_close(RTSPMedia *media);

#endif
