#include <stdio.h>
<<<<<<< HEAD
<<<<<<< HEAD
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/msg.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#define INPUT_DEVICE_LIST "/dev/input/event"


#define PROBE_FILE "/proc/bus/input/devices"

#define HAVE_TO_FIND_1 "N: Name=\"ecube-button\"\n"
#define HAVE_TO_FIND_2 "H: Handlers=kbd event"

int probeButtonPath(char *newPath)
{
        int returnValue = 0;
        int number = 0;
        FILE *fp = fopen(PROBE_FILE, "rt");
        while(!feof(fp))
        {
                char tmpStr[200];
                fgets(tmpStr,200,fp);
                printf("%s",tmpStr);
                if(strcmp(tmpStr,HAVE_TO_FIND1) == 0)
                {
                        printf("YES! I found!: %s\r\n", tmpStr);
                        returnValue = 1; //found
                }
                if((returnValue == 1) && (strncasecmp(tmpStr,HAVE_TO_FIND_2, strlen(HAVE_TO_FIND_2))) == 0)
                {
                        printf("-->%s",tmpStr);
                        printf("\t%c\r\n",tmpStr[strlen(tmpStr)-3]) - '0';
                        break;
                }
        }
fclose(fp);
if(returnValue == 1)
{
        sprintf(newPath,"%s%d",INPUT_DEVICE_LIST,number);
        return returnValue;
}
}






int score1, score2, score3;


int main(int argc, char *argv[])
{
	int fp_button;
	int readSize, inputIndex;
	struct input_event stEvent;
	char inputDevPath[200] = {0,};
	if( probeButtonPath(inputDevPath) == 0)
	{
		printf("ERROR! File Not Found!\r\n");
		printf("Did you insmod?\r\n");
		return 0;
	}

	printf("inputDevPath: %s\r\n", O_RDONLY);
	fp = open(inputDevPath, O_RDONLY);
	while(1)
	{
		readSize = read(fp, &stEvent, sizeof(stEvent));
		if (readSize != sizeof(stEvent))
		{
			continue;
		}

		if(stEvent.type == EV_KEY)
		{
			printf("EV_KEY");
			switch(stEvent.code)
			{
				case KEY_VOLUMEUP: printf("Volume up key):");
						   //add volume up func(or different func)
						   break;
				case KEY_HOME: printf("Home key):");
					       //add home func (return to home)
					       break;
				case KEY_SEARCH: printf("Search key):");
						//add enter func(select)
						 break;
				case KEY_BACK: printf("Back key):");
					       //back func
					       break;
				case KEY_MENU: printf("Menu key):");
					       //open the menu
					       break;
				case KEY_VOLUMEDOWN: printf("Volume down key):");
						     //add volume down func (maybe different func..?)
						     break;


			}
			if(stEvent.value) printf("pressed\n");
			else printf("released\n");

		}
		else
		;
	}

	close(fp_button);




}		



