/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/19/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/19/2026 12:45:07 AM"
 *                 
 ********************************************************************************/

#include "rtsp_server.h"

int main()
{
	rtsp_server_start(8554);
	return 0;
}
