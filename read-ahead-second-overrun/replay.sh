#!/bin/sh
# Usage: ./replay.sh /path/to/K-UI-NeXT [output-directory]
# Requires a C11 host compiler with ASan/UBSan; does not need an SH toolchain.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=${1:-.}
repo_dir=$(CDPATH= cd -- "$repo_dir" && pwd)
build_dir=${2:-"$script_dir/replay-build"}
mkdir -p -- "$build_dir"
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
host_cc=${HOST_CC:-cc}
cd -- "$repo_dir"
"$host_cc" -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
 -ffunction-sections -fdata-sections -fsanitize=address,undefined \
 -fno-omit-frame-pointer -fno-pie -no-pie -O2 \
 -I"$repo_dir/include" -I"$repo_dir/src/loader" -I"$repo_dir/tests" \
 -DKUI_NATIVE_READER_DIAGNOSTIC=1 -DKUI_RETAIL_NATIVE_HYBRID=1 \
 -DKUI_SCI_SD_NO_BLOCK=1 -DKUI_GENERAL_SCI_RESUME_FIX=1 \
 -DKUI_RETAIL_NATIVE_DISC_CHECK=1 -DKUI_RETAIL_NATIVE_DRIVE_STATE=1 \
 -DKUI_RETAIL_ASYNC_TEST=1 -DKUI_SCI_STREAM_TEST=1 \
 -DKUI_RETAIL_ASYNC=1 -DKUI_RETAIL_GD_ASYNC=1 -DKUI_RETAIL_TRANSPORT=1 \
 -DKUI_NATIVE_READAHEAD=1 -DKUI_VMU_NATIVE_BACKGROUND=1 \
 -DKUI_NATIVE_REQUEST_TIMING=1 -DKUI_NATIVE_READAHEAD_AUX_HOST_TEST=1 \
 "$script_dir/second_overrun_harness.c" \
 src/loader/native_readahead_aux.c src/core/native_readahead_cache.c \
 src/core/retail_cursor.c src/core/retail_image.c src/loader/sci_stream.c \
 -Wl,--gc-sections -o "$build_dir/second-overrun"
ASAN_OPTIONS=detect_leaks=0 "$build_dir/second-overrun" > "$build_dir/second-overrun.log" 2>&1
cat "$build_dir/second-overrun.log"
