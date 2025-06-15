/**********************************************************************
 *  project_final.c — Frame‑buffer 기반 자동차 게임 (완전 통합판)
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

static int fbfd = -1;
struct fb_var_screeninfo fbinfo;
struct fb_fix_screeninfo fbfix;
int fbWidth, fbHeight;
int line_length;                 

static unsigned long *pfbmap      = NULL; 
static unsigned long *pbackbuffer = NULL; 
static size_t         backbufBytes= 0;

static uint32_t car_sprite[CAR_SPRITE_HEIGHT][CAR_SPRITE_WIDTH];

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
            obstacles[i].x += 15;
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

    // 장애물 그리기 (lane 기반)
    uint32_t red = make_pixel(0xFF, 0x00, 0x00);
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            int obsX = obstacles[i].x;
            int obsLane = obstacles[i].lane;
            
            int obsW = CAR_ORIG_WIDTH * 0.8;
            int obsH = CAR_ORIG_HEIGHT * 0.4;
            int obsY;
    
            switch (obsLane) {
                case 0: obsY = fbHeight / 6 - obsW / 2; break;
                case 1: obsY = fbHeight / 2 - obsW / 2; break;
                case 2: obsY = fbHeight * 5 / 6 - obsW / 2; break;
                default: obsY = fbHeight / 2 - obsW / 2;
            }
    
            for (int y = obsY; y < obsY + obsW; y++) {
                for (int x = obsX; x < obsX + obsH; x++) {
                    if (x >= 0 && x < fbWidth && y >= 0 && y < fbHeight) {
                        put_px(pbackbuffer, y, x, red);
                    }
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
    
    int carLeft = carX;
    int carRight = carX + CAR_SPRITE_WIDTH;
    int carTop = carY;
    int carBottom = carY + CAR_SPRITE_HEIGHT;
    
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            int obsX = obstacles[i].x;
            int obsW = CAR_ORIG_WIDTH * 0.8;
            int obsH = CAR_ORIG_HEIGHT * 0.4;
            int obsY;
            
            switch (obstacles[i].lane) {
                case 0: obsY = fbHeight / 6 - obsW / 2; break;
                case 1: obsY = fbHeight / 2 - obsW / 2; break;
                case 2: obsY = fbHeight * 5 / 6 - obsW / 2; break;
                default: obsY = fbHeight / 2 - obsW / 2;
            }
            
            int obsLeft = obsX;
            int obsRight = obsX + obsH;
            int obsTop = obsY;
            int obsBottom = obsY + obsW;
            
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
    isPaused=0; elapsed_ms=0; paused_duration_ms=0; carY_offset=0;
    for(int i=0;i<8;i++) ledOnOff(i,0);
    fndDisp(0,0); init_obstacles();
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

    BUTTON_MSG_T msg;
    while (1)
    {  
        /* 버튼 비동기 수신 */
        if (msgrcv(msgID,&msg,sizeof(msg.keyInput)+sizeof(msg.pressed),0,IPC_NOWAIT)!=-1)
        {
            buzzerPlaySong(5); usleep(100000); buzzerStopSong();
            switch(msg.keyInput)
            {
                case KEY_HOME:
                    if (gameState==STATE_GAME_MENU||gameState==STATE_GAME_OVER)
                    {
                        text("HELLO USER","MAIN MENU");
                        reset_all_systems();
                        draw_bmp_image("Title_start.bmp"); fb_update(); sleep(2);
                        user_life = 3;
                        gameState = STATE_LED_COUNTDOWN;
                    } break;

                case KEY_BACK:
                    if (gameState==STATE_GAME_RUNNING||isPaused)
                    {
                        isPaused=!isPaused;
                        if(isPaused) gettimeofday(&pauseTime,NULL);
                        else {
                            struct timeval r;gettimeofday(&r,NULL);
                            paused_duration_ms += (r.tv_sec-pauseTime.tv_sec)*1000 +
                                                  (r.tv_usec-pauseTime.tv_usec)/1000;
                        }
                    } 
                    else if (gameState==STATE_GAME_OVER)
                    {
                        user_life = 3;
                        gameState = STATE_LED_COUNTDOWN;
                    }
                    break;

                case KEY_SEARCH:
                    if (gameState==STATE_MINI_GAME)
                    {
                        isPaused = !isPaused;
                        update_leaderboard(elapsed_ms);
                        draw_bmp_image("minigame.bmp"); fb_update();
                        if(minigame_over)
                        {
                            isPaused = !isPaused;
                        }
                    }
                    else if (gameState==STATE_GAME_RUNNING||gameState==STATE_GAME_OVER)
                    {
                        update_leaderboard(elapsed_ms);
                        reset_all_systems(); 
                        gameState=STATE_GAME_MENU;
                        draw_bmp_image("Title.bmp"); fb_update();
                    }
                    break;
            }
        }

        /* 상태 머신 */
        if (!isPaused)
        {
            if (gameState==STATE_LED_COUNTDOWN)
            {
                long best=read_best_record();
                if(best!=-1){display_time_on_fnd(best);sleep(1);fndDisp(0,0);sleep(1);}
                for(int i=0;i<3;i++)ledOnOff(i,1);sleep(1);
                for(int i=2;i>=0;i--){ledOnOff(i,0);sleep(1);}
                gettimeofday(&startTime,NULL);
                gettimeofday(&lastSpawnTime,NULL);
                gameState=STATE_GAME_RUNNING;
            }
            else if (gameState==STATE_GAME_RUNNING)
            {
                struct timeval now; gettimeofday(&now,NULL);
                elapsed_ms = (now.tv_sec-startTime.tv_sec)*1000 +
                             (now.tv_usec-startTime.tv_usec)/1000 -
                              paused_duration_ms;
                display_time_on_fnd(elapsed_ms);
                
                sprintf(user_life_str,"%d",user_life);
                text("USER LIFE:",user_life_str);

                if ((now.tv_sec-lastSpawnTime.tv_sec)*1000 +
                    (now.tv_usec-lastSpawnTime.tv_usec)/1000 > 1500)
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
                    text("WATCH OUT!","LIFE -1");
                    
                    if(user_life == 0)
                    {
                        text("GAME OVER", "CONTINUE?");
                        update_leaderboard(elapsed_ms);
                        gameState = STATE_GAME_OVER;
                        draw_bmp_image("game_over.bmp"); fb_update(); fndDisp(0,0);
                    }
                    else
                    {
                        // 생명이 남아있으면 잠깐 멈춤 후 계속
                        sleep(1);
                    }
                }
                
                // 시간 제한 (60초)
                if (elapsed_ms >= 60000)
                {
                    printf("Time up!\n");
                    update_leaderboard(elapsed_ms);
                    gameState = STATE_GAME_OVER;
                    draw_bmp_image("game_over.bmp"); fb_update(); fndDisp(0,0);
                }
            }   
            else if(gameState == STATE_GAME_OVER)
            {
                draw_bmp_image("game_over.bmp"); fb_update();
            }
        }
        
        usleep(30000); 
    }
    return 0;
}