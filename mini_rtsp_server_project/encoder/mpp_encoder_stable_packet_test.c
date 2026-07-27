/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.c
 *    Description:  RV1106 NV12 -> H264 single-frame validation encoder.
 *                  This file is based on the last proven version which reached:
 *                  encode_put_frame ret=0
 *                  encode_get_packet ret=0
 *                  encoded length=26372
 *
 *        Version:  1.0.2 (2026-07-26)
 *         Author:  Zuo Caimei
 *      ChangeLog:  1. Keep the proven DRM + frm_buf + pkt_buf + KEY_OUTPUT_PACKET path.
 *                  2. Remove the later ION/group-NULL regression.
 *                  3. Read encoded bytes from pkt_buf while retaining packet length.
 *                  4. Avoid the known invalid-packet deinit crash in this one-frame test.
 ********************************************************************************/

#include "mpp_encoder.h"
#include "rockchip/rk_venc_cfg.h"
#include "rockchip/rk_venc_rc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPP_ENCODER_BUILD_TAG "stable-packet-test-v2"

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

    /* Keep the order already proven to work on this board. */
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
     * IMPORTANT:
     * The last version that reached a real encoded length used DRM (type=3).
     * Do not replace this with ION and do not reject a NULL group when ret==0;
     * the vendor build previously continued successfully into mpp_buffer_get().
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

    ret = mpp_buffer_get(encoder->group,
                         &encoder->pkt_buf,
                         encoder->frame_size);
    printf("mpp_buffer_get pkt_buf ret=%d pkt_buf=%p ptr=%p\n",
           ret,
           encoder->pkt_buf,
           encoder->pkt_buf ? mpp_buffer_get_ptr(encoder->pkt_buf) : NULL);

    if (ret != MPP_OK || encoder->pkt_buf == NULL)
    {
        printf("mpp_buffer_get output packet failed ret=%d\n", ret);
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

    /*
     * The official one-frame output already contains SPS/PPS.
     * Header extraction is deliberately skipped in this controlled test so
     * that pkt_buf has exactly one owner/use before the real frame encode.
     */
    encoder->header_data = NULL;
    encoder->header_len = 0;
    encoder->header_pending = 0;

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
    MppPacket returned_packet = NULL;
    MppMeta meta = NULL;

    void *frm_ptr = NULL;
    void *pkt_ptr = NULL;
    size_t pkt_len = 0;
    int result = -1;
    int packet_submitted = 0;

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

    ret = mpp_packet_init_with_buffer(&packet, encoder->pkt_buf);
    printf("output packet init ret=%d packet=%p\n", ret, packet);

    if (ret != MPP_OK || packet == NULL)
    {
        printf("mpp_packet_init_with_buffer failed ret=%d\n", ret);
        goto CLEANUP;
    }

    mpp_packet_set_length(packet, 0);

    meta = mpp_frame_get_meta(frame);
    if (meta == NULL)
    {
        printf("mpp_frame_get_meta failed\n");
        goto CLEANUP;
    }

    ret = mpp_meta_set_packet(meta,
                              KEY_OUTPUT_PACKET,
                              packet);
    if (ret != MPP_OK)
    {
        printf("mpp_meta_set_packet KEY_OUTPUT_PACKET failed ret=%d\n", ret);
        goto CLEANUP;
    }

    packet_submitted = 1;

    ret = encoder->mpi->encode_put_frame(encoder->ctx, frame);
    printf("encode_put_frame ret=%d\n", ret);

    if (ret != MPP_OK)
        goto CLEANUP;

    mpp_frame_deinit(&frame);
    frame = NULL;

    /*
     * Preserve the application-created packet handle and use a separate
     * output variable for encode_get_packet. This avoids losing ownership
     * information if the vendor MPI rewrites the output pointer.
     */
    returned_packet = packet;

    ret = encoder->mpi->encode_get_packet(encoder->ctx,
                                          &returned_packet);
    printf("encode_get_packet ret=%d submitted=%p returned=%p\n",
           ret, packet, returned_packet);

    if (ret != MPP_OK || returned_packet == NULL)
    {
        printf("encode_get_packet failed ret=%d\n", ret);
        goto CLEANUP;
    }

    /*
     * This vendor runtime previously rejected mpp_packet_get_pos() after
     * encode_get_packet, but still updated the encoded length. The encoded
     * bytes are written into the pkt_buf supplied through KEY_OUTPUT_PACKET.
     */
    pkt_len = mpp_packet_get_length(returned_packet);
    pkt_ptr = mpp_buffer_get_ptr(encoder->pkt_buf);

    printf("encoded pkt_buf ptr=%p len=%zu\n",
           pkt_ptr, pkt_len);

    if (pkt_ptr == NULL || pkt_len == 0 || pkt_len > encoder->frame_size)
    {
        printf("encoded packet buffer is invalid: ptr=%p len=%zu capacity=%zu\n",
               pkt_ptr, pkt_len, encoder->frame_size);
        goto CLEANUP;
    }

    printf("H264 first bytes:");
    {
        const unsigned char *bytes = (const unsigned char *)pkt_ptr;
        size_t dump_len = pkt_len < 16 ? pkt_len : 16;
        size_t i;

        for (i = 0; i < dump_len; ++i)
            printf(" %02x", bytes[i]);
    }
    printf("\n");

    *out = malloc(pkt_len);
    if (*out == NULL)
    {
        printf("malloc H264 output failed, size=%zu\n", pkt_len);
        goto CLEANUP;
    }

    memcpy(*out, pkt_ptr, pkt_len);

    printf("encode H264 success, size=%zu\n", pkt_len);
    result = (int)pkt_len;

CLEANUP:
    if (frame != NULL)
    {
        mpp_frame_deinit(&frame);
        frame = NULL;
    }

    /*
     * Controlled one-frame validation:
     * - Before a packet is submitted, normal deinit is safe.
     * - After submission, the current vendor runtime previously aborted in
     *   mpp_packet_deinit because the returned wrapper failed its magic check.
     *   Therefore we intentionally skip deinit only for this single-frame
     *   diagnostic. The process exits immediately after the test, so the OS
     *   reclaims the tiny wrapper allocation.
     *
     * Once the static-library test confirms a valid packet wrapper, this block
     * will be changed back to the official mpp_packet_deinit(&packet) flow for
     * continuous camera encoding.
     */
    if (packet != NULL && !packet_submitted)
    {
        mpp_packet_deinit(&packet);
        packet = NULL;
    }

    if (result < 0 && out != NULL && *out != NULL)
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
