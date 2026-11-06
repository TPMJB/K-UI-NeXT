# VC2 temporary storage ownership must not fabricate drive errors

The active VMU provider can borrow SCI while the foreground GD reader has no
pending command. The production loan test demonstrates that successful idle
claim and the dedicated `active=2` owner. A concurrent ordinary CE driver call
therefore needs the borrower's temporary-busy convention, independently of any
collection Return or stale state from an earlier game.

The old high GD gate parked only FN0 requests. All other functions entered the
low legacy busy branch. That branch returns `-1` for FN4 DRIVE. In the exact
owner-supplied VC2 WSEGACD driver, the call returns at RVA23CE, saves that result
in R9, restores its previous kernel mode, then tests R9 at RVA23D6. A nonzero
result branches to RVA23F0 and returns `C0000185`, a permanent I/O-device error.
No drive-status retry exists in that path. A temporary VMU read could therefore
become a permanent game-loading error even on the first VC2 launch.

The correction extends VC2's existing ordinary-stack Sleep(1) wait to the eleven
other source-admitted control call sites in that same WSEGACD owner. It parks
the original arguments, including output pointers, waits for loan2 to finish,
then retries before the low dispatch trace and executes the real operation.
The source, function, return site and unmasked normal CE context must match;
FN0 also retains its exact command check. No success result is invented.

CHECK/FN1 retains status4: the original driver dispatches that status to its
Sleep(5) retry at RVA1C0C. EXEC/FN2's result is ignored by the source caller.
These two functions retain their existing legacy path. The remaining exact
control sites cover INIT, DRIVE, DMA transfer/check, ABORT and PIO callback,
transfer and check. Extended38/39 streams retain `pending=1` between pieces,
so the loan already excludes those active stream intervals.

The four-scalar admission ABI is retained: its fourth scalar packs entry source
in the low byte and function above it. Original R4 through R7 remain parked and
are restored for the real dispatch. The gate remains192 bytes at ACFCE000;
redundant GBR restoration is removed only after both poll entries already
restored it. Two existing delay slots perform caller-owned stack adjustments.
Adversarial argument-home, volatile-register and GBR tests cover both changes.
The independently accepted Swirl implementation remains byte-exact through its
existing preprocessor profile. Native workers, return controllers, graphics
copy code, reservations and collection options/Save Game are unchanged.

`tests/test_vc2_ce_vmu_gd_control_collision.py` consumes actual old and candidate
helper/resident ELFs and the locally supplied original kernel. It executes the
old linked gate and legacy busy instructions, then the original DRIVE error
branch. That negative must produce `FFFFFFFF` then `C0000185`. Candidate linked
gates execute all eleven control sites across multiple authored release delays;
the admission callback is the production C implementation. The linked gate,
input files and original source window are hashed in the report. Original game
bytes or disassembly are never emitted.

The request/control scalar test covers all eighteen FN0 sites and eleven
controls, including mismatched source/function/return site and forbidden SR.
The original gate regression was observed failing before the production edit:
DRIVE went to `legacy` instead of `retry`. The corrected native gate tests pass.
The loan host test executes production C with modeled cache/SR operations.

This establishes an active-runtime arbitration defect. The SH integer model,
modeled Sleep release and source driver branch do not reproduce the complete
console scheduler or prove this is the only cause of the reported white/black
screens. Console acceptance remains outstanding; the package must retain its
real new helper identity and run the actual linked collision check before any
new delivery is described as verified.
