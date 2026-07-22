/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  base64.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/22/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/22/2026 05:06:39 PM"
 *                 
 ********************************************************************************/

#include "base64.h"

static const char base64_table[] = 
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

int base64_encode(const unsigned char *input, int input_len, char *output, int output_size)
{
	int i = 0;
	int out = 0;

	while(i < input_len)
	{
		unsigned int value = 0;
		int remain = input_len - i;
		value |= input[i] << 16;

		if(remain > 1)
		{
			value |= input[i+1] << 8;
		}
		if(remain > 2)
		{
			value |= input[i+2];
		}

		if(out + 4 >= output_size)
		{
			return -1;
		}

		output[out++] = base64_table[(value >> 18) & 0x3f];
		output[out++] = base64_table[(value >> 12) & 0x3f];

		if(remain > 1)
		{
			output[out++] = base64_table[(value >> 6) & 0x3f];
		}
		else
		{
			output[out++] = '=';
		}

		if(remain > 2)
		{
			output[out++] = base64_table[value & 0x3f];
		}
		else
		{
			output[out++] = '=';
		}

		i += 3;
	}

	output[out] = '\0';
	return out;
}

