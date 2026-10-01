# Crimson Dáinsleif boot artwork

`boot-red.png` is the separate 640×480 RGB CD/Card boot artwork. It uses a new
visor portrait, crimson light and chrome K-UI lettering. The original runtime
`startup.png` and startup sound remain separate. No version number is baked
into this artwork; software draws its own version/build. The owner approved
this artwork on 2026-09-30 and plans to carry Dáinsleif into release 2.0 after
SCI testing/tuning. The current software version remains 1.5.1.

The built-in image-generation tool created a new composition using `startup.png`
as the identity reference. The full-resolution original is retained separately
as `K-UI-Dainsleif-Boot-Art-Source.png`; the repository contains the exact
deployment PNG used by builds. The complete prompt is retained in
`boot-red-prompt.txt`. No lettering or illustration was edited after generation.
Deployment conversion gives the 4:3 image TV-safe margins:

```sh
convert K-UI-Dainsleif-Boot-Art-Source.png -filter Lanczos -resize 576x432 \
  -background '#090102' -gravity center -extent 640x480 -strip \
  -define png:color-type=2 resources/branding/boot-red.png
```

- Source PNG: 1448×1086 RGB; SHA-256 `d4b67cc589009c776bff4488d0206dc71abc3efce566b0248a624abeed1314fe`.
- Deployment PNG Git blob: `f2b9132613a527f90759fdb0fd70d47a6eb07563`.
- Deployment PNG SHA-256: `f1cfdd82dba67518af18fe208a1334f18dc0bee2041590f9ade2af0f2c3549e1`.

`tools/build_splash.py` verifies the deployment PNG and converts it to RGB565.
Only the bootstrap links this new array. Menu darkening reads the pixels from
RAM and writes each destination pixel once, without reading the framebuffer
or adding a second full-screen artwork buffer. The countdown uses the full
brightness artwork. The separate badge beneath the Sega logo is unchanged.
