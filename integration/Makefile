# Makefile for final project (Revised)

CROSS_COMPILE   := arm-linux-gnueabi-
CC              := $(CROSS_COMPILE)gcc
AR              := $(CROSS_COMPILE)ar

TARGET          := project_final.elf


SRCS            := project_final.c
OBJS            := $(SRCS:.c=.o)


LIB_A           := libMyPeri.a
LIB_OBJS        := fnd.o led.o button.o buzzer.o accel.o


CFLAGS          := -I.
LDFLAGS         := -L. -lMyPeri -lpthread



all: $(TARGET)


$(TARGET): $(OBJS) $(LIB_A)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	@echo "================================================="
	@echo " Final Executable [$(TARGET)] is created."
	@echo "================================================="


$(LIB_A): $(LIB_OBJS)
	$(AR) rc $@ $^
	@echo "---- Static Library [$(LIB_A)] is created. ----"


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@



fnd.o: fnd.h fnd.c
	$(CC) $(CFLAGS) -c fnd.c -o fnd.o

led.o: led.h led.c
	$(CC) $(CFLAGS) -c led.c -o led.o

button.o: button.h button.c
	$(CC) $(CFLAGS) -c button.c -o button.o

buzzer.o: buzzer.h buzzer.c
	$(CC) $(CFLAGS) -c buzzer.c -o buzzer.o

accel.o: accel.h accel.c
	$(CC) $(CFLAGS) -c accel.c -o accel.o



clean:
	rm -f $(OBJS) $(LIB_OBJS) $(LIB_A) $(TARGET)
	@echo "---- All generated files are cleaned. ----"
