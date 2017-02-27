LDLIBS += -lX11 -lXi -lm
CFLAGS += -std=c99 -Wall -Wextra

all: tuxeye2

tuxeye2: tuxeye2.c libff.h
