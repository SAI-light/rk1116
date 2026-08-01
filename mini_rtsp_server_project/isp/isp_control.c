/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  isp_control.c
 *    Description:  RKAIQ ISP control implementation
 *
 *        Version:  1.0.0 (08/01/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1. Release initial version on 08/01/2026
 *
 ********************************************************************************/

#include "isp_control.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rk_aiq_user_api2_sysctl.h>

#define ISP_MAX_CAMERA_COUNT 8

struct IspControl
{
        int camera_id;
        char *iq_dir;
        rk_aiq_working_mode_t working_mode;
        bool multi_cam;

        rk_aiq_sys_ctx_t *aiq_ctx;
        bool started;
};

/*
 * The current project uses one camera. RKAIQ callbacks do not carry a
 * user-defined pointer, so the callback state is kept at file scope.
 */
static atomic_uint g_sof_count = 0;
static atomic_bool g_aiq_should_quit = false;

static XCamReturn isp_sof_callback(rk_aiq_metas_t *meta)
{
        unsigned int count;

        count = atomic_fetch_add(&g_sof_count, 1) + 1;

        if (meta != NULL && count <= 2)
        {
                printf("[ISP] SOF frame_id=%u\n", meta->frame_id);
        }

        return XCAM_RETURN_NO_ERROR;
}

static XCamReturn isp_error_callback(rk_aiq_err_msg_t *msg)
{
        if (msg != NULL && msg->err_code == XCAM_RETURN_BYPASS)
        {
                atomic_store(&g_aiq_should_quit, true);
                fprintf(stderr,
                        "[ISP] RKAIQ reported XCAM_RETURN_BYPASS\n");
        }

        return XCAM_RETURN_NO_ERROR;
}

int isp_control_create(IspControl **out, const IspConfig *config)
{
        IspControl *isp;
        rk_aiq_static_info_t static_info;
        XCamReturn ret;
        char hdr_str[16];
        size_t iq_dir_len;

        if (out == NULL)
        {
                fprintf(stderr, "[ISP] output pointer is NULL\n");
                return -1;
        }

        *out = NULL;

        if (config == NULL ||
            config->iq_dir == NULL ||
            config->iq_dir[0] == '\0')
        {
                fprintf(stderr, "[ISP] invalid create configuration\n");
                return -1;
        }

        if (config->camera_id < 0 ||
            config->camera_id >= ISP_MAX_CAMERA_COUNT)
        {
                fprintf(stderr,
                        "[ISP] invalid camera id: %d\n",
                        config->camera_id);
                return -1;
        }

        isp = calloc(1, sizeof(*isp));
        if (isp == NULL)
        {
                perror("[ISP] calloc");
                return -1;
        }

        iq_dir_len = strlen(config->iq_dir);

        isp->iq_dir = malloc(iq_dir_len + 1);
        if (isp->iq_dir == NULL)
        {
                perror("[ISP] malloc iq_dir");
                free(isp);
                return -1;
        }

        memcpy(isp->iq_dir, config->iq_dir, iq_dir_len + 1);

        isp->camera_id = config->camera_id;
        isp->working_mode =
            (rk_aiq_working_mode_t)config->hdr_mode;
        isp->multi_cam = config->multi_cam;
        isp->aiq_ctx = NULL;
        isp->started = false;

        /*
         * The official RKAIQ example sets HDR_MODE before sysctl_init().
         */
        snprintf(hdr_str,
                 sizeof(hdr_str),
                 "%d",
                 config->hdr_mode);

        if (setenv("HDR_MODE", hdr_str, 1) != 0)
        {
                perror("[ISP] setenv HDR_MODE");
                free(isp->iq_dir);
                free(isp);
                return -1;
        }

        memset(&static_info, 0, sizeof(static_info));

        ret = rk_aiq_uapi2_sysctl_enumStaticMetas(
            isp->camera_id,
            &static_info);

        if (ret != XCAM_RETURN_NO_ERROR)
        {
                fprintf(stderr,
                        "[ISP] enumStaticMetas failed: %d\n",
                        (int)ret);
                free(isp->iq_dir);
                free(isp);
                return -1;
        }

        /*
         * sensor_name is an array in this SDK, so only test whether its first
         * byte is empty. Comparing it with NULL would trigger -Waddress.
         */
        if (static_info.sensor_info.sensor_name[0] == '\0')
        {
                fprintf(stderr,
                        "[ISP] Sensor name is empty for camera %d\n",
                        isp->camera_id);
                free(isp->iq_dir);
                free(isp);
                return -1;
        }

        printf("[ISP] camera_id=%d sensor=%s\n",
               isp->camera_id,
               static_info.sensor_info.sensor_name);
        printf("[ISP] IQ directory: %s\n", isp->iq_dir);
        printf("[ISP] HDR mode: %d\n", config->hdr_mode);

        atomic_store(&g_sof_count, 0);
        atomic_store(&g_aiq_should_quit, false);

        isp->aiq_ctx = rk_aiq_uapi2_sysctl_init(
            static_info.sensor_info.sensor_name,
            isp->iq_dir,
            isp_error_callback,
            isp_sof_callback);

        if (isp->aiq_ctx == NULL)
        {
                fprintf(stderr,
                        "[ISP] rk_aiq_uapi2_sysctl_init failed\n");
                free(isp->iq_dir);
                free(isp);
                return -1;
        }

        if (isp->multi_cam)
        {
                rk_aiq_uapi2_sysctl_setMulCamConc(
                    isp->aiq_ctx,
                    true);
        }

        printf("[ISP] RKAIQ initialized successfully\n");

        *out = isp;
        return 0;
}

int isp_control_start(IspControl *isp)
{
        XCamReturn ret;

        if (isp == NULL || isp->aiq_ctx == NULL)
        {
                fprintf(stderr, "[ISP] start called before create\n");
                return -1;
        }

        if (isp->started)
        {
                return 0;
        }

        ret = rk_aiq_uapi2_sysctl_prepare(
            isp->aiq_ctx,
            0,
            0,
            isp->working_mode);

        if (ret != XCAM_RETURN_NO_ERROR)
        {
                fprintf(stderr,
                        "[ISP] rk_aiq_uapi2_sysctl_prepare failed: %d\n",
                        (int)ret);
                return -1;
        }

        printf("[ISP] RKAIQ prepared successfully\n");

        ret = rk_aiq_uapi2_sysctl_start(isp->aiq_ctx);
        if (ret != XCAM_RETURN_NO_ERROR)
        {
                fprintf(stderr,
                        "[ISP] rk_aiq_uapi2_sysctl_start failed: %d\n",
                        (int)ret);
                return -1;
        }

        isp->started = true;

        printf("[ISP] RKAIQ started successfully\n");
        return 0;
}

void isp_control_stop(IspControl *isp)
{
        XCamReturn ret;

        if (isp == NULL ||
            isp->aiq_ctx == NULL ||
            !isp->started)
        {
                return;
        }

        printf("[ISP] stopping RKAIQ\n");

        ret = rk_aiq_uapi2_sysctl_stop(
            isp->aiq_ctx,
            false);

        if (ret != XCAM_RETURN_NO_ERROR)
        {
                fprintf(stderr,
                        "[ISP] rk_aiq_uapi2_sysctl_stop failed: %d\n",
                        (int)ret);
        }

        isp->started = false;
        printf("[ISP] RKAIQ stopped\n");
}

void isp_control_destroy(IspControl *isp)
{
        if (isp == NULL)
        {
                return;
        }

        isp_control_stop(isp);

        if (isp->aiq_ctx != NULL)
        {
                printf("[ISP] deinitializing RKAIQ\n");
                rk_aiq_uapi2_sysctl_deinit(isp->aiq_ctx);
                isp->aiq_ctx = NULL;
                printf("[ISP] RKAIQ deinitialized\n");
        }

        free(isp->iq_dir);
        isp->iq_dir = NULL;

        free(isp);
}

bool isp_control_should_quit(const IspControl *isp)
{
        if (isp == NULL)
        {
                return false;
        }

        return atomic_load(&g_aiq_should_quit);
}
