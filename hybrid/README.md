# REJECTED on console — native hybrid 1920659698ae

The owner reported a severe regression after installing this candidate: the intro video took about a minute longer than usual and audio glitched roughly every half second. Gameplay was not reached. Association follows the supplied build and test sequence; no separate runtime build screen was supplied.

Do not use this candidate as a performance baseline or merge it as a fix. The former host tests proved work limits and exact data, but did not establish sufficient service rate or movie playback deadlines. The capped CHECK path and shared 1,024-poll EXEC allowance are being removed from the next experiment.

For the previously smooth Background FMV behavior, restore the retained [74958b387827 package](../builds/K-UI-Native-Background-Bounds-74958b387827.zip?raw=true), using the same existing profile and game selection. That prior build still has the owner's reported gameplay stalls; it is a rollback, not a gameplay fix.

The rejected binary, original reports and exact source/history remain preserved for reproducibility. TEST-EVIDENCE.json records the earlier software results only; this console failure supersedes its pending hardware status. No save/load result is claimed for this test.

## Exact source and history

From this branch's repository root, run:

```sh
python3 hybrid/restore-hybrid.py
```

The script restores the original 74958b387827 checkpoint, imports the incremental bundle, and verifies source commit 1920659698aeec78c1e812bbb76b2b9274f7558d and tree 569869f7b50ec1096c1cfb4a19664ad9e5f37973. Original commit identities are retained. No user card, profile, journal, saves or retail game files are included.
