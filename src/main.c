#include "button.h"
#include "buzzer.h"
#include "led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/msg.h>
#include <pthread.h>
#include <linux/input.h>
#include <signal.h>


BUTTON_MSG_T receive;

int freIndex;

static int msgID;

#define ACCELPATH "/sys/class/misc/FreescaleAccelerometer/"

// 가속도계 X, Y, Z 값을 저장할 전역 변수
int accel_data[3];

pthread_t accl;

int accel(void){
    int fd = 0;
    FILE *fp = NULL;

    fd = open (ACCELPATH "enable",O_WRONLY);
    if (fd < 0) {
        perror("Failed to open accelerometer enable file");
        return -1;
    }
    dprintf(fd, "1");
    close(fd);
	
        fp = fopen (ACCELPATH "data", "rt");
        if (fp == NULL) {
            perror("Failed to open accelerometer data file");
            return -1;
        }
        fscanf(fp, "%d, %d, %d",&accel_data[0],&accel_data[1],&accel_data[2]);
        fclose(fp);
        usleep(100000);
	    //it was while end

    return 0;
}

int game_handle_logic(int accel_d) //accel_d is accel_data[0]
{
	printf("%d\n",accel_d);
	int STABLE,LEFT_weak, LEFT_strong, RIGHT_weak, RIGHT_strong;
	STABLE = (accel_d < 700) && (accel_d > -700); //go straight in stable
	LEFT_weak = (accel_d > 1000) && (accel_d < 3000);
	LEFT_strong = accel_d > 3000;
	RIGHT_weak = (accel_d < -1000) && (accel_d > -3000);
	RIGHT_strong = accel_d < -3000;
		if(STABLE)
		{
			printf("car goes straight\n");
		}
		else if(LEFT_weak)
		{
			printf("car goes little bit left\n");
		}
		else if(LEFT_strong)
		{
			printf("car goes very left\n");
		}
		else if(RIGHT_weak)
		{
			printf("car goes little bit right\n");
		}
		else if(RIGHT_strong)
		{
			printf("car goes very right\n");
		}
		else
		{
			;
		}
		return 0;
}



void signal_handler(int nSingal)
{
	printf("Good-bye!\n");
	for(int t = 8; t >=1;t--) //system off sound
        {
		buzzerPlaySong(t);
                usleep(400000);
        }
	buzzerStopSong();
	printf("hello button exit\n");
        buttonExit();
        ledLibExit();

	exit(0);
}

void* accel_do(void *arg)
{
	while(receive.keyInput != 5)
	{
	accel();
	game_handle_logic(accel_data[0]);
	}
	pthread_exit(0);
}

int main(void)
{	
	ledLibInit();
	ledStatus();
	findBuzzerSysPath();
	buzzerInit();
	signal(SIGINT,signal_handler);
	for(int p = 1; p <= 8;p++) //system start sound
        {
		ledOnOff(p-1,1); //turn on the led
                buzzerPlaySong(p);
                usleep(400000);
		ledStatus();
        }
	for(int t = 7; t >=0;t--) //system off sound
        {
                ledOnOff(t,0);
                usleep(100000);
        }
	buzzerStopSong();
	receive.messageNum = 0;
	receive.keyInput = 0;
	receive.pressed = 0;
        msgID = msgget(MESSAGE_ID, IPC_CREAT|0666);
        buttonInit();

        while(1)
        {
                printf("hello im in while loop\n");
                msgrcv(msgID, &receive.keyInput, sizeof(receive.keyInput),0,0);
                switch(receive.keyInput)
                {
                        case 1:
                                printf("vlm up\n");
                                break;
                        case 2:
                                printf("home\n");
                                break;
                        case 3: //search key
				pthread_create(&accl, NULL, &(accel_do), NULL);
		                pthread_join(accl,NULL);//when button 5(menu)is selected thread exit
				break;
                        case 4:
                                printf("back\n");
                                break;
                        case 5:
                                printf("menu\n"); //game pause
                                break;
                        case 6:
				printf("vlm dwn\n");
				break;
                }
                msgrcv(msgID, &receive.pressed, sizeof(receive.pressed),0,0);
                switch(receive.pressed)
                {
                        case 1:	
				buzzerPlaySong(1);
                                printf("btn pressed\n");
				usleep(100000);
				buzzerStopSong();
                                break;
                        case 2:
                                printf("btn unpressed..?\n");
                                break;

                }
        }
        return 0;
}

