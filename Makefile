# SPDX-License-Identifier: GPL-3.0-only
CC ?= cc
HOST_FLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SANITIZERS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
INCLUDES = -Iinclude -I.deps/fatfs/source
CORE = src/core/command.c src/core/data.c src/core/diskio.c src/core/clock.c
LOADER_PROBE = src/core/loader_probe.c
RESIDENT_IMAGE = src/core/resident_image.c
GD_SERVICE = src/core/gd_service.c
DESTINATION = src/core/destination.c src/core/destination_file.c
CAPTURE = $(DESTINATION) src/core/hash.c src/core/capture_plan.c src/core/capture.c src/core/known_dumps.c src/core/timing.c
FATFS = .deps/fatfs/source/ff.c .deps/fatfs/source/ffunicode.c

.PHONY: test test-recovery test-images deps diagnostic clean
test: build/test-recovery-manifest build/scan-fixtures/.stamp build/test-music-ogg-seek
test: build/test-game-image build/test-game-metadata build/test-loader-probe build/test-loader-sd build/loader-probe.dat
test: build/test-resident-image build/test-gd-service build/test-image-client
test: build/test-retail-image build/test-retail-gd build/test-retail-sd
test: build/test-cd-audio build/test-network-probe build/test-network-connect build/test-menu-sound build/test-music-ogg build/test-capture-display build/test-viewport build/test-clock build/test-clock-platform build/test-music-thread build/test-recovery-checks build/test-wav-stream build/test-music-player build/test-startup-sound build/test-splash build/test-gd-play build/test-network-app build/test-system-settings build/test-disc-identity build/test-wav build/test-music build/test-memory-app build/test-core build/test-capture-core build/test-timing build/test-disc build/test-options build/test-ui-rate build/test-known-dumps build/test-crc16 build/test-settings build/test-shell build/test-shell-font build/test-capture-adapter build/test-destination
	./build/test-cd-audio
	./build/test-game-image
	./build/test-game-metadata
	./build/test-loader-probe build/loader-probe.dat
	./build/test-loader-sd
	./build/test-resident-image
	./build/test-gd-service
	./build/test-image-client
	./build/test-retail-image
	./build/test-retail-gd
	./build/test-retail-sd
	./build/test-cd-audio guard
	./build/test-network-probe
	./build/test-network-connect
	./build/test-menu-sound
	./build/test-music-ogg
	./build/test-music-ogg-seek
	./build/test-capture-display
	./build/test-viewport
	./build/test-clock
	./build/test-clock-platform
	./build/test-recovery-manifest build/scan-fixtures/clean/manifest.json
	./build/test-music-thread
	./build/test-recovery-checks
	./build/test-wav-stream
	./build/test-music-player
	./build/test-startup-sound
	./build/test-splash
	./build/test-gd-play
	./build/test-network-app
	./build/test-system-settings
	./build/test-disc-identity
	./build/test-wav
	./build/test-music
	./build/test-memory-app
	./build/test-core
	./build/test-options docs/bench.cfg.example docs/bench-cfgs/*.cfg
	./build/test-ui-rate
	./build/test-known-dumps
	./build/test-crc16
	./build/test-destination
	./build/test-settings
	./build/test-shell
	./build/test-shell-font
	./build/test-capture-adapter
	./build/test-capture-core
	./build/test-timing
	./build/test-disc
	./build/test-disc abort-fail
	./build/test-disc media-change
	./build/test-disc guard-failed
	./build/test-disc guard-before
	./build/test-disc dma
	./build/test-disc dma-fail
	./build/test-disc dma-timeout
	./build/test-disc dma-guard
	./build/test-disc dma-async-stop
	./build/test-disc dma-async-change
	./build/test-disc dma-async-fail
	./build/test-disc dma-async-timeout
	python3 -m unittest discover -s tests -p 'test_*.py' -v

deps:
	python3 tools/fetch_deps.py

build/test-loader-probe: tests/test_loader_probe.c $(LOADER_PROBE) src/loader/client.c include/kui/loader_probe.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(LOADER_PROBE) src/loader/client.c tests/test_loader_probe.c -o $@

build/loader-probe.dat: tools/make_loader_probe.py
	python3 tools/make_loader_probe.py $@

build/test-loader-sd: tests/test_loader_sd.c src/loader/sd_reader.c src/loader/sd_reader.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) -Isrc/loader src/loader/sd_reader.c tests/test_loader_sd.c -o $@

build/test-retail-sd: tests/test_retail_sd.c src/loader/retail_sd.c src/loader/retail_sd.h src/loader/sd_reader.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) -DKUI_RETAIL_SD_TEST -Isrc/loader src/loader/retail_sd.c tests/test_retail_sd.c -o $@

.PHONY: test-retail-fast-io
test-retail-fast-io: build/test-retail-sd build/test-loader-sd-fast build/test-retail-minic
	./build/test-retail-sd
	./build/test-loader-sd-fast
	./build/test-retail-minic

build/test-loader-sd-fast: tests/test_loader_sd.c src/loader/sd_reader.c src/loader/sd_reader.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) -DKUI_RETAIL_FAST_IO=1 -Isrc/loader src/loader/sd_reader.c tests/test_loader_sd.c -o $@

.PHONY: test-retail-sd-bench
test-retail-sd-bench: build/test-loader-sd-fast build/test-retail-sd-bench
	./build/test-loader-sd-fast
	./build/test-retail-sd-bench

build/test-retail-sd-bench: tests/test_retail_sd_bench.c src/loader/retail_sd_bench.c src/loader/retail_sd_bench.h src/core/retail_image.c
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) -Iinclude -Isrc/loader src/loader/retail_sd_bench.c src/core/retail_image.c tests/test_retail_sd_bench.c -o $@

build/retail-minic-test.o: src/loader/minic.c
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) -fno-builtin -DKUI_RETAIL_FAST_IO=1 \
		-Dmemcpy=kui_test_memcpy -Dmemset=kui_test_memset -Dmemmove=kui_test_memmove \
		-Dmemcmp=kui_test_memcmp -Dstrlen=kui_test_strlen -c $< -o $@

build/test-retail-minic: tests/test_retail_minic.c build/retail-minic-test.o
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $^ -o $@

build/test-retail-image: tests/test_retail_image.c src/core/retail_image.c include/kui/retail_image.h include/kui/game_image.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/retail_image.c tests/test_retail_image.c -o $@

build/test-retail-gd: tests/test_retail_gd.c src/core/retail_gd.c include/kui/retail_gd.h include/kui/retail_image.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/retail_gd.c tests/test_retail_gd.c -o $@

build/loader-probe-image: tests/loader_probe_image.c src/apps/games_probe.c $(LOADER_PROBE) src/core/runtime_image.c src/core/runtime_file.c src/core/storage_probe.c $(CORE) $(FATFS) include/kui/games_probe.h include/kui/loader_probe.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Isrc/dreamcast tests/loader_probe_image.c src/apps/games_probe.c $(LOADER_PROBE) src/core/runtime_image.c src/core/runtime_file.c src/core/storage_probe.c $(CORE) $(FATFS) -Wl,--wrap=f_open -Wl,--wrap=f_read -Wl,--wrap=f_lseek -Wl,--wrap=f_close -Wl,--wrap=f_mount -Wl,--wrap=f_write -Wl,--wrap=f_mkdir -Wl,--wrap=f_unlink -Wl,--wrap=f_rename -o $@

build/test-resident-image: tests/test_resident_image.c $(RESIDENT_IMAGE) include/kui/resident_image.h include/kui/game_image.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(RESIDENT_IMAGE) tests/test_resident_image.c -o $@

build/test-gd-service: tests/test_gd_service.c $(GD_SERVICE) include/kui/gd_service.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(GD_SERVICE) tests/test_gd_service.c -o $@

build/test-image-client: tests/test_image_client.c src/loader/image_client.c $(GD_SERVICE) include/kui/image_client.h include/kui/gd_service.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/loader/image_client.c $(GD_SERVICE) tests/test_image_client.c -o $@

GAMES_IMAGE_PROBE = src/apps/games_image_probe.c $(RESIDENT_IMAGE) src/core/game_image.c src/core/game_metadata.c $(DESTINATION) src/core/runtime_image.c src/core/runtime_file.c src/core/storage_probe.c
GAMES_IMAGE_PROBE_WRAP = -Wl,--wrap=f_open,--wrap=f_read,--wrap=f_lseek,--wrap=f_close,--wrap=f_mount,--wrap=f_write,--wrap=f_mkdir,--wrap=f_unlink,--wrap=f_rename
build/games-image-probe: tests/games_image_probe.c $(GAMES_IMAGE_PROBE) $(CORE) $(FATFS) include/kui/games_image_probe.h include/kui/resident_image.h include/kui/game_image.h include/kui/game_metadata.h include/kui/image_loader_layout.h include/kui/runtime.h include/kui/destination.h include/kui/media.h src/dreamcast/platform.h config/ffconf.h .deps/fatfs/source/ff.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Isrc/dreamcast tests/games_image_probe.c $(GAMES_IMAGE_PROBE) $(CORE) $(FATFS) $(GAMES_IMAGE_PROBE_WRAP) -o $@

GAMES_RETAIL = src/apps/games_retail.c src/core/retail_image.c src/core/game_image.c src/core/game_metadata.c $(DESTINATION) src/core/runtime_image.c src/core/runtime_file.c src/core/storage_probe.c
build/games-retail: tests/games_retail.c $(GAMES_RETAIL) $(CORE) $(FATFS) include/kui/games_retail.h include/kui/retail_image.h include/kui/retail_loader_layout.h include/kui/game_image.h include/kui/game_metadata.h include/kui/runtime.h include/kui/destination.h include/kui/media.h src/dreamcast/platform.h config/ffconf.h .deps/fatfs/source/ff.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Isrc/dreamcast tests/games_retail.c $(GAMES_RETAIL) $(CORE) $(FATFS) $(GAMES_IMAGE_PROBE_WRAP) -o $@

build/test-core: tests/test_core.c $(CORE) include/kui/core.h include/kui/media.h config/ffconf.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) tests/test_core.c -o $@

build/test-clock: tests/test_clock.c src/core/clock.c include/kui/clock.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/clock.c tests/test_clock.c -o $@

build/test-clock-platform: tests/test_clock_platform.c src/core/clock.c src/dreamcast/clock.c include/kui/clock.h include/kui/clock_platform.h tests/clock_stubs/arch/rtc.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/clock_stubs src/core/clock.c src/dreamcast/clock.c tests/test_clock_platform.c -o $@

build/scan-fixtures/.stamp: tools/make_scan_fixtures.py tests/make_recovery_vectors.py
	python3 tools/make_scan_fixtures.py build/scan-fixtures
	touch $@

build/test-recovery-manifest: tests/test_recovery_manifest.c src/core/recovery_manifest.c include/kui/recovery_scan.h include/kui/recovery_checks.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/recovery_manifest.c tests/test_recovery_manifest.c -o $@

build/test-capture-core: tests/test_capture_core.c src/core/hash.c src/core/capture_plan.c src/core/data.c include/kui/hash.h include/kui/capture.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/data.c src/core/hash.c src/core/capture_plan.c tests/test_capture_core.c -o $@

build/test-options: tests/test_options.c src/core/options.c include/kui/options.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/options.c tests/test_options.c -o $@

build/test-ui-rate: tests/test_ui_rate.c include/kui/ui_rate.h include/kui/options.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) tests/test_ui_rate.c -o $@

build/test-settings: tests/test_settings.c src/core/settings.c src/core/data.c include/kui/settings.h .deps/fatfs/source/ff.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/settings.c src/core/data.c tests/test_settings.c -o $@

build/test-shell: src/core/clock.c src/apps/system_settings.c include/kui/system_settings.h src/core/destination.c include/kui/destination.h tests/test_shell.c src/core/shell.c src/dreamcast/shell_draw.c src/dreamcast/shell_font.c src/dreamcast/shell_font_data.inc src/dreamcast/shell_art.inc include/kui/shell_font.h src/core/settings.c src/core/data.c include/kui/shell.h include/kui/settings.h .deps/fatfs/source/ff.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/destination.c src/core/shell.c src/core/clock.c src/dreamcast/shell_draw.c src/dreamcast/shell_font.c src/apps/system_settings.c src/core/settings.c src/core/data.c tests/test_shell.c -o $@

build/test-shell-font: tests/test_shell_font.c src/dreamcast/shell_font.c src/dreamcast/shell_font_data.inc include/kui/shell_font.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/dreamcast/shell_font.c tests/test_shell_font.c -o $@

build/render-shell: src/core/clock.c src/apps/system_settings.c include/kui/system_settings.h src/core/destination.c src/core/data.c include/kui/destination.h tests/render_shell.c src/core/shell.c src/dreamcast/shell_draw.c src/dreamcast/shell_font.c src/dreamcast/shell_art.inc src/dreamcast/shell_font_data.inc include/kui/shell.h include/kui/shell_font.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(INCLUDES) src/core/destination.c src/core/data.c src/core/shell.c src/core/clock.c src/dreamcast/shell_draw.c src/dreamcast/shell_font.c src/apps/system_settings.c tests/render_shell.c -o $@

build/test-capture-adapter: src/core/destination.c src/core/data.c include/kui/destination.h tests/test_capture_adapter.c src/dreamcast/capture.c src/dreamcast/platform.h include/kui/capture.h .deps/fatfs/source/ff.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/destination.c src/core/data.c -Itests/stubs -Isrc/dreamcast src/dreamcast/capture.c tests/test_capture_adapter.c -o $@

build/test-known-dumps: tests/test_known_dumps.c src/core/known_dumps.c include/kui/known_dumps.h $(CORE) $(FATFS)
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) src/core/known_dumps.c tests/test_known_dumps.c -o $@

build/test-crc16: tests/test_crc16.c src/core/crc16.c include/kui/crc16.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/crc16.c tests/test_crc16.c -o $@

build/test-timing: tests/test_timing.c src/core/timing.c include/kui/timing.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/timing.c tests/test_timing.c -o $@

build/test-disc: tests/test_disc.c src/dreamcast/disc.c src/dreamcast/platform.h src/core/command.c src/core/data.c $(wildcard tests/stubs/dc/*.h tests/stubs/kos/*.h tests/stubs/arch/*.h) .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -DKUI_EXPERIMENTAL_DMA=1 -Itests/stubs -Isrc/dreamcast src/dreamcast/disc.c src/core/command.c src/core/data.c tests/test_disc.c -o $@

build/capture-image: tests/capture_image.c $(CORE) $(CAPTURE) src/core/storage_probe.c $(FATFS) include/kui/capture.h include/kui/timing.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(CAPTURE) src/core/storage_probe.c $(FATFS) tests/capture_image.c -o $@

build/storage-image: tests/storage_image.c $(CORE) src/core/storage_probe.c $(FATFS) include/kui/probe.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) src/core/storage_probe.c $(FATFS) tests/storage_image.c -o $@

build/runtime-image: tests/runtime_image.c $(CORE) src/core/storage_probe.c src/core/runtime_image.c src/core/runtime_file.c $(FATFS) include/kui/runtime.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) src/core/storage_probe.c src/core/runtime_image.c src/core/runtime_file.c $(FATFS) tests/runtime_image.c -o $@

build/bench-image: tests/bench_image.c $(CORE) $(CAPTURE) src/core/crc16.c src/core/options.c src/core/bench.c src/core/storage_probe.c $(FATFS) include/kui/bench.h include/kui/options.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(CAPTURE) src/core/crc16.c src/core/options.c src/core/bench.c src/core/storage_probe.c $(FATFS) tests/bench_image.c -o $@

build/report-image: tests/report_image.c $(CORE) src/core/storage_probe.c src/core/report.c $(FATFS) include/kui/report.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) src/core/storage_probe.c src/core/report.c $(FATFS) tests/report_image.c -o $@

build/settings-image: tests/settings_image.c $(CORE) $(FATFS) src/core/storage_probe.c src/core/settings.c src/core/settings_file.c src/core/options.c src/core/options_file.c include/kui/settings.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) src/core/storage_probe.c src/core/settings.c src/core/settings_file.c src/core/options.c src/core/options_file.c tests/settings_image.c -o $@

test-images: build/games-retail build/games-image-probe build/loader-probe-image build/games-image build/salvage-image build/maintenance-image build/recovery-scan-image build/clock-image build/test-vmu-app build/system-settings-image build/destination-image build/storage-image build/runtime-image build/capture-image build/report-image build/bench-image build/settings-image
	python3 tests/test_games_retail.py
	python3 tests/test_games_image_probe.py
	python3 tests/test_loader_probe_images.py
	python3 tests/test_games_images.py
	python3 tests/test_salvage_images.py
	python3 tests/test_maintenance_images.py
	python3 tests/test_recovery_scan_images.py
	python3 tests/test_clock_images.py
	python3 tests/test_vmu_images.py
	python3 tests/test_system_settings_images.py
	python3 tests/test_images.py
	python3 tests/test_runtime_images.py
	python3 tests/test_capture_images.py
	python3 tests/test_report_images.py
	python3 tests/test_bench_images.py
	python3 tests/test_known_images.py
	python3 tests/test_capture_options.py
	python3 tests/test_settings_images.py
	python3 tests/test_destination_images.py
	python3 tests/test_named_capture_images.py

build/clock-image: tests/clock_image.c $(CORE) $(FATFS) include/kui/clock.h config/ffconf.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) tests/clock_image.c -o $@

RECOVERY_SCAN = src/core/storage_probe.c src/core/destination.c src/core/hash.c src/core/capture_plan.c src/core/recovery_manifest.c src/core/recovery_scan.c src/core/recovery_sector.c src/core/known_dumps.c
build/recovery-scan-image: tests/recovery_scan_image.c $(CORE) $(FATFS) $(RECOVERY_SCAN) include/kui/recovery_scan.h include/kui/recovery_checks.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) $(RECOVERY_SCAN) tests/recovery_scan_image.c -Wl,--wrap=f_rename -Wl,--wrap=f_close -Wl,--wrap=f_read -o $@

diagnostic:
	$(MAKE) -f Makefile.dc

clean:
	rm -rf build dist


build/test-destination: tests/test_destination.c src/core/destination.c src/core/data.c include/kui/destination.h .deps/fatfs/source/ff.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/destination.c src/core/data.c tests/test_destination.c -o $@

build/destination-image: tests/destination_image.c $(CORE) $(DESTINATION) $(FATFS) src/core/storage_probe.c src/core/settings.c include/kui/destination.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(DESTINATION) $(FATFS) src/core/storage_probe.c src/core/settings.c tests/destination_image.c -o $@

# App modules are host-checkable independently of the frozen reader.
build/test-system-settings: tests/test_system_settings.c src/apps/system_settings.c src/core/data.c include/kui/system_settings.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/apps/system_settings.c src/core/data.c tests/test_system_settings.c -o $@

build/system-settings-image: tests/system_settings_image.c $(CORE) $(FATFS) src/core/storage_probe.c src/apps/system_settings.c src/apps/system_settings_file.c include/kui/system_settings.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) src/core/storage_probe.c src/apps/system_settings.c src/apps/system_settings_file.c tests/system_settings_image.c -o $@

build/test-disc-identity: tests/test_disc_identity.c src/apps/disc_identity.c src/core/data.c src/core/hash.c include/kui/disc_identity.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -DKUI_ON_CONSOLE=1 -Itests/identity_stubs -Isrc/dreamcast src/apps/disc_identity.c src/core/data.c src/core/hash.c tests/test_disc_identity.c -o $@

build/test-wav: tests/test_wav.c src/apps/wav.c include/kui/wav.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/apps/wav.c tests/test_wav.c -o $@

build/test-music: tests/test_music.c src/apps/music.c src/apps/wav.c src/apps/music_ogg.c include/kui/music.h include/kui/wav.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -DKUI_MUSIC_ALLOC_TEST=1 -Itests/stubs -Isrc/dreamcast src/apps/music.c src/apps/wav.c src/apps/music_ogg.c tests/test_music.c -Wl,--wrap=malloc,--wrap=free -lm -o $@

build/test-music-thread: tests/test_music.c src/apps/music.c src/apps/wav.c src/apps/music_ogg.c include/kui/music.h include/kui/wav.h tests/stubs/kos/mutex.h tests/stubs/kos/thread.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -DKUI_MUSIC_ALLOC_TEST=1 -DKUI_ON_CONSOLE=1 -Itests/stubs -Isrc/dreamcast src/apps/music.c src/apps/wav.c src/apps/music_ogg.c tests/test_music.c -Wl,--wrap=malloc,--wrap=free -lm -o $@

build/test-memory-app: tests/test_memory_app.c src/apps/memory_pattern.c include/kui/memory_test.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/apps/memory_pattern.c tests/test_memory_app.c -o $@

build/test-network-app: tests/test_network_app.c src/apps/network_test.c src/apps/network_status.c include/kui/network_test.h $(wildcard tests/apps_stubs/kos/*.h tests/apps_stubs/dc/*.h tests/apps_stubs/dc/net/*.h)
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/apps_stubs src/apps/network_test.c src/apps/network_status.c tests/test_network_app.c -o $@

build/test-vmu-app: tests/test_vmu_app.c $(CORE) $(DESTINATION) src/core/storage_probe.c $(FATFS) src/apps/vmu.c include/kui/apps.h $(wildcard tests/vmu_stubs/dc/*.h tests/vmu_stubs/dc/maple/*.h)
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/vmu_stubs -Isrc/dreamcast $(CORE) $(DESTINATION) src/core/storage_probe.c $(FATFS) src/apps/vmu.c tests/test_vmu_app.c -o $@

# Original startup assets are encoded on the host, never decoded during ripping.
build/splash_pixels.inc build/startup_pcm.inc &: tools/build_splash.py resources/branding/startup.png
	python3 tools/build_splash.py

build/test-splash: tests/test_splash.c src/apps/splash.c include/kui/splash.h build/splash_pixels.inc
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/apps/splash.c tests/test_splash.c -o $@

build/test-gd-play: tests/test_gd_play.c src/apps/gd_play.c include/kui/gd_play.h tests/gd_play_stubs/arch/arch.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/gd_play_stubs src/apps/gd_play.c tests/test_gd_play.c -o $@


build/test-wav-stream: tests/test_wav_stream.c src/apps/wav.c include/kui/wav.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/apps/wav.c tests/test_wav_stream.c -o $@

build/test-music-player: tests/test_music_player.c src/apps/music_player.c src/apps/wav.c src/core/destination.c src/core/data.c include/kui/music_player.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/stubs -Isrc/dreamcast src/apps/music_player.c src/apps/wav.c src/core/destination.c src/core/data.c tests/test_music_player.c -o $@

build/test-startup-sound: tests/test_startup_sound.c src/apps/startup_sound.c include/kui/music.h build/startup_pcm.inc
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/stubs -Isrc/dreamcast src/apps/startup_sound.c tests/test_startup_sound.c -o $@

# Recovery checks: sector validation is used by Advanced CRC; CRC replacement stays host-only.
build/recovery-vectors.inc: tests/make_recovery_vectors.py
	@mkdir -p $(@D)
	python3 $< > $@.tmp
	mv $@.tmp $@

build/test-recovery-checks: tests/test_recovery_checks.c tests/make_recovery_vectors.py build/recovery-vectors.inc src/core/recovery_crc.c src/core/recovery_sector.c src/core/data.c include/kui/recovery_checks.h include/kui/core.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) -Iinclude -Ibuild src/core/recovery_crc.c src/core/recovery_sector.c src/core/data.c tests/test_recovery_checks.c -o $@

test-recovery: build/test-recovery-checks
	./build/test-recovery-checks

build/test-capture-display: tests/test_capture_display.c src/core/capture_display.c include/kui/capture_display.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/capture_display.c tests/test_capture_display.c -o $@

build/test-viewport: tests/test_viewport.c src/core/viewport.c include/kui/viewport.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/viewport.c tests/test_viewport.c -o $@

build/test-music-ogg: tests/test_music_ogg.c src/apps/music_ogg.c include/kui/music_ogg.h third_party/stb/stb_vorbis.c tests/fixtures/music_vorbis.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/stubs -Isrc/dreamcast src/apps/music_ogg.c tests/test_music_ogg.c -lm -o $@

build/test-music-ogg-seek: tests/test_music_ogg_seek.c src/apps/music_ogg.c include/kui/music_ogg.h third_party/stb/stb_vorbis.c
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/stubs -Isrc/dreamcast tests/test_music_ogg_seek.c -lm -o $@

build/test-menu-sound: tests/test_menu_sound.c src/apps/menu_sound.c include/kui/menu_sound.h $(wildcard tests/menu_sound_stubs/dc/sound/*.h)
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/menu_sound_stubs src/apps/menu_sound.c tests/test_menu_sound.c -o $@

build/test-network-probe: tests/test_network_probe.c src/apps/network_probe.c include/kui/network_probe.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/apps/network_probe.c tests/test_network_probe.c -o $@

build/test-network-connect: tests/test_network_connect.c src/apps/network_probe.c src/apps/network_connect.c include/kui/network_probe.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/apps_stubs src/apps/network_probe.c src/apps/network_connect.c tests/test_network_connect.c -o $@

build/maintenance-image: tests/maintenance_image.c $(CORE) $(FATFS) src/core/storage_probe.c src/apps/maintenance.c include/kui/maintenance.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) src/core/storage_probe.c src/apps/maintenance.c tests/maintenance_image.c -o $@

build/salvage-image: tests/salvage_image.c $(CORE) $(FATFS) src/core/salvage.c src/core/recovery_crc.c src/core/recovery_sector.c src/core/hash.c src/core/storage_probe.c include/kui/salvage.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) $(FATFS) src/core/salvage.c src/core/recovery_crc.c src/core/recovery_sector.c src/core/hash.c src/core/storage_probe.c tests/salvage_image.c -Wl,--wrap=f_write,--wrap=f_sync,--wrap=f_read,--wrap=f_rename -o $@

build/test-cd-audio: tests/test_cd_audio.c src/apps/cd_audio.c src/core/command.c src/core/data.c include/kui/cd_audio.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Itests/cdda_stubs -Itests/stubs -Isrc/dreamcast src/apps/cd_audio.c src/core/command.c src/core/data.c tests/test_cd_audio.c -o $@

# Games uses a read-only image backend separate from the accepted optical reader.
build/test-game-image: tests/test_game_image.c src/core/game_image.c include/kui/game_image.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/game_image.c tests/test_game_image.c -o $@

build/test-game-metadata: tests/test_game_metadata.c src/core/game_metadata.c include/kui/game_metadata.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/game_metadata.c tests/test_game_metadata.c -o $@

GAMES = src/apps/games.c src/core/game_image.c src/core/game_metadata.c src/core/destination.c src/core/storage_probe.c
GAMES_WRAP = -Wl,--wrap=f_open,--wrap=f_read,--wrap=f_lseek,--wrap=f_close,--wrap=f_write,--wrap=f_mkdir,--wrap=f_unlink,--wrap=f_rename,--wrap=f_opendir,--wrap=f_readdir,--wrap=f_closedir
build/games-image: tests/games_image.c $(GAMES) $(CORE) $(FATFS) include/kui/games.h include/kui/game_image.h include/kui/game_metadata.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) -Isrc/dreamcast $(GAMES) $(CORE) $(FATFS) tests/games_image.c $(GAMES_WRAP) -o $@
