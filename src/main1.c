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
#include <linux/fb.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <stdbool.h>
#include <linux/kd.h>

#define FBDEV_FILE "/dev/fb0"
#define ACCELPATH "/sys/class/misc/FreescaleAccelerometer/"
unsigned long *pfbmap;
int fbfd;
struct fb_var_screeninfo fbinfo;
struct fb_fix_screeninfo fbfix;
int fbWidth, fbHeight;
int line_length;
BUTTON_MSG_T receive;

int freIndex;

static int msgID;

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

int game_handle_logic(int accel_d) {//accel_d is accel_data[0]
    {	int distance = 0;
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
                distance = 0;
            }
            else if(LEFT_weak)
            {
                printf("car goes little bit left\n");
                distance = -5;
            }
            else if(LEFT_strong)
            {
                printf("car goes very left\n");
                distance = -10;
            }
            else if(RIGHT_weak)
            {
                printf("car goes little bit right\n");
                distance = 5;
            }
            else if(RIGHT_strong)
            {
                printf("car goes very right\n");
                distance = 10;
            }
            else
            {
                ;
            }
            return distance;
    }
}



void signal_handler(int nSingal)
{
	printf("Good-bye!\n");
	for(int t = 8; t >=1;t--) //system off sound
        {
		buzzerPlaySong(t);
                usleep(50000);
        }
	buzzerStopSong();
	printf("hello button exit\n");
        buttonExit();
        ledLibExit();

	exit(0);
}


void* msg_ingame(void *arg)
{
	while(1)
	{
	msgrcv(msgID, &receive.keyInput, sizeof(receive.keyInput),0,0);
	if(receive.keyInput == 5)
	{

		break;
	}
	}

}

void fb_close() {
    if (pfbmap)
        munmap(pfbmap, fbWidth * fbHeight * 4);
    if (fbfd >= 0)
        close(fbfd);
}

void fb_clear() {
    for (int y = 0; y < fbHeight; y++) {
        unsigned long *ptr = pfbmap + y * fbWidth;
        for (int x = 0; x < fbWidth; x++) {
            *ptr++ = 0x000000;
        }
    }
}

void draw_game_scene(int car_lane, int carY_offset) {
    fb_clear();

    // 도로 배경
    for (int y = 0; y < fbHeight; y++) {
        for (int x = 0; x < fbWidth; x++) {
            pfbmap[y * fbWidth + x] = 0x404040;
        }
    }

    int lane1 = fbHeight / 3;
    int lane2 = fbHeight * 2 / 3;
    for (int x = 0; x < fbWidth; x += 40) {
        for (int dx = 0; dx < 20 && x + dx < fbWidth; dx++) {
            if (lane1 >= 0 && lane1 < fbHeight)
                pfbmap[lane1 * fbWidth + (x + dx)] = 0xFFFFFF;
            if (lane2 >= 0 && lane2 < fbHeight)
                pfbmap[lane2 * fbWidth + (x + dx)] = 0xFFFFFF;
        }
    }

    int carW = fbHeight / 5;
    int carH = (carW * 2) * 0.8; // 자동차 길이를 조금 줄임
    int carX = fbWidth / 2 - carH / 2;
    int carY;
 
    switch (car_lane) {
        case 0:
            carY = fbHeight / 6 - carW / 2;
            break;
        case 1:
            carY = fbHeight / 2 - carW / 2;
            break;
        case 2:
            carY = fbHeight * 5 / 6 - carW / 2;
            break;
        default:
            carY = fbHeight / 2 - carW / 2;
    }

    carY += carY_offset;
    if (carY < 0) carY = 0;
    if (carY + carW > fbHeight) carY = fbHeight - carW;

    for (int y = carY; y < carY + carW; y++) {
        for (int x = carX; x < carX + carH; x++) {
            if (x >= 0 && x < fbWidth && y >= 0 && y < fbHeight)
                pfbmap[y * fbWidth + x] = 0x000000;
        }
    }
}

int load_bmp(const char *filename, unsigned char **rgbdata, int *width, int *height) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;

    fseek(fp, 18, SEEK_SET);
    fread(width, sizeof(int), 1, fp);
    fread(height, sizeof(int), 1, fp);
    fseek(fp, 54, SEEK_SET);

    int row_padded = (*width * 3 + 3) & (~3);
    *rgbdata = (unsigned char *)malloc(row_padded * (*height));
    fread(*rgbdata, sizeof(unsigned char), row_padded * (*height), fp);
    fclose(fp);
    return 0;
}

void draw_bmp_image(const char *filename) {
    unsigned char *bmpdata = NULL;
    int bmpW = 0, bmpH = 0;
    if (load_bmp(filename, &bmpdata, &bmpW, &bmpH) != 0) return;

    fb_clear();
    int row_padded = (bmpW * 3 + 3) & (~3);
    for (int y = 0; y < bmpH && y < fbHeight; y++) {
        for (int x = 0; x < bmpW && x < fbWidth; x++) {
            int idx = (bmpH - 1 - y) * row_padded + x * 3;
            unsigned char b = bmpdata[idx];
            unsigned char g = bmpdata[idx + 1];
            unsigned char r = bmpdata[idx + 2];
            pfbmap[y * fbWidth + x] = (r << 16) | (g << 8) | b;
        }
    }
    free(bmpdata);
}

void draw_menu_screen() {
    draw_bmp_image("menu.bmp");
}

void set_nonblocking() {
    struct termios ttystate;
    tcgetattr(STDIN_FILENO, &ttystate);
    ttystate.c_lflag &= ~ICANON;
    ttystate.c_lflag &= ~ECHO;
    ttystate.c_cc[VMIN] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

void game_loop() {
    set_nonblocking();
    int car_lane = 1;
    int carY_offset = 0;
    while (1) {
        accel();
        int distance = game_handle_logic(accel_data[0]); // 이동 방향 및 양 판단
        carY_offset += distance;
        draw_game_scene(car_lane, carY_offset);
        usleep(30000);

        char input = getchar();
        if (input == 'q') break;
    }
}

int main(void){
    int conFD = open("/dev/tty0", O_RDWR);
    ioctl(conFD, KDSETMODE, KD_GRAPHICS);
    close(conFD);

    fbfd = open(FBDEV_FILE, O_RDWR);
    if (fbfd < 0) {
        perror("open");
        return -1;
    }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &fbinfo) || ioctl(fbfd, FBIOGET_FSCREENINFO, &fbfix)) {
        perror("ioctl");
        fb_close();
        return -1;
    }

    if (fbinfo.bits_per_pixel != 32) {
        fb_close();
        return -1;
    }

    fbWidth = fbinfo.xres;
    fbHeight = fbinfo.yres;
    line_length = fbfix.line_length;

    pfbmap = (unsigned long *)mmap(0, fbWidth * fbHeight * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if ((unsigned long)pfbmap == (unsigned long)-1) {
        perror("mmap");
        fb_close();
        return -1;
    }

    draw_bmp_image("last.bmp");
    sleep(2);

    while (1) {
        draw_menu_screen();
        char input = getchar();
        getchar();

        switch (input) {
            case '1':
                draw_bmp_image("game_start.bmp");
                while (1) {
                    char subinput = getchar();
                    getchar();
                    if (subinput == 's') {
                        game_loop();
                        break;
                    }
                }
                break;
            case '2':
                draw_bmp_image("setting.bmp");
                sleep(2);
                break;
            case '3':
                draw_bmp_image("leaderboard.bmp");
                sleep(2);
                break;
            case '4':
                goto quit;
            default:
                break;
        }
    }

quit:
    fb_clear();
    fb_close();
    return 0;    	
	ledLibInit();
	ledStatus();
	findBuzzerSysPath();
	buzzerInit();
	signal(SIGINT,signal_handler);
	for(int p = 1; p <= 8;p++) //system start sound
        {
		ledOnOff(p-1,1); //turn on the led
                buzzerPlaySong(p);
                usleep(50000);
		ledStatus();
        }
	for(int t = 7; t >=0;t--) //system off sound
        {
           	 ledOnOff(t,0);
		 usleep(50000);
		 ledStatus();
	}
	buzzerStopSong();
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
				pthread_create(&accl,NULL,&msg_ingame,NULL);
			        while(1)
				 {
       				 accel();
        			 game_handle_logic(accel_data[0]);
				 if(receive.keyInput == 5)
				 {
					 pthread_detach(accl);
					 break;
				 }
        			 }
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


