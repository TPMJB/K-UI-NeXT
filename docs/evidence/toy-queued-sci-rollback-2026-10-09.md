# Queued SCI hardware regression and rollback

On 2026-10-09 the user tested build `71fe85e01bbb` on their Dreamcast.
They reported unchanged intro video and substantially worse audio, with
breaks at several points, and explicitly requested that the change be undone.
The supplied diagnostic photographs identify the tested build. This is a
hardware rejection of the queued SCI candidate; passing host models did not
predict its playback quality. The counters alone do not establish the cause.

## Source rollback

Revert native commits `71fe85e`, `a74bb0f`, and `3e61937` in reverse order.
The resulting code, tests, and build tools match pre-queue checkpoint
`b33e9556e6180549807c989b53138e3e973cf340` exactly. This evidence note is the
only addition to that tree. The failed experiment remains available in Git
history; no branch history is rewritten.

The restored default profile is `GD_FIXED_STEP=2`, `SHARED_SCI=0`,
`ASYNC_CDDA=0`. Older shared SCI experiments remain opt-in and are not the
restored operational runtime. No AICA DMA change was made after the queued
package was delivered; the subsequent SWAT feedback was a read-only audit.

## Runtime rollback

Restore the exact earlier clean-audio runtime, build `7b55156aafa2`, from the
rejected package's `fallback/KUI/apps/games/retail-boot.kui` member. Install it
as `/KUI/apps/games/retail-boot.kui`, with the console off, then cold boot.

- File size: 61,712 bytes.
- SHA-256: `161ea24d655b6531867c6704c57a57c1ee25668e534d2f56dc793a5cd73a80de`.
- Byte-for-byte identical to the original clean eight-block runtime and the
  game runtime retained in the pre-queue integrated checkpoint.
- Retain the existing fixed launcher, build `6c4a9915ba33`.

This differs from the preceding foreground top-up transport experiment,
`dc455cbfa50b`, whose evidence recorded recovery of earlier video performance
but did not explicitly establish uninterrupted audio. The rollback selects
the retained clean-audio profile and makes no claim of improved FMV playback.

## Verification

Before adding this note, the reverted index tree compared equal to `b33e955`.
All 30 existing Toy pilot regression suites passed after the rollback.
The runtime is reused byte-for-byte, rather than rebuilt with a new identity.
