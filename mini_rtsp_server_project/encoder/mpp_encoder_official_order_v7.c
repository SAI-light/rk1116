/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.c
 *    Description:  RV1106 NV12 -> H264 encoder.
 *                  Exact encoder simple-flow with buffers allocated before
 *                  mpp_create(), following Rockchip mpi_enc_test ordering.
 *
 *        Version:  1.0.7 (2026-07-27)
 *         Author:  Zuo Caimei
 *      ChangeLog:  1. Remove unsupported MppTask encoder path.
 *                  2. Allocate DRM|CACHABLE group, frm_buf and pkt_buf before
 *                     mpp_create(), matching mpi_enc_test ordering.
 *                  3. Keep the already verified mpp_create -> mpp_init ->
 *                     MPP_SET_OUTPUT_TIMEOUT order for this RV1106 image.
 *                  4. Restore encode_put_frame / encode_get_packet flow.
 *                  5. Avoid double-free when vendor returns an invalid packet.
 *                  6. Add a safe Annex-B scan fallback in pkt_buf for diagnosis.
 ********************************************************************************/

#include "mpp_encoder.h"
#include "rockchip/rk_venc_cfg.h"
#include "rockchip/rk_venc_rc.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPP_ENCODER_BUILD_TAG "stable-official-order-v7"

static void print_first_bytes(const uint8_t *data, size_t length)
{
    size_t i;
    size_t show = length < 16U ? length : 16U;

    printf("H264 first bytes:");
    for (i = 0; i < show; ++i)
        printf(" %02x", data[i]);
    printf("\n");
}

static size_t find_annexb_start(const uint8_t *data, size_t capacity)
{
    size_t i;

    if (data == NULL)
        return SIZE_MAX;

    for (i = 0; i + 3U < capacity; ++i)
    {
        if (data[i] == 0x00 &&
            data[i + 1U] == 0x00 &&
            data[i + 2U] == 0x00 &&
            data[i + 3U] == 0x01)
        {
            return i;
        }

        if (data[i] == 0x00 &&
            data[i + 1U] == 0x00 &&
            data[i + 2U] == 0x01)
        {
            return i;
        }
    }

    return SIZE_MAX;
}

static size_t find_first_nonzero(const uint8_t *data, size_t capacity)
{
    size_t i;

    if (data == NULL)
        return SIZE_MAX;

    for (i = 0; i < capacity; ++i)
    {
        if (data[i] != 0)
            return i;
    }

    return SIZE_MAX;
}

static int mpp_encoder_get_header(MppEncoder *encoder)
{
    MPP_RET ret;
    MppPacket packet = NULL;
    void *packet_pos = NULL;
    size_t packet_len = 0;
    int result = -1;

    if (encoder == NULL ||
        encoder->ctx == NULL ||
        encoder->mpi == NULL ||
        encoder->pkt_buf == NULL)
    {
        printf("mpp_encoder_get_header invalid parameter\n");
        return -1;
    }

    ret = mpp_packet_init_with_buffer(&packet, encoder->pkt_buf);
    printf("header packet init_with_buffer ret=%d packet=%p\n",
           ret, packet);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("mpp_packet_init_with_buffer(header) failed ret=%d\n", ret);
        return -1;
    }

    mpp_packet_set_length(packet, 0);

    ret = encoder->mpi->control(encoder->ctx,
                                MPP_ENC_GET_HDR_SYNC,
                                packet);
    printf("MPP_ENC_GET_HDR_SYNC ret=%d\n", ret);

    if (ret != MPP_OK)
        goto CLEANUP;

    packet_pos = mpp_packet_get_pos(packet);
    packet_len = mpp_packet_get_length(packet);

    printf("H264 header packet pos=%p len=%zu\n",
           packet_pos, packet_len);

    if (packet_pos == NULL || packet_len == 0)
    {
        printf("H264 header is empty\n");
        goto CLEANUP;
    }

    encoder->header_data = (uint8_t *)malloc(packet_len);
    if (encoder->header_data == NULL)
    {
        printf("malloc H264 header failed, size=%zu\n", packet_len);
        goto CLEANUP;
    }

    memcpy(encoder->header_data, packet_pos, packet_len);
    encoder->header_len = packet_len;
    encoder->header_pending = 1;

    printf("MPP H264 header size=%zu\n", packet_len);
    print_first_bytes(encoder->header_data, encoder->header_len);
    result = 0;

CLEANUP:
    if (packet != NULL)
        mpp_packet_deinit(&packet);

    return result;
}

int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
    MPP_RET ret;
    MppPollType timeout = MPP_POLL_BLOCK;
    MppEncCfg cfg = NULL;
    MppBufferType buffer_type;

    if (encoder == NULL || width <= 0 || height <= 0)
    {
        printf("invalid encoder parameter\n");
        return -1;
    }

    memset(encoder, 0, sizeof(*encoder));

    encoder->width = width;
    encoder->height = height;
    encoder->frame_size = (size_t)width * (size_t)height * 3U / 2U;
    encoder->frame_index = 0;

    printf("MPP encoder build: %s\n", MPP_ENCODER_BUILD_TAG);

    /*
     * Important: mpi_enc_test allocates its buffer group and both frame/packet
     * buffers before mpp_create(). Earlier tests moved only the group call but
     * stopped on group == NULL, so this complete ordering was not tested.
     */
    buffer_type = (MppBufferType)(MPP_BUFFER_TYPE_DRM |
                                  MPP_BUFFER_FLAGS_CACHABLE);

    ret = mpp_buffer_group_get_internal(&encoder->group, buffer_type);
    printf("pre-create buffer_group ret=%d group=%p type=%d\n",
           ret, encoder->group, buffer_type);

    /* Vendor build may return MPP_OK with group == NULL; mpp_buffer_get still
     * provides its default allocation path, so only the return code is fatal.
     */
    if (ret != MPP_OK)
    {
        printf("mpp_buffer_group_get_internal failed ret=%d\n", ret);
        goto FAIL;
    }

    ret = mpp_buffer_get(encoder->group,
                         &encoder->frm_buf,
                         encoder->frame_size);
    printf("pre-create frm_buf ret=%d buf=%p ptr=%p\n",
           ret,
           encoder->frm_buf,
           encoder->frm_buf ? mpp_buffer_get_ptr(encoder->frm_buf) : NULL);

    if (ret != MPP_OK || encoder->frm_buf == NULL)
    {
        printf("mpp_buffer_get frm_buf failed ret=%d\n", ret);
        goto FAIL;
    }

    ret = mpp_buffer_get(encoder->group,
                         &encoder->pkt_buf,
                         encoder->frame_size);
    printf("pre-create pkt_buf ret=%d buf=%p ptr=%p\n",
           ret,
           encoder->pkt_buf,
           encoder->pkt_buf ? mpp_buffer_get_ptr(encoder->pkt_buf) : NULL);

    if (ret != MPP_OK || encoder->pkt_buf == NULL)
    {
        printf("mpp_buffer_get pkt_buf failed ret=%d\n", ret);
        goto FAIL;
    }

    printf("before mpp_create\n");

    ret = mpp_create(&encoder->ctx, &encoder->mpi);
    printf("after mpp_create: ret=%d ctx=%p mpi=%p\n",
           ret, encoder->ctx, encoder->mpi);

    if (ret != MPP_OK || encoder->ctx == NULL || encoder->mpi == NULL)
    {
        printf("mpp_create failed ret=%d\n", ret);
        goto FAIL;
    }

    printf("MppApi sizeof=%zu api_size=%u version=%u\n",
           sizeof(MppApi),
           encoder->mpi->size,
           encoder->mpi->version);

    ret = mpp_init(encoder->ctx,
                   MPP_CTX_ENC,
                   MPP_VIDEO_CodingAVC);
    printf("after mpp_init: ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        printf("mpp_init failed ret=%d\n", ret);
        goto FAIL;
    }

    encoder->initialized = 1;

    /* Keep the order already verified not to crash on this board image. */
    ret = encoder->mpi->control(encoder->ctx,
                                MPP_SET_OUTPUT_TIMEOUT,
                                &timeout);
    printf("MPP_SET_OUTPUT_TIMEOUT ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        printf("MPP_SET_OUTPUT_TIMEOUT failed ret=%d\n", ret);
        goto FAIL;
    }

    ret = mpp_enc_cfg_init(&cfg);
    if (ret != MPP_OK || cfg == NULL)
    {
        printf("mpp_enc_cfg_init failed ret=%d\n", ret);
        goto FAIL;
    }

    ret = encoder->mpi->control(encoder->ctx,
                                MPP_ENC_GET_CFG,
                                cfg);
    if (ret != MPP_OK)
    {
        printf("MPP_ENC_GET_CFG failed ret=%d\n", ret);
        goto FAIL;
    }

    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);

    mpp_enc_cfg_set_s32(cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", width);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", height);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", 4000000);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", 4500000);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", 3000000);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", 30);

    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", 30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", 30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

    ret = encoder->mpi->control(encoder->ctx,
                                MPP_ENC_SET_CFG,
                                cfg);
    if (ret != MPP_OK)
    {
        printf("MPP_ENC_SET_CFG failed ret=%d\n", ret);
        goto FAIL;
    }

    mpp_enc_cfg_deinit(cfg);
    cfg = NULL;

    ret = mpp_encoder_get_header(encoder);
    printf("mpp_encoder_get_header ret=%d\n", ret);

    if (ret != 0)
    {
        printf("warning: H264 SPS/PPS header unavailable; continue frame test\n");
        free(encoder->header_data);
        encoder->header_data = NULL;
        encoder->header_len = 0;
        encoder->header_pending = 0;
    }

    printf("MPP encoder init success, frame_size=%zu\n",
           encoder->frame_size);
    return 0;

FAIL:
    if (cfg != NULL)
        mpp_enc_cfg_deinit(cfg);

    mpp_encoder_close(encoder);
    return -1;
}

int mpp_encoder_encode(MppEncoder *encoder,
                       const uint8_t *nv12,
                       int size,
                       uint8_t **out)
{
    MPP_RET ret;
    MppFrame frame = NULL;
    MppPacket packet = NULL;
    MppMeta meta = NULL;
    void *frm_ptr;
    uint8_t *pkt_base;
    const uint8_t *encoded_data = NULL;
    void *packet_pos = NULL;
    size_t packet_len = 0;
    size_t prefix_len = 0;
    size_t total_len = 0;
    size_t start_offset;
    size_t first_nonzero;
    int packet_can_deinit = 0;
    int result = -1;

    if (encoder == NULL ||
        encoder->ctx == NULL ||
        encoder->mpi == NULL ||
        encoder->frm_buf == NULL ||
        encoder->pkt_buf == NULL ||
        nv12 == NULL ||
        out == NULL)
    {
        printf("mpp_encoder_encode invalid parameter\n");
        return -1;
    }

    *out = NULL;

    printf("start encode NV12 with simple API, input=%d expected=%zu\n",
           size, encoder->frame_size);

    if (size < 0 || (size_t)size < encoder->frame_size)
    {
        printf("NV12 size too small: input=%d expected=%zu\n",
               size, encoder->frame_size);
        return -1;
    }

    frm_ptr = mpp_buffer_get_ptr(encoder->frm_buf);
    pkt_base = (uint8_t *)mpp_buffer_get_ptr(encoder->pkt_buf);

    if (frm_ptr == NULL || pkt_base == NULL)
    {
        printf("mpp_buffer_get_ptr failed frm=%p pkt=%p\n",
               frm_ptr, pkt_base);
        return -1;
    }

    memcpy(frm_ptr, nv12, encoder->frame_size);
    memset(pkt_base, 0, encoder->frame_size);

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK || frame == NULL)
    {
        printf("mpp_frame_init failed ret=%d\n", ret);
        goto CLEANUP;
    }

    mpp_frame_set_width(frame, encoder->width);
    mpp_frame_set_height(frame, encoder->height);
    mpp_frame_set_hor_stride(frame, encoder->width);
    mpp_frame_set_ver_stride(frame, encoder->height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buf_size(frame, encoder->frame_size);
    mpp_frame_set_pts(frame, encoder->frame_index++);
    mpp_frame_set_eos(frame, 0);
    mpp_frame_set_buffer(frame, encoder->frm_buf);

    ret = mpp_packet_init_with_buffer(&packet, encoder->pkt_buf);
    printf("output packet init_with_buffer ret=%d packet=%p\n",
           ret, packet);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("mpp_packet_init_with_buffer(output) failed ret=%d\n", ret);
        goto CLEANUP;
    }

    mpp_packet_set_length(packet, 0);
    packet_can_deinit = 1;

    meta = mpp_frame_get_meta(frame);
    if (meta == NULL)
    {
        printf("mpp_frame_get_meta failed\n");
        goto CLEANUP;
    }

    ret = mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);
    if (ret != MPP_OK)
    {
        printf("mpp_meta_set_packet KEY_OUTPUT_PACKET failed ret=%d\n", ret);
        goto CLEANUP;
    }

    ret = encoder->mpi->encode_put_frame(encoder->ctx, frame);
    printf("encode_put_frame ret=%d\n", ret);

    if (ret != MPP_OK)
        goto CLEANUP;

    /* Match mpi_enc_test ownership: creator releases input frame after put. */
    mpp_frame_deinit(&frame);
    frame = NULL;

    ret = encoder->mpi->encode_get_packet(encoder->ctx, &packet);
    printf("encode_get_packet ret=%d packet=%p\n", ret, packet);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("encode_get_packet failed ret=%d packet=%p\n", ret, packet);
        packet_can_deinit = 0;
        packet = NULL;
        goto CLEANUP;
    }

    packet_pos = mpp_packet_get_pos(packet);
    packet_len = mpp_packet_get_length(packet);

    printf("encoded packet pos=%p len=%zu\n",
           packet_pos, packet_len);

    if (packet_pos != NULL && packet_len > 0)
    {
        encoded_data = (const uint8_t *)packet_pos;
    }
    else
    {
        /*
         * The vendor wrapper previously returned a dead packet object while
         * reporting a non-zero length. Before changing APIs again, inspect the
         * application-owned pkt_buf itself for an Annex-B start code.
         */
        packet_can_deinit = 0;
        packet = NULL;

        start_offset = find_annexb_start(pkt_base, encoder->frame_size);
        first_nonzero = find_first_nonzero(pkt_base, encoder->frame_size);

        if (start_offset != SIZE_MAX)
        {
            printf("fallback Annex-B start found at pkt_buf offset=%zu\n",
                   start_offset);

            if (packet_len == 0 ||
                packet_len > encoder->frame_size - start_offset)
            {
                printf("fallback length invalid: len=%zu offset=%zu capacity=%zu\n",
                       packet_len, start_offset, encoder->frame_size);
                goto CLEANUP;
            }

            encoded_data = pkt_base + start_offset;
        }
        else
        {
            if (first_nonzero == SIZE_MAX)
                printf("pkt_buf remains all zero after encode_get_packet\n");
            else
                printf("pkt_buf first nonzero offset=%zu value=%02x, no Annex-B start found\n",
                       first_nonzero, pkt_base[first_nonzero]);

            goto CLEANUP;
        }
    }

    print_first_bytes(encoded_data, packet_len);

    if (encoder->header_pending &&
        encoder->header_data != NULL &&
        encoder->header_len > 0)
    {
        prefix_len = encoder->header_len;
    }

    if (packet_len > SIZE_MAX - prefix_len)
    {
        printf("H264 output length overflow\n");
        goto CLEANUP;
    }

    total_len = prefix_len + packet_len;
    if (total_len == 0 || total_len > (size_t)INT_MAX)
    {
        printf("H264 output size invalid: %zu\n", total_len);
        goto CLEANUP;
    }

    *out = (uint8_t *)malloc(total_len);
    if (*out == NULL)
    {
        printf("malloc H264 output failed, size=%zu\n", total_len);
        goto CLEANUP;
    }

    if (prefix_len > 0)
    {
        memcpy(*out, encoder->header_data, prefix_len);
        encoder->header_pending = 0;
    }

    memcpy(*out + prefix_len, encoded_data, packet_len);

    printf("encode H264 success, header=%zu frame=%zu total=%zu\n",
           prefix_len, packet_len, total_len);

    result = (int)total_len;

CLEANUP:
    if (frame != NULL)
        mpp_frame_deinit(&frame);

    if (packet_can_deinit && packet != NULL)
        mpp_packet_deinit(&packet);

    if (result < 0 && *out != NULL)
    {
        free(*out);
        *out = NULL;
    }

    return result;
}

void mpp_encoder_close(MppEncoder *encoder)
{
    if (encoder == NULL)
        return;

    if (encoder->initialized &&
        encoder->ctx != NULL &&
        encoder->mpi != NULL)
    {
        encoder->mpi->reset(encoder->ctx);
    }

    if (encoder->ctx != NULL)
    {
        mpp_destroy(encoder->ctx);
        encoder->ctx = NULL;
    }

    encoder->mpi = NULL;
    encoder->initialized = 0;

    if (encoder->frm_buf != NULL)
    {
        mpp_buffer_put(encoder->frm_buf);
        encoder->frm_buf = NULL;
    }

    if (encoder->pkt_buf != NULL)
    {
        mpp_buffer_put(encoder->pkt_buf);
        encoder->pkt_buf = NULL;
    }

    if (encoder->group != NULL)
    {
        mpp_buffer_group_put(encoder->group);
        encoder->group = NULL;
    }

    free(encoder->header_data);
    encoder->header_data = NULL;
    encoder->header_len = 0;
    encoder->header_pending = 0;
}
