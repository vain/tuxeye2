LDLIBS += -lX11 -lXi -lXext -lm
CFLAGS += -std=c99 -Wall -Wextra
CFLAGS += -DTHEME_PATH=\"$(THEME_PATH)\"

all: tuxeye2

tuxeye2: tuxeye2.c libff.h
