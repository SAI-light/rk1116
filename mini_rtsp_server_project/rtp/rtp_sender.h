/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  rtp_sender.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 11:03:17 PM"
 *                 
 ********************************************************************************/

#ifndef RTP_SENDER_H

#define RTP_SENDER_H

#include <stdint.h>

/* 初始化UDP发送端 VLC客户端IP, VLC接收RTP端口; 返回:0 成功,-1失败 */
int rtp_sender_init(const char *ip, int port);

/* 发送RTP数据包 */
int rtp_sender_send(uint8_t *packet, int size);

/* 关闭socket */
void rtp_sender_close();

#endif

