# 1011 discriminator GAP — the vm_map_entry cell (C/E) is not instrumented

READ-ONLY analysis (2026-10-14). Verdict: **F1a vs F1b is NOT decidable from the 1010/1010c cells
alone.** Design §2's third cell — the `vm_map_entry`'s `protection`/`needs_copy` for the faulting VA —
is captured by **nothing** in `src/entry/`.

## What IS published (by value, read from source)

`entry_note_abt_pte(va)` (`src/entry/entry_stubs.c:2339`) — the (A) L2 walk — publishes, on the
1008/1009 ladder (`n<=4 || power-of-two`): `xnu_live_abt_pten/pteva/ptel2/ptel1/ptettbr/pteu/pteoow/
ptealias/ptemapok/pteslot/ptewbase/ptewlen` (`:2372-2383`) + change-count `ptenx` (`:2393`).

`entry_note_abt_vmf(ret, va, prot)` (`:2407`) — the (B) return — publishes, on the SAME ladder:
`xnu_live_abt_vmfn/vmfret/vmfva/vmfprot` (`:2425-2428`) + change-count `vmfnret` (`:2432`).

**No key carries a `vm_map_entry` field.** `grep -rn "vmeprot|vm_map_entry|needs_copy|max_protection|
->protection|entry->prot" src/entry/*.c src/entry/*.h` → **0 hits**. Neither entry TU includes a kernel
VM header (`osfmk/…/vm_map.h`); they link by symbol against the pool objects only.

## Why this is THE deciding datum (design §1.3, re-verified by value in the BUILT tree)

The built tree is `external/xnu-hd2-darwin13/xnu` (NOT `xnu-4570.1.46`, which §1.3's line numbers
`2353/1881/1904/3855` correctly cite; the same lines exist in D13).

Every `prot &= ~VM_PROT_WRITE` in `xnu-hd2-darwin13/xnu/osfmk/vm/vm_fault.c` is guarded by a
NON-write condition: `:1881`, `:1904`, `:2353` under `(fault_type & VM_PROT_WRITE) == 0`; `:3855`
under `m->object->copy != old_copy_object` (object moved). `vm_fault.c:3769` (`fault_type &
~VM_PROT_WRITE`) is the `RetryFault` verify branch. **No strip fires for a write fault.** §1.3 holds.

So on `pmap_enter`'s `prot` LACKING WRITE, the two candidates are:
- **F1b** — the anti-grant test `vm_map.c:9076` `(fault_type & prot) != fault_type` passed because
  `fault_type` chose READ (so the entry text-scanned at `:9139` for a read-only COW fault, or the
  entry's `protection` genuinely denies write but the lookup fault_type was READ).
- **F1a** — `prot` had WRITE across the CS block (`:2565` PMAP_ENTER_OPTIONS, then `arm/pmap.c`
  `pmap_vm_prot_to_page_flags` → APX=1), and the page entered WRITE into the PTE. Then the retry is
  **not a prot bug** — the handler re-faulted and got a WRITE the PTE refuses.

These are separated ONLY by reading the entry's `protection`. §1.5's "the `:2435` block is never
entered (F1b)" is one branch; §1.2's accept path (F1a) is the other.

## Consequence — a result sharper than the design's two cases

If the entry DOES deny write and a WRITE fault reaches the lookup, `vm_map.c:9076` returns
`KERN_PROTECTION_FAILURE` (2) → `vm_fault.c:2836 goto done` → `return kr` (`:4058`) → trap.c:834 is
false → the handler prints `usermode data abort, EXC_BAD_ACCESS …` (`trap.c:871-875`) and SIGBUSes.
**1009's capture has ZERO such lines over 2.1 M faults.** Therefore the entry does NOT plainly deny
the write — so if the PTE is APX=1 (user-RO) the fault_type reaching the lookup was **READ (0x3)**,
not write (0x5). The "write" is the hypervisor's `DFSR.WnR`, which is unreliable on Krait. That makes
(C)/(E) — the entry's protection **+ needs_copy** — *the* discriminator, and it re-ranks F1b: a
read-only fault on a read-only page is not a retry-storm at all unless the CPU keeps writing.

## The minimal third cell (E) — spec

- **Where to read it:** the OS is NOT the place — `struct vm_map_entry` (`vm_map.h:207`) and
  `vm_map_lookup_entry` (`vm_map.c:1541`) are kernel-internal. `__wrap_vm_fault`'s args name it but
  cannot deref it. The first lookup call site is the vm_fault prologue, **before** any
  `vm_fault_enter`/`vm_fault_page` branch (the 1010 §IP-relative fault hazard). Emit the run's only
  publish there.
- **Record site:** ESCAPE `__wrap_vm_fault`'s pass-through with `__real_vm_fault(..., NULL …)` — but
  the wrapper is a PURE PASS-THROUGH by design (`entry_trace.c:5794-5804`); passing a non-NULL
  `physpage_p` changes vm_fault's behavior at `:2387`/`:3863`. Instead detect the storm ONCE (the
  latch, `entry_note_abt_pte`) and record on the NEXT wrapped call. Add an instrument store to
  `entry_trace.c` (kernel half, like `entry_note_sleh`).
- **Record instruction (at the first-lookup site):** `str r7, [r6, #entrecord_off]` where r7 holds the
  `vm_map_entry_t` the lookup is about to inspect (already live in the prologue) and r6 the
  instrument address; the pool object's own archive member (per 1011 §2's "patch by text, applied to
  the pool object" mechanism).
- **Hook instruction (in `stage90_read_l2_pte` / `entry_note_abt_pte`):** read the published record —
  `ldr r?, [r6, #entrecord_off]`, skip if 0, else publish.
- **Publish (following the `xnu_live_abt_` literal set + `entry_write_kv`):**
  `xnu_live_abt_vmeprot` = word at entry+24 (bits[2:0]=`protection`, bits[5:3]=`max_protection`,
  bit[9]=`needs_copy`); and a matched-pair decode `xnu_live_abt_vmeva` (the entry's `vme_start`,
  `*(u32*)entry`, to PROVE the record is the faulting entry). Offsets are byte-exact for D13:
  `links{VACD}={0,4,8,12}`, `vm_map_store{VACD}={16,20,24,28}` (`VM_MAP_STORE_USE_RB`,
  `vm_map_store.h:39`; empty → 4×4), `object`=32, `offset`=36, flags word=40.
- **Trigger / gating:** publish on the (A) ladder ordinals only, exactly as `entry_note_abt_pte`
  does (`:2349`,`:2371`) — 2.1 M aborts must not fill the channel; bound the copy count (`_vmeprotx`
  change-count, `_ptenx`'s rule).
- **In the (`__PTE__`) guard:** `#if defined(STAGE90_XNU_ABT_PTE) && STAGE90_XNU_ABT_PTE`, matching
  `entry_stubs.c:2114`.
- **Checker:** `tools/check_1011_capture.py` currently adjudicates F1a/F1b ONLY from F1b's console
  line (`:3627-3705`); it never reads an entry-protection cell (0 hits). Add a `[CFFIX]` verdict from
  `_vmeprot`: read-only word (`prot&VM_PROT_WRITE==0`) → F1b; write-granting word with a matching
  `_ptel2` APX=1 → F1a; `needs_copy` bit set → F1c.
- **Prove the patch lands:** config-magic (a per-instrument compile-time constant echoed into a
  published key) + a build-time by-value check that the immediate is in the linked image (the
  1000–1010 by-value-checker discipline).

## Also a GAP: the (B) cell has never been read at the STORM

- 1010's four `_vmf` rows are VAs `0xcfdbd000` (kernel stack), `0xcfdfe000` (kernel stack),
  `0x2bdc3000`, `0x2be3d000` — **none is the storm's `far`** (`0x5d000`/`0x9a000`). They are the
  pre-storm prefix; the first-4 bound stopped at `vmfn=4` (`records/1010-press-read.txt:37-48`).
- 1010b **did not reach the storm** — it died at launchd-start with only 4 abort records
  (`records/1010b-press-read.txt:14-30`), so its extended ladder published nothing at the storm.
- 1010c (`b1535d4`) is written but **has no capture record** — not yet pressed.

So the storm's `vmfret` is, to date, an EXTRAPOLATION from a different process's pre-storm fault, not
a measurement at the storm. Any 1011 claim resting on it repeats `mi4-measurement-defects` ("the
evidence was not from the thing it is said to be from").