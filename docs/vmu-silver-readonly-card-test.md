# Silver: read-only virtual blank card

This test presents a synthetic empty VMU at **A1** to the exact US Silver
executable supplied for analysis. It checks virtual-card discovery, media
information and reads. **Saving and formatting are not enabled.** No SD save
image, journal, imported card or physical VMU is used for its replies.

The existing ABC card (`p0001`), full white-card import (`p0002`) and separate
offline Silver image are not opened. This is not the imported-save test.

## Install and test

1. Back up the installed 2.0.0 `KUI/apps/games/retail-boot.kui` outside the
   `KUI` folder. If an earlier test is installed, keep it separately from that
   usual loader.
2. With the console off, remove **all physical VMUs and controller accessories**.
   Use one ordinary controller in port A. Keep this arrangement until power-off.
3. Copy this ZIP's `KUI/apps/games/retail-boot.kui` over the installed copy.
4. Launch the supplied original, uncompressed raw-sector Silver GDI using
   **SCI**, **Standard** reader, **background loading Off**, **CDDA emulation
   Off**, and **RAM patches Off**.
5. Startup performs diagnostic executable checks and can take a long time.
   Visible checkpoints and progress bars distinguish those checks from a
   stopped game. If the bar advances, let it finish. If it stops, photograph
   the last checkpoint and bar and note roughly how long you waited.
6. If Silver reaches its menu, open **Load Game** or its memory-card screen.
   The virtual card is intentionally empty, so there should be no saved
   profiles to load. Record whether Silver detects a card, reports no saves,
   reports a card error, or asks for a card. Do not select Save, Delete or
   Format; decline any formatting request.
7. Photograph **all four diagnostic pages**, numbered 0 through 3. The test
   stops after at most 36,000 attempted SDK DMA starts, or 18,000 additional starts
   after its first card media/read request. Timing depends on the game's
   schedule rate. An earlier refusal is useful: photograph that screen too.
8. Power off. Restore the backed-up 2.0.0 `KUI/apps/games/retail-boot.kui` before normal play or other games.

The final diagnostic schedule is deliberately not submitted. A stopped report
is the intended result. The card disappears when this test is removed.

The first read-only test stopped at start 733 because Silver had already read
card metadata during startup. That report shows one information reply, one
media reply and 15 block replies, with no refusal or guard faults. This build
extends only the run limits to leave time to reach the menu. At 60 starts per
second the post-read window is about five minutes; the actual rate can vary.

## What this tests

The baseline observer completed 3,600 starts with no malformed packets, stack
faults or invalid receive buffers. It found that Silver uses interrupt mask 6
as well as 15 at the exact normal completion wrapper. This test admits only
those two masks for this exact read-only profile. The assembly interposer
raises the mask to 15 throughout its worker; the original mask is restored
before the original SDK consumer resumes.

Every admitted reply also requires the exact caller, held SDK lock and ready
state, idle software-triggered DMA, unchanged source metadata and selectors,
both fixed receive banks, all 48 live slot pointers and inactive timer callbacks. Unsupported state
stops the proof. The game's own normal submission path refuses re-entry while
completion owns the lock. This does not extend the admission to other masks,
titles, storage modes or arbitrary Maple routines.

Only the absent A1 slot's reply and the main controller's A1-presence bit may
be published. Main-controller discovery and input requests pass through;
other accessories or commands stop the test. A detected physical A1
conflict is refused. Unsupported card write requests stop before their DMA
is submitted; the test does not acknowledge a saved or formatted card.

Page 2 records virtual advertisements/replies, device information, media
requests and block reads. Success requires actual card traffic and replies,
not just the game reaching its title. This is a step toward loading imported
Silver saves; it does not establish persistence, save updates or recovery.

The package audit checks its actual linked code, profile, low heap bootstrap,
exit pointer cell, stack/buffer bounds and integer trampoline preservation.
Host fixtures cover both banks, replies, supported masks, unchanged unrelated
bytes and guard failures. Console behavior still needs this test.
