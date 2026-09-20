# SPDX-License-Identifier: GPL-3.0-only
CC ?= cc
HOST_FLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SANITIZERS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
INCLUDES = -Iinclude -I.deps/fatfs/source
CORE = src/core/command.c src/core/data.c src/core/diskio.c
CAPTURE = src/core/hash.c src/core/capture_plan.c src/core/capture.c src/core/known_dumps.c src/core/timing.c
FATFS = .deps/fatfs/source/ff.c .deps/fatfs/source/ffunicode.c

.PHONY: test test-images deps diagnostic clean
test: build/test-core build/test-capture-core build/test-timing build/test-disc build/test-options build/test-ui-rate build/test-known-dumps build/test-crc16
	./build/test-core
	./build/test-options docs/bench.cfg.example docs/bench-cfgs/*.cfg
	./build/test-ui-rate
	./build/test-known-dumps
	./build/test-crc16
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
	python3 -m unittest discover -s tests -p 'test_*.py' -v

deps:
	python3 tools/fetch_deps.py

build/test-core: tests/test_core.c $(CORE) include/kui/core.h include/kui/media.h config/ffconf.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) tests/test_core.c -o $@

build/test-capture-core: tests/test_capture_core.c src/core/hash.c src/core/capture_plan.c src/core/data.c include/kui/hash.h include/kui/capture.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/data.c src/core/hash.c src/core/capture_plan.c tests/test_capture_core.c -o $@

build/test-options: tests/test_options.c src/core/options.c include/kui/options.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) src/core/options.c tests/test_options.c -o $@

build/test-ui-rate: tests/test_ui_rate.c include/kui/ui_rate.h include/kui/options.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) tests/test_ui_rate.c -o $@

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

test-images: build/storage-image build/runtime-image build/capture-image build/report-image build/bench-image
	python3 tests/test_images.py
	python3 tests/test_runtime_images.py
	python3 tests/test_capture_images.py
	python3 tests/test_report_images.py
	python3 tests/test_bench_images.py
	python3 tests/test_known_images.py
	python3 tests/test_capture_options.py

diagnostic:
	$(MAKE) -f Makefile.dc

clean:
	rm -rf build dist
