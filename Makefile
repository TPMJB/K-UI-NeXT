# SPDX-License-Identifier: GPL-3.0-only
CC ?= cc
HOST_FLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SANITIZERS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
INCLUDES = -Iinclude -I.deps/fatfs/source
CORE = src/core/command.c src/core/data.c src/core/diskio.c
FATFS = .deps/fatfs/source/ff.c .deps/fatfs/source/ffunicode.c

.PHONY: test test-images deps diagnostic clean
test: build/test-core
	./build/test-core
	python3 -m unittest discover -s tests -p 'test_*.py' -v

deps:
	python3 tools/fetch_deps.py

build/test-core: tests/test_core.c $(CORE) include/kui/core.h include/kui/media.h config/ffconf.h .deps/fatfs/source/ff.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) tests/test_core.c -o $@

build/storage-image: tests/storage_image.c $(CORE) src/core/storage_probe.c $(FATFS) include/kui/probe.h
	@mkdir -p build
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) src/core/storage_probe.c $(FATFS) tests/storage_image.c -o $@

build/runtime-image: tests/runtime_image.c $(CORE) src/core/storage_probe.c src/core/runtime_image.c src/core/runtime_file.c $(FATFS) include/kui/runtime.h
	@mkdir -p $(@D)
	$(CC) $(HOST_FLAGS) $(SANITIZERS) $(INCLUDES) $(CORE) src/core/storage_probe.c src/core/runtime_image.c src/core/runtime_file.c $(FATFS) tests/runtime_image.c -o $@

test-images: build/storage-image build/runtime-image
	python3 tests/test_images.py
	python3 tests/test_runtime_images.py

diagnostic:
	$(MAKE) -f Makefile.dc

clean:
	rm -rf build dist
