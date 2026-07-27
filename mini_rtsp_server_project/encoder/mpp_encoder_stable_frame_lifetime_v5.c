/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.c
 *    Description:  RV1106 NV12 -> H264 encoder.
 *                  Stable input-buffer path + malloc-backed output MppPacket.
 *
 *        Version:  1.0.4 (2026-07-27)
 *         Author:  Zuo Caimei
 *      ChangeLog:  1. Keep the stable mpp_create -> mpp_init path.
 *                  2. Keep the proven DRM input MppBuffer path.
 *                  3. Stop rejecting ret=MPP_OK when vendor group is NULL.
 *                  4. Replace zero-size pkt_buf packet with malloc-backed packet.
 *                  5. Obtain SPS/PPS with the same malloc-backed packet method.
 ********************************************************************************/

#include "mpp_encoder.h"
#include "rockchip/rk_venc_cfg.h"
#include "rockchip/rk_venc_rc.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPP_ENCODER_BUILD_TAG "stable-frame-lifetime-v5"

static int packet_range_is_valid(const void *base,
                                 size_t capacity,
                                 const void *pos,
                                 size_t length)
{
    uintptr_t base_addr;
    uintptr_t pos_addr;
    size_t offset;

    if (base == NULL || pos == NULL)
        return 0;

    base_addr = (uintptr_t)base;
    pos_addr = (uintptr_t)pos;

    if (pos_addr < base_addr)
        return 0;

    offset = (size_t)(pos_addr - base_addr);

    if (offset > capacity)
        return 0;

    if (length > capacity - offset)
        return 0;

    return 1;
}

static void print_first_bytes(const uint8_t *data, size_t length)
{
    size_t i;
    size_t show = length < 16U ? length : 16U;

    printf("H264 first bytes:");
    for (i = 0; i < show; ++i)
        printf(" %02x", data[i]);
    printf("\n");
}

static int mpp_encoder_get_header(MppEncoder *encoder)
{
    MPP_RET ret;
    MppPacket packet = NULL;
    uint8_t *packet_mem = NULL;
    void *packet_pos = NULL;
    size_t packet_len = 0;
    size_t packet_capacity;
    int result = -1;

    if (encoder == NULL ||
        encoder->ctx == NULL ||
        encoder->mpi == NULL ||
        encoder->frame_size == 0)
    {
        printf("mpp_encoder_get_header invalid parameter\n");
        return -1;
    }

    packet_capacity = encoder->frame_size;
    packet_mem = (uint8_t *)malloc(packet_capacity);
    if (packet_mem == NULL)
    {
        printf("malloc header packet memory failed, size=%zu\n",
               packet_capacity);
        return -1;
    }

    ret = mpp_packet_init(&packet, packet_mem, packet_capacity);
    printf("header packet init ret=%d packet=%p capacity=%zu\n",
           ret, packet, packet_capacity);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("mpp_packet_init(header) failed ret=%d\n", ret);
        goto CLEANUP;
    }

    /* Output packet length must be zero before encoder writes into it. */
    mpp_packet_set_length(packet, 0);

    ret = encoder->mpi->control(encoder->ctx,
                                MPP_ENC_GET_HDR_SYNC,
                                packet);
    printf("MPP_ENC_GET_HDR_SYNC ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        printf("MPP_ENC_GET_HDR_SYNC failed ret=%d\n", ret);
        goto CLEANUP;
    }

    packet_pos = mpp_packet_get_pos(packet);
    packet_len = mpp_packet_get_length(packet);

    printf("H264 header packet pos=%p len=%zu capacity=%zu\n",
           packet_pos, packet_len, packet_capacity);

    if (packet_pos == NULL || packet_len == 0)
    {
        printf("H264 header is empty\n");
        goto CLEANUP;
    }

    if (!packet_range_is_valid(packet_mem,
                               packet_capacity,
                               packet_pos,
                               packet_len))
    {
        printf("H264 header range invalid: base=%p pos=%p len=%zu capacity=%zu\n",
               packet_mem, packet_pos, packet_len, packet_capacity);
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

    free(packet_mem);
    return result;
}

int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
    MPP_RET ret;
    MppPollType timeout = MPP_POLL_BLOCK;
    MppEncCfg cfg = NULL;

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
    printf("before mpp_create\n");

    ret = mpp_create(&encoder->ctx, &encoder->mpi);
    printf("after mpp_create: ret=%d ctx=%p mpi=%p\n",
           ret, encoder->ctx, encoder->mpi);

    if (ret != MPP_OK || encoder->ctx == NULL || encoder->mpi == NULL)
    {
        printf("mpp_create failed ret=%d\n", ret);
        goto FAIL;
    }

    /* This order has already been verified on the current RV1106 image. */
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

    ret = encoder->mpi->control(encoder->ctx,
                                MPP_SET_OUTPUT_TIMEOUT,
                                &timeout);
    printf("MPP_SET_OUTPUT_TIMEOUT ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        printf("MPP_SET_OUTPUT_TIMEOUT failed ret=%d\n", ret);
        goto FAIL;
    }

    /*
     * Preserve the last path that reached real hardware output.
     * On this vendor build ret can be MPP_OK while group remains NULL;
     * mpp_buffer_get then uses the vendor/default allocation path.
     */
    ret = mpp_buffer_group_get_internal(&encoder->group,
                                        MPP_BUFFER_TYPE_DRM);
    printf("mpp_buffer_group_get_internal ret=%d group=%p type=%d\n",
           ret, encoder->group, MPP_BUFFER_TYPE_DRM);

    if (ret != MPP_OK)
    {
        printf("mpp_buffer_group_get_internal failed ret=%d\n", ret);
        goto FAIL;
    }

    ret = mpp_buffer_get(encoder->group,
                         &encoder->frm_buf,
                         encoder->frame_size);
    printf("mpp_buffer_get frm_buf ret=%d frm_buf=%p ptr=%p\n",
           ret,
           encoder->frm_buf,
           encoder->frm_buf ? mpp_buffer_get_ptr(encoder->frm_buf) : NULL);

    if (ret != MPP_OK || encoder->frm_buf == NULL)
    {
        printf("mpp_buffer_get input frame failed ret=%d\n", ret);
        goto FAIL;
    }

    /*
     * Deliberately do not allocate encoder->pkt_buf here.
     * This vendor path reports pkt_buf size as zero, which makes
     * mpp_packet_init_with_buffer create an unusable output packet.
     * Output is instead backed by normal malloc memory per packet.
     */
    encoder->pkt_buf = NULL;

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
    MppPacket submitted_packet = NULL;
    MppMeta meta = NULL;

    uint8_t *packet_mem = NULL;
    void *frm_ptr = NULL;
    void *packet_pos = NULL;
    size_t packet_capacity;
    size_t packet_len = 0;
    size_t prefix_len = 0;
    size_t total_len = 0;
    int result = -1;

    if (encoder == NULL ||
        encoder->ctx == NULL ||
        encoder->mpi == NULL ||
        encoder->frm_buf == NULL ||
        nv12 == NULL ||
        out == NULL)
    {
        printf("mpp_encoder_encode invalid parameter\n");
        return -1;
    }

    *out = NULL;

    printf("start encode NV12, input=%d expected=%zu\n",
           size, encoder->frame_size);

    if (size < 0 || (size_t)size < encoder->frame_size)
    {
        printf("NV12 size too small: input=%d expected=%zu\n",
               size, encoder->frame_size);
        return -1;
    }

    frm_ptr = mpp_buffer_get_ptr(encoder->frm_buf);
    if (frm_ptr == NULL)
    {
        printf("mpp_buffer_get_ptr input frame failed\n");
        return -1;
    }

    memcpy(frm_ptr, nv12, encoder->frame_size);

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

    packet_capacity = encoder->frame_size;
    packet_mem = (uint8_t *)malloc(packet_capacity);
    if (packet_mem == NULL)
    {
        printf("malloc output packet memory failed, size=%zu\n",
               packet_capacity);
        goto CLEANUP;
    }

    ret = mpp_packet_init(&packet, packet_mem, packet_capacity);
    printf("output packet init ret=%d packet=%p data=%p capacity=%zu\n",
           ret, packet, packet_mem, packet_capacity);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("mpp_packet_init(output) failed ret=%d\n", ret);
        goto CLEANUP;
    }

    mpp_packet_set_length(packet, 0);
    submitted_packet = packet;

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

    /*
     * IMPORTANT: keep frame and its meta alive until the returned packet has
     * been inspected and copied. On the current RV1106 vendor MPP build the
     * packet stored in KEY_OUTPUT_PACKET may be tied to the frame meta
     * lifetime. Deinitializing frame here was the only operation between a
     * valid packet and the later check_is_mpp_packet failure.
     */
    printf("packet checkpoint after put: pos=%p len=%zu\n",
           mpp_packet_get_pos(packet),
           mpp_packet_get_length(packet));

    ret = encoder->mpi->encode_get_packet(encoder->ctx, &packet);
    printf("encode_get_packet ret=%d submitted=%p returned=%p\n",
           ret, submitted_packet, packet);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("encode_get_packet failed ret=%d packet=%p\n",
               ret, packet);
        goto CLEANUP;
    }

    if (packet != submitted_packet)
    {
        printf("unexpected packet replacement: submitted=%p returned=%p\n",
               submitted_packet, packet);
        goto CLEANUP;
    }

    packet_pos = mpp_packet_get_pos(packet);
    packet_len = mpp_packet_get_length(packet);

    printf("encoded packet pos=%p len=%zu capacity=%zu\n",
           packet_pos, packet_len, packet_capacity);

    if (packet_pos == NULL || packet_len == 0)
    {
        printf("encoded packet is empty\n");
        goto CLEANUP;
    }

    if (!packet_range_is_valid(packet_mem,
                               packet_capacity,
                               packet_pos,
                               packet_len))
    {
        printf("encoded packet range invalid: base=%p pos=%p len=%zu capacity=%zu\n",
               packet_mem, packet_pos, packet_len, packet_capacity);
        goto CLEANUP;
    }

    print_first_bytes((const uint8_t *)packet_pos, packet_len);

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
    if (total_len > (size_t)INT_MAX)
    {
        printf("H264 output too large: %zu\n", total_len);
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

    memcpy(*out + prefix_len, packet_pos, packet_len);

    printf("encode H264 success, header=%zu frame=%zu total=%zu\n",
           prefix_len, packet_len, total_len);

    result = (int)total_len;

CLEANUP:
    /*
     * The packet was attached to frame meta through KEY_OUTPUT_PACKET.
     * Release the frame first and do not explicitly deinit the same packet in
     * this single-frame diagnostic build. The previous explicit deinit caused
     * an abort after the vendor library had already invalidated / released the
     * packet object. This avoids a double-release while we verify ownership.
     */
    if (frame != NULL)
        mpp_frame_deinit(&frame);

    packet = NULL;

    free(packet_mem);

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

    if (encoder->ctx != NULL)
    {
        mpp_destroy(encoder->ctx);
        encoder->ctx = NULL;
    }

    encoder->mpi = NULL;
    encoder->initialized = 0;
}
