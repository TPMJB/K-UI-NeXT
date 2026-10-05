# SPDX-License-Identifier: MIT
# Host tests for the Wi-Fi firmware's portable code: `make -C firmware/kui-wifi/test`.
CC ?= cc
CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion \
          -fsanitize=address,undefined -fno-omit-frame-pointer
KWLINK := ../components/kwlink
INCLUDES := -I$(KWLINK)/include -I../main
BUILD := build

.PHONY: all test clean
all: test

$(BUILD):
	mkdir -p $@

$(BUILD)/test_kwlink: test_kwlink.c $(KWLINK)/kwlink.c $(KWLINK)/include/kwlink.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_kwlink.c $(KWLINK)/kwlink.c

$(BUILD)/test_bridge: test_bridge.c ../main/bridge.c ../main/bridge.h ../main/bridge_platform.h $(KWLINK)/kwhost.c \
		$(KWLINK)/kwlink.c $(wildcard $(KWLINK)/include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_bridge.c ../main/bridge.c $(KWLINK)/kwhost.c $(KWLINK)/kwlink.c

$(BUILD)/test_bridge_throughput: test_bridge_throughput.c ../main/bridge.c ../main/bridge.h ../main/bridge_platform.h $(KWLINK)/kwhost.c \
		$(KWLINK)/kwlink.c $(wildcard $(KWLINK)/include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_bridge_throughput.c ../main/bridge.c $(KWLINK)/kwhost.c $(KWLINK)/kwlink.c

$(BUILD)/test_wifi_band_control: test_wifi_band_control.c ../main/wifi_band_control.c ../main/wifi_band_control.h \
		$(wildcard $(KWLINK)/include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_wifi_band_control.c ../main/wifi_band_control.c

test: $(BUILD)/test_kwlink $(BUILD)/test_bridge $(BUILD)/test_bridge_throughput $(BUILD)/test_wifi_band_control
	./$(BUILD)/test_kwlink
	./$(BUILD)/test_bridge
	./$(BUILD)/test_bridge_throughput
	./$(BUILD)/test_wifi_band_control

clean:
	rm -rf $(BUILD)
