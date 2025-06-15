/**********************************************************************
 * project_final.c — Frame‑buffer 기반 자동차 게임 (완전 통합판)
 *********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/input.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/msg.h>
#include <pthread.h>
#include <signal.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <stdbool.h>
#include <linux/kd.h>
#include <dirent.h>
#include <stdint.h>  
#include "textlcd.h"
#include "led.h"
#include "button.h"
#include "fnd.h"
#include "buzzer.h"
#include "accel.h"


#define STATE_IDLE          0
#define STATE_LED_COUNTDOWN 1
#define STATE_FND_COUNTUP   2
#define STATE_GAME_MENU     3
#define STATE_GAME_RUNNING  4
#define STATE_GAME_OVER     5
#define STATE_MINI_GAME     6
#define LEADERBOARD_FILE "leaderboard.csv"
#define MAX_RECORDS 100
#define FBDEV_FILE "/dev/fb0"


#define MAX_OBSTACLES   10
#define CAR_ORIG_WIDTH     120
#define CAR_ORIG_HEIGHT    200
#define CAR_SPRITE_WIDTH   CAR_ORIG_HEIGHT
#define CAR_SPRITE_HEIGHT  CAR_ORIG_WIDTH

volatile int user_life;

static int gameState = STATE_GAME_MENU;
static int isPaused  = 0;
static int msgID     = 0;
int minigame_over;
int minigame_win;
int led_stop;
int led_now;

static int fbfd = -1;
struct fb_var_screeninfo fbinfo;
struct fb_fix_screeninfo fbfix;
int fbWidth, fbHeight;
int line_length;                

static unsigned long *pfbmap      = NULL;
static unsigned long *pbackbuffer = NULL;
static size_t         backbufBytes= 0;

static uint32_t car_sprite[CAR_SPRITE_HEIGHT][CAR_SPRITE_WIDTH];
BUTTON_MSG_T msg;
static struct timeval startTime, pauseTime, lastSpawnTime;
static long   elapsed_ms         = 0;
static long   paused_duration_ms = 0;
int           carY_offset        = 0;

typedef struct {
    int x;          // 장애물의 x 좌표
    int lane;       // 0, 1, 2 중 하나
    bool active;    // 활성화 여부
} Obstacle;

Obstacle obstacles[MAX_OBSTACLES];

static inline uint32_t make_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return (0xFF << fbinfo.transp.offset) |    
           (r    << fbinfo.red.offset)    |
           (g    << fbinfo.green.offset)  |
           (b    << fbinfo.blue.offset);
}

static inline void put_px(void *buf, int y, int x, uint32_t px)
{
    uint32_t *line = (uint32_t *)((uint8_t *)buf + y * line_length);
    line[x] = px;
}

void mini_game(void)
{
    minigame_over = 0;
    minigame_win = 0;
    draw_bmp_image("minigame.bmp"); fb_update();
    while(!led_stop)
    {

    msgrcv(msgID,&msg,sizeof(msg.keyInput)+sizeof(msg.pressed),0,IPC_NOWAIT);
    
    for(int i = 0; i < 3; i++)
    {
        ledOnOff(i,rand()%2);   
        usleep(100000);    
    }
    
    if(msg.keyInput == KEY_VOLUMEUP)
    {
        led_stop = !led_stop;
        usleep(300000);
    if((led_now = ledStatus()) == 7) //if led is all 1(ON)
        {
        text("minigame success","CONGRATS!");
        
        buzzerPlaySong(1);//success music
        usleep(300000);
        buzzerStopSong();
        buzzerPlaySong(3);
        usleep(300000);
        buzzerStopSong();
        buzzerPlaySong(5);
        usleep(300000);
        buzzerStopSong();
        minigame_win = 1;
        minigame_over = 1;
        break;
        }
    }
    led_stop = 0;       

    }
    
}


void fb_close(void);
void fb_update(void);
void fb_clear(void);
void signal_handler(int);
void reset_all_systems(void);
void init_obstacles(void);
void init_car_sprite(void);
bool check_collision(int car_lane, int offset);
void draw_game_scene(int offset);
void draw_bmp_image(const char *file);
int  load_bmp(const char *file, unsigned char **rgb, int *w, int *h);
void spawn_obstacle(void);
void update_obstacles(void);
int  game_handle_logic(int accel_d);
void display_time_on_fnd(long ms);
long read_best_record(void);
void update_leaderboard(long);
int  compare_records(const void*, const void*);
char user_life_str[4];
int minigame_over;
int striked_obs;
void minigame(void);

int led_now;
int minigame_win;
int minigame_over;
int led_stop;
int run_once;
char life_display_str[17];

void init_obstacles(void)
{
    for (int i = 0; i < MAX_OBSTACLES; ++i) obstacles[i].active = false;
}

void spawn_obstacle(void)
{
    for (int i = 0; i < MAX_OBSTACLES; ++i)
    {
        if (!obstacles[i].active)
        {
            obstacles[i].x      = 0;
            obstacles[i].lane   = rand() % 3;
            obstacles[i].active = true;
            return;
        }
    }
}

void init_car_sprite(void) {
    memset(car_sprite, 0, sizeof(car_sprite));

    for (int y = 0; y < CAR_ORIG_HEIGHT; y++) {
        for (int x = 0; x < CAR_ORIG_WIDTH; x++) {
            int dx = x - CAR_ORIG_WIDTH / 2;
            int dy = (y < CAR_ORIG_HEIGHT / 2) ? (CAR_ORIG_HEIGHT / 4 - y) : (y - CAR_ORIG_HEIGHT * 3 / 4);
            int dist_sq = dx * dx + dy * dy;

            if ((y < 20 || y > CAR_ORIG_HEIGHT - 20) && (x < 20 || x > CAR_ORIG_WIDTH - 20) && dist_sq > 22 * 22)
                continue;

            uint32_t rgb_val = 0xFF2A2A;

            if ((x < 15 || x > 105) && (y > 24 && y < 168)) rgb_val = 0x1A1A1A;
            if (y > 50 && y < 142 && x > 30 && x < 90) rgb_val = 0x111111;
            if (y < 36 && x > 35 && x < 85) rgb_val = 0x66BFFF;
            if (y > 156 && x > 35 && x < 85) rgb_val = 0x66BFFF;
            if (y == 50 || y == 142) rgb_val = 0x222222;
            if (y < 10 && ((x > 18 && x < 38) || (x > 82 && x < 102))) rgb_val = 0xFFFF66;
            if (y > 182 && ((x > 18 && x < 38) || (x > 82 && x < 102))) rgb_val = 0xFF3333;
            if ((y > 80 && y < 85) && (x == 34 || x == 86)) rgb_val = 0xDDDDDD;
            if (x > 58 && x < 62) rgb_val = 0x000000;
           
            uint8_t r = (rgb_val >> 16) & 0xFF;
            uint8_t g = (rgb_val >> 8) & 0xFF;
            uint8_t b = rgb_val & 0xFF;

            // 90도 회전된 좌표 계산
            int rotated_x = CAR_ORIG_HEIGHT - 1 - y;
            int rotated_y = x;

            if (rotated_x < CAR_SPRITE_WIDTH && rotated_y < CAR_SPRITE_HEIGHT) {
                 car_sprite[rotated_y][rotated_x] = make_pixel(r, g, b);
            }
        }
    }
}

void update_obstacles(void)
{
    for (int i = 0; i < MAX_OBSTACLES; ++i)
    {
        if (obstacles[i].active)
        {
            obstacles[i].x += 30;
            if (obstacles[i].x > fbWidth) obstacles[i].active = false;
        }
    }
}

int game_handle_logic(int accel_d)
{
    int STABLE      = (accel_d < 700)  && (accel_d > -700);
    int LEFT_weak   = (accel_d > 1000) && (accel_d < 3000);
    int LEFT_strong = accel_d > 3000;
    int RIGHT_weak  = (accel_d < -1000)&& (accel_d > -3000);
    int RIGHT_strong= accel_d < -3000;

    if (STABLE)          return  0;
    else if (LEFT_weak)  return  10;
    else if (LEFT_strong)return 20;
    else if (RIGHT_weak) return -10;
    else if (RIGHT_strong)return -20;
    return 0;
}

void signal_handler(int sig)
{
    printf("\nGood‑bye!\n");
    for (int t = 8; t >= 1; --t) { buzzerPlaySong(t); usleep(50000); }
    buzzerStopSong();

    ledLibExit();
    fndExit();
    buzzerExit();
    accelExit();
    buttonExit();
    fb_close();
    int conFD = open("/dev/tty0", O_RDWR);
    ioctl(conFD, KDSETMODE, KD_TEXT);
    close(conFD);
    exit(0);
}

int fb_init(void)
{
    int conFD = open("/dev/tty0", O_RDWR);
    ioctl(conFD, KDSETMODE, KD_GRAPHICS);
    close(conFD);

    fbfd = open(FBDEV_FILE, O_RDWR);
    if (fbfd < 0) { perror("open fb"); return -1; }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &fbinfo) ||
        ioctl(fbfd, FBIOGET_FSCREENINFO, &fbfix))
    {
        perror("ioctl fb");
        fb_close(); return -1;
    }
    if (fbinfo.bits_per_pixel != 32)
    {
        fprintf(stderr,"Not 32‑bpp FB!\n");
        fb_close(); return -1;
    }

    fbWidth     = fbinfo.xres;
    fbHeight    = fbinfo.yres;
    line_length = fbfix.line_length;

    pfbmap = (unsigned long *)mmap(0, line_length * fbHeight,
                                   PROT_READ|PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (pfbmap == MAP_FAILED)
    {
        perror("mmap"); fb_close(); return -1;
    }

    backbufBytes = line_length * fbHeight;                
    pbackbuffer  = (unsigned long *)malloc(backbufBytes);  
    if (!pbackbuffer) { perror("malloc backbuffer"); fb_close(); return -1; }

    return 0;
}

void fb_close(void)
{
    if (pbackbuffer) free(pbackbuffer);
    if (pfbmap) munmap(pfbmap, backbufBytes);
    if (fbfd >= 0)  close(fbfd);
}

void fb_clear(void)
{
    memset(pbackbuffer, 0x00, backbufBytes);            
}

void fb_update(void)
{
    if (!pfbmap || !pbackbuffer) return;

    for (int y = 0; y < fbHeight; ++y)
        memcpy((uint8_t*)pfbmap      + y * line_length,
               (uint8_t*)pbackbuffer + y * line_length,
               line_length);
}

int load_bmp(const char *file, unsigned char **rgb, int *w, int *h)
{
    FILE *fp = fopen(file,"rb");
    if (!fp) return -1;

    fseek(fp, 18, SEEK_SET);
    fread(w, sizeof(int), 1, fp);
    fread(h, sizeof(int), 1, fp);
    fseek(fp, 54, SEEK_SET);

    int row_padded = (*w * 3 + 3) & (~3);
    *rgb = (unsigned char *)malloc(row_padded * (*h));
    if (!*rgb) { fclose(fp); return -1; }

    fread(*rgb, 1, row_padded * (*h), fp);
    fclose(fp);
    return 0;
}

void draw_bmp_image(const char *filename)
{
    unsigned char *bmp = NULL;
    int bw = 0, bh = 0;
    if (load_bmp(filename, &bmp, &bw, &bh) != 0)
    {
        printf("fail load %s\n", filename); return;
    }
    fb_clear();
    int row_padded = (bw * 3 + 3) & (~3);

    for (int y = 0; y < bh && y < fbHeight; ++y)
    {
        for (int x = 0; x < bw && x < fbWidth; ++x)
        {
            int idx = (bh - 1 - y) * row_padded + x * 3;
            uint8_t b = bmp[idx], g = bmp[idx+1], r = bmp[idx+2];
            put_px(pbackbuffer, y, x, make_pixel(r,g,b));
        }
    }
    free(bmp);
}

void draw_game_scene(int offset)
{
    fb_clear();

    // 배경과 차선 그리기
    uint32_t gray = make_pixel(0x40, 0x40, 0x40);
    uint32_t white = make_pixel(0xFF, 0xFF, 0xFF);
   
    for (int y = 0; y < fbHeight; ++y) {
        for (int x = 0; x < fbWidth; ++x) {
            put_px(pbackbuffer, y, x, gray);
        }
    }

    int lane1 = fbHeight / 3, lane2 = fbHeight * 2 / 3;
    for (int x = 0; x < fbWidth; x += 40) {
        for (int dx = 0; dx < 20 && x + dx < fbWidth; ++dx) {
            if (lane1 >= 0 && lane1 < fbHeight) put_px(pbackbuffer, lane1, x + dx, white);
            if (lane2 >= 0 && lane2 < fbHeight) put_px(pbackbuffer, lane2, x + dx, white);
        }
    }

    // 자동차 그리기 (최적화된 sprite 시스템 사용)
    int carX = fbWidth - (CAR_SPRITE_WIDTH + 20);
    int carY = fbHeight / 2 - CAR_SPRITE_HEIGHT / 2 + offset;
   
    if (carY < 0) carY = 0;
    if (carY + CAR_SPRITE_HEIGHT > fbHeight) carY = fbHeight - CAR_SPRITE_HEIGHT;

    for (int y = 0; y < CAR_SPRITE_HEIGHT; y++) {
        for (int x = 0; x < CAR_SPRITE_WIDTH; x++) {
            if (car_sprite[y][x] != 0) {
                int screenX = carX + x;
                int screenY = carY + y;
                if (screenX >= 0 && screenX < fbWidth && screenY >= 0 && screenY < fbHeight) {
                    *((uint32_t*)((uint8_t*)pbackbuffer + screenY * line_length) + screenX) = car_sprite[y][x];
                }
            }
        }
    }

    // 타이어 장애물 그리기 (크기 정확히 맞춤)
    uint32_t tireColor = 0x000000;    // 타이어 본체 (검정색)
    uint32_t treadColor = 0xFFFFFF;   // 휠자국 (흰색)
   
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            int obsX = obstacles[i].x;
            int obsLane = obstacles[i].lane;

            // 타이어 크기를 더 정확하게 설정 (충돌 감지와 동일하게)
            int obsW = CAR_ORIG_WIDTH * 0.5;  // 높이 (Y) - 0.6에서 0.5로 축소
            int obsH = CAR_ORIG_HEIGHT * 0.5; // 폭   (X) - 0.6에서 0.5로 축소
            int obsY;

            switch (obsLane) {
                case 0: obsY = fbHeight / 6 - obsW / 2; break;
                case 1: obsY = fbHeight / 2 - obsW / 2; break;
                case 2: obsY = fbHeight * 5 / 6 - obsW / 2; break;
                default: obsY = fbHeight / 2 - obsW / 2;
            }

            int tread1 = obsW / 4;
            int tread2 = obsW / 2;
            int tread3 = obsW * 3 / 4;

            for (int y = 0; y < obsW; y++) {
                for (int x = 0; x < obsH; x++) {
                    int drawX = obsX + x;
                    int drawY = obsY + y;
                    if (drawX < 0 || drawX >= fbWidth || drawY < 0 || drawY >= fbHeight)
                        continue;

                    uint32_t color;

                    // --- 휠자국: 세로 3줄, 굵게 ---
                    if ((y >= tread1 - 1 && y <= tread1 + 1) ||
                        (y >= tread2 - 1 && y <= tread2 + 1) ||
                        (y >= tread3 - 1 && y <= tread3 + 1)) {
                        color = treadColor;
                    } else {
                        // --- 그라데이션 적용: 중심 → 밝게, 가장자리 → 어둡게 ---
                        float distFromCenter = fabs((float)x - obsH / 2) / (obsH / 2); // 0 ~ 1
                        float brightness = 1.0f - distFromCenter * 0.6f;  // 중앙은 1.0, 가장자리는 0.4

                        int gray = (int)(brightness * 50); // 0 ~ 50 정도의 밝기 차이
                        color = (gray << 16) | (gray << 8) | gray; // RGB 동일한 회색 조합
                    }

                    *((uint32_t*)((uint8_t*)pbackbuffer + drawY * line_length) + drawX) = color;
                }
            }
        }
    }
}

bool check_collision(int car_lane, int carY_offset) {
    int carX = fbWidth - (CAR_SPRITE_WIDTH + 20);
    int carY = fbHeight / 2 - CAR_SPRITE_HEIGHT / 2 + carY_offset;
   
    if (carY < 0) carY = 0;
    if (carY + CAR_SPRITE_HEIGHT > fbHeight) carY = fbHeight - CAR_SPRITE_HEIGHT;
   

    int carMargin = 10;
    int carLeft = carX + carMargin;
    int carRight = carX + CAR_SPRITE_WIDTH - carMargin;
    int carTop = carY + carMargin;
    int carBottom = carY + CAR_SPRITE_HEIGHT - carMargin;
   
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            int obsX = obstacles[i].x;
       
            int obsW = CAR_ORIG_WIDTH * 0.6;  // 높이 (Y)
            int obsH = CAR_ORIG_HEIGHT * 0.6; // 폭   (X)
            int obsY;
           
            switch (obstacles[i].lane) {
                case 0: obsY = fbHeight / 6 - obsW / 2; break;
                case 1: obsY = fbHeight / 2 - obsW / 2; break;
                case 2: obsY = fbHeight * 5 / 6 - obsW / 2; break;
                default: obsY = fbHeight / 2 - obsW / 2;
            }
           
            // 타이어의 실제 충돌 영역 (여백 없음 - 정확한 충돌)
            int obsLeft = obsX;
            int obsRight = obsX + obsH;
            int obsTop = obsY;
            int obsBottom = obsY + obsW;
           
            // 충돌 감지 - 더 정확한 범위
            if (carLeft < obsRight && carRight > obsLeft &&
                carTop < obsBottom && carBottom > obsTop) {
                striked_obs = i;
                return true;
            }
        }
    }
    return false;
}

void display_time_on_fnd(long ms)
{
    int m  = (ms/60000)%60;
    int s  = (ms%60000)/1000;
    int cs = (ms%1000)/10;
    int num = m*10000 + s*100 + cs;
    fndDisp(num, 1<<3);
}

long read_best_record(void)
{
    FILE *fp = fopen(LEADERBOARD_FILE,"r");
    if (!fp) return -1;
    long best=-1; fscanf(fp,"%ld",&best); fclose(fp); return best;
}

int compare_records(const void *a,const void *b)
{
    long A=*(long*)a,B=*(long*)b;
    return (A<B)-(A>B);  
}

void update_leaderboard(long new_ms)
{
    long rec[MAX_RECORDS+1]; int cnt=0;
    FILE *fp=fopen(LEADERBOARD_FILE,"r");
    if(fp){while(fscanf(fp,"%ld",&rec[cnt])==1&&cnt<MAX_RECORDS)cnt++;fclose(fp);}
    rec[cnt++]=new_ms;
    qsort(rec,cnt,sizeof(long),compare_records);
    fp=fopen(LEADERBOARD_FILE,"w");
    if(!fp) return;
    for(int i=0;i<cnt&&i<10;i++) fprintf(fp,"%ld\n",rec[i]);
    fclose(fp);
}

void reset_all_systems(void)
{
    isPaused=0; elapsed_ms=0; paused_duration_ms=0; carY_offset=0; run_once; minigame_over=0;
    for(int i=0;i<8;i++) ledOnOff(i,0);
    fndDisp(0,0); init_obstacles();
    text("","");
}

int main(void)
{
    signal(SIGINT, signal_handler);
    srand(time(NULL));
    int car_lane = 1;

    if (ledLibInit()<0||fndInit()<0||buzzerInit()==0||accelInit()==0||fb_init()<0)
    { fprintf(stderr,"HW init fail\n"); return -1; }

    init_car_sprite();
    msgID = buttonInit();
    if (msgID<0){fprintf(stderr,"button init fail\n");return 1;}

    /* 부팅 효과 */
    for(int i=1;i<=8;i++){ledOnOff(i-1,1);buzzerPlaySong(i);usleep(50000);}
    for(int i=7;i>=0;i--){ledOnOff(i,0);usleep(50000);} buzzerStopSong();

    reset_all_systems();
    gameState = STATE_GAME_MENU;
    draw_bmp_image("Title.bmp"); fb_update();

    
    while (1)
    {  
        if (msgrcv(msgID,&msg,sizeof(msg.keyInput)+sizeof(msg.pressed),0,IPC_NOWAIT)!=-1)
        {
            buzzerPlaySong(5); usleep(100000); buzzerStopSong();
            switch(msg.keyInput)
            {
                case KEY_HOME:
                    if (gameState == STATE_GAME_MENU || gameState == STATE_GAME_OVER)
                    {
                        
                        reset_all_systems();
                        text("GET READY","START SOON");
                        draw_bmp_image("Title_start.bmp"); fb_update(); sleep(2);
                        user_life = 3;
                        gameState = STATE_LED_COUNTDOWN;
                    }
                    break;

                case KEY_BACK:
                    if (gameState == STATE_GAME_RUNNING || isPaused)
                    {
                        isPaused = !isPaused;
                        if (isPaused)
                        {
                            gettimeofday(&pauseTime, NULL);
                        }
                        else
                        {
                            struct timeval resumeTime;
                            gettimeofday(&resumeTime, NULL);
                            paused_duration_ms += (resumeTime.tv_sec - pauseTime.tv_sec) * 1000 +
                                                  (resumeTime.tv_usec - pauseTime.tv_usec) / 1000;
                        }
                    }
                    break;
               
                case KEY_MENU:
                    gameState = STATE_GAME_MENU;
                    
                    reset_all_systems();
                    text("SHADOW RACER","MAIN MENU");
                    draw_bmp_image("Title.bmp");
                    fb_update();
                    break;

                case KEY_SEARCH:
                    if (gameState == STATE_GAME_OVER)
                    {
                        gameState = STATE_MINI_GAME;
                        mini_game();
                        if(minigame_over && minigame_win)
                        {
                            run_once = 0;
                            user_life++;
                            sprintf(user_life_str,"%d",user_life);
                            minigame_over = 0;
                            minigame_win = 0;
                            gameState = STATE_GAME_RUNNING;
                           
                            goto resume;
                        }
                        else
                        {
                            update_leaderboard(elapsed_ms);
                            reset_all_systems();
                            gameState = STATE_GAME_MENU;
                        }
                        
                    }
                    break;
            }
        }

        /* 상태 머신 */
        if (!isPaused)
        {
            resume:
            
            sprintf(life_display_str, "LIFE: %d", user_life);
            if(!run_once)
            {
            text(life_display_str, "");
            run_once = 1;
            }
            if (gameState==STATE_LED_COUNTDOWN)
            {
                long best=read_best_record();
                if(best!=-1){display_time_on_fnd(best);usleep(500000) ;fndDisp(0,0); usleep(500000);}
                for(int i=0;i<3;i++)ledOnOff(i,1);usleep(300000);
                for(int i=2;i>=0;i--)
                {
                    ledOnOff(i,0);
                    buzzerPlaySong(2);
                    usleep(30000);
                    buzzerStopSong();
                }
                gettimeofday(&startTime,NULL);
                gettimeofday(&lastSpawnTime,NULL);
               
                char life_display_str[17];
                sprintf(life_display_str, "LIFE: %d", user_life);
                text(life_display_str, "");

                gameState = STATE_GAME_RUNNING;
            }
            else if (gameState==STATE_GAME_RUNNING)
            {

                struct timeval now; gettimeofday(&now,NULL);
                elapsed_ms = (now.tv_sec-startTime.tv_sec)*1000 +
                             (now.tv_usec-startTime.tv_usec)/1000 -
                              paused_duration_ms;
                display_time_on_fnd(elapsed_ms);
               

                if ((now.tv_sec-lastSpawnTime.tv_sec)*1000 +
                    (now.tv_usec-lastSpawnTime.tv_usec)/1000 > 3000)
                { spawn_obstacle(); lastSpawnTime=now; }

                update_obstacles();
                carY_offset += game_handle_logic(g_accel_data[0]);
                draw_game_scene(carY_offset); fb_update();

                if (check_collision(car_lane, carY_offset))
                {
                    obstacles[striked_obs].x = 2000;    // 장애물 제거
                    obstacles[striked_obs].active = false;
                    printf("Collision!\n");
                    user_life--;
                    run_once = 0;
                    for(int z = 0; z < 2; z++)
                    {                    
                    buzzerPlaySong(1);
                    usleep(50000);
                    buzzerStopSong();
                    }
                    text("WATCH OUT!","LIFE -1");
                   
                    if(user_life == 0)
                    {
                        text("GAME OVER", "CONTINUE?");
                        gameState = STATE_GAME_OVER;
                        draw_bmp_image("game_over.bmp"); fb_update(); fndDisp(0,0);
                    }
                    else
                    {
                        sleep(1);
                    }
                }
            }  
        }
    }
    return 0;
}
