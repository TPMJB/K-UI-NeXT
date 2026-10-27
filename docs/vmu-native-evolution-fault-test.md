# Evolution black-screen diagnosis

The owner confirmed that build `2bf215f0fb72` plays Evolution's opening FMV
with `--control`, while its virtual-VMU launch stops at MPEG Sofdec before
the movie. This implicates the VMU launch path; it does not yet establish
whether the cause is its memory reservation, hooks, or another runtime
interaction. Grandia II was reported working on `7753391a62e4`.

This package reports the game's previously silent allocation failure. It is
a diagnostic, not a confirmed compatibility fix.

## Install and test

Extract the ZIP into a fresh folder and run:

```sh
python3 personalize.py /run/media/apollonius/Dreamcast --profile KUI/vmu-test/p0005/retail-persistent-profile-0005.bin --game evolution1
```

Use normal mode, without `--control` or `--profile-runtime`. This updates
only the personalized `KUI/apps/games/retail-boot.kui`; existing prepared
cards and saves remain in place. Keep the same Evolution GDI and SCI/Standard,
Auto encoding, CDDA/background loading/RAM patches off, and controller setup.

Launch Evolution and let it reach MPEG Sofdec and the opening movie. Photograph
the entire diagnostic if it appears. If the screen remains black, report that
and roughly how long it stayed there. No new comparison or Grandia/SA2 retest
is needed for this batch.

## What the new report means

The `GAME ALLOCATION FAILED` report shows requested bytes, allocation caller,
and the original game stack. It stops the launch and performs no diagnostic
save or storage write.
The existing virtual-VMU save behavior is otherwise retained, so this is still
a persistent-VMU build rather than a read-only probe.

The new screen is headed `NATIVE VMU RUNTIME FAILURE`, phase `00000012`,
detail `00000001`. Its `KIND VALUE CONTEXT1 CONTEXT2` row contains
`1`, requested bytes, caller and original stack, followed by the lease and
heap geometry.

The allocation report replaces twelve bytes in the existing null-allocation
arm of the exact Evolution executable identified by SHA-256
`ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35`.
That game path already ends in an infinite loop. The successful allocation
path is unchanged. Other executable identities skip this extra patch.

The resident disc reader is unchanged. A proposed second diagnostic for its
destination refusal did not fit the existing low-memory budget and was
removed. Such a refusal remains another possible cause of a black screen;
this retry cannot distinguish it if the allocation report does not appear.

## Current evidence and limits

The opening movie asks the game's normal allocator for a workspace of
`0019ab20` bytes, rounded to `0019b000` by its wrapper. The VMU currently holds
a `00030000`-byte lease. Allocation failure remains a hypothesis until the
console displays the new report; this package does not change that lease.

Execution tests of Evolution's original cache setter cover P1/P2 aliases and
four initial cache modes. They found no FPU/MMU changes in that setter. This
does not model every physical cache interaction and does not rule out all
other causes of the black screen. No cache-mode or FPU-state change is made
in response to that hypothesis.

The earlier first-transfer context diagnostic is retained. A failure there
still reports its exact `CHECK`, `AT/VALUE/EXPECT`, and frame witnesses.
Host and compiled-artifact checks validate the new instrumentation, but
console compatibility remains unverified until this retry.

Virtual A1 and SCI backing remain the current scope. This package adds no
A2, SCIF VMU backing, combined CDDA/VMU, or physical-disc feature.
