/* SPDX-License-Identifier: GPL-3.0-only */
/* CD-only ext4 reader. Mounts read-only; adapter rejects every write.
 * Configuration is separate from the pinned upstream source and documented
 * read-path fixes. */
#ifndef KUI_LWEXT4_CONFIG_H
#define KUI_LWEXT4_CONFIG_H
#define CONFIG_JOURNALING_ENABLE 0
#define CONFIG_XATTR_ENABLE 1
#define CONFIG_EXTENTS_ENABLE 1
#define CONFIG_DEBUG_PRINTF 0
#define CONFIG_DEBUG_ASSERT 0
#define CONFIG_HAVE_OWN_ASSERT 0
#define CONFIG_HAVE_OWN_ERRNO 0
#define CONFIG_HAVE_OWN_OFLAGS 1
#define CONFIG_UNALIGNED_ACCESS 0
#define CONFIG_BLOCK_DEV_CACHE_SIZE 8
#define CONFIG_BLOCK_DEV_ENABLE_STATS 0
#define CONFIG_EXT4_BLOCKDEVS_COUNT 1
#define CONFIG_EXT4_MOUNTPOINTS_COUNT 1
#endif
