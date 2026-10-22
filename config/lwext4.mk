# SPDX-License-Identifier: GPL-3.0-only
# The bootstrap mounts read-only and never calls formatting/partition writers.
LWEXT4_SOURCES := $(filter-out third_party/lwext4/src/ext4_mkfs.c third_party/lwext4/src/ext4_mbr.c third_party/lwext4/src/ext4_journal.c,$(wildcard third_party/lwext4/src/*.c))
LWEXT4_HEADERS := $(wildcard third_party/lwext4/include/*.h) config/lwext4/generated/ext4_config.h
LWEXT4_CPPFLAGS := -Ithird_party/lwext4/include -Iconfig/lwext4 -DCONFIG_USE_DEFAULT_CFG=0
