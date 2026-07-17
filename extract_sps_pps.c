/*********************************************************************************
 *      Copyright:  (C) 2026 Zuo Caimei<zuocaimei@gmail.com>
 *                  All rights reserved.
 *
 *       Filename:  extract_sps_pps.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(07/17/2026)
 *         Author:  Zuo Caimei <zuocaimei@gmail.com>
 *      ChangeLog:  1, Release initial version on "07/17/2026 04:34:57 PM"
 *                 
 ********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int find_start_code(uint8_t *buf, int len)
{
	for(int i=0;i<len-4;i++)
	{
		if(buf[i]==0x00 && buf[i+1]==0x00 && buf[i+2]==0x00 && buf[i+3]==0x01)
		{
			return i;
		}
	}
	return -1;
}

int main()
{
	FILE *fp=fopen("test.h264","rb");

	if(!fp)
	{
		perror("open");
		return -1;
	}

	fseek(fp,0,SEEK_END);

	int size=ftell(fp);

	rewind(fp);

	uint8_t *data=malloc(size);

	fread(data,1,size,fp);

	fclose(fp);

	int offset=0;

	while(offset<size)
	{
		int start=find_start_code(data+offset, size-offset);

		if(start<0)
			break;

		start+=offset;

		int next=find_start_code(data+start+4, size-start-4);

		int end;

		if(next>=0)
			end=start+4+next;
		else
			end=size;

		uint8_t type=data[start+4]&0x1f;

		if(type==7)
		{
			printf("SPS size=%d\n", end-start-4);
			for(int i=start+4;i<end;i++)
			{
				printf("%02X ", data[i]);
			}
			printf("\n\n");
		}

		if(type==8)
		{
			printf("PPS size=%d\n", end-start-4);
			for(int i=start+4;i<end;i++)
			{
				printf("%02X ",data[i]);
			}
			printf("\n\n");
		}

		offset=end;
	}
	free(data);
	return 0;
}
