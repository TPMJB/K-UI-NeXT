<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Worms Armageddon 2048 DATA: three cold starts

On 2026-10-15 the owner reported that the diagnostic Worms Armageddon module
booted the converted 2048-byte DATA image on three full cold starts with
**CD music On**. This updates the recorded hardware evidence for that exact
converted US release; it does not identify an additional diagnostic build or
claim zero underruns, a measured frame rate, or complete game compatibility.
The previously working diagnostic Worms binary is preserved unchanged.

The launcher had deliberately retained an all-raw requirement for its tested
label after adding cooked-image launch support. Consequently the successful
2048 copy still displayed **Experimental / untested**. The evidence check now
recognizes its exact converted layout as a tested version too:

- US Worms Armageddon, product `T40601N`, original eight-track starts and ends.
- All three DATA tracks contain 2048-byte Mode1 payloads with zero data/file
  offsets; every AUDIO track retains its original 2352-byte sectors and length.
- The original IP fingerprint and complete unchanged `0WINCEOS.BIN` kernel SHA
  must still match; title text alone cannot supply a tested label.

The evidence check leaves mixed raw/cooked DATA, changed audio geometry,
modified IP/kernel bytes, other releases and other cooked WinCE titles
experimental. It changes the launcher status, not its audio worker, SCI
reader, runtime admission rules, performance settings or diagnostic module.
The host evidence tests cover this narrow addition and rejection of altered
storage layouts and content fingerprints; host tests do not replace the
owner's hardware result.
