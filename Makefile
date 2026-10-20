# xpad-ps5: wired Xbox controllers on a jailbroken PS5.
#
#   make test     unit-tests the protocol on this machine (any Unix with cc)
#   make ps5      builds dist/xpad-ps5.elf (needs PS5_PAYLOAD_SDK, see README.md)
#   make send     sends it to Payload Manager and starts it (PS5_HOST=<console ip>)

BUILD := build

.PHONY: test ps5 send clean

$(BUILD)/test_gip: src/gip.c tests/test_gip.c src/gip.h src/anypad/pad.h
	@mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -Isrc -o $@ src/gip.c tests/test_gip.c -lm

test: $(BUILD)/test_gip
	./$(BUILD)/test_gip

ps5:
	$(MAKE) -f ps5.mk

send: ps5
	./tools/send.sh dist/xpad-ps5.elf

clean:
	rm -rf $(BUILD) dist
