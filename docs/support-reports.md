<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Support reports

Open **Diagnostics → START → Create problem report**. X toggles **Include latest
screenshot**; A exports. Wait for a confirmed saved path. B stops an export.
Y opens **SD card in browser**, where A explicitly starts read-only downloads.

Each completed report is a new directory such as:

`/KUI/reports/support/REPORT-000001/`

Existing reports are retained. Exporting does not clear the live Diagnostics
log or change the source launch reports, screenshots, game files or saves.

## Contents

| File | What it establishes |
| --- | --- |
| `session-log.txt` | A frozen snapshot taken when Export is queued, with runtime/build and log-truncation information. The bounded live log holds up to 1,500 lines; this is not an unlimited boot history. |
| `module-headers.txt` | Header-derived build, payload size, memory size and recorded CRC32 for a fixed list of runtime/loader packages. Missing or invalid headers are identified. Payloads are not reread, so this is not a full module-integrity check. |
| `last-launch-a.txt`, `last-launch-b.txt` | The fixed prior launch-preparation reports, when present and within their size bounds. They describe preparation, not successful gameplay or a clean return. |
| `screenshot.bmp` | Optional copy of the latest numbered K-UI menu BMP, selected by saved date/number. This is an existing saved capture, not a new capture taken by report export. It may be absent when no suitable capture exists. |
| `manifest.json` | Report schema `kui.support-report.v1`, version/build, RTC validity/local timestamp, included source-file byte lengths and CRC32 values, and the count of unavailable optional inputs. |

The exporter uses fixed inputs. It does not open game images, virtual-card
contents/journals, Wi-Fi/FTP password files, or arbitrary card directories. The log
and optional screenshot can still contain filenames or other displayed text;
review them before sharing.

## Completion and cancellation

Export starts in `REPORT-000001.part`. Every output is created fresh, synced,
closed and reread for exact length/CRC comparison before its name is finalized
inside that staging directory. The manifest is written last. Only after all
checks pass is the directory renamed to `REPORT-000001` and a completed path
published. The runtime also requires filesystem release to succeed before
showing that path as confirmed.

Stop, read/write failure or an unconfirmed release produces an explicit result;
it never claims a completed report merely because some bytes were written.
An interrupted `.part` directory may remain for inspection. The next export
uses another fresh name. The browser rejects `.part` files and directories,
including direct requests to them. Do not rename one into a completed report.

Missing optional launch reports or a missing screenshot are counted rather
than invented. A real input/storage error can fail export. The report-directory
and screenshot searches each have a 4,096-entry bound; many retained reports or
unfinished directories may require copying old material to a computer first.

## Retrieving a report

Choose **Y SD browser**, then **A Start server**, or open **Network → SD card in
browser → A**. Use the displayed LAN URL from a phone/PC and select **Problem
reports**. Download the files from the completed report’s directory. This first
browser version downloads individual files; it does not generate a ZIP folder.
Alternatively power off, copy the complete report directory from the SD card,
and ZIP that directory on your computer.

Include the game/disc, storage adapter, reader/VMU choices, action taken and
observed symptom. For lag, include elapsed time or a short video. A CRC-checked
export and successful host tests do not establish physical-console acceptance,
gameplay compatibility, or power-loss behavior of a particular SD card.
