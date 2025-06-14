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

// 사용자 정의 헤더
#include "led.h"
#include "button.h"
#include "fnd.h"
#include "buzzer.h"
#include "accel.h"

// 게임 상태 정의
#define STATE_IDLE          0
#define STATE_LED_COUNTDOWN 1
#define STATE_FND_COUNTUP   2
#define STATE_GAME_MENU     3 // 메뉴 상태 추가
#define STATE_GAME_RUNNING  4 // 게임 진행 상태 추가
#define STATE_GAME_OVER     5 // 게임 오버 상태 추가

#define LEADERBOARD_FILE "leaderboard.csv"
#define MAX_RECORDS 100
#define FBDEV_FILE "/dev/fb0"

// 전역 변수
static int gameState = STATE_GAME_MENU; // 프로그램 시작 시 메뉴 상태
static int isPaused = 0;
static int msgID = 0;

// 프레임버퍼 관련 전역 변수
unsigned long *pfbmap;
int fbfd;
struct fb_var_screeninfo fbinfo;
struct fb_fix_screeninfo fbfix;
int fbWidth, fbHeight;
int line_length;

// 스톱워치 관련 변수
static struct timeval startTime, pauseTime;
static long elapsed_ms = 0;
static long paused_duration_ms = 0;

// 게임 관련 변수
int car_lane = 1; // 0: top, 1: middle, 2: bottom
int carY_offset = 0;

// 함수 선언
void fb_close();
void signal_handler(int nSignal);
void reset_all_systems(void);

// 자동차 이동 로직 (가속도 센서 값 기반)
int game_handle_logic(int accel_d) { // accel_d is g_accel_data[0]
    int distance = 0;
    int STABLE, LEFT_weak, LEFT_strong, RIGHT_weak, RIGHT_strong;

    STABLE = (accel_d < 700) && (accel_d > -700);
    LEFT_weak = (accel_d > 1000) && (accel_d < 3000);
    LEFT_strong = accel_d > 3000;
    RIGHT_weak = (accel_d < -1000) && (accel_d > -3000);
    RIGHT_strong = accel_d < -3000;

    if (STABLE) {
        distance = 0;
    } else if (LEFT_weak) {
        distance = 5;
    } else if (LEFT_strong) {
        distance = 10;
    } else if (RIGHT_weak) {
        distance = -5;
    } else if (RIGHT_strong) {
        distance = -10;
    }
    return distance;
}

// 종료 시그널 핸들러
void signal_handler(int nSignal) {
    printf("\nGood-bye!\n");
    for (int t = 8; t >= 1; t--) { // 시스템 종료 사운드
        buzzerPlaySong(t);
        usleep(50000);
    }
    buzzerStopSong();

    ledLibExit();
    fndExit();
    buzzerExit();
    accelExit();
    buttonExit();
    fb_close();

    // 터미널 모드를 텍스트 모드로 복구
    int conFD = open("/dev/tty0", O_RDWR);
    ioctl(conFD, KDSETMODE, KD_TEXT);
    close(conFD);

    exit(0);
}


// 프레임버퍼 리소스 해제
void fb_close() {
    if (pfbmap)
        munmap(pfbmap, fbWidth * fbHeight * 4);
    if (fbfd >= 0)
        close(fbfd);
}

// 프레임버퍼 초기화
int fb_init() {
    int conFD = open("/dev/tty0", O_RDWR);
    ioctl(conFD, KDSETMODE, KD_GRAPHICS);
    close(conFD);

    fbfd = open(FBDEV_FILE, O_RDWR);
    if (fbfd < 0) {
        perror("Framebuffer open");
        return -1;
    }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &fbinfo) || ioctl(fbfd, FBIOGET_FSCREENINFO, &fbfix)) {
        perror("Framebuffer ioctl");
        fb_close();
        return -1;
    }

    if (fbinfo.bits_per_pixel != 32) {
        fprintf(stderr, "Error: Not supported 32-bit color\n");
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
    return 0;
}


void fb_clear() {
    for (int y = 0; y < fbHeight; y++) {
        unsigned long *ptr = pfbmap + y * fbWidth;
        for (int x = 0; x < fbWidth; x++) {
            *ptr++ = 0x000000; // Black
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
    if(!(*rgbdata)){
        fclose(fp);
        return -1;
    }
    fread(*rgbdata, sizeof(unsigned char), row_padded * (*height), fp);
    fclose(fp);
    return 0;
}

void draw_bmp_image(const char *filename) {
    unsigned char *bmpdata = NULL;
    int bmpW = 0, bmpH = 0;
    if (load_bmp(filename, &bmpdata, &bmpW, &bmpH) != 0) {
        printf("Failed to load %s\n", filename);
        return;
    }

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

void draw_game_scene(int carY_offset) {
    fb_clear();

    // 도로 배경
    for (int y = 0; y < fbHeight; y++) {
        for (int x = 0; x < fbWidth; x++) {
            pfbmap[y * fbWidth + x] = 0x404040; // Dark Gray
        }
    }

    // 차선
    int lane1 = fbHeight / 3;
    int lane2 = fbHeight * 2 / 3;
    for (int x = 0; x < fbWidth; x += 40) {
        for (int dx = 0; dx < 20 && x + dx < fbWidth; dx++) {
            if (lane1 >= 0 && lane1 < fbHeight)
                pfbmap[lane1 * fbWidth + (x + dx)] = 0xFFFFFF; // White
            if (lane2 >= 0 && lane2 < fbHeight)
                pfbmap[lane2 * fbWidth + (x + dx)] = 0xFFFFFF; // White
        }
    }

    // 자동차
    int carW = fbHeight / 5;
    int carH = (carW * 2) * 0.8;
    int carX = fbWidth - (carH + 20);
    int carY = fbHeight / 2 - carW / 2; // 자동차의 기본 위치는 중앙

    carY += carY_offset; // 가속도 센서 값에 따라 Y 위치 변경

    // 화면 밖으로 나가지 않도록 경계 처리
    if (carY < 0) carY = 0;
    if (carY + carW > fbHeight) carY = fbHeight - carW;

    // 자동차 그리기 (빨간색)
    for (int y = carY; y < carY + carW; y++) {
        for (int x = carX; x < carX + carH; x++) {
            if (x >= 0 && x < fbWidth && y >= 0 && y < fbHeight)
                pfbmap[y * fbWidth + x] = 0xFF0000;
        }
    }
}


// FND에 시간 표시
void display_time_on_fnd(long time_ms) {
    if (time_ms < 0) return;
    int minutes = (time_ms / 60000) % 60;
    int seconds = (time_ms % 60000) / 1000;
    int centiseconds = (time_ms % 1000) / 10;
    int fnd_number = (minutes * 10000) + (seconds * 100) + centiseconds;
    int dot_flag = 1 << 3;
    fndDisp(fnd_number, dot_flag);
}

// 리더보드 최고 기록 읽기 (이제 최고 기록은 최장 시간 기록이 됨)
long read_best_record(void) {
    FILE *fp = fopen(LEADERBOARD_FILE, "r");
    if (fp == NULL) return -1;
    long best_time = -1;
    char line[256];
    // 파일의 첫 번째 줄이 최고 기록이므로 첫 줄만 읽음
    if (fgets(line, sizeof(line), fp))
    {
        best_time = atol(line);
    }
    fclose(fp);
    return best_time;
}

// 내림차순 (긴 시간이 위로)으로 정렬
int compare_records(const void *a, const void *b) {
    long time_a = *(long*)a;
    long time_b = *(long*)b;
    if (time_a < time_b) return 1;  // a가 작으면 뒤로 보냄 (내림차순)
    if (time_a > time_b) return -1; // a가 크면 앞으로 보냄 (내림차순)
    return 0;
}

// 리더보드 업데이트
void update_leaderboard(long new_time_ms) {
    long records[MAX_RECORDS + 1];
    int count = 0;
    FILE *fp = fopen(LEADERBOARD_FILE, "r");
    if (fp != NULL) {
        while (fscanf(fp, "%ld", &records[count]) == 1 && count < MAX_RECORDS) count++;
        fclose(fp);
    }
    records[count++] = new_time_ms;
    qsort(records, count, sizeof(long), compare_records);
    fp = fopen(LEADERBOARD_FILE, "w");
    if (fp == NULL) return;
    for (int i = 0; i < count && i < 10; i++) fprintf(fp, "%ld\n", records[i]); // 상위 10개만 저장
    fclose(fp);
    printf("Record saved and leaderboard sorted: %ld ms -> %s\n", new_time_ms, LEADERBOARD_FILE);
}

// 시스템 리셋
void reset_all_systems(void) {
    printf("SYSTEM RESET...\n");
    isPaused = 0;
    elapsed_ms = 0;
    paused_duration_ms = 0;
    carY_offset = 0;
    for (int i = 0; i < 8; i++) ledOnOff(i, 0);
    fndDisp(0, 0);
    printf("\n");
}

int main(void) {
    // 1. 모든 라이브러리 및 하드웨어 초기화
    signal(SIGINT, signal_handler);

    if (ledLibInit() < 0) { printf("ledLibInit failed\n"); return -1; }
    if (fndInit() < 0) { printf("fndInit failed\n"); return -1; }
    if (buzzerInit() == 0) { printf("buzzerInit failed\n"); return -1; }
    if (accelInit() == 0) { printf("accelInit failed\n"); return -1; }
    if (fb_init() < 0) { printf("fb_init failed\n"); return -1; }

    msgID = buttonInit();
    if (msgID < 0) {
        printf("Button Init Failed! (msgget failed)\n");
        return 1;
    }

    // 2. 프로그램 시작 효과 (LED & Buzzer)
    for (int p = 1; p <= 8; p++) {
        ledOnOff(p - 1, 1);
        buzzerPlaySong(p);
        usleep(50000);
    }
    for (int t = 7; t >= 0; t--) {
        ledOnOff(t, 0);
        usleep(50000);
    }
    buzzerStopSong();

    reset_all_systems();
    printf("Program Ready. Current state: MENU\n");
    gameState = STATE_GAME_MENU;
    draw_bmp_image("last.bmp");

    BUTTON_MSG_T messageRxData;

    // 3. 메인 이벤트 루프
    while (1) {
        // 버튼 입력 처리
        if (msgrcv(msgID, &messageRxData, sizeof(messageRxData.keyInput) + sizeof(messageRxData.pressed), 0, IPC_NOWAIT) != -1) {
            buzzerPlaySong(5);
            usleep(100000);
            buzzerStopSong();

            switch (messageRxData.keyInput) {
                case KEY_HOME: // 게임 시작 / 메뉴로 돌아가기
                    printf("HOME button pressed.\n");
                    if (gameState == STATE_GAME_MENU || gameState == STATE_GAME_OVER) {
                        reset_all_systems();
                        draw_bmp_image("game_start.bmp"); // 게임 시작 안내 화면
                        sleep(2); // 2초 대기
                        gameState = STATE_LED_COUNTDOWN;
                    }
                    break;

                case KEY_BACK: // 일시정지 / 재개
                     if (gameState == STATE_GAME_RUNNING || isPaused) {
                         isPaused = !isPaused;
                         if (isPaused) {
                             gettimeofday(&pauseTime, NULL);
                             printf("\nPAUSED.\n");
                         } else {
                             struct timeval resumeTime;
                             gettimeofday(&resumeTime, NULL);
                             paused_duration_ms += (resumeTime.tv_sec - pauseTime.tv_sec) * 1000 + (resumeTime.tv_usec - pauseTime.tv_usec) / 1000;
                             printf("RESUMED.\n");
                         }
                     }
                    break;

                case KEY_SEARCH:
                    printf("SEARCH button pressed.\n");


                    if (gameState == STATE_GAME_RUNNING || gameState == STATE_GAME_OVER) {
                        update_leaderboard(elapsed_ms);
                    }

                    reset_all_systems();
                    gameState = STATE_GAME_MENU;
                    draw_bmp_image("last.bmp"); // 메뉴 화면 표시
                    break;
            }
        }

        // 상태에 따른 동작 처리
        if (!isPaused) {
            if (gameState == STATE_LED_COUNTDOWN) {
                printf("Starting 5-second LED countdown...\n");

                long best_time = read_best_record();
                if (best_time != -1) {
                    printf("Best record found: %ld ms. Blinking twice.\n", best_time);
                    display_time_on_fnd(best_time); sleep(1);
                    fndDisp(0,0); sleep(1);
                    display_time_on_fnd(best_time); sleep(1);
                    fndDisp(0,0);
                }

                // 5개의 LED로 5초 카운트다운
                for (int i = 0; i < 3; i++) {
                    ledOnOff(i, 1);
                }
                sleep(1);
                for (int i = 2; i >= 0; i--) {
                    ledOnOff(i, 0);
                    sleep(1);
                }

                printf("LED countdown finished. Starting Game.\n");
                gameState = STATE_GAME_RUNNING;
                gettimeofday(&startTime, NULL);
                paused_duration_ms = 0;
            }
            else if (gameState == STATE_GAME_RUNNING) {
                struct timeval currentTime;
                gettimeofday(&currentTime, NULL);
                elapsed_ms = (currentTime.tv_sec - startTime.tv_sec) * 1000 + (currentTime.tv_usec - startTime.tv_usec) / 1000;
                elapsed_ms -= paused_duration_ms;
                display_time_on_fnd(elapsed_ms);

                printf("ACCEL: X=%-5d Y=%-5d Z=%-5d\r", g_accel_data[0], g_accel_data[1], g_accel_data[2]);
                fflush(stdout);

                int distance = game_handle_logic(g_accel_data[0]);
                carY_offset += distance;
                draw_game_scene(carY_offset);

                if (elapsed_ms >= 60000) {
                    printf("\nTIME UP! Game Over.\n");
                    gameState = STATE_GAME_OVER;
                    draw_bmp_image("last.bmp");
                    fndDisp(0, 0);
                }
            }
        }
        usleep(30000);
    }

    signal_handler(0);
    return 0;
}