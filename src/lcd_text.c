#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "textlcd.h"

#define MAX_LIVES 3

int main() {
	int current_lives = MAX_LIVES;
	char life_display_str[COLUMN_NUM + 1];

	while (current_lives > 0){
		memset(life_display_str, ' ', COLUMN_NUM);
		life_display_str[COLUMN_NUM] = '\0';

		fir (int i = 0; i < current_lives; i++){
			if (i < COLUMN_NUM){
				life_display_str[1] = '@';
			}
		}

		int result = text("life", life_display_str);

		sleep(2);


		current_lives--;
	}

	memset(life_display_str, ' ', COLUMN_NUM);
	life_display_str[COLUMN_NUM] = '\0';
	text("life", life_display_str);
	sleep(1);

	text("GAME", "OVER");
	sleep(2);

	return EXIT_SUCCESS;
}

