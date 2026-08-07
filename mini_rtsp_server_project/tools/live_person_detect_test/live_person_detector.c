/********************************************************************************
 * Copyright: (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *
 * Filename: live_person_detector.c
 * Description: Asynchronous RockIVA PERSON-only detector for RV1106.
 ********************************************************************************/

#include "live_person_detector.h"

#include "rockiva_common.h"
#include "rockiva_det_api.h"
#include "rockiva_image.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PFP_MODEL_FILENAME "object_detection_pfp.data"
#define ROCKIVA_CHANNEL_ID 0U
#define MAX_DMA_SLOTS      4U

typedef struct
{
    RockIvaImage image;
    int allocated;
    int in_use;
    uint32_t frame_id;
    unsigned int camera_sequence;
    int64_t camera_timestamp_us;
    int64_t submit_monotonic_ms;
} DetectorSlot;

struct LivePersonDetector
{
    RockIvaHandle handle;

    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int sync_initialized;

    unsigned int width;
    unsigned int height;
    size_t frame_size;
    uint8_t person_threshold;

    DetectorSlot slots[MAX_DMA_SLOTS];
    unsigned int slot_count;
    uint32_t next_frame_id;

    int global_initialized;
    int detect_initialized;
    int shutting_down;

    LivePersonDetectorStats stats;
};

static int64_t monotonic_ms(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return -1;

    return (int64_t)value.tv_sec * 1000LL +
           (int64_t)value.tv_nsec / 1000000LL;
}

static int normalized_to_pixel(int value, unsigned int extent)
{
    long scaled;

    if (value < 0)
        value = 0;
    else if (value > 9999)
        value = 9999;

    scaled = (long)value * (long)extent;
    return (int)(scaled / 10000L);
}

static void fill_full_frame_roi(RockIvaAreas *areas)
{
    RockIvaArea *area;

    memset(areas, 0, sizeof(*areas));
    areas->areaNum = 1U;

    area = &areas->areas[0];
    area->pointNum = 4U;

    area->points[0].x = 0;
    area->points[0].y = 0;
    area->points[1].x = 9999;
    area->points[1].y = 0;
    area->points[2].x = 9999;
    area->points[2].y = 9999;
    area->points[3].x = 0;
    area->points[3].y = 9999;
}

static int build_model_file_path(const char *model_dir,
                                 char *path,
                                 size_t path_size)
{
    int written;

    written = snprintf(path,
                       path_size,
                       "%s/%s",
                       model_dir,
                       PFP_MODEL_FILENAME);

    if (written < 0 || (size_t)written >= path_size)
    {
        fprintf(stderr, "[AI] model path is too long\n");
        return -1;
    }

    if (access(path, R_OK) != 0)
    {
        fprintf(stderr,
                "[AI] cannot read model file %s: %s\n",
                path,
                strerror(errno));
        return -1;
    }

    return 0;
}

static DetectorSlot *find_slot_by_frame_id(
    LivePersonDetector *detector,
    uint32_t frame_id)
{
    unsigned int i;

    for (i = 0U; i < detector->slot_count; ++i)
    {
        if (detector->slots[i].in_use &&
            detector->slots[i].frame_id == frame_id)
        {
            return &detector->slots[i];
        }
    }

    return NULL;
}

static int all_slots_idle(const LivePersonDetector *detector)
{
    unsigned int i;

    for (i = 0U; i < detector->slot_count; ++i)
    {
        if (detector->slots[i].in_use)
            return 0;
    }

    return 1;
}

static void detect_result_callback(
    const RockIvaDetectResult *result,
    const RockIvaExecuteStatus status,
    void *userdata)
{
    LivePersonDetector *detector =
        (LivePersonDetector *)userdata;
    DetectorSlot *slot = NULL;
    unsigned int camera_sequence = 0U;
    int64_t submit_ms = -1;
    int64_t callback_ms;
    int64_t latency_ms = -1;
    uint32_t person_count = 0U;
    uint32_t i;

    if (detector == NULL)
        return;

    callback_ms = monotonic_ms();

    pthread_mutex_lock(&detector->mutex);
    detector->stats.result_callbacks++;

    if (status != ROCKIVA_SUCCESS || result == NULL)
    {
        detector->stats.failed_results++;
        pthread_mutex_unlock(&detector->mutex);

        fprintf(stderr,
                "[AI] result failed: status=%d result=%p\n",
                (int)status,
                (const void *)result);
        return;
    }

    slot = find_slot_by_frame_id(detector, result->frameId);
    if (slot != NULL)
    {
        camera_sequence = slot->camera_sequence;
        submit_ms = slot->submit_monotonic_ms;
    }

    for (i = 0U; i < result->objNum; ++i)
    {
        if (result->objInfo[i].type ==
            ROCKIVA_OBJECT_TYPE_PERSON)
        {
            person_count++;
        }
    }

    if (person_count > 0U)
        detector->stats.person_positive_frames++;

    pthread_mutex_unlock(&detector->mutex);

    if (callback_ms >= 0 && submit_ms >= 0)
        latency_ms = callback_ms - submit_ms;

    printf("[AI] frame_id=%u camera_sequence=%u "
           "status=%d person_count=%u latency_ms=%lld\n",
           result->frameId,
           camera_sequence,
           (int)status,
           person_count,
           (long long)latency_ms);

    for (i = 0U; i < result->objNum; ++i)
    {
        const RockIvaObjectInfo *object =
            &result->objInfo[i];

        if (object->type == ROCKIVA_OBJECT_TYPE_PERSON)
        {
            int left = normalized_to_pixel(
                object->rect.topLeft.x,
                result->frame.info.width);
            int top = normalized_to_pixel(
                object->rect.topLeft.y,
                result->frame.info.height);
            int right = normalized_to_pixel(
                object->rect.bottomRight.x,
                result->frame.info.width);
            int bottom = normalized_to_pixel(
                object->rect.bottomRight.y,
                result->frame.info.height);

            printf("[AI]   PERSON obj_id=%u score=%u "
                   "box_norm=(%d,%d)-(%d,%d) "
                   "box_px=(%d,%d)-(%d,%d)\n",
                   object->objId,
                   object->score,
                   object->rect.topLeft.x,
                   object->rect.topLeft.y,
                   object->rect.bottomRight.x,
                   object->rect.bottomRight.y,
                   left,
                   top,
                   right,
                   bottom);
        }
    }
}

static void frame_release_callback(
    const RockIvaReleaseFrames *release_frames,
    void *userdata)
{
    LivePersonDetector *detector =
        (LivePersonDetector *)userdata;
    uint32_t i;

    if (detector == NULL || release_frames == NULL)
        return;

    pthread_mutex_lock(&detector->mutex);

    for (i = 0U; i < release_frames->count; ++i)
    {
        DetectorSlot *slot = find_slot_by_frame_id(
            detector,
            release_frames->frames[i].frameId);

        if (slot != NULL)
        {
            slot->in_use = 0;
            slot->frame_id = 0U;
            slot->camera_sequence = 0U;
            slot->camera_timestamp_us = 0;
            slot->submit_monotonic_ms = 0;
            detector->stats.released_frames++;
        }
        else
        {
            fprintf(stderr,
                    "[AI] release callback: unknown frame_id=%u\n",
                    release_frames->frames[i].frameId);
        }
    }

    pthread_cond_broadcast(&detector->condition);
    pthread_mutex_unlock(&detector->mutex);
}

static int allocate_dma_slots(LivePersonDetector *detector)
{
    unsigned int i;

    for (i = 0U; i < detector->slot_count; ++i)
    {
        DetectorSlot *slot = &detector->slots[i];
        RockIvaRetCode ret;

        memset(slot, 0, sizeof(*slot));

        slot->image.info.width =
            (uint16_t)detector->width;
        slot->image.info.height =
            (uint16_t)detector->height;
        slot->image.info.format =
            ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
        slot->image.info.transformMode =
            ROCKIVA_IMAGE_TRANSFORM_NONE;
        slot->image.size =
            (uint32_t)detector->frame_size;
        slot->image.dataFd = -1;

        ret = ROCKIVA_IMAGE_AllocMem(
            &slot->image,
            ROCKIVA_MEM_TYPE_DMA);

        if (ret != ROCKIVA_RET_SUCCESS)
        {
            fprintf(stderr,
                    "[AI] allocate DMA slot %u failed: ret=%d\n",
                    i,
                    (int)ret);
            return -1;
        }

        slot->allocated = 1;

        /* The allocator may reset metadata; restore it after allocation. */
        slot->image.frameId = 0U;
        slot->image.channelId = ROCKIVA_CHANNEL_ID;
        slot->image.info.width =
            (uint16_t)detector->width;
        slot->image.info.height =
            (uint16_t)detector->height;
        slot->image.info.format =
            ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
        slot->image.info.transformMode =
            ROCKIVA_IMAGE_TRANSFORM_NONE;
        slot->image.size =
            (uint32_t)detector->frame_size;
        slot->image.extData = slot;

        if (slot->image.dataAddr == NULL ||
            slot->image.dataFd < 0)
        {
            fprintf(stderr,
                    "[AI] unusable DMA slot %u: addr=%p fd=%d\n",
                    i,
                    (void *)slot->image.dataAddr,
                    slot->image.dataFd);
            return -1;
        }

        printf("[AI] DMA slot %u: addr=%p fd=%d size=%u\n",
               i,
               (void *)slot->image.dataAddr,
               slot->image.dataFd,
               slot->image.size);
    }

    return 0;
}

static void free_dma_slots(LivePersonDetector *detector)
{
    unsigned int i;

    for (i = 0U; i < detector->slot_count; ++i)
    {
        DetectorSlot *slot = &detector->slots[i];

        if (slot->allocated)
        {
            RockIvaRetCode ret =
                ROCKIVA_IMAGE_FreeMem(&slot->image);

            if (ret != ROCKIVA_RET_SUCCESS)
            {
                fprintf(stderr,
                        "[AI] free DMA slot %u failed: ret=%d\n",
                        i,
                        (int)ret);
            }

            slot->allocated = 0;
        }
    }
}

static void cleanup_detector(LivePersonDetector *detector)
{
    if (detector == NULL)
        return;

    if (detector->detect_initialized)
    {
        RockIvaRetCode ret =
            ROCKIVA_DETECT_Release(detector->handle);

        if (ret != ROCKIVA_RET_SUCCESS)
        {
            fprintf(stderr,
                    "[AI] ROCKIVA_DETECT_Release failed: ret=%d\n",
                    (int)ret);
        }

        detector->detect_initialized = 0;
    }

    if (detector->global_initialized)
    {
        RockIvaRetCode ret =
            ROCKIVA_Release(detector->handle);

        if (ret != ROCKIVA_RET_SUCCESS)
        {
            fprintf(stderr,
                    "[AI] ROCKIVA_Release failed: ret=%d\n",
                    (int)ret);
        }

        detector->global_initialized = 0;
        detector->handle = NULL;
    }

    free_dma_slots(detector);

    if (detector->sync_initialized)
    {
        pthread_cond_destroy(&detector->condition);
        pthread_mutex_destroy(&detector->mutex);
        detector->sync_initialized = 0;
    }
}

int live_person_detector_create(
    LivePersonDetector **out,
    const LivePersonDetectorConfig *config)
{
    LivePersonDetector *detector = NULL;
    RockIvaInitParam init_param;
    RockIvaDetTaskParams det_param;
    RockIvaRetCode ret;
    char model_file[PATH_MAX];
    char version[128];
    size_t pixels;

    if (out == NULL)
        return -1;

    *out = NULL;

    if (config == NULL ||
        config->model_dir == NULL ||
        config->model_dir[0] == '\0' ||
        config->width == 0U ||
        config->height == 0U ||
        (config->width & 1U) != 0U ||
        (config->height & 1U) != 0U ||
        config->width > UINT16_MAX ||
        config->height > UINT16_MAX ||
        config->buffer_count == 0U ||
        config->buffer_count > MAX_DMA_SLOTS)
    {
        fprintf(stderr, "[AI] invalid detector configuration\n");
        return -1;
    }

    if (build_model_file_path(config->model_dir,
                              model_file,
                              sizeof(model_file)) != 0)
    {
        return -1;
    }

    if ((size_t)config->width >
        SIZE_MAX / (size_t)config->height)
    {
        fprintf(stderr, "[AI] frame size overflow\n");
        return -1;
    }

    pixels = (size_t)config->width *
             (size_t)config->height;

    if (pixels > SIZE_MAX - pixels / 2U ||
        pixels + pixels / 2U > UINT32_MAX)
    {
        fprintf(stderr, "[AI] NV12 frame size overflow\n");
        return -1;
    }

    detector = calloc(1, sizeof(*detector));
    if (detector == NULL)
    {
        fprintf(stderr, "[AI] allocate detector failed\n");
        return -1;
    }

    detector->width = config->width;
    detector->height = config->height;
    detector->frame_size = pixels + pixels / 2U;
    detector->person_threshold =
        config->person_threshold;
    detector->slot_count = config->buffer_count;
    detector->next_frame_id = 1U;

    if (pthread_mutex_init(&detector->mutex, NULL) != 0)
    {
        fprintf(stderr, "[AI] pthread_mutex_init failed\n");
        free(detector);
        return -1;
    }

    if (pthread_cond_init(&detector->condition, NULL) != 0)
    {
        fprintf(stderr, "[AI] pthread_cond_init failed\n");
        pthread_mutex_destroy(&detector->mutex);
        free(detector);
        return -1;
    }

    detector->sync_initialized = 1;

    if (allocate_dma_slots(detector) != 0)
    {
        cleanup_detector(detector);
        free(detector);
        return -1;
    }

    memset(version, 0, sizeof(version));
    ret = ROCKIVA_GetVersion((uint32_t)sizeof(version), version);
    if (ret == ROCKIVA_RET_SUCCESS)
        printf("[AI] RockIVA version: %s\n", version);

    memset(&init_param, 0, sizeof(init_param));
    init_param.logLevel = ROCKIVA_LOG_WARN;
    init_param.channelId = ROCKIVA_CHANNEL_ID;
    init_param.imageInfo.width =
        (uint16_t)detector->width;
    init_param.imageInfo.height =
        (uint16_t)detector->height;
    init_param.imageInfo.format =
        ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
    init_param.imageInfo.transformMode =
        ROCKIVA_IMAGE_TRANSFORM_NONE;
    init_param.cameraType = ROCKIVA_CAMERA_TYPE_ONE;

    /* The installed model is PFP, while the result mask is PERSON only. */
    init_param.detModel = ROCKIVA_DET_MODEL_PFP;
    init_param.trackerVersion = 0U;
    init_param.newModelInstance = 0U;
    fill_full_frame_roi(&init_param.roiAreas);

    if (strlen(config->model_dir) >=
        sizeof(init_param.modelPath))
    {
        fprintf(stderr, "[AI] model directory path is too long\n");
        cleanup_detector(detector);
        free(detector);
        return -1;
    }

    strcpy(init_param.modelPath, config->model_dir);

    ret = ROCKIVA_Init(&detector->handle,
                       ROCKIVA_MODE_VIDEO,
                       &init_param,
                       detector);

    if (ret != ROCKIVA_RET_SUCCESS)
    {
        fprintf(stderr,
                "[AI] ROCKIVA_Init failed: ret=%d\n",
                (int)ret);
        cleanup_detector(detector);
        free(detector);
        return -1;
    }

    detector->global_initialized = 1;

    ret = ROCKIVA_SetFrameReleaseCallback(
        detector->handle,
        frame_release_callback);

    if (ret != ROCKIVA_RET_SUCCESS)
    {
        fprintf(stderr,
                "[AI] set frame release callback failed: ret=%d\n",
                (int)ret);
        cleanup_detector(detector);
        free(detector);
        return -1;
    }

    memset(&det_param, 0, sizeof(det_param));
    det_param.detObjectType =
        ROCKIVA_OBJECT_TYPE_BITMASK(
            ROCKIVA_OBJECT_TYPE_PERSON);
    fill_full_frame_roi(&det_param.roiAreas);
    det_param.scores[ROCKIVA_OBJECT_TYPE_PERSON] =
        detector->person_threshold;
    det_param.min_det_count = 0U;

    ret = ROCKIVA_DETECT_Init(
        detector->handle,
        &det_param,
        detect_result_callback);

    if (ret != ROCKIVA_RET_SUCCESS)
    {
        fprintf(stderr,
                "[AI] ROCKIVA_DETECT_Init failed: ret=%d\n",
                (int)ret);
        cleanup_detector(detector);
        free(detector);
        return -1;
    }

    detector->detect_initialized = 1;

    printf("[AI] live detector initialized\n");
    printf("[AI]   work mode      : VIDEO\n");
    printf("[AI]   model          : PFP\n");
    printf("[AI]   returned class : PERSON only\n");
    printf("[AI]   source image   : %ux%u NV12 DMA\n",
           detector->width,
           detector->height);
    printf("[AI]   threshold      : %u\n",
           detector->person_threshold);
    printf("[AI]   DMA slots      : %u\n",
           detector->slot_count);
    printf("[AI]   model file     : %s\n",
           model_file);

    *out = detector;
    return 0;
}

int live_person_detector_submit(
    LivePersonDetector *detector,
    const uint8_t *nv12,
    size_t nv12_size,
    unsigned int camera_sequence,
    int64_t camera_timestamp_us)
{
    DetectorSlot *slot = NULL;
    uint32_t frame_id;
    unsigned int i;
    RockIvaRetCode ret;
    int64_t submit_ms;

    if (detector == NULL ||
        nv12 == NULL ||
        nv12_size < detector->frame_size)
    {
        return -1;
    }

    submit_ms = monotonic_ms();
    if (submit_ms < 0)
        return -1;

    pthread_mutex_lock(&detector->mutex);

    if (detector->shutting_down)
    {
        pthread_mutex_unlock(&detector->mutex);
        return -1;
    }

    for (i = 0U; i < detector->slot_count; ++i)
    {
        if (!detector->slots[i].in_use)
        {
            slot = &detector->slots[i];
            break;
        }
    }

    if (slot == NULL)
    {
        detector->stats.busy_skips++;
        pthread_mutex_unlock(&detector->mutex);
        return 1;
    }

    frame_id = detector->next_frame_id++;
    if (detector->next_frame_id == 0U)
        detector->next_frame_id = 1U;

    slot->in_use = 1;
    slot->frame_id = frame_id;
    slot->camera_sequence = camera_sequence;
    slot->camera_timestamp_us = camera_timestamp_us;
    slot->submit_monotonic_ms = submit_ms;

    slot->image.frameId = frame_id;
    slot->image.channelId = ROCKIVA_CHANNEL_ID;
    slot->image.extData = slot;

    detector->stats.submitted_frames++;

    pthread_mutex_unlock(&detector->mutex);

    /* Copy before the caller returns the MMAP frame to V4L2. */
    memcpy(slot->image.dataAddr,
           nv12,
           detector->frame_size);

    ret = ROCKIVA_PushFrame(
        detector->handle,
        &slot->image,
        NULL);

    if (ret != ROCKIVA_RET_SUCCESS)
    {
        pthread_mutex_lock(&detector->mutex);
        slot->in_use = 0;
        slot->frame_id = 0U;
        detector->stats.submitted_frames--;
        detector->stats.failed_results++;
        pthread_cond_broadcast(&detector->condition);
        pthread_mutex_unlock(&detector->mutex);

        fprintf(stderr,
                "[AI] ROCKIVA_PushFrame failed: frame_id=%u ret=%d\n",
                frame_id,
                (int)ret);
        return -1;
    }

    return 0;
}

static int make_deadline(struct timespec *deadline,
                         int timeout_ms)
{
    long nanoseconds;

    if (deadline == NULL || timeout_ms <= 0)
        return -1;

    if (clock_gettime(CLOCK_REALTIME, deadline) != 0)
        return -1;

    deadline->tv_sec += timeout_ms / 1000;
    nanoseconds = deadline->tv_nsec +
                  (long)(timeout_ms % 1000) * 1000000L;
    deadline->tv_sec += nanoseconds / 1000000000L;
    deadline->tv_nsec = nanoseconds % 1000000000L;

    return 0;
}

int live_person_detector_wait_idle(
    LivePersonDetector *detector,
    int timeout_ms)
{
    RockIvaRetCode ret;
    struct timespec deadline;
    int wait_ret = 0;

    if (detector == NULL || timeout_ms <= 0)
        return -1;

    ret = ROCKIVA_WaitFinish(
        detector->handle,
        -1L,
        timeout_ms);

    if (ret != ROCKIVA_RET_SUCCESS)
    {
        fprintf(stderr,
                "[AI] ROCKIVA_WaitFinish(all) failed: ret=%d\n",
                (int)ret);
        return -1;
    }

    if (make_deadline(&deadline, timeout_ms) != 0)
        return -1;

    pthread_mutex_lock(&detector->mutex);

    while (!all_slots_idle(detector) && wait_ret == 0)
    {
        wait_ret = pthread_cond_timedwait(
            &detector->condition,
            &detector->mutex,
            &deadline);
    }

    if (!all_slots_idle(detector))
    {
        pthread_mutex_unlock(&detector->mutex);

        if (wait_ret == ETIMEDOUT)
            fprintf(stderr,
                    "[AI] timeout waiting for DMA slots to be released\n");
        else
            fprintf(stderr,
                    "[AI] wait for DMA release failed: %s\n",
                    strerror(wait_ret));

        return -1;
    }

    pthread_mutex_unlock(&detector->mutex);
    return 0;
}

void live_person_detector_get_stats(
    LivePersonDetector *detector,
    LivePersonDetectorStats *stats)
{
    if (detector == NULL || stats == NULL)
        return;

    pthread_mutex_lock(&detector->mutex);
    *stats = detector->stats;
    pthread_mutex_unlock(&detector->mutex);
}

void live_person_detector_destroy(
    LivePersonDetector *detector)
{
    if (detector == NULL)
        return;

    pthread_mutex_lock(&detector->mutex);
    detector->shutting_down = 1;
    pthread_mutex_unlock(&detector->mutex);

    (void)live_person_detector_wait_idle(detector, 5000);

    cleanup_detector(detector);
    free(detector);
}
