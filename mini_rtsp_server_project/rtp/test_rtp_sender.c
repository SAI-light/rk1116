/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_rtp_sender.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/18/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/18/2026 09:40:32 PM"
 *                 
 ********************************************************************************/

#include "rtp_sender.h"
#include <stdio.h>

int main()
{
	RTPSender sender;
	rtp_sender_init(&sender, "127.0.0.1", 5000);

	uint8_t packet[] =
	{
		0x80,
		0x60,
		0x00,
		0x01,
		0x00,
		0x00,
		0x00,
		0x01
	};

	rtp_sender_send(&sender, packet, sizeof(packet));
	rtp_sender_close(&sender);

	return 0;
}
