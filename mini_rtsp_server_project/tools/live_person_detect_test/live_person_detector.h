/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: live_person_detector.h
 * Description: Asynchronous RockIVA PERSON-only detector.
 ********************************************************************************/

#ifndef LIVE_PERSON_DETECTOR_H
#define LIVE_PERSON_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LivePersonDetector LivePersonDetector;

typedef struct
{
    const char *model_dir;
    unsigned int width;
    unsigned int height;
    uint8_t person_threshold;
    unsigned int buffer_count;
} LivePersonDetectorConfig;

typedef struct
{
    uint64_t submitted_frames;
    uint64_t busy_skips;
    uint64_t result_callbacks;
    uint64_t person_positive_frames;
    uint64_t released_frames;
    uint64_t failed_results;
} LivePersonDetectorStats;

int live_person_detector_create(
    LivePersonDetector **out,
    const LivePersonDetectorConfig *config);

/*
 * Copy one tightly packed NV12 frame into a free RockIVA DMA slot.
 *
 * Return values:
 *   0: submitted
 *   1: all DMA slots are busy; this AI sample was skipped
 *  -1: hard error
 */
int live_person_detector_submit(
    LivePersonDetector *detector,
    const uint8_t *nv12,
    size_t nv12_size,
    unsigned int camera_sequence,
    int64_t camera_timestamp_us);

int live_person_detector_wait_idle(
    LivePersonDetector *detector,
    int timeout_ms);

void live_person_detector_get_stats(
    LivePersonDetector *detector,
    LivePersonDetectorStats *stats);

void live_person_detector_destroy(
    LivePersonDetector *detector);

#ifdef __cplusplus
}
#endif

#endif /* LIVE_PERSON_DETECTOR_H */
