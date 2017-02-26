LDLIBS += -lX11 -lXi
CFLAGS += -std=c99 -Wall -Wextra

all: tuxeye2

tuxeye2: tuxeye2.c libff.h
