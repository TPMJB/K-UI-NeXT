# K-UI V1.5 "Dáinsleif" — RC1

Version `1.5.0-rc1` is a release candidate for console use and compatibility
checks. It includes the SD launcher, normal Games reader, menu music and
reference catalogues. Only **Dead or Alive 2** has confirmed gameplay on this
reader so far, on the preserved CMD18 baseline. This RC still needs console
acceptance. See [release notes](release-v1.5-rc1-notes.md) for the limits.

Download **`kui-1.5.0-rc1-dainsleif-release-candidate`** from the successful
build. The other artifacts serve these purposes:

| Artifact suffix | Contents |
| --- | --- |
| `release-candidate` | Normal SD files, optional boot CDI and concise install/release notes |
| `sd-update` | Developer SD update including diagnostic fixtures and demo music |
| `bootstrap-cd` | Optional CD refresh only |
| `sd-benchmark` | Separate test payload; installing it replaces normal game launch with a benchmark |
| `diagnostic` | Full build outputs, previews, source archives and dependency/license records |

Every artifact starts with `kui-1.5.0-rc1-dainsleif`. The optional experimental
build adds `-experimental`; it is separate from the normal candidate.

## Install on your existing SD card

1. Keep your working SD update or archived baseline ZIP for rollback.
2. **Merge the supplied `KUI` folder into the SD card's root**, replacing only
   matching supplied files. Preserve the existing folder, preferences, music
   selections and game dumps. The package contains no preference files.
   Update **both** `KUI/runtime.kui` and `KUI/apps/games/retail-boot.kui`
   from this candidate.
3. Boot with your **current working CD**. Confirm `K-UI V1.5 RC1` and the source
   build ID shown by the launcher. The new Dáinsleif splash is in the SD runtime.

`KUI/apps/music` is the menu music; `KUI/apps/games` includes the normal launch
payload and the read probes used by Advanced diagnostics. The two `.db`
catalogues support optional dump comparison. Scan fixtures and the separate
Music demo are not included.

There is **no need to burn another CD**. `boot-cd/kui-v1.5-rc1.cdi` is provided
for an optional replacement disc; burn it as a disc image. It carries the
original K-UI badge below the SEGA logo. Holding B from power-on selects its
built-in CD tools; R starts the existing benchmark runner only when requested.

## Launch an existing game

Open Games, select an owned `.gdi` and press **A** to inspect it. On the detail
screen, press **A** for launch confirmation, then **A** again to launch.
**B** backs out before launch. The **Y** read probe is available for diagnosis
and is not required for this check.

Games reads the SD card without writing it. A running game may write saves
to an attached VMU. **Power off and on to return to K-UI.**

## Next normal-use check

Try **one other owned native GD title** within the documented format limits.
Let it boot, reach normal gameplay, and use an ordinary VMU save/load if the
game supports it, preferably in a new save slot. Report the title and region,
whether those steps worked, and any audio, FMV or loading problems. Photograph
an error screen if one appears. No benchmark or previously passed read probe
needs repeating.

`build.json` identifies the exact source and package contents; `SHA256SUMS`
covers the delivered files. Full corresponding source and dependency records
are in the `diagnostic` artifact from the same workflow run, as noted in
`SOURCE.txt`. This candidate has not been published as a final release.
