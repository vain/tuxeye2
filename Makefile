LDLIBS += -lX11 -lXext -lm
CFLAGS += -std=c99 -Wall -Wextra

THEME_PATH = $(shell pwd)
CFLAGS += -DTHEME_PATH=\"$(THEME_PATH)\"

.PHONY: all clean

all: tuxeye2

clean:
	rm tuxeye2

tuxeye2: tuxeye2.c libff.h
