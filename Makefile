CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc

SYSROOT ?=
TARGET := build/garlic-mp3-player
DIST_ROOT := dist/APPS
DIST_DIR := $(DIST_ROOT)/GarlicMP3

SDL_CFLAGS ?=
SDL_LIBS ?= -lSDL
GARLIC_SYSROOT ?= garlic-sysroot
GARLIC_IMAGE ?= garlic.img
GARLIC_TMP ?= /tmp/garlic-img
GARLIC_DYNAMIC_LINKER ?= libs/ld-linux-armhf.so.3
NO_BUNDLED_LIBS ?= 0

ifneq ($(SYSROOT),)
SYSROOT_FLAGS := --sysroot=$(SYSROOT)
endif

CFLAGS ?= -std=c99 -Wall -Wextra -Os
CPPFLAGS += $(SYSROOT_FLAGS) $(SDL_CFLAGS) -Isrc
LDFLAGS += $(SYSROOT_FLAGS)
LDLIBS += $(SDL_LIBS)

SRCS := \
	src/main.c \
	src/audio_mpg123.c \
	src/file_scan.c \
	src/input.c \
	src/ui_sdl.c

OBJS := $(SRCS:src/%.c=build/%.o)

.PHONY: all clean dist
.PHONY: extract-garlic-sysroot garlic-img-build garlic-img-dist docker-miyoo-dist

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

dist: all
	rm -rf $(DIST_ROOT)
	mkdir -p $(DIST_DIR)/MUSIC
	cp $(TARGET) $(DIST_DIR)/garlic-mp3-player
	cp APPS/GarlicMP3.sh $(DIST_ROOT)/GarlicMP3.sh
	cp README.md $(DIST_DIR)/README.txt
	if [ -d "assets" ]; then cp -a assets $(DIST_DIR)/; fi
	if [ "$(NO_BUNDLED_LIBS)" != "1" ] && [ -d "ref/drastic/libs" ]; then cp -a ref/drastic/libs $(DIST_DIR)/; fi
	if [ "$(NO_BUNDLED_LIBS)" != "1" ] && [ -f "$(GARLIC_SYSROOT)/lib/libSDL-1.2.so.0.11.4" ]; then cp "$(GARLIC_SYSROOT)/lib/libSDL-1.2.so.0.11.4" $(DIST_DIR)/libs/; fi
	if [ "$(NO_BUNDLED_LIBS)" != "1" ] && [ -d "$(DIST_DIR)/libs" ]; then rm -f $(DIST_DIR)/libs/libSDL-1.2.so.0 $(DIST_DIR)/libs/libSDL.so; cp $(DIST_DIR)/libs/libSDL-1.2.so.0.11.4 $(DIST_DIR)/libs/libSDL-1.2.so.0; cp $(DIST_DIR)/libs/libSDL-1.2.so.0.11.4 $(DIST_DIR)/libs/libSDL.so; fi
	if [ -f "ref/drastic/.directfbrc" ]; then cp ref/drastic/.directfbrc $(DIST_DIR)/.directfbrc; fi
	chmod +x $(DIST_DIR)/garlic-mp3-player $(DIST_ROOT)/GarlicMP3.sh

extract-garlic-sysroot:
	rm -rf $(GARLIC_SYSROOT)
	mkdir -p $(GARLIC_TMP) $(GARLIC_SYSROOT)
	7z e $(GARLIC_IMAGE) SYSTEM.img -o$(GARLIC_TMP) -y
	7z x $(GARLIC_TMP)/SYSTEM.img lib usr/lib usr/local/lib usr/alsa.conf usr/cards usr/pcm bin -o$(GARLIC_SYSROOT) -y
	rm -f $(GARLIC_SYSROOT)/lib/libSDL.so $(GARLIC_SYSROOT)/lib/libSDL-1.2.so.0
	ln -s libSDL-1.2.so.0.11.4 $(GARLIC_SYSROOT)/lib/libSDL.so
	ln -s libSDL-1.2.so.0.11.4 $(GARLIC_SYSROOT)/lib/libSDL-1.2.so.0

garlic-img-build:
	$(MAKE) CFLAGS="-std=c99 -Wall -Wextra -Os -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" LDFLAGS="-no-pie" SDL_CFLAGS="-Ithird_party/sdl12-min" SDL_LIBS="-Wl,--dynamic-linker=$(GARLIC_DYNAMIC_LINKER) -Wl,-rpath,libs -Wl,--copy-dt-needed-entries -Wl,--allow-shlib-undefined $(GARLIC_SYSROOT)/lib/libSDL-1.2.so.0.11.4 -Lref/drastic/libs -ldl -lpthread -lm"

garlic-img-dist:
	$(MAKE) dist CFLAGS="-std=c99 -Wall -Wextra -Os -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" LDFLAGS="-no-pie" SDL_CFLAGS="-Ithird_party/sdl12-min" SDL_LIBS="-Wl,--dynamic-linker=$(GARLIC_DYNAMIC_LINKER) -Wl,-rpath,libs -Wl,--copy-dt-needed-entries -Wl,--allow-shlib-undefined $(GARLIC_SYSROOT)/lib/libSDL-1.2.so.0.11.4 -Lref/drastic/libs -ldl -lpthread -lm"

docker-miyoo-dist:
	sh scripts/build-miyoo-docker.sh

docker-rg35xx-dist:
	sh scripts/build-rg35xx-docker.sh

clean:
	rm -rf build dist
