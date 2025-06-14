#ifndef _ACCEL_H_
#define _ACCEL_H_

#include <pthread.h>

extern int g_accel_data[3];

int accelInit(void);
void accelExit(void);

#endif
