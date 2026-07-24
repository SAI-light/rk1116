/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  test_create_only.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/24/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/24/2026 03:32:48 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include "rockchip/rk_mpi.h"


int main()
{
	MppCtx ctx = NULL;
	MppApi *mpi = NULL;

	MPP_RET ret;
	ret = mpp_create(&ctx, &mpi);

	printf("mpp_create ret=%d ctx=%p mpi=%p\n", ret, ctx, mpi);
	return 0;
}
