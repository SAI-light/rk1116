/********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  base64.h
 *    Description:  This file 
 *
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 04:58:19 PM"
 *                 
 ********************************************************************************/

#ifndef BASE64_H
#define BASE64_H

int base64_encode(
		const unsigned char *input,
		int input_len,
		char *output,
		int output_size
		);

#endif
