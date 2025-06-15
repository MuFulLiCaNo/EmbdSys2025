#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#define ACCELPATH "/sys/class/misc/FreescaleAccelerometer/"

int g_accel_data[3];
static pthread_t accel_thread_id;
static int end_thread_flag = 0;

void* accel_thread_func(void* arg)
{
    FILE *fp = NULL;
    while(!end_thread_flag)
    {
        fp = fopen(ACCELPATH "data", "rt");
        if (fp == NULL) {
            continue;
        }
        fscanf(fp, "%d, %d, %d", &g_accel_data[0], &g_accel_data[1], &g_accel_data[2]);
        fclose(fp);
    }
    return NULL;
}

int accelInit(void)
{
    int fd = 0;
    fd = open(ACCELPATH "enable", O_WRONLY);
    if (fd < 0) {
        perror("Failed to open accelerometer enable file");
        return 0;
    }
    dprintf(fd, "1");
    close(fd);

    if (pthread_create(&accel_thread_id, NULL, accel_thread_func, NULL) != 0)
    {
        perror("Accelerometer thread create error");
        return 0;
    }
    return 1;
}

void accelExit(void)
{
    end_thread_flag = 1; // 스레드 종료 플래그 설정
    pthread_join(accel_thread_id, NULL); // 스레드가 끝날 때까지 대기

    int fd = open(ACCELPATH "enable", O_WRONLY);
    if (fd >= 0) {
        dprintf(fd, "0");
        close(fd);
    }
}
