#ifndef ROCKIVA_DETECTOR_H
#define ROCKIVA_DETECTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frame_id;
    uint32_t object_count;
    uint32_t person_count;
    uint32_t pet_count;
} RockIvaDetectionSummary;

int rockiva_detect_nv12(const char *model_dir,
                        const char *nv12_path,
                        unsigned int width,
                        unsigned int height,
                        uint8_t person_threshold,
                        uint8_t pet_threshold,
                        int timeout_ms,
                        RockIvaDetectionSummary *summary);

#ifdef __cplusplus
}
#endif

#endif
