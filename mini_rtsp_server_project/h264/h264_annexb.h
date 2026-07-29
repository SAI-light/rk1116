#ifndef H264_ANNEXB_H
#define H264_ANNEXB_H

#include <stddef.h>
#include <stdint.h>

#define H264_ANNEXB_MAX_NALUS 64

typedef struct
{
    const uint8_t *data;
    size_t size;
    unsigned int type;
} H264NaluView;

int h264_annexb_collect(const uint8_t *data,
                        size_t size,
                        H264NaluView *nalus,
                        size_t capacity,
                        size_t *nalu_count);

int h264_annexb_contains_type(const uint8_t *data,
                              size_t size,
                              unsigned int nal_type);

int h264_annexb_copy_parameter_sets(const uint8_t *data,
                                    size_t size,
                                    uint8_t **sps,
                                    size_t *sps_size,
                                    uint8_t **pps,
                                    size_t *pps_size);

#endif
