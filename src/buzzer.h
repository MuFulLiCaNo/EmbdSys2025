#ifndef _BUZZER_H_
#define _BUZZER_H_
int buzzerInit(void);
int buzzerPlaySong(int scale);
int buzzerStopSong(void);
int findBuzzerSysPath(void);
void doHelp(void);
void buzzerEnable(int bEnable);
void setFrequency(int frequency);

#endif
