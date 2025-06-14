#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define ACCELPATH "/sys/class/misc/FreescaleAccelerometer/"

// 가속도계 X, Y, Z 값을 저장할 전역 변수
int accel_data[3];

int main (void){
    int fd = 0;
    FILE *fp = NULL;

    fd = open (ACCELPATH "enable",O_WRONLY);
    if (fd < 0) {
        perror("Failed to open accelerometer enable file");
        return 1;
    }
    dprintf(fd, "1");
    close(fd);

    while(1) {
        fp = fopen (ACCELPATH "data", "rt");
        if (fp == NULL) {
            perror("Failed to open accelerometer data file");
            return 1;
        }
        fscanf(fp, "%d, %d, %d",&accel_data[0],&accel_data[1],&accel_data[2]);
        printf ("Accelerometer Data: X=%d, Y=%d, Z=%d\r\n",accel_data[0],accel_data[1],accel_data[2]);
        fclose(fp);
        usleep(100000);
    }

    return 0;
}