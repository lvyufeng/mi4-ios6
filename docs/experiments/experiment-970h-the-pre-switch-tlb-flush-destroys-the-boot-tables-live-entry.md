# Experiment 970h — the pre-switch TLB flush destroyed the boot table's live identity entry

**Status:** host-side COMPLETE and BUILT. The D13 entry image is `299ee994` (was 970g's `321e3332`),
the payload is rebuilt around it, and the arm `armed-d13-299ee994` is **PARKED** (verify_press_ready
5/5, `make check` 0). **NOT PRESSED** — the press is the operator's.

Supersedes 970g as the whole cause. Continues `experiment-970-the-window-is-ram-not-just-address.md`
§6g (970g). 971 (`experiment-971-the-console-dies-at-arm-vm-init.md`) is the **next** rung, staged but
not built into this arm.

---

## 1. 970g was FALSIFIED, and this is why

970g (`tools/patch_d13_boot_path.py`) restored the D13 `_start` boot path — removed the MMU fast path
and zeroed the boot table after the TTBR0 write, so the table is HIGH and the console's L1 slot is
free. The prediction was: the D13 line becomes OBSERVABLE for the first time (`xnu_live_console` and
the first ~15 keys).

**The press produced ZERO output.** Capture `970g-armed-d13-321e3332-20261010-last_kmsg.txt`
(294656 B, 3932 lines): **no `xnu_live_*` key at all**, and the log ends at

```
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000000
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
```

So the boot path *was* built (the fast path was gone, verified by value in the linked ELF), and the
console slot was free — yet nothing ran, or nothing that could write ran. 970g was not the cause.

## 2. The cause: 970g made live an instruction 4570 does not have

970g's edits left ONE instruction in the D13 boot path that 4570's `_start` does not have — a
whole-TLB invalidate that runs **before** the TTBR0 switch:

```
    /* Clean TLB and instruction cache. */
    mov     r4, #0
    mcr     p15, 0, r4, c8, c7, 0      <-- D13 (locore.s:85): the pre-switch TLB invalidate
    mcr     p15, 0, r4, c7, c5, 0
    mcr     p15, 0, r4, c2, c0, 2      <-- TTBCR, a different operation, stays
```

**The payload hands off with the MMU ON and caches OFF** (`sctlr_before = 0x00c5487b`: SCTLR.M=1,
bit 2 C=0, bit 28 TRE=0), TTBR0 = the payload's own table at PA `0x6c4000`. The payload's L1
**identity-maps** `[0x80000000, 0x81000000)` (`src/mmu.c:5506`, `STAGE90_XNU_ENTRY_IDENTITY_LIMIT`
= 16 MiB), so at the jump VA `0x80a00000` — the boot table, `topOfKernelData` — there is a **valid
identity TLB entry** (VA == PA) at the moment `_start` is entered.

D13's `c8,c7,0` at `:85` **destroys that entry**. Then 970g's zeroing loop's first store —
`str r3, [r5], #4` to VA `0x80a00000` — must WALK the just-switched (stale) boot table. The table
itself is DRAM: its L1 slot index `0x80a` holds garbage, not a descriptor → **translation fault**. At
that instant `cpsid if` is already set (`locore.s:55`) and VBAR is still the **payload's** (D13 sets
its own only at `:235`, after the table is built); the payload's vector page is not mapped by the new
table, so the abort-vector fetch itself faults: a **double fault**, no handler, no print, watchdog
reset. That is the whole-D13-line silence.

## 3. Why 4570 boots — the positive control

4570's `start.s` `_start` performs **no** TLB maintenance before its TTBR0 write (`start.s:152`); its
only `c8,c7,0` is at `start.s:337`, in `join_start`, **after** the boot table is built. Its first
post-switch store — the `invalidate_tte` loop's `str r11, [r5]` to the **same** VA `0x80a00000`
(`start.s:166-170`) — does not fault, which is only possible if a **live identity entry survived the
switch** (ARMv7 does not auto-invalidate the TLB on a TTBR write). 4570's success is therefore
positive proof that the boot-table write rides a surviving identity entry — exactly the entry D13's
pre-switch flush removes. The verifier's sharp corroboration: both trees' first post-switch store
targets the same stale table word, so 4570's success and D13's silence differ by exactly this one
instruction.

## 4. The fix

Remove the pre-switch whole-TLB invalidate, exactly as 4570's `_start` has none. D13 already performs
a safe whole-TLB invalidate **after** the table is built (`locore.s:274`, in `mmu_initialized` — the
same place 4570 does it), so the removed one is redundant. The TTBCR write just below stays.

- `tools/patch_d13_boot_path_970h.py` — the idempotent edit (anchor matched whole; the replacement's
  comment token is the "already applied" marker; asserts exactly ONE `r4, c8, c7, 0` remains).
- `tools/stage_d13_boot_path_970h.sh` — the thin, named applier (`external/` is re-provisionable).
- `tools/check_d13_boot_path_970h_staged.sh` — re-derives the property **by position** (no `c8,c7,0`
  between `mmu_reinitialize:` and `mmu_initialized:`; the post-build one remains), wired into
  `make check`. Verified to FAIL on a reverted tree.

Scope: **one instruction removed in `osfmk/arm/locore.s` alone.** 970g's two edits remain. The 4570
tree is untouched.

## 5. By-value verification (the LINKED image)

`out/stage90/xnu_arm_entry.elf`, `mmu_reinitialize` @ `0x80000008`:

| VA | 970g (`321e3332`) | 970h (`299ee994`) |
|----|-------------------|-------------------|
| 0x8000000c | DACR (`cr3,cr0`) | DACR (`cr3,cr0`) |
| 0x80000018 | **`cr8,cr7` — the pre-switch flush** | **`cr7,cr5` — I-cache only** |
| 0x8000001c | TTBCR (`cr2,cr0,{2}`) | TTBCR (`cr2,cr0,{2}`) |
| 0x80000048 | TTBR0 write (`cr2,cr0`) | TTBR0 write (`cr2,cr0`) |
| 0x80000060 | `_970g_zero_tte` | `_970g_zero_tte` |
| 0x8000012c | `cr8,cr7` (in `mmu_initialized`) | `cr8,cr7` (in `mmu_initialized`) — **remains** |

`diff` of the two images' boot-path disassembly is exactly the one removed instruction (and the
addresses it shifts). The entry records are **byte-identical except the SHA256/BYTES fields** (the
change is a tree edit, not a switch), and the payload's own switch record is byte-identical
(`6c2b6038`) — the payload's sources are unchanged; only the embedded entry copy moved. Verified: the
embedded copy in `stage90.bin` (offset `0x78a3c`) hashes to the built entry `299ee994`.

## 6. What the press must show

`make check` 0; `verify_press_ready.sh` 5/5; the press sends `f3b082ba…` (`stage90-qcdt.img`).

- **If the cause is right:** `xnu_live_console` appears and the first ~15 keys (the init block and the
  first wrapped probes, which fire inside `arm_init` **before** `arm_vm_init`) appear. Keys **after**
  `arm_vm_init` are **not** expected yet — D13's `arm_vm_init.c:352-353` bzeroes a FRESH `cpu_ttb`
  where 4570 bcopy's the boot table (971, a later rung).
- **If it is still zero:** the cause is below `_start` too; recorded, not papered over.

## 7. Scope / non-goals

- **The 3 GB** is not touched (958's `max_mem` report and 915-B's low-bank pmap port are separate).
- **The `arm_vm_init` boot-table copy** is 971, staged, **not** in this arm.
- **Reversible:** `fastboot boot` only, never flash; the card base is never written (`CARD_COW=1`).
  The resident rung (`POST_END_TICKS=0`) budgets a black screen + a power-cycle capture
  (`mi4-xnu-reboot-path-cannot-reset`). **PRESS IS THE OPERATOR'S.**

## 8. Arm bookkeeping

`armed-d13-299ee994` (entry bin `299ee994`, payload rebuilt around it). All ten members parked in
`out/stage90/frozen/armed-d13-299ee994/` (`SHA256SUMS.txt`), the set recorded in
`records/revert-set.txt`. The live entry bin is the recorded one:
`resolve_arm_set` names `armed-d13-299ee994` from `stage90-qcdt.img` `f3b082ba…`.