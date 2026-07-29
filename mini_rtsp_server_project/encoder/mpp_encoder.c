/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  mpp_encoder.c
 *    Description:  RV1106 NV12 -> H264 encoder using the vendor packet ABI.
 *
 *                  Important RV1106 differences confirmed from the board's
 *                  official /oem/usr/bin/mpi_enc_test binary and matching
 *                  librockchip_mpp.so.1:
 *
 *                  1. Initialize the encoder with mpp_init_ext().
 *                  2. Do not create or attach a public MppPacket through
 *                     KEY_OUTPUT_PACKET.
 *                  3. encode_get_packet() fills struct venc_packet supplied by
 *                     the caller.
 *                  4. The encoded stream lives in an internal circular buffer.
 *                     Convert its mpi_buf_id to a dma-buf fd through
 *                     /dev/mpi/valloc, mmap it, and copy len bytes from offset.
 *                  5. Return the packet with encode_release_packet().
 *
 *        Version:  1.1.0 (2026-07-29)
 ********************************************************************************/

#include "mpp_encoder.h"

#include "rockchip/rk_mpi_mb_cmd.h"
#include "rockchip/rk_venc_cfg.h"
#include "rockchip/rk_venc_rc.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define MPP_ENCODER_BUILD_TAG      "vendor-packet-v9.1-ring-boundary"
#define VALLOC_DEVICE              "/dev/mpi/valloc"
#define DEFAULT_ENCODER_FPS        30
#define DEFAULT_ENCODER_GOP        30
#define DEFAULT_ENCODER_BIT_RATE   4000000

static size_t align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static size_t visible_nv12_size(int width, int height)
{
    return (size_t)width * (size_t)height * 3U / 2U;
}

/*
 * The official mpi_enc_test over-allocates its input buffers using 64-byte
 * aligned horizontal and vertical dimensions. The frame itself still carries
 * the real hor_stride / ver_stride configured below.
 */
static size_t allocated_nv12_size(int width, int height)
{
    size_t aligned_width = align_up_size((size_t)width, 64U);
    size_t aligned_height = align_up_size((size_t)height, 64U);

    return aligned_width * aligned_height * 3U / 2U;
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

static int set_encoder_config(MppEncoder *encoder)
{
    MPP_RET ret;
    MppEncCfg cfg = NULL;

    ret = mpp_enc_cfg_init(&cfg);
    if (ret != MPP_OK || cfg == NULL)
    {
        printf("mpp_enc_cfg_init failed ret=%d\n", ret);
        return -1;
    }

    ret = encoder->mpi->control(encoder->ctx, MPP_ENC_GET_CFG, cfg);
    if (ret != MPP_OK)
    {
        printf("MPP_ENC_GET_CFG failed ret=%d\n", ret);
        mpp_enc_cfg_deinit(cfg);
        return -1;
    }

    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);

    mpp_enc_cfg_set_s32(cfg, "prep:width", encoder->width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", encoder->height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", encoder->width);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", encoder->height);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", encoder->bit_rate);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", encoder->bit_rate_min);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", encoder->bit_rate_max);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", encoder->gop);

    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", encoder->fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", encoder->fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);

    ret = encoder->mpi->control(encoder->ctx, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);

    if (ret != MPP_OK)
    {
        printf("MPP_ENC_SET_CFG failed ret=%d\n", ret);
        return -1;
    }

    return 0;
}

/*
 * Copy one RV1106 vendor venc_packet from its internal circular stream buffer.
 * This follows the board's official mpi_enc_test binary:
 *
 *   packet.u64priv_data -> mpi_buf_id
 *   VALLOC_IOCTL_MB_GET_FD -> dma_buf_fd
 *   mmap(packet.buf_size)
 *   copy packet.len bytes from packet.offset, with ring wrap handling
 */
static int copy_vendor_packet(const struct venc_packet *packet,
                              uint8_t **out)
{
    struct valloc_mb mb;
    uint8_t *mapped = MAP_FAILED;
    uint8_t *result = NULL;
    size_t packet_len;
    size_t buffer_size;
    size_t offset;
    size_t first_part;
    int valloc_fd = -1;
    int dma_fd = -1;
    int ret = -1;

    if (packet == NULL || out == NULL)
        return -1;

    *out = NULL;

    packet_len = (size_t)packet->len;
    buffer_size = (size_t)packet->buf_size;
    offset = (size_t)packet->offset;

    printf("vendor packet: mpi_buf_id=%" PRIu32
           " len=%" PRIu32
           " buf_size=%" PRIu32
           " offset=%" PRIu32
           " data_num=%" PRIu32
           " flag=0x%08" PRIx32
           " pts=%" PRIu64 "\n",
           (RK_U32)packet->u64priv_data,
           packet->len,
           packet->buf_size,
           packet->offset,
           packet->data_num,
           packet->flag,
           (uint64_t)packet->u64pts);

    if (packet_len == 0U || buffer_size == 0U)
    {
        printf("vendor packet is empty\n");
        return -1;
    }

    /*
     * RV1106 uses offset == buf_size as a valid end-of-ring sentinel.
     * The official mpi_enc_test treats that case as a zero-byte tail followed
     * by packet_len bytes from the beginning of the circular buffer.
     *
     * Normalize the sentinel to offset 0 before mmap copying. Only an offset
     * strictly greater than buf_size is invalid.
     */
    if (offset > buffer_size || packet_len > buffer_size)
    {
        printf("invalid vendor packet range: offset=%zu len=%zu buf_size=%zu\n",
               offset, packet_len, buffer_size);
        return -1;
    }

    if (offset == buffer_size)
    {
        printf("vendor ring boundary: normalize offset=%zu to 0\n", offset);
        offset = 0U;
    }

    valloc_fd = open(VALLOC_DEVICE, O_RDWR);
    if (valloc_fd < 0)
    {
        printf("open %s failed: %s\n",
               VALLOC_DEVICE, strerror(errno));
        return -1;
    }

    memset(&mb, 0, sizeof(mb));
    mb.mpi_buf_id = (int)(RK_U32)packet->u64priv_data;
    mb.struct_size = (int)sizeof(mb);

    if (ioctl(valloc_fd, VALLOC_IOCTL_MB_GET_FD, &mb) < 0)
    {
        printf("VALLOC_IOCTL_MB_GET_FD failed: mpi_buf_id=%d error=%s\n",
               mb.mpi_buf_id, strerror(errno));
        goto CLEANUP;
    }

    dma_fd = mb.dma_buf_fd;
    if (dma_fd < 0)
    {
        printf("VALLOC_IOCTL_MB_GET_FD returned invalid dma fd=%d\n", dma_fd);
        goto CLEANUP;
    }

    printf("valloc packet buffer: mpi_buf_id=%d dma_buf_fd=%d size=%d\n",
           mb.mpi_buf_id, dma_fd, mb.size);

    mapped = (uint8_t *)mmap(NULL,
                             buffer_size,
                             PROT_READ,
                             MAP_SHARED,
                             dma_fd,
                             0);
    if (mapped == MAP_FAILED)
    {
        printf("mmap encoded stream failed: %s\n", strerror(errno));
        goto CLEANUP;
    }

    result = (uint8_t *)malloc(packet_len);
    if (result == NULL)
    {
        printf("malloc H264 output failed, size=%zu\n", packet_len);
        goto CLEANUP;
    }

    first_part = buffer_size - offset;
    if (first_part > packet_len)
        first_part = packet_len;

    memcpy(result, mapped + offset, first_part);

    if (packet_len > first_part)
        memcpy(result + first_part, mapped, packet_len - first_part);

    print_first_bytes(result, packet_len);

    *out = result;
    result = NULL;
    ret = (int)packet_len;

CLEANUP:
    free(result);

    if (mapped != MAP_FAILED)
        munmap(mapped, buffer_size);

    if (dma_fd >= 0)
        close(dma_fd);

    if (valloc_fd >= 0)
        close(valloc_fd);

    return ret;
}

int mpp_encoder_init(MppEncoder *encoder, int width, int height)
{
    return mpp_encoder_init_ex(encoder,
                               width,
                               height,
                               DEFAULT_ENCODER_FPS,
                               DEFAULT_ENCODER_GOP,
                               DEFAULT_ENCODER_BIT_RATE);
}

int mpp_encoder_init_ex(MppEncoder *encoder,
                        int width,
                        int height,
                        int fps,
                        int gop,
                        int bit_rate)
{
    MPP_RET ret;
    MppPollType timeout = MPP_POLL_BLOCK;
    vcodec_attr attr;
    size_t frame_buffer_size;
    int64_t bit_rate_min;
    int64_t bit_rate_max;

    if (encoder == NULL ||
        width <= 0 ||
        height <= 0 ||
        fps <= 0 ||
        gop <= 0 ||
        bit_rate <= 0)
    {
        printf("invalid encoder parameter: width=%d height=%d "
               "fps=%d gop=%d bitrate=%d\n",
               width, height, fps, gop, bit_rate);
        return -1;
    }

    bit_rate_min = (int64_t)bit_rate * 3LL / 4LL;
    bit_rate_max = (int64_t)bit_rate * 9LL / 8LL;

    if (bit_rate_min <= 0 || bit_rate_max > INT_MAX)
    {
        printf("encoder bitrate range overflow: target=%d\n", bit_rate);
        return -1;
    }

    memset(encoder, 0, sizeof(*encoder));

    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->gop = gop;
    encoder->bit_rate = bit_rate;
    encoder->bit_rate_min = (int)bit_rate_min;
    encoder->bit_rate_max = (int)bit_rate_max;
    encoder->frame_size = visible_nv12_size(width, height);
    encoder->frame_index = 0;

    frame_buffer_size = allocated_nv12_size(width, height);

    printf("MPP encoder build: %s\n", MPP_ENCODER_BUILD_TAG);
    printf("NV12 visible_size=%zu allocated_buffer_size=%zu\n",
           encoder->frame_size, frame_buffer_size);
    printf("encoder config: fps=%d gop=%d "
           "bps_target=%d bps_min=%d bps_max=%d\n",
           encoder->fps,
           encoder->gop,
           encoder->bit_rate,
           encoder->bit_rate_min,
           encoder->bit_rate_max);

    /*
     * The official mpi_enc_test does not create an explicit BufferGroup here.
     * It calls mpp_buffer_get() with a NULL group and lets the vendor runtime
     * use its default allocator.
     */
    encoder->group = NULL;

    ret = mpp_buffer_get(NULL, &encoder->frm_buf, frame_buffer_size);
    printf("pre-create frm_buf ret=%d buf=%p ptr=%p\n",
           ret,
           encoder->frm_buf,
           encoder->frm_buf ? mpp_buffer_get_ptr(encoder->frm_buf) : NULL);

    if (ret != MPP_OK || encoder->frm_buf == NULL)
    {
        printf("mpp_buffer_get frm_buf failed ret=%d\n", ret);
        goto FAIL;
    }

    /*
     * The vendor mpi_enc_test allocates a second frame-sized buffer before
     * mpp_create(). Keep the same allocation pattern. It is not used as a
     * public output MppPacket; encoded output comes from struct venc_packet.
     */
    ret = mpp_buffer_get(NULL, &encoder->pkt_buf, frame_buffer_size);
    printf("pre-create auxiliary buf ret=%d buf=%p ptr=%p\n",
           ret,
           encoder->pkt_buf,
           encoder->pkt_buf ? mpp_buffer_get_ptr(encoder->pkt_buf) : NULL);

    if (ret != MPP_OK || encoder->pkt_buf == NULL)
    {
        printf("mpp_buffer_get auxiliary buffer failed ret=%d\n", ret);
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

    printf("MppApi sizeof=%zu api_size=%u version=%u release_packet=%p\n",
           sizeof(MppApi),
           encoder->mpi->size,
           encoder->mpi->version,
           (void *)encoder->mpi->encode_release_packet);

    memset(&attr, 0, sizeof(attr));
    attr.type = MPP_CTX_ENC;
    attr.coding = MPP_VIDEO_CodingAVC;
    attr.chan_id = 0;

    ret = mpp_init_ext(encoder->ctx, &attr);
    printf("after mpp_init_ext: ret=%d\n", ret);

    if (ret != MPP_OK)
    {
        printf("mpp_init_ext failed ret=%d\n", ret);
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

    if (set_encoder_config(encoder) != 0)
        goto FAIL;

    /*
     * Do not call MPP_ENC_GET_HDR_SYNC here. On this vendor path the first
     * encoded venc_packet contains the H264 parameter sets and first frame,
     * exactly as observed from official_test.h264.
     */
    encoder->header_data = NULL;
    encoder->header_len = 0;
    encoder->header_pending = 0;

    printf("MPP encoder init success\n");
    return 0;

FAIL:
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
    struct venc_packet vendor_packet;
    MppPacket packet_handle;
    void *frame_ptr;
    size_t frame_buffer_size;
    int result = -1;
    int packet_acquired = 0;

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

    if (size < 0 || (size_t)size < encoder->frame_size)
    {
        printf("NV12 size too small: input=%d expected=%zu\n",
               size, encoder->frame_size);
        return -1;
    }

    frame_buffer_size = allocated_nv12_size(encoder->width,
                                            encoder->height);

    frame_ptr = mpp_buffer_get_ptr(encoder->frm_buf);
    if (frame_ptr == NULL)
    {
        printf("mpp_buffer_get_ptr(frm_buf) failed\n");
        return -1;
    }

    memset(frame_ptr, 0, frame_buffer_size);
    memcpy(frame_ptr, nv12, encoder->frame_size);

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
    mpp_frame_set_pts(frame, encoder->frame_index++);
    mpp_frame_set_eos(frame, 0);
    mpp_frame_set_buffer(frame, encoder->frm_buf);

    printf("start encode NV12 with RV1106 vendor packet ABI, frame=%" PRId64 "\n",
           encoder->frame_index - 1);

    ret = encoder->mpi->encode_put_frame(encoder->ctx, frame);
    printf("encode_put_frame ret=%d\n", ret);

    /* Official mpi_enc_test releases the input frame immediately after put. */
    mpp_frame_deinit(&frame);
    frame = NULL;

    if (ret != MPP_OK)
        goto CLEANUP;

    memset(&vendor_packet, 0, sizeof(vendor_packet));
    packet_handle = (MppPacket)&vendor_packet;

    ret = encoder->mpi->encode_get_packet(encoder->ctx, &packet_handle);
    printf("encode_get_packet ret=%d handle=%p expected=%p\n",
           ret, packet_handle, (void *)&vendor_packet);

    if (ret != MPP_OK)
        goto CLEANUP;

    packet_acquired = 1;

    if (packet_handle != (MppPacket)&vendor_packet)
    {
        printf("unexpected vendor packet handle replacement: %p\n",
               packet_handle);
        goto RELEASE_PACKET;
    }

    result = copy_vendor_packet(&vendor_packet, out);

RELEASE_PACKET:
    if (packet_acquired)
    {
        if (encoder->mpi->encode_release_packet == NULL)
        {
            printf("encode_release_packet API is NULL\n");
            result = -1;
        }
        else
        {
            ret = encoder->mpi->encode_release_packet(encoder->ctx,
                                                       &packet_handle);
            printf("encode_release_packet ret=%d\n", ret);
            if (ret != MPP_OK)
            {
                free(*out);
                *out = NULL;
                result = -1;
            }
        }
    }

CLEANUP:
    if (frame != NULL)
        mpp_frame_deinit(&frame);

    return result;
}

void mpp_encoder_close(MppEncoder *encoder)
{
    if (encoder == NULL)
        return;

    free(encoder->header_data);
    encoder->header_data = NULL;
    encoder->header_len = 0;
    encoder->header_pending = 0;

    if (encoder->ctx != NULL)
    {
        if (encoder->mpi != NULL && encoder->initialized)
            encoder->mpi->reset(encoder->ctx);

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

    /* No explicit group was created in this vendor path. */
    encoder->group = NULL;

    encoder->width = 0;
    encoder->height = 0;
    encoder->fps = 0;
    encoder->gop = 0;
    encoder->bit_rate = 0;
    encoder->bit_rate_min = 0;
    encoder->bit_rate_max = 0;
    encoder->frame_size = 0U;
    encoder->frame_index = 0;
}
