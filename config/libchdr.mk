# SPDX-License-Identifier: GPL-3.0-only
# Build these only for the shell/cache preparation path, never a game module.
LIBCHDR_SOURCES := $(wildcard third_party/libchdr/src/*.c) \
    third_party/libchdr/deps/lzma-26.02/src/LzmaDec.c \
    third_party/libchdr/deps/miniz-3.1.2/miniz.c \
    third_party/libchdr/deps/zstd-1.5.7/zstddeclib.c
LIBCHDR_CPPFLAGS := -Ithird_party/libchdr/include -include third_party/libchdr/kui_config.h
LIBCHDR_HEADERS := $(wildcard third_party/libchdr/include/*/*.h \
    third_party/libchdr/src/*.h third_party/libchdr/deps/*/*.h \
    third_party/libchdr/deps/*/include/*.h third_party/libchdr/deps/*/include/*/*.h) \
    third_party/libchdr/kui_config.h
