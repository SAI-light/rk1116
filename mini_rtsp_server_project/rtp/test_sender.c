/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_sender.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 11:27:40 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include "rtp_sender.h"

int main()
{
	if(rtp_sender_init("127.0.0.1", 5000)<0)
	{
		return -1;
	}

	uint8_t packet[] =
	{
		0x80,
		0xE0,
		0x00,
		0x01,
		0x65,
		0x88
	};

	rtp_sender_send(packet,sizeof(packet));

	rtp_sender_close();

	return 0;
}
