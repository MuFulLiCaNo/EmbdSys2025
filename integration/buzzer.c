#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include "buzzer.h"

#define MAX_SCALE_STEP 8
#define BUZZER_BASE_SYS_PATH "/sys/bus/platform/devices/"
#define BUZZER_FILENAME "peribuzzer"
#define BUZZER_ENABLE_NAME "enable"
#define BUZZER_FREQUENCY_NAME "frequency"

static char gBuzzerBaseSysDir[128];

const int musicScale[MAX_SCALE_STEP] = {
    262, /* 도 */ 294, /* 레 */ 330, /* 미 */ 349, /* 파 */
    392, /* 솔 */ 440, /* 라 */ 494, /* 시 */ 523  /* 높은 도 */
};

int findBuzzerSysPath(void)
{
    DIR *dir_info = opendir(BUZZER_BASE_SYS_PATH);
    if (dir_info == NULL) {
        perror("opendir error");
        return 0;
    }

    struct dirent *dir_entry;
    while ((dir_entry = readdir(dir_info)) != NULL) 
    {
        if (strncasecmp(BUZZER_FILENAME, dir_entry->d_name, strlen(BUZZER_FILENAME)) == 0)
        {
            sprintf(gBuzzerBaseSysDir, "%s%s/", BUZZER_BASE_SYS_PATH, dir_entry->d_name);
            closedir(dir_info);
            return 1;
        }
    }
    closedir(dir_info);
    return 0; 
}

void buzzerEnable(int bEnable)
{
    char path[200];
    sprintf(path, "%s%s", gBuzzerBaseSysDir, BUZZER_ENABLE_NAME); 
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open buzzer enable file");
        return;
    }
    if (bEnable)
        write(fd, "1", 1);
    else
        write(fd, "0", 1); 
    close(fd);
}

void setFrequency(int frequency)
{
    char path[200];
    sprintf(path, "%s%s", gBuzzerBaseSysDir, BUZZER_FREQUENCY_NAME); 
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open buzzer frequency file");
        return;
    }
    dprintf(fd, "%d", frequency); // 
    close(fd);
}

int buzzerInit(void)
{
    if (findBuzzerSysPath() == 0)
    {
        printf("ERROR: Buzzer device not found in sysfs.\n");
        return 0;
    }
    printf("Buzzer path found: %s\n", gBuzzerBaseSysDir);
    return 1;
}

int buzzerPlaySong(int scale)
{
    if (scale < 1 || scale > MAX_SCALE_STEP)
    {
        printf("Invalid scale. It should be 1~%d\n", MAX_SCALE_STEP);
        return 0;
    }
    int frequency = musicScale[scale - 1];
    setFrequency(frequency);
    buzzerEnable(1);
    return 1;
}

int buzzerStopSong(void)
{
    buzzerEnable(0);
    return 1;
}

int buzzerExit(void)
{
    buzzerStopSong();
    return 1;
}
