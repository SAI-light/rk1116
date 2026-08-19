#include "rockiva_detector.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rockiva_common.h"
#include "rockiva_det_api.h"
#include "rockiva_image.h"

#define PFP_MODEL_FILENAME "object_detection_pfp.data"
#define TEST_FRAME_ID 1U
#define TEST_CHANNEL_ID 0U

typedef struct {
    RockIvaHandle handle;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RockIvaDetectionSummary summary;
    RockIvaExecuteStatus execute_status;
    uint32_t expected_frame_id;
    int callback_received;
    int frame_released;
    int global_initialized;
    int detect_initialized;
    int sync_initialized;
} DetectorContext;

static const char *object_type_name(RockIvaObjectType type)
{
    switch (type) {
    case ROCKIVA_OBJECT_TYPE_PERSON:
        return "PERSON";
    case ROCKIVA_OBJECT_TYPE_PET:
        return "PET";
    case ROCKIVA_OBJECT_TYPE_FACE:
        return "FACE";
    case ROCKIVA_OBJECT_TYPE_HEAD:
        return "HEAD";
    case ROCKIVA_OBJECT_TYPE_VEHICLE:
        return "VEHICLE";
    case ROCKIVA_OBJECT_TYPE_NON_VEHICLE:
        return "NON_VEHICLE";
    case ROCKIVA_OBJECT_TYPE_NONE:
        return "NONE";
    default:
        return "OTHER";
    }
}

static int normalized_to_pixel(int value, unsigned int extent)
{
    long scaled;

    if (value < 0) {
        value = 0;
    } else if (value > 9999) {
        value = 9999;
    }

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

    written = snprintf(path, path_size, "%s/%s",
                       model_dir, PFP_MODEL_FILENAME);
    if (written < 0 || (size_t)written >= path_size) {
        fprintf(stderr, "Model path is too long\n");
        return -1;
    }

    if (access(path, R_OK) != 0) {
        fprintf(stderr, "Cannot read model file %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    return 0;
}

static int calculate_nv12_size(unsigned int width,
                               unsigned int height,
                               size_t *size_out)
{
    size_t pixels;

    if (size_out == NULL ||
        width == 0U || height == 0U ||
        (width & 1U) != 0U || (height & 1U) != 0U) {
        return -1;
    }

    if ((size_t)width > SIZE_MAX / (size_t)height) {
        return -1;
    }

    pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX - pixels / 2U) {
        return -1;
    }

    *size_out = pixels + pixels / 2U;
    return 0;
}

static int validate_file_size(const char *path, size_t expected_size)
{
    struct stat file_stat;

    if (stat(path, &file_stat) != 0) {
        fprintf(stderr, "stat(%s) failed: %s\n",
                path, strerror(errno));
        return -1;
    }

    if (file_stat.st_size < 0 ||
        (uintmax_t)file_stat.st_size != (uintmax_t)expected_size) {
        fprintf(stderr,
                "NV12 file size mismatch: actual=%jd expected=%zu\n",
                (intmax_t)file_stat.st_size,
                expected_size);
        return -1;
    }

    return 0;
}

static int read_file_into_buffer(const char *path,
                                 uint8_t *buffer,
                                 size_t size)
{
    FILE *file;
    size_t total = 0U;

    if (path == NULL || buffer == NULL || size == 0U) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "fopen(%s) failed: %s\n",
                path, strerror(errno));
        return -1;
    }

    while (total < size) {
        size_t bytes = fread(buffer + total, 1U, size - total, file);
        if (bytes == 0U) {
            if (ferror(file)) {
                fprintf(stderr, "fread(%s) failed\n", path);
            } else {
                fprintf(stderr, "Unexpected end of file: %s\n", path);
            }
            fclose(file);
            return -1;
        }
        total += bytes;
    }

    fclose(file);
    return 0;
}

static void detect_result_callback(const RockIvaDetectResult *result,
                                   const RockIvaExecuteStatus status,
                                   void *userdata)
{
    DetectorContext *context = (DetectorContext *)userdata;
    uint32_t i;

    if (context == NULL) {
        return;
    }

    pthread_mutex_lock(&context->mutex);
    memset(&context->summary, 0, sizeof(context->summary));
    context->execute_status = status;

    if (result != NULL) {
        context->summary.frame_id = result->frameId;
        context->summary.object_count = result->objNum;

        printf("\nRockIVA result: frame_id=%u status=%d object_count=%u\n",
               result->frameId, (int)status, result->objNum);

        for (i = 0U; i < result->objNum; ++i) {
            const RockIvaObjectInfo *obj = &result->objInfo[i];
            const unsigned int width = result->frame.info.width;
            const unsigned int height = result->frame.info.height;
            const int left =
                normalized_to_pixel(obj->rect.topLeft.x, width);
            const int top =
                normalized_to_pixel(obj->rect.topLeft.y, height);
            const int right =
                normalized_to_pixel(obj->rect.bottomRight.x, width);
            const int bottom =
                normalized_to_pixel(obj->rect.bottomRight.y, height);

            if (obj->type == ROCKIVA_OBJECT_TYPE_PERSON) {
                context->summary.person_count++;
            } else if (obj->type == ROCKIVA_OBJECT_TYPE_PET) {
                context->summary.pet_count++;
            }

            printf("  object[%u]: type=%s(%d) score=%u cls_score=%u "
                   "box_norm=(%d,%d)-(%d,%d) "
                   "box_px=(%d,%d)-(%d,%d)\n",
                   i,
                   object_type_name(obj->type),
                   (int)obj->type,
                   obj->score,
                   obj->clsScore,
                   obj->rect.topLeft.x,
                   obj->rect.topLeft.y,
                   obj->rect.bottomRight.x,
                   obj->rect.bottomRight.y,
                   left, top, right, bottom);
        }
    } else {
        fprintf(stderr,
                "RockIVA result callback returned NULL result, status=%d\n",
                (int)status);
    }

    context->callback_received = 1;
    pthread_cond_broadcast(&context->condition);
    pthread_mutex_unlock(&context->mutex);
}

static void frame_release_callback(const RockIvaReleaseFrames *release_frames,
                                   void *userdata)
{
    DetectorContext *context = (DetectorContext *)userdata;
    uint32_t i;

    if (context == NULL || release_frames == NULL) {
        return;
    }

    pthread_mutex_lock(&context->mutex);

    for (i = 0U; i < release_frames->count; ++i) {
        if (release_frames->frames[i].frameId ==
            context->expected_frame_id) {
            context->frame_released = 1;
            break;
        }
    }

    pthread_cond_broadcast(&context->condition);
    pthread_mutex_unlock(&context->mutex);
}

static int add_milliseconds(struct timespec *time_value, int timeout_ms)
{
    long nanoseconds;

    if (clock_gettime(CLOCK_REALTIME, time_value) != 0) {
        return -1;
    }

    time_value->tv_sec += timeout_ms / 1000;
    nanoseconds = time_value->tv_nsec +
                  (long)(timeout_ms % 1000) * 1000000L;
    time_value->tv_sec += nanoseconds / 1000000000L;
    time_value->tv_nsec = nanoseconds % 1000000000L;
    return 0;
}

static int wait_for_result(DetectorContext *context, int timeout_ms)
{
    struct timespec deadline;
    int wait_ret = 0;

    if (add_milliseconds(&deadline, timeout_ms) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&context->mutex);
    while (!context->callback_received && wait_ret == 0) {
        wait_ret = pthread_cond_timedwait(&context->condition,
                                          &context->mutex,
                                          &deadline);
    }

    if (!context->callback_received) {
        pthread_mutex_unlock(&context->mutex);
        if (wait_ret == ETIMEDOUT) {
            fprintf(stderr,
                    "Timed out waiting for RockIVA result callback\n");
        } else {
            fprintf(stderr,
                    "Failed while waiting for RockIVA result: %s\n",
                    strerror(wait_ret));
        }
        return -1;
    }

    pthread_mutex_unlock(&context->mutex);
    return 0;
}

static void detector_cleanup(DetectorContext *context)
{
    RockIvaRetCode iva_ret;

    if (context->detect_initialized) {
        iva_ret = ROCKIVA_DETECT_Release(context->handle);
        if (iva_ret != ROCKIVA_RET_SUCCESS) {
            fprintf(stderr,
                    "ROCKIVA_DETECT_Release failed: ret=%d\n",
                    (int)iva_ret);
        }
        context->detect_initialized = 0;
    }

    if (context->global_initialized) {
        iva_ret = ROCKIVA_Release(context->handle);
        if (iva_ret != ROCKIVA_RET_SUCCESS) {
            fprintf(stderr,
                    "ROCKIVA_Release failed: ret=%d\n",
                    (int)iva_ret);
        }
        context->global_initialized = 0;
        context->handle = NULL;
    }

    if (context->sync_initialized) {
        pthread_cond_destroy(&context->condition);
        pthread_mutex_destroy(&context->mutex);
        context->sync_initialized = 0;
    }
}

int rockiva_detect_nv12(const char *model_dir,
                        const char *nv12_path,
                        unsigned int width,
                        unsigned int height,
                        uint8_t person_threshold,
                        uint8_t pet_threshold,
                        int timeout_ms,
                        RockIvaDetectionSummary *summary)
{
    DetectorContext context;
    RockIvaImage image;
    RockIvaInitParam init_param;
    RockIvaDetTaskParams det_param;
    RockIvaRetCode iva_ret;
    char model_file[PATH_MAX];
    size_t nv12_size = 0U;
    char version[128];
    int dma_allocated = 0;
    int result = -1;

    if (model_dir == NULL || nv12_path == NULL ||
        summary == NULL || timeout_ms <= 0 ||
        width > UINT16_MAX || height > UINT16_MAX) {
        fprintf(stderr, "Invalid detection arguments\n");
        return -1;
    }

    memset(&context, 0, sizeof(context));
    memset(&image, 0, sizeof(image));
    memset(summary, 0, sizeof(*summary));

    if (build_model_file_path(model_dir,
                              model_file,
                              sizeof(model_file)) != 0) {
        return -1;
    }

    if (calculate_nv12_size(width, height, &nv12_size) != 0) {
        fprintf(stderr, "Invalid or overflowing NV12 dimensions\n");
        return -1;
    }

    if (validate_file_size(nv12_path, nv12_size) != 0) {
        return -1;
    }

    /*
     * Use RockIVA's DMA allocator instead of malloc-backed CPU memory.
     * The RV1106 RockIVA build omits the CPU/libyuv conversion path;
     * DMA-backed input allows the SDK to use its hardware/media path.
     */
    image.info.width = (uint16_t)width;
    image.info.height = (uint16_t)height;
    image.info.format = ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12;
    image.info.transformMode = ROCKIVA_IMAGE_TRANSFORM_NONE;
    image.size = (uint32_t)nv12_size;
    image.dataFd = -1;

    iva_ret = ROCKIVA_IMAGE_AllocMem(&image, ROCKIVA_MEM_TYPE_DMA);
    if (iva_ret != ROCKIVA_RET_SUCCESS) {
        fprintf(stderr,
                "ROCKIVA_IMAGE_AllocMem(DMA) failed: ret=%d\n",
                (int)iva_ret);
        return -1;
    }
    dma_allocated = 1;

    if (image.dataAddr == NULL || image.dataFd < 0) {
        fprintf(stderr,
                "DMA allocation returned unusable image: "
                "dataAddr=%p dataFd=%d size=%u\n",
                (void *)image.dataAddr,
                image.dataFd,
                image.size);
        goto cleanup;
    }

    /*
     * ROCKIVA_IMAGE_AllocMem() may reset metadata fields while creating
     * the DMA-backed image. Restore the frame/channel identifiers after
     * allocation so WaitFinish() and the release callback use frame 1.
     */
    image.frameId = TEST_FRAME_ID;
    image.channelId = TEST_CHANNEL_ID;

    if (read_file_into_buffer(nv12_path,
                              image.dataAddr,
                              nv12_size) != 0) {
        goto cleanup;
    }

    if (pthread_mutex_init(&context.mutex, NULL) != 0) {
        fprintf(stderr, "pthread_mutex_init failed\n");
        goto cleanup;
    }

    if (pthread_cond_init(&context.condition, NULL) != 0) {
        fprintf(stderr, "pthread_cond_init failed\n");
        pthread_mutex_destroy(&context.mutex);
        goto cleanup;
    }
    context.sync_initialized = 1;
    context.expected_frame_id = image.frameId;
    context.execute_status = ROCKIVA_UNKNOWN;

    memset(version, 0, sizeof(version));
    iva_ret = ROCKIVA_GetVersion((uint32_t)sizeof(version), version);
    if (iva_ret == ROCKIVA_RET_SUCCESS) {
        printf("RockIVA version: %s\n", version);
    } else {
        printf("RockIVA version query failed: ret=%d\n", (int)iva_ret);
    }

    printf("\nInput NV12 (DMA-backed)\n");
    printf("  path     : %s\n", nv12_path);
    printf("  width    : %u\n", width);
    printf("  height   : %u\n", height);
    printf("  format   : ROCKIVA_IMAGE_FORMAT_YUV420SP_NV12\n");
    printf("  size     : %zu\n", nv12_size);
    printf("  dataAddr : %p\n", (void *)image.dataAddr);
    printf("  dataFd   : %d\n", image.dataFd);

    memset(&init_param, 0, sizeof(init_param));
    init_param.logLevel = ROCKIVA_LOG_INFO;
    init_param.channelId = TEST_CHANNEL_ID;
    init_param.imageInfo = image.info;
    init_param.cameraType = ROCKIVA_CAMERA_TYPE_ONE;
    init_param.detModel = ROCKIVA_DET_MODEL_PFP;
    init_param.trackerVersion = 0U;
    init_param.newModelInstance = 0U;
    fill_full_frame_roi(&init_param.roiAreas);

    if (strlen(model_dir) >= sizeof(init_param.modelPath)) {
        fprintf(stderr, "Model directory is too long\n");
        goto cleanup;
    }
    strcpy(init_param.modelPath, model_dir);

    iva_ret = ROCKIVA_Init(&context.handle,
                           ROCKIVA_MODE_PICTURE,
                           &init_param,
                           &context);
    if (iva_ret != ROCKIVA_RET_SUCCESS) {
        fprintf(stderr, "ROCKIVA_Init failed: ret=%d\n", (int)iva_ret);
        goto cleanup;
    }
    context.global_initialized = 1;

    iva_ret = ROCKIVA_SetFrameReleaseCallback(
        context.handle, frame_release_callback);
    if (iva_ret != ROCKIVA_RET_SUCCESS) {
        fprintf(stderr,
                "ROCKIVA_SetFrameReleaseCallback failed: ret=%d\n",
                (int)iva_ret);
        goto cleanup;
    }

    memset(&det_param, 0, sizeof(det_param));
    det_param.detObjectType =
        ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PERSON) |
        ROCKIVA_OBJECT_TYPE_BITMASK(ROCKIVA_OBJECT_TYPE_PET);
    fill_full_frame_roi(&det_param.roiAreas);
    det_param.scores[ROCKIVA_OBJECT_TYPE_PERSON] = person_threshold;
    det_param.scores[ROCKIVA_OBJECT_TYPE_PET] = pet_threshold;
    det_param.min_det_count = 0U;

    iva_ret = ROCKIVA_DETECT_Init(context.handle,
                                  &det_param,
                                  detect_result_callback);
    if (iva_ret != ROCKIVA_RET_SUCCESS) {
        fprintf(stderr,
                "ROCKIVA_DETECT_Init failed: ret=%d\n",
                (int)iva_ret);
        goto cleanup;
    }
    context.detect_initialized = 1;

    printf("\nRockIVA detector initialized\n");
    printf("  model_dir        : %s\n", model_dir);
    printf("  model_file       : %s\n", model_file);
    printf("  model            : ROCKIVA_DET_MODEL_PFP\n");
    printf("  mode             : ROCKIVA_MODE_PICTURE\n");
    printf("  person_threshold : %u\n", person_threshold);
    printf("  pet_threshold    : %u\n", pet_threshold);

    iva_ret = ROCKIVA_PushFrame(context.handle, &image, NULL);
    if (iva_ret != ROCKIVA_RET_SUCCESS) {
        fprintf(stderr,
                "ROCKIVA_PushFrame failed: ret=%d\n",
                (int)iva_ret);
        goto cleanup;
    }

    iva_ret = ROCKIVA_WaitFinish(context.handle,
                                 (long)image.frameId,
                                 timeout_ms);
    if (iva_ret != ROCKIVA_RET_SUCCESS) {
        fprintf(stderr,
                "ROCKIVA_WaitFinish failed: ret=%d\n",
                (int)iva_ret);
        goto cleanup;
    }

    if (wait_for_result(&context, timeout_ms) != 0) {
        goto cleanup;
    }

    pthread_mutex_lock(&context.mutex);
    *summary = context.summary;
    if (context.execute_status != ROCKIVA_SUCCESS) {
        fprintf(stderr,
                "Detection callback status=%d\n",
                (int)context.execute_status);
        pthread_mutex_unlock(&context.mutex);
        goto cleanup;
    }
    pthread_mutex_unlock(&context.mutex);

    result = 0;

cleanup:
    detector_cleanup(&context);

    if (dma_allocated) {
        iva_ret = ROCKIVA_IMAGE_FreeMem(&image);
        if (iva_ret != ROCKIVA_RET_SUCCESS) {
            fprintf(stderr,
                    "ROCKIVA_IMAGE_FreeMem failed: ret=%d\n",
                    (int)iva_ret);
            result = -1;
        }
    }

    return result;
}
