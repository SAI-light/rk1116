#include "h264_annexb.h"

#include <stdlib.h>
#include <string.h>

static size_t start_code_size(const uint8_t *data,
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

static int find_next_start_code(const uint8_t *data,
                                size_t size,
                                size_t from,
                                size_t *offset,
                                size_t *code_size)
{
    size_t i;

    if (data == NULL || offset == NULL || code_size == NULL)
        return 0;

    for (i = from; i < size; ++i)
    {
        size_t current_size = start_code_size(data, size, i);

        if (current_size != 0U)
        {
            *offset = i;
            *code_size = current_size;
            return 1;
        }
    }

    return 0;
}

int h264_annexb_collect(const uint8_t *data,
                        size_t size,
                        H264NaluView *nalus,
                        size_t capacity,
                        size_t *nalu_count)
{
    size_t current_start;
    size_t current_code_size;
    size_t count = 0U;

    if (nalu_count != NULL)
        *nalu_count = 0U;

    if (data == NULL || size == 0U ||
        nalus == NULL || capacity == 0U ||
        nalu_count == NULL)
    {
        return -1;
    }

    if (!find_next_start_code(data,
                              size,
                              0U,
                              &current_start,
                              &current_code_size))
    {
        return -1;
    }

    while (current_start < size)
    {
        size_t nalu_start = current_start + current_code_size;
        size_t next_start;
        size_t next_code_size;
        size_t nalu_end;

        if (nalu_start >= size)
            break;

        if (find_next_start_code(data,
                                 size,
                                 nalu_start,
                                 &next_start,
                                 &next_code_size))
        {
            nalu_end = next_start;
        }
        else
        {
            nalu_end = size;
            next_start = size;
            next_code_size = 0U;
        }

        /* Annex-B permits trailing_zero_8bits before the next start code. */
        while (nalu_end > nalu_start && data[nalu_end - 1U] == 0x00U)
            --nalu_end;

        if (nalu_end > nalu_start)
        {
            if (count >= capacity)
                return -1;

            nalus[count].data = data + nalu_start;
            nalus[count].size = nalu_end - nalu_start;
            nalus[count].type = data[nalu_start] & 0x1fU;
            ++count;
        }

        if (next_start >= size)
            break;

        current_start = next_start;
        current_code_size = next_code_size;
    }

    if (count == 0U)
        return -1;

    *nalu_count = count;
    return 0;
}

int h264_annexb_contains_type(const uint8_t *data,
                              size_t size,
                              unsigned int nal_type)
{
    H264NaluView nalus[H264_ANNEXB_MAX_NALUS];
    size_t count;
    size_t i;

    if (h264_annexb_collect(data,
                            size,
                            nalus,
                            H264_ANNEXB_MAX_NALUS,
                            &count) != 0)
    {
        return 0;
    }

    for (i = 0U; i < count; ++i)
    {
        if (nalus[i].type == nal_type)
            return 1;
    }

    return 0;
}

int h264_annexb_copy_parameter_sets(const uint8_t *data,
                                    size_t size,
                                    uint8_t **sps,
                                    size_t *sps_size,
                                    uint8_t **pps,
                                    size_t *pps_size)
{
    H264NaluView nalus[H264_ANNEXB_MAX_NALUS];
    size_t count;
    size_t i;
    uint8_t *new_sps = NULL;
    uint8_t *new_pps = NULL;
    size_t new_sps_size = 0U;
    size_t new_pps_size = 0U;

    if (sps == NULL || sps_size == NULL ||
        pps == NULL || pps_size == NULL)
    {
        return -1;
    }

    *sps = NULL;
    *sps_size = 0U;
    *pps = NULL;
    *pps_size = 0U;

    if (h264_annexb_collect(data,
                            size,
                            nalus,
                            H264_ANNEXB_MAX_NALUS,
                            &count) != 0)
    {
        return -1;
    }

    for (i = 0U; i < count; ++i)
    {
        if (nalus[i].type == 7U && new_sps == NULL)
        {
            new_sps = (uint8_t *)malloc(nalus[i].size);
            if (new_sps == NULL)
                goto FAIL;

            memcpy(new_sps, nalus[i].data, nalus[i].size);
            new_sps_size = nalus[i].size;
        }
        else if (nalus[i].type == 8U && new_pps == NULL)
        {
            new_pps = (uint8_t *)malloc(nalus[i].size);
            if (new_pps == NULL)
                goto FAIL;

            memcpy(new_pps, nalus[i].data, nalus[i].size);
            new_pps_size = nalus[i].size;
        }
    }

    if (new_sps == NULL || new_pps == NULL)
        goto FAIL;

    *sps = new_sps;
    *sps_size = new_sps_size;
    *pps = new_pps;
    *pps_size = new_pps_size;
    return 0;

FAIL:
    free(new_sps);
    free(new_pps);
    return -1;
}
