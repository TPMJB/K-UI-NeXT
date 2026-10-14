<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Armada CE full CD music stream test

## Playback clock correction

Console build `6f7a47ced587` returned SOURCE `001A82E0`, GAPS `3`, PLAY `3`,
HR `0` and no identity fault. The user heard only a brief sound and reported
heavy, unintelligible FMV echo. This is a failed hardware test.

Cursor observations now use a fresh clock reading after the native position
call returns. Playback start also records time after native Play returns.
Previously both used the earlier worker-entry time, so a delayed native call
could falsely trigger a stop and full refill. Idle refill visits no longer
acquire the shared native sound-service lock.

The return screen retains the last recovery when there is no actual audio
fault. AUDIO WHY `13` means cursor inconsistency, `14` observation age and
`15` underrun; NODE is cursor, VALUE is cursor delta, EXPECT is allowed delta
and DETAIL is elapsed milliseconds. Actual identity/context faults take
precedence. These are hexadecimal values.

Only a focused delayed-call regression and the console compilation/package
checks were run for this change. Console audio and FMV behavior remain
unverified. Test the opening FMV speech and 30 seconds at the menu, then return
with A+B+X+Y+Start if either still fails. No long playtest is requested.

## Native owner read correction

Console build `a79c66682379` reached Armada but returned CONTEXT (`12` in
hex), SOURCE `0002DF00`, WORKER `1` and PLAY `0`. It fetched 188,160 bytes
(80 raw audio sectors) and stopped before the native ring was primed. The
reported CPU/FPU mode passed admission, identifying the live object or method
check as the failure path; that screen did not preserve its exact reason.

The worker now samples retained sound objects through the verified native
owner's virtual address, with the whole bounded read masked against IRQs.
Owner process, slot, handle, table entries and physical pages are checked
before and after. Physical addresses serve as mapping witnesses, rather
than alternative aliases for object values. The existing bootstrap/binder
observer retains its separate cache checks.

Every early failure revokes its owned audio job. A pending reception drains
without new source requests, and the worker exits after releasing its SCI
lease. Invalid sound identity prevents further native sound calls. The
return screen preserves the first precise audio failure in **AUDIO WHY**.

First run a short check: cold boot with CD music On, listen at the menu for
30 seconds, and note whether FMV speech remains intelligible. If music is
still absent or speech echoes, return immediately and capture the full
screen. Longer playback is useful only after this check succeeds. The
console result of this correction remains unverified.

## Bootstrap correction

Build `41f643fa37bd` stopped before Windows CE with **BOOTSTRAP ALTERED
ARMADA BIND RAM**, detail `00007E30`. That offset was the controller's own
transport state: resident initialization attached the shared SCI descriptor
before the stage checked that the controller's uninitialized RAM was zero.
Bootstrap GD initialization could also queue STOP and modify audio state.

The corrected build keeps the controller dormant through bootstrap. After
the existing executable, controller, zero-state, reservation and source-map
checks pass, the stage follows a checked entry in the immutable resident to
arm streaming once. Bootstrap data reads retain their ordinary behavior.
The guard remains enabled. The boot regression test exercises the actual
reader and audio command routes before and after this transition.

First confirm that the corrected build reaches Armada. If it does, continue
the playback checks below. Hardware playback and FMV improvement are still
unverified.

The previous working build, **ecf23db0a902**, demonstrated the short native
stereo loop. This test replaces that fixed excerpt with an experimental
full-track source: Armada's audio commands select track ranges from your
original image, and the controller refills the native sound ring as playback
advances. Music should develop past the earlier two-second sample and follow
the game's track changes.

The controller retains the verified CE sound owner, native owner-process
callback, fresh identity and handle checks, cache/mapping checks and sound
voice admission. Audio and data reads share the SCI card. This is a
console test of the complete streaming path; host and build checks do not
establish hardware playback, uninterrupted sound or FMV performance.

## Install and test

1. With the console off, back up `KUI/runtime.kui` and
   `KUI/apps/games/ce-cdda-armada.kui`.
2. Merge this ZIP's `KUI` folder onto the SCI microSD card, replacing **both
   files**, and cold boot. The menu and optional audio loader show the same
   new build ID.
3. Select **Original Armada → Y Options → CD music On → START**.
4. Listen for **two minutes at the main menu**. Check that music progresses
   beyond the old short sample. Note any silence, repeated short segment,
   gap, crackle or unexpected track restart.
5. Play for **five minutes**, firing guns and checking other effects. Open
   and leave menus, pause and resume gameplay, and note how the music behaves
   during those transitions. Report missing effects, audio cutouts, disc
   errors, freezing or slowdown.
6. If practical, play the same FMV with **CD music Off** and compare it with
   the enabled test. Report whether picture smoothness and sound change.
7. Return with **A+B+X+Y+Start** and send **one full photograph of the cached
   diagnostic screen**, including binding, context, playback, buffer and
   stream/cursor rows, even if music never starts.

No new extraction, private upload or 2048 test is needed. Use the same
original Armada GDI on the SCI card. Saved launch toggles, the game library
and the GitHub link beneath the K-UI logo remain available. The unused
**L Memory** Diagnostics shortcut remains removed.

## Interpret the result

| Result | Meaning |
| --- | --- |
| Music develops and follows track changes | The full-track source is reaching native playback. Duration and transition tests check continued refill. |
| A short portion repeats repeatedly | The buffer may be playing without sufficient source progression; the cached counters help locate it. |
| Silence with a binding, callback or native error | Playback did not pass one of the retained admission/API checks. Send the complete result. |
| Music gaps during data loading | The SCI/shared-read or refill path needs further investigation; note which game action coincided with the gap. |
| Gunfire or other effects disappear | Native game audio has regressed; report the action and send the result. |

The first row is **GD BIND SOURCE GAPS WORKER**. SOURCE counts fetched audio
bytes in hexadecimal and should grow beyond the earlier 352,800-byte clip.
GAPS counts cursor-age or underrun recoveries. WORKER `1` means the native
worker started; other values help identify a startup refusal. The remaining
five rows retain the first audio failure (or binder failure), sound-owner, native Play, buffer and
cursor details. The display remains a single screen.

The audio failure WHY values are hexadecimal: `1` caller SR, `2` FPSCR,
`3` binding, `4` current process, `5` owner slot, `6` owner handle, `7` device
object, `8` buffer object, `9` native method and `A` native clock/context.
NODE, VALUE, EXPECT and DETAIL preserve the address, observed value,
expected value and mapping/read reason where applicable.

Full-stream operation is implemented for this exact Original US Armada
profile. Hardware success is not yet claimed, and other CE games are not
admitted by this package. No universal CE support or FMV improvement is
claimed. The ordinary CE loader and native, Toy Commander and MDK2 loaders
remain as installed. **CD music Off** selects the ordinary CE loader.

## Restore and provenance

To restore the last working excerpt test exactly, merge
`restore-full-stream/KUI` onto the card with the console off and cold boot.
Both backup files are byte-identical build **ecf23db0a902**.

Packaging verifies that exact working ZIP, both installed payloads, every
member's size/SHA256/CRC32, and the retained source archive's file bytes and
modes against its Git commit. Both new payloads must match clean compiled
ELFs from the same final commit, including their compiled and envelope build
IDs. The linked stream controller's memory, stack, native bridges and seed
are audited with the explicit stream profile.

The new loader contains a **64-byte CEX2 seed plus a 4,096-byte all-zero
wire-map placeholder**. Before boot the launcher fills the complete source
map from your selected original image. The new loader distributes no game
PCM. Its 352,768-byte native ring is working memory refilled at runtime,
not the old fixed excerpt. The rollback retains its original all-zero PCM
placeholder.

Current and working ecf23 source archives, complete recovered Git history,
dependency sources, licenses, prior GPL provenance, build/test evidence and
output member manifests are included. Redundant older source tars are
omitted; their exact trees remain in the Git bundle. No private game
executable, CE kernel, ARM firmware or music sample is distributed. The ZIP
must remain below 50 MiB.
