/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei <zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  isp_control.h
 *    Description:  RKAIQ ISP control interface
 *
 *        Version:  1.0.0 (08/01/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1. Release initial version on 08/01/2026
 *
 ********************************************************************************/

#ifndef ISP_CONTROL_H
#define ISP_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
        int camera_id;
        const char *iq_dir;
        int hdr_mode;
        bool multi_cam;
} IspConfig;

typedef struct IspControl IspControl;

int isp_control_create(IspControl **out, const IspConfig *config);
int isp_control_start(IspControl *isp);
void isp_control_stop(IspControl *isp);
void isp_control_destroy(IspControl *isp);
bool isp_control_should_quit(const IspControl *isp);

#ifdef __cplusplus
}
#endif

#endif /* ISP_CONTROL_H */
