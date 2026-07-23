/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_mpp_init.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/23/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/23/2026 11:08:53 PM"
 *                 
 ********************************************************************************/

#include "mpp_encoder.h"

#include <stdio.h>

int main()
{
	MPPEncoder encoder;
	if(mpp_encoder_init(&encoder, 2304, 1296)<0)
	{
		return -1;
	}
	printf("test success\n");
	mpp_encoder_close(&encoder);
	return 0;
}
