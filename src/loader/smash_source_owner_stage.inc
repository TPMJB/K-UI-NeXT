/* SPDX-License-Identifier: GPL-3.0-only */
/* Complete source identity is required even when the SDK's proved lifetime
 * excludes a reclaimed suffix. Only invocation-local fresh hashes qualify. */
#include "kui/hash.h"
#include "kui/smash_native_child.h"
bool __attribute__((noipa)) kui_smash_native_menu_full_source_owner(void) {
    if(manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES)return false;
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,(const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,manifest.boot_bytes);
    kui_sha256_digest(&hash,digest);
    return kui_smash_native_child_engine_owner(manifest.boot_bytes,digest);
}
extern bool kui_smash_native_menu_cached_full_source_owner(void);
static bool smash_native_complete_source_owner(void) {
    if(manifest.boot_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES ||
       native_driver.load_address!=KUI_RETAIL_EXEC_ADDRESS || native_driver.image_bytes!=exec_bytes ||
       !exec_bytes || exec_bytes>manifest.boot_bytes)return false;
    if(exec_bytes==manifest.boot_bytes)
        return kui_smash_native_child_engine_owner(exec_bytes,native_driver.image_sha256);
    return kui_smash_native_menu_cached_full_source_owner();
}
