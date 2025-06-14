#include "button.h"

static int fd = 0;
static int msgID = 0;
char inputDevPath[200] = {0,};
static pthread_t buttonTh_id;


int buttonInit(void)
{
    if (probeButtonPath(inputDevPath) == 0)
    {
        printf ("ERROR! File Not Found!\r\n");
        printf ("Did you insmod?\r\n");
        return 0;
    }
    printf("inputDevPath:%s\r\n", inputDevPath);
    fd = open(inputDevPath, O_RDONLY);
    msgID = msgget(MESSAGE_ID, IPC_CREAT|0666);
    pthread_create(&buttonTh_id, NULL, buttonThFunc, NULL);
    return msgID;
}

int probeButtonPath(char *newPath)
{
    int returnValue = 0;
    int number = 0;
    FILE *fp = fopen(PROBE_FILE,"rt");
    while(!feof(fp)) 
    {
        char tmpStr[200];
        fgets(tmpStr,200,fp);
        if (strcmp(tmpStr,HAVE_TO_FIND_1) == 0)
        {
            printf("YES! I found!: %s\r\n", tmpStr);
            returnValue = 1;
        }
        if ((returnValue == 1) && (strncasecmp(tmpStr, HAVE_TO_FIND_2, strlen(HAVE_TO_FIND_2)) == 0))
        {
            printf ("-->%s",tmpStr);
            printf("\t%c\r\n",tmpStr[strlen(tmpStr)-3]);
            number = tmpStr[strlen(tmpStr)-3] - '0'; 
            break;
        }
    }
    fclose(fp);
    if (returnValue == 1)   sprintf (newPath,"%s%d",INPUT_DEVICE_LIST,number);
    return returnValue;
}

void* buttonThFunc(void *arg)
{
    BUTTON_MSG_T messageTxData;
    messageTxData.messageNum = 1;
    struct input_event stEvent;
    while (1)
    {
        read(fd, &stEvent, sizeof(stEvent));
        if ((stEvent.type == EV_KEY) && (stEvent.value == 1))
        {
            messageTxData.keyInput = stEvent.code;
            messageTxData.pressed = stEvent.value;
            msgsnd(msgID, &messageTxData, sizeof(int), 0);
        }
    }
}

int buttonExit(void)
{
    pthread_cancel(buttonTh_id);
    close(fd);
}

