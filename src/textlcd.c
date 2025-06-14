#include <stdio.h>
#include <stdib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "textlcd.h"
#define TEXTLCD_DRIVER_NAME "/dev/peritextlcd"

int text(const char *str1 , const char *str2)
{
	unsigned int linenum = 0;
	stTextLCD stlcd;
	int fd;
	int len;

	memeset(&stlcd,0,sizeof(stTextLCD));

	stlcd.cmdData = CMD_DATA_WRITE_LINE_1;
	len =strlen(str1);

	if ( len > COLUMN_NUM)
		memcpy(stlcd.TextData[stlcd.cmdData - 1], str1, COLUMN_NUM)
	else
		memcpy(stlcd.TextData[stlcd.cmdData - 1], str1, len);

	stlcd.cmd = CMD_WRITE_STRING;
	fd = open(TEXTLCD_DRIVER_NAME, O_RDWR);

	if ( fd<0) {
		perror("driver (//dev//peritxtlcd)) open error.\n");
		return -1;
	}

	write(fd,&stlcd,sizeof(stTextLCD));

	stlcd.cmdData = CMD_DATA_WRITE_LINE_2;
	len = strlen(str2);

	if(len > COLUMN_NUM)
		memcpy(stlcd.TextData[stlcd.cmdData - 1], str2, COLUMN_NUM)
	else
		memcpy(stlcd.TextData[stlcd.cmdData -1 ], str2, len);

	stlcd.cmd = CMD_WRITE_STRING;

	write(fd, &stlcd, sizeof(stTextLCD));

	close(fd);

	return 0;

}
