# 681: the line carrying the reset-reason word had the other address's name on it

674 named a mapping as load-bearing for the arm the next press sends: `stage90_candidate_l1` already maps
`0x0fa00000`, so the entry image's seam can reach `RESTART_REASON` with no install of its own and 663 §3's
"one more section install" was unnecessary. That claim is what makes 678's arm correct, and it had been read
off the doc rather than off the source. This step reads it off the source. **The claim holds.** The line that
carries it was also the one line in the region whose comment named **a different megabyte**, and that is the
whole of the change: one comment corrected, one mapping documented, and the payload's bytes proven unmoved.

**No device was touched, nothing was flashed, nothing was written to storage, and no press is armed.**

## 1. The claim, checked against the source

`stage90_xnu_arm_vm_init_full_pmap_run` builds the table that `xnu_handoff.c:318` installs into TTBR0
(`handoff_write_ttbr0(r->candidate_l1_base)`) and keeps live across the no-return handoff. In that table:

```
line 453   map_l1_section_mmio(stage90_candidate_l1, 0x0fa00000u, 0x0fa00000u);
```

* **Unconditional** — it sits in Phase 5 with no branch, no `#if`, and no guard around it.
* `MSM_IMEM_BASE_PHYS` is `0x0fa00000u` (`stage90.h:17`) and `RESTART_REASON` is that base plus `0x65c`
  (`stage90.h:18`) — the word `platform_reboot` writes and the entry image's `entry_epilogue` writes
  (`entry_stubs.c:4227-4234`), which is how every returning run comes back.
* It lands in `stage90_candidate_l1`, so the byte is reachable from anything running on the handed-off
  tables — including `entry_seam_flush`, whose 678 ending writes that word from that context.

The same fact is stated twice more in the tree, and both were checked rather than trusted: `mmu.c:5156` maps
the same base into the TTBR0 round-trip table (`ttbr_map_section_pa(stage90_ttbr0_roundtrip_l1,
MSM_IMEM_BASE_PHYS, MSM_IMEM_BASE_PHYS)`), and `entry_reset.h:33` spells the same word as the literal
`0x0fa0065c` — necessarily, because the entry image cannot include `stage90.h`. **So the one value has two
definitions, and that is structural here rather than a defect**; what is a defect is when one of them is
*wrong*, and one of them was.

## 2. The defect: the name was on the line that does not carry the word

```
-    map_l1_section_mmio(stage90_candidate_l1, 0xfa000000u, 0xfa000000u);         /* MSM IMEM */
+    map_l1_section_mmio(stage90_candidate_l1, 0xfa000000u, 0xfa000000u);
```

`0xfa000000` and `0x0fa00000` are different megabytes — the first has no leading `f`, the second does. The
comment claimed the IMEM for the line **one row above** the line that actually maps the IMEM, which is
`0x0fa00000`. So an operator looking for where the reset path's word becomes reachable would have found the
name on the wrong line and the right line unlabelled.

This is the project's most expensive defect class — **one value, two definitions** (m699 and the memory file
it lives in) — in its cheapest possible form: only prose was wrong, no instruction moved. It is worth
repairing anyway, and precisely here, because the arm about to be fired ends the run by writing that word.
The wrong half of the pair was silent; the right half was the one with no name on it.

What `0xfa000000` **is** for is not established by this step and the replacement comment says so rather than
guessing: the entry image's own GIC probe maps `0xf9000000` live (Phase 4's line below it), and this VA has no
reader in either image. Naming it would be inventing a fact at the end of a step about a different one.

## 3. The edit, and why it is provably inert

Two comment blocks, no code. The payload build is the proof:

```
STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh

  SAME  stage90-qcdt.img          SAME  stage90.img
  SAME  stage90.bin               SAME  SHA256SUMS.txt
  SAME  stage90.elf               SAME  stage90-build-config.txt
```

All six, against `out/stage90/frozen/armed-seam-endrun-88972ba9/`. `tools/resolve_arm_set.sh out/stage90`
still answers `armed-seam-endrun-88972ba9  94c95342...  8540160`, which is the arm the tree was already on.

## 4. The mistake this step made, and the park doing exactly its job

The first rebuild was run **without** the arm's own flags:

```
./build.sh                      # no STAGE90_EXTRA_CFLAGS
```

That is not a rebuild of this arm. Without `-DSTAGE90_XNU_ENTRY=1` the payload comes out with
`STAGE90_XNU_ENTRY 0u` — **a different arm** — and `./build.sh` writes into `out/stage90/` in place, so the
live tree silently became that other arm.

Nothing was fired and nothing was lost, because the park exists for this: all 11 files were copied back with
plain `cp -r`, and `tools/verify_revert_set.sh` then reported `resolve_arm_set`'s answer above, with the
record and the park unmoved. **The recovery cost one `cp -r` and no device.**

Two things this fixes in the record:

* **The payload build IS reproducible** — byte for byte, when the arm's own flags are passed. So the 408
  narration that a rebuilt payload does not reproduce is not a property of the build; it is a property of
  running `build.sh` **without** the arm's flags, which produces a different and equally valid arm whose
  bytes are, correctly, different. That distinction is reported to the peer lane, because the gate's
  narration is where it matters.
* **The order that would have avoided it**: the inertness question was answerable without ever touching
  `out/` — build to a scratch directory, or hash first and rebuild in place only after the park is known
  good. `build.sh` in place is the shortcut this step took, and the failure mode it has is that the arm
  changes and the tool that would notice (`resolve_arm_set.sh`) is not on the path.

## 5. What this changes about the press

Nothing mechanical. The press is still blocked by exactly one thing, and it is still the peer lane's single
`STAGE90_XNU_SEAM_END_RUN` name in `ENTRY_CFG_KEYS` (678 §6, 679 §6, 680 §5). Readiness is green on four of
its five rows and red on that one.

What it changes is the confidence behind 678's design. The arm ends the run by writing `RESTART_REASON` and
then `PS_HOLD`, and the reason that is allowed to work from inside the seam — without installing a mapping of
its own — is that the handed-off L1 already covers `0x0fa00000`. **That is now read from the source and from
the two other places the same value is written down, and the line that carries it is labelled.**
