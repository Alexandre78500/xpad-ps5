# The console payload, built with ps5-payload-sdk. Called from the Makefile.

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF   := dist/xpad-ps5.elf
OBJD  := build/ps5

CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -Isrc -Isrc/anypad
LDLIBS += -lScePad -lSceUserService -lSceSystemService -ldl

SRCS := src/main.c src/gip.c src/weblog.c src/rest.c src/anypad/ps5_vpad.c src/anypad/log.c src/anypad/util.c src/anypad/ps5_power.c
OBJS := $(patsubst src/%.c,$(OBJD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
