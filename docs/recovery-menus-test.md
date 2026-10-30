# Recovery menu update — @BUILD@

This GUI update reorganizes disc recovery around the task. It retains the
tested single-block CMD17 SD fix and full saved-salvage revalidation.

## Finish the current operation first

Let the verification already running on the console finish and save its
report. Install this update after the console is idle and powered off; keep
the saved salvage job and its files in place.

## Install

Mount the SD card on the PC, extract the ZIP into a new folder, and run from
the folder containing `install.py`:

```sh
python3 install.py /run/media/apollonius/Dreamcast
```

Use the actual mount path if different. The installer is at the ZIP root.
Only `KUI/runtime.kui` is replaced, with a checkpoint of its predecessor.
Native game modules, VMUs, preferences, and recovery jobs are preserved. The
same command with `--restore` rolls back the GUI update.

## New layout

**Disc tools** directly offers copying, damaged-disc recovery, saved-dump
verification, and playing the inserted disc.

**Recover a damaged disc** opens a dashboard with:

- **Continue recovery**: continue the existing matching job, including
  unfinished collection or subsequent retries of unresolved sectors.
- **Start a new recovery**: the existing confirmation and new-job behavior.
- **Recovery options**: firmware retries, pass limit, completed-file
  verification, optional speed request, and explicit unread-range deferral.
- **Diagnostics**: full saved-progress verification, SD read comparison,
  bounded recovery trial, speed support, and direct G1 testing.

When a previously admitted job is known to be blocked, the primary action
offers **Check saved progress**. A full check has an explicit confirmation
and explains that it may take tens of minutes. It does not automatically
start optical recovery afterward.

The dashboard summary is a cache of observed progress, not recovery
authority. It starts unknown after boot and is discarded when the disc
context changes. No counts are invented before a job has been admitted, and
navigation does not scan saved files or start disc reads. The existing
identity and storage checks still run when an operation starts.

**B** returns from options and diagnostics to the dashboard, then to Disc
tools. During an operation it requests the existing safe stop. Saved jobs
keep their current folders and formats; named job browsing is a later update.
During normal recovery, **Y** switches between the simple progress display
and technical details. It does not change settings or start another action.

## Console check

After the earlier verification has completed, check the new direct entry,
options, diagnostics, and Back navigation. Changing an option or opening a
page should not begin recovery. If the earlier full verification passed and
cleared its block, **Continue recovery** continues the existing Power Stone
job. It retains saved repairs and works on the unresolved sectors.

If verification failed or the dashboard requests a saved-progress check,
keep the job intact and send the automatic diagnostic report. The menu
reorganization does not bypass the recovery guard or change repair evidence.
