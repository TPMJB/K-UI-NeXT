# FTP upload slowdown: completed listing connections

The owner's capture on the `1da58d6` W5500 build covered 103.108077 seconds,
12,895,813 packets and 1,020,628,510 bytes. Analysis used exact per-connection
100 ms counters across the entire recording, plus packet headers for each
connection's first 12 seconds of data. Repeated packets after FIN/RST and
traffic later than 12 seconds were sampled; no sample-size budget was hit.
Raw captures and file listings are not committed.

## Evidence

Five directory listings completed their FIN exchange, then repeatedly sent
ACKs answered by client RSTs. The first excerpt alone contained 9,996 console
ACKs and 9,995 client resets in about 67 ms. Advancing console IP IDs show
new transmissions, not duplicated capture entries. The 100 Mbit/s link was
carrying roughly 148,000 of these packets per second in each direction.

Times below are seconds from the beginning of the recording. Rates are
approximate network payload rates, not SD-write benchmarks.

| Connection | Listing storm | Upload start | Upload behavior |
| --- | --- | --- | --- |
| Listing port 50001, upload port 50002 | 12.077–22.077 | 22.143 | Starts after storm; settles around 830 KiB/s immediately |
| Listing port 50003, upload port 50004 | 42.164–52.196 | 45.495 | Around 264 KiB/s, then 103–126; recovers around 830 as storm ends |
| Listing port 50005, upload port 50006 | 71.162–81.172 | 73.899 | Around 262 KiB/s, then 98–126; recovers around 830 as storm ends |

The first listing's storm lasted from 0.024 to 10.026 seconds. The final
listing was still storming when capture ended. The completed storms last
almost exactly the existing ten-second data-socket drain deadline.

For upload port 50004, the storm ends at 52.196 s; subsequent 100 ms bins
rise from about 11 KiB to 35 KiB and then 128 KiB. Port 50006 recovers just
after 81.172 s in the same manner. Both later uploads were affected, although
the owner initially noticed only the second.

The fast upload also has short zero-window periods. Long zero-window pauses
are not the distinguishing behavior. During the completely retained first
12 seconds, the second upload slows without repeated payload sequence ranges;
the third has three repeated segments totaling 4,096 bytes. Retransmissions
therefore do not explain the slowdown by themselves.

This strongly implicates the retired listing connection's packet storm. It
does not expose the W5500's internal Sn_SR register; TIME_WAIT is a targeted
workaround to verify on hardware, not a hardware-proven root-state diagnosis.

## First test and hardware result

`data_poll()` now explicitly closes unowned, draining W5500 data sockets in
TIME_WAIT, after the FIN exchange. Other closing states retain the existing
deadline. A failed status read or CLOSE no longer silently clears cleanup
tracking, and `data_take()` does not reuse a socket after failed forced CLOSE.
Diagnostic logs record close-state transitions and successful explicit closes.
The loop holds the asynchronous stream before accessing these registers.

This shortens TIME_WAIT for finished FTP data sockets, which use rotating
passive ports. It does not change active uploads, control connections, music,
SD writes or DMA timing. This branch is based on the tested W5500 build; a
later port to the Wi-Fi branch must restrict the workaround to W5500 sockets.

One focused host regression checks handshake-state gating, ownership, failed
register operations, deadline cleanup and one-time logging. It cannot simulate
or prove elimination of the hardware packet storm.

The owner tested `6968f755` and supplied an untruncated diagnostic log. No
TIME_WAIT cleanup occurred: every retired listing remained in **CLOSING
(`1A`)** and was closed only at the existing deadline (10,014–10,088 ms).
Each slow upload recovered immediately following those closes. Upload
averages were 698, 631 and 620 KiB/s, while active card-write rates remained
836, 842 and 843 KiB/s. Completed upload sockets closed normally through
LAST_ACK (`1D`) then CLOSED (`00`), or were already CLOSED when polled.
The initial TIME_WAIT-only workaround therefore missed the observed state.

## Correction: bound the observed CLOSING state

Only unowned, gracefully draining data sockets are force-closed after 250 ms
continuously observed in CLOSING. The timer begins on the state transition,
not on the earlier DISCON request. TIME_WAIT is still reaped immediately;
FIN_WAIT/LAST_ACK retain the original overall ten-second deadline. Failed
status reads or CLOSE preserve cleanup responsibility. Logs now include both
total close time and time in the observed state.

This limit is a hardware workaround, not a claim that CLOSING means a fully
completed TCP handshake. Listings and downloads enter this cleanup only after
P_DRAIN confirms SEND completion and a fully acknowledged transmit buffer;
uploads enter it only after peer FIN, drained receive data and file
finalization. The tradeoff is a shorter final FIN/ACK retry period, rather
than dropping unacknowledged application payload. The existing forced socket
reuse already permits cutting a retired connection short after one second.
250 ms bounds the demonstrated storm without retaining another full second
of network saturation. Normal-state progression and failure retention are
covered by the focused regression; this correction still needs console
confirmation.

## Hardware check

Use the SD-update artifact from the test commit. Browse a directory and upload
the same existing test file two or three times promptly, without waiting ten
seconds between listings and uploads. Save Diagnostics after stopping FTP.
Look for `closed from 1A ... state 250 ms (CLOSING timeout)` (allowing normal
polling/scheduling delay) and steady initial rates. If logs instead reach a
ten-second `deadline` close, inspect the recorded state; do not assume the
workaround succeeded. A bounded packet capture
can then confirm whether the ACK/RST storm disappeared if needed.
