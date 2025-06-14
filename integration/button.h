#ifndef _BUTTON_H_
#define _BUTTON_H_
#define MESSAGE_ID 1818
#define INPUT_DEVICE_LIST "/dev/input/event"
#define PROBE_FILE "/proc/bus/input/devices"
#define HAVE_TO_FIND_1 "N: Name=\"ecube-button\"\n"
#define HAVE_TO_FIND_2 "H: Handlers=kbd event"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/msg.h>
#include <pthread.h>

typedef struct
{
    long int messageNum;
    int keyInput;
    int pressed;
} BUTTON_MSG_T;

int buttonInit(void);
int buttonExit(void);
void* buttonThFunc(void *);
int probeButtonPath(char *);



#endif
