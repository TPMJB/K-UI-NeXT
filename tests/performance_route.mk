# SPDX-License-Identifier: GPL-3.0-only
include Makefile
PERF_ROUTE_FLAG ?= 1
PERF_ROUTE_BINARY = build/games-retail-perf-route-$(PERF_ROUTE_FLAG)
PERF_ROUTE_EXTRA = src/core/vmu_game_card.c src/core/vmu_persist_profile.c src/core/native_vmu_standard.c
$(PERF_ROUTE_BINARY): tests/games_retail.c tests/games_retail_perf_route.inc $(GAMES_RETAIL) $(CORE) $(FATFS) $(PERF_ROUTE_EXTRA)
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Isrc/dreamcast -fno-pie -no-pie \
	 -DKUI_PERF_ROUTE_TEST=1 -DKUI_VMU_GAME_CARDS=1 -DKUI_PERF_NATIVE_STANDALONE=$(PERF_ROUTE_FLAG) \
	 tests/games_retail.c $(GAMES_RETAIL) $(CORE) $(FATFS) $(PERF_ROUTE_EXTRA) $(GAMES_IMAGE_PROBE_WRAP) \
	 -Wl,--wrap=kui_native_sdk_sci_family_find,--wrap=kui_vmu_native_program_extent \
	 -Wl,--wrap=kui_vmu_native_resolution_cache_build,--wrap=kui_vmu_native_cache_seed_fill -o $@
