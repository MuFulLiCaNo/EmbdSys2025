#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/input.h>
#include "led.h"
#include "button.h"
#include "fnd.h"
#include "buzzer.h" // 버저 헤더 추가
#include "accel.h"  // 가속도 센서 헤더 추가

#define STATE_IDLE           0
#define STATE_LED_COUNTDOWN  1
#define STATE_FND_COUNTUP    2

#define LEADERBOARD_FILE "leaderboard.csv"
#define MAX_RECORDS 100

// 애플리케이션의 현재 상태를 관리하는 전역 변수
static int gameState = STATE_IDLE;
static int isPaused = 0;

// stopwatch 기능용 변수
static struct timeval startTime, pauseTime;
static long elapsed_ms = 0;
static long paused_duration_ms = 0;

// FND에 시간을 표시하는 함수
void display_time_on_fnd(long time_ms)
{
    if (time_ms < 0) return;
    int minutes = (time_ms / 60000) % 60;
    int seconds = (time_ms % 60000) / 1000;
    int centiseconds = (time_ms % 1000) / 10;
    int fnd_number = (minutes * 10000) + (seconds * 100) + centiseconds;
    int dot_flag = 1 << 3;
    fndDisp(fnd_number, dot_flag);
}

// 리더보드 관련 함수
long read_best_record(void)
{
    FILE *fp = fopen(LEADERBOARD_FILE, "r");
    if (fp == NULL) return -1;
    long best_time = -1, current_time;
    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        current_time = atol(line);
        if (best_time == -1 || current_time < best_time) best_time = current_time;
    }
    fclose(fp);
    return best_time;
}

int compare_records(const void *a, const void *b) {
    long time_a = *(long*)a;
    long time_b = *(long*)b;
    if (time_a < time_b) return -1;
    if (time_a > time_b) return 1;
    return 0;
}

void update_leaderboard(long new_time_ms)
{
    long records[MAX_RECORDS + 1];
    int count = 0;
    FILE *fp = fopen(LEADERBOARD_FILE, "r");
    if (fp != NULL)
    {
        while (fscanf(fp, "%ld", &records[count]) == 1 && count < MAX_RECORDS) count++;
        fclose(fp);
    }
    records[count++] = new_time_ms;
    qsort(records, count, sizeof(long), compare_records);
    fp = fopen(LEADERBOARD_FILE, "w");
    if (fp == NULL) return;
    for (int i = 0; i < count; i++) fprintf(fp, "%ld\n", records[i]);
    fclose(fp);
    printf("Record saved and leaderboard sorted: %ld ms -> %s\n", new_time_ms, LEADERBOARD_FILE);
}

// 모든 동작을 초기화하는 함수
void reset_all_systems(void)
{
    printf("SYSTEM RESET...\n");
    isPaused = 0;
    gameState = STATE_IDLE;
    elapsed_ms = 0;
    paused_duration_ms = 0;
    for (int i = 0; i < 8; i++) ledOnOff(i, 0);
    fndDisp(0, 0);
    // 가속도 값 표시 중지
    printf("\n"); // Clear accel data line
}

int main(void)
{
    int msgID = 0;
    BUTTON_MSG_T messageRxData;

    // 모든 라이브러리 초기화
    ledLibInit();
    fndInit();
    buzzerInit();
    accelInit();
    msgID = buttonInit();
    if (msgID < 0)
    {
        printf("Button Init Failed! (msgget failed)\n");
        return 1;
    }

    reset_all_systems();
    printf("Program Ready. Press Home(1st) button to start.\n");

    while(1)
    {
        if (msgrcv(msgID, &messageRxData, sizeof(int), 0, IPC_NOWAIT) != -1)
        {
            buzzerPlaySong(5); 
            usleep(100000); 
            buzzerStopSong();

            switch(messageRxData.keyInput)
            {
                case KEY_HOME:
                    printf("HOME button pressed. Starting routine.\n");
                    reset_all_systems();
                    gameState = STATE_LED_COUNTDOWN;
                    break;

                case KEY_BACK:
                    if (gameState != STATE_IDLE)
                    {
                        isPaused = !isPaused;
                        if (isPaused) printf("\nPAUSED.\n");
                        else printf("RESUMED.\n");
                    }
                    break;
                    
                case KEY_SEARCH:
                    if (gameState == STATE_FND_COUNTUP) update_leaderboard(elapsed_ms);
                    reset_all_systems();
                    break;
            }
        }

        if (!isPaused)
        {
            if (gameState == STATE_LED_COUNTDOWN)
            {
                // LED 카운트다운 시작 시, 최고 기록을 읽어와 2번 깜빡인다.
                long best_time = read_best_record();
                if (best_time != -1)
                {
                    printf("Best record found: %ld ms. Blinking twice.\n", best_time);
                    display_time_on_fnd(best_time); sleep(1);
                    fndDisp(0,0); sleep(1);
                    display_time_on_fnd(best_time); sleep(1);
                    fndDisp(0,0);
                }

                printf("Starting LED countdown...\n");
                for (int i = 3; i <= 7; i++) ledOnOff(i, 1);
                
                for (int i = 7; i >= 3; i--)
                {
                    sleep(1);
                    ledOnOff(i, 0);
                }

                printf("LED countdown finished. Starting FND stopwatch.\n");
                gameState = STATE_FND_COUNTUP;
                gettimeofday(&startTime, NULL);
                paused_duration_ms = 0;
            }
            else if (gameState == STATE_FND_COUNTUP)
            {
                struct timeval currentTime;
                gettimeofday(&currentTime, NULL);
                
                elapsed_ms = (currentTime.tv_sec - startTime.tv_sec) * 1000 + (currentTime.tv_usec - startTime.tv_usec) / 1000;
                elapsed_ms -= paused_duration_ms;

                display_time_on_fnd(elapsed_ms);
                
                // 가속도 센서 값 출력
                printf("ACCEL: X=%-5d Y=%-5d Z=%-5d\r", g_accel_data[0], g_accel_data[1], g_accel_data[2]);
                fflush(stdout);
            }
        }

        usleep(10000);
    }

    ledLibExit();
    fndExit();
    buzzerExit();
    accelExit();
    buttonExit();

    return 0;
}
