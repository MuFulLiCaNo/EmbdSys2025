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

int freIndex;

static int msgID;


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
        BUTTON_MSG_T receive;
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
                        case 3:
                                printf("search\n");
                                break;
                        case 4:
                                printf("back\n");
                                break;
                        case 5:
                                printf("menu\n");
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

