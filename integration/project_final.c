/**********************************************************************
 *  car_game.c  — Frame‑buffer 기반 자동차 게임 (stride 대응 완전판)
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
#define LEADERBOARD_FILE "leaderboard.csv"
#define MAX_RECORDS 100
#define FBDEV_FILE "/dev/fb0"


#define MAX_OBSTACLES   10
#define OBSTACLE_WIDTH  40
#define OBSTACLE_HEIGHT 40
#define OBSTACLE_SPEED  15
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


static struct timeval startTime, pauseTime, lastSpawnTime;
static long   elapsed_ms         = 0;
static long   paused_duration_ms = 0;
int           carY_offset        = 0;


typedef struct { int x, y; bool active; } Obstacle;
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
bool check_collision(int offset);
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
char user_life_str [4];

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
            obstacles[i].y      = rand() % (fbHeight - OBSTACLE_HEIGHT);
            obstacles[i].active = true;
            return;
        }
    }
}
void update_obstacles(void)
{
    for (int i = 0; i < MAX_OBSTACLES; ++i)
    {
        if (obstacles[i].active)
        {
            obstacles[i].x += OBSTACLE_SPEED;
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
    else if (LEFT_weak)  return  5;
    else if (LEFT_strong)return 10;
    else if (RIGHT_weak) return -5;
    else if (RIGHT_strong)return-10;
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



void draw_car_sprite(int carX, int carY) {
    const int carW = 120;
    const int carH = 192;

    for (int y = 0; y < carH; y++) {
        for (int x = 0; x < carW; x++) {
            // 둥글게 잘라내기: 모서리 곡선 처리
            int dx = x - carW / 2;
            int dy = (y < carH / 2) ? (carH / 4 - y) : (y - carH * 3 / 4);
            int dist_sq = dx * dx + dy * dy;

            if ((y < 20 || y > carH - 20) && (x < 20 || x > carW - 20) && dist_sq > 22 * 22)
                continue; // 바깥 둥근 영역 제외

            // 회전: 좌측으로 90도 회전
            int newX = carX + (carH - 1 - y);
            int newY = carY + x;

            if (newX < 0 || newX >= fbWidth || newY < 0 || newY >= fbHeight)
                continue;

            uint32_t color = 0xFF2A2A; // 기본 차체: 강렬한 레드

            // 바퀴 영역 (곡선 타원 바퀴 느낌)
            if ((x < 15 || x > 105) && (y > 24 && y < 168)) {
                color = 0x1A1A1A; // 진한 회색 바퀴
            }

            // 윈도우 루프 영역
            if (y > 50 && y < 142 && x > 30 && x < 90) {
                color = 0x111111;
            }

            // 앞유리
            if (y < 36 && x > 35 && x < 85) {
                color = 0x66BFFF;
            }

            // 뒷유리
            if (y > 156 && x > 35 && x < 85) {
                color = 0x66BFFF;
            }

            // 자동차 루프 라인
            if (y == 50 || y == 142) {
                color = 0x222222;
            }

            // 라이트 (앞)
            if (y < 10 && ((x > 18 && x < 38) || (x > 82 && x < 102))) {
                color = 0xFFFF66;
            }

            // 라이트 (뒤)
            if (y > 182 && ((x > 18 && x < 38) || (x > 82 && x < 102))) {
                color = 0xFF3333;
            }

            // 도어 핸들
            if ((y > 80 && y < 85) && (x == 34 || x == 86)) {
                color = 0xDDDDDD;
            }

            // 중앙 스트라이프
            if (x > 58 && x < 62) {
                color = 0x000000;
            }

            pfbmap[newY * fbWidth + newX] = color;
        }
    }
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
            put_px(pbackbuffer, y, x, make_pixel(r,g,b));  /* [수정] */
        }
    }
    free(bmp);
}


void draw_game_scene(int offset)
{
    fb_clear();

    uint32_t gray = make_pixel(0x40, 0x40, 0x40);
    for (int y = 0; y < fbHeight; ++y)
        for (int x = 0; x < fbWidth; ++x)
            put_px(pbackbuffer, y, x, gray);

    /* 2) 차선 */
    int lane1 = fbHeight / 3, lane2 = fbHeight * 2 / 3;
    for (int x = 0; x < fbWidth; x += 40)
        for (int dx = 0; dx < 20 && x+dx < fbWidth; ++dx)
        {
            uint32_t white = make_pixel(0xFF,0xFF,0xFF);
            if (lane1 >= 0 && lane1 < fbHeight)
                put_px(pbackbuffer, lane1, x+dx, white);
            if (lane2 >= 0 && lane2 < fbHeight)
                put_px(pbackbuffer, lane2, x+dx, white);
        }

    /* 3) 자동차 */
    int carW = fbHeight / 5;
    int carH = (int)((carW * 2) * 0.8);
    int carX = fbWidth - (carH + 20);
    int carY = fbHeight / 2 - carW / 2 + offset;
    if (carY < 0)            carY = 0;
    if (carY + carW > fbHeight) carY = fbHeight - carW;

    draw_car_sprite(carX,carY);
    /* 4) 장애물 */
    uint32_t blue = make_pixel(0x00,0x00,0xFF);
    for (int i = 0; i < MAX_OBSTACLES; ++i)
    {
        if (!obstacles[i].active) continue;
        int ox = obstacles[i].x, oy = obstacles[i].y;
        for (int y = oy; y < oy + OBSTACLE_HEIGHT; ++y)
            for (int x = ox; x < ox + OBSTACLE_WIDTH; ++x)
                if (x >= 0 && x < fbWidth && y >= 0 && y < fbHeight)
                    put_px(pbackbuffer, y, x, blue);
    }
}


bool check_collision(int offset)
{
    int carW = fbHeight / 5;
    int carH = (int)((carW * 2) * 0.8);
    int carX = fbWidth - (carH + 20);
    int carY = fbHeight / 2 - carW / 2 + offset;
    if (carY < 0) carY = 0;
    if (carY + carW > fbHeight) carY = fbHeight - carW;

    int carL = carX, carR = carX + carH;
    int carT = carY, carB = carY + carW;

    for (int i = 0; i < MAX_OBSTACLES; ++i)
    {
        if (!obstacles[i].active) continue;
        int oL = obstacles[i].x,
            oR = obstacles[i].x + OBSTACLE_WIDTH,
            oT = obstacles[i].y,
            oB = obstacles[i].y + OBSTACLE_HEIGHT;

        if (carL < oR && carR > oL && carT < oB && carB > oT)
            return true;
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

    if (ledLibInit()<0||fndInit()<0||buzzerInit()==0||accelInit()==0||fb_init()<0)
    { fprintf(stderr,"HW init fail\n"); return -1; }

    msgID = buttonInit();
    if (msgID<0){fprintf(stderr,"button init fail\n");return 1;}

    /* 부팅 효과 */
    for(int i=1;i<=8;i++){ledOnOff(i-1,1);buzzerPlaySong(i);usleep(50000);}
    for(int i=7;i>=0;i--){ledOnOff(i,0);usleep(50000);} buzzerStopSong();

    reset_all_systems();
    gameState = STATE_GAME_MENU;
    draw_bmp_image("last.bmp"); fb_update();

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
                        draw_bmp_image("game_start.bmp"); fb_update(); sleep(2);
                        user_life =3;
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
                    } break;

                case KEY_SEARCH:
                    if (gameState==STATE_GAME_RUNNING||gameState==STATE_GAME_OVER)
                        update_leaderboard(elapsed_ms);
                    reset_all_systems(); gameState=STATE_GAME_MENU;
                    draw_bmp_image("last.bmp"); fb_update();
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

                if (check_collision(carY_offset))
                {
                    if(check_collision(carY_offset))
                    {
                        printf("Collision!\n");
                        user_life--;
                        if(user_life == 0)
                        {
                            update_leaderboard(elapsed_ms);
                            gameState = STATE_GAME_OVER;
                            draw_bmp_image("last.bmp"); fb_update(); fndDisp(0,0);
                        }
                        else //life remain
                        {
                            gameState = STATE_LED_COUNTDOWN;
                        }
                        
                    }

                    
                }   
            }   
            else if(gameState == STATE_GAME_OVER)
            {
                text("GAME OVER", "CONTINUE?");
                if(msg.keyInput == KEY_HOME) //goto first screen if press HOME
                {
                    gameState = STATE_GAME_MENU;
                }
                else if(msg.keyInput == KEY_BACK) //restart if press BACK
                {
                    user_life = 3;
                    gameState = STATE_LED_COUNTDOWN;
                }

            }
        }
        
        usleep(30000); 
    }
    return 0;
}
