/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_base64.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 05:12:59 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <string.h>

#include "base64.h"

int main()
{
	unsigned char data[] =
	{
		0x67,
		0x42,
		0x00,
		0x1f,
		0xe5
	};

	char output[128];
	int ret = base64_encode(data, sizeof(data), output, sizeof(output));

	if(ret < 0)
	{
		printf("base64 failed\n");
		return -1;
	}

	printf("input size=%ld\n", sizeof(data));
	printf("base64=%s\n", output);

	return 0;
}
