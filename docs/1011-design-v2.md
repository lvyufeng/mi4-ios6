# 1011 v2 — the two cells that decide F1a/F1b (design)

**Status:** design only. Nothing here is built or pressed. This supersedes `docs/1011-design.md`
§1.5/§2 *only where marked*; the F4 draft (`tools/patch_d13_abt_retry_bound.py`) stands.

---

## 0. Corrigenda — read this before reusing any offset or the prior cause

### 0.1 The discriminator-gap offsets are WRONG (corrigendum to `docs/1011-discriminator-gap.md`)

The audit's numbers (`protection` at `entry+24`, bits[0:2]; `needs_copy` bit 9; `store` = four
pointers `{16,20,24,28}`; `object`=32, `offset`=36) must not be reused. Derived by value from the
D13 headers and confirmed by a 32-bit freestanding probe:

| field | offset | source (D13) |
|---|---|---|
| `links` (`vm_map_links`: prev,next,start,end — 4×`vm_map_offset_t`) | +0..+16 | `osfmk/vm/vm_map.h:200-206` |
| `store` (`struct vm_map_store { RB_ENTRY(...) entry; }`) | +16..+28 | `osfmk/vm/vm_map_store.h:50-52` |
| `object` (`union vm_map_object`) | +28 | `osfmk/vm/vm_map.h:215` |
| `offset` (`vm_object_offset_t`) | +32..+40 | `osfmk/vm/vm_map.h:216` |
| bitfield word (`unsigned int`) | +40 | `osfmk/vm/vm_map.h:221-241` |
| `needs_copy` | bit 6 of word@+40 | `vm_map.h:227` |
| `protection` | bits[7..9] of word@+40 | `vm_map.h:229` |
| `max_protection` | bits[10..12] of word@+40 | `vm_map.h:230` |
| `vme_start` | +8 | `vm_map.h:204` (`links.start`) |

- `RB_ENTRY(type)` is **three** pointers — `rbe_parent, rbe_left, rbe_right`
  (`libkern/libkern/tree.h:336-342`), so `store` is 12 B, not 16.
- `vm_object_offset_t` is **uint64_t** (`osfmk/mach/vm_types.h:114`), 8-aligned, so it occupies
  +32..+39 and pushes the bitfield word to **+40**, not +28.
- `vm_map_offset_t` is `uint32_t` on this target: `osfmk/mach/arm/vm_types.h:111` takes the
  `#else` (non-`__LP64__`) branch.

**By-value checks (not a claim in a comment).** A 32-bit freestanding probe of the real field
shapes gives `sizeof(struct vm_map_entry)=48`, `offsetof(object)=28`, `offsetof(offset)=32`, and
`protection=5/max_protection=7` landing as `0x1e80` in word@+40 (bit 6 `needs_copy`, bits[7..9]
`protection`, bits[10..12] `max_protection`).

### 0.2 The 1010c record's CAUSE for the (A) defect does NOT hold on the built image

`records/1010c-press-read.txt` originally attributed the wrong-table read to `locore.s:1035-1039`
switching TTBR0 to the kernel ttep under `__ARM_USER_PROTECT__` before `bl sleh_abort`. On the
built artifact that mechanism is **not present** (verified by value):

- The slot the entry installs is `locore_fleh_dataabt` (`entry_vectors.s:239`); the object that
  defines it is Apple's D13 handler `traps_lo.s:411` (`EnterARM(fleh_dataabt)`), whose
  `data_abort_crash_in_usermode` path saves regs / DFSR / DFAR and `blx _sleh_abort` — it does
  **not** write TTBR0.
- `__ARM_USER_PROTECT__` exists only in the 4570 tree
  (`external/xnu-4570.1.46/osfmk/arm/proc_reg.h:97`); `grep` over `external/xnu-hd2-darwin13/xnu`
  finds **zero** hits. D13's `struct machine_thread` (`osfmk/arm/thread.h:64-86`) has **no**
  `uptw_ttb`/`kptw_ttb`. D13's `sleh_abort` (`trap.c:451`) only READS TTBR0 for a diagnostic.

**Consequence:** the mechanism that makes the recorder read a table with no coarse descriptor at
the storm is **OPEN**. The contrast with 1010 is decisive: 1010 (wrong base mask) read
`0x81a86000` slot 0 → `0x81bb1001` (COARSE); 1010c (correct base mask) read `0x81a84000` slot 0 →
`0xfeedface` (section/garbage); the live TTBR0 (`0x81a8605b`) is the same. Cell (A) must therefore
be a *measurement of which base answered*, not a fix built on an assumed cause.

---

## 1. The 1010c frontier (restated from `records/1010c-press-read.txt`)

- **1010c fixed the crash**: the full ladder runs to `xnu_live_abt_pcn=0x00200000` with no FS=5
  walk fault; the 1010b unbounded L2 deref is gone.
- **The alias mechanism works** (once, at `pten=4`): `ptel1=0x46300201` coarse, `pteoow=1`,
  `ptealias=0xa0000000`, `ptemapok=1`.
- **At the storm the walk reads a non-coarse L1**: tail rows show `ptel1=0xfeedface` (section),
  `ptel2=0`, `pteoow=ptemapok=ptealias=0`, `ptettbr=0x81a8605b`. FS=15 (level-2 permission)
  requires a COARSE L1, so the walk returned early at `entry_stubs.c:2291` (`(l1e & 3) != 1`)
  before the L2 deref — which is why the window bound and alias never engaged.
- **(B) is now MEASURED at the storm**: `xnu_live_abt_vmfret=0` (KERN_SUCCESS),
  `vmfprot=0x3` (READ|WRITE — a write fault), `vmfva=0x9a000`. The ~2.1 M retry, 24.4 s freeze,
  is the faithful consequence of the handler's `code == KERN_SUCCESS` silent-retry arm.
- **The discriminator is still unread**: the `vm_map_entry.protection` is published by nothing,
  and F1a vs F1b remains UNDECIDABLE.

---

## 2. The two cells

Discipline for both: every publish is a key on the `entry_write_kv` channel (`entry_stubs.c`),
`xnu_live_abt_*`, on the abort LADDER only (`n <= 4 || power-of-two`) plus a bounded change-count,
so the 2.1 M storm cannot flood the channel.

### 2 (E) — DECISIVE — the entry's effective protection for the faulting VA

#### (E′) wrap `vm_map_lookup_locked` — SIMPLE, no struct offsets, no tree patch  ← RECOMMENDED

`vm_fault.c:2830-2834` (read by value):

```c
kr = vm_map_lookup_locked(&map, vaddr, fault_type,
                          object_lock_type, &version,
                          &object, &offset, &prot, &wired,
                          &fault_info,
                          &real_map);
```

`prot` is an OUT-param (`vm_map.h:503-515`, arg 8). `vm_map_lookup_locked` writes it at
`vm_map.c:9167` (`*out_prot = prot;`), where `prot` was set from `entry->protection` at
`vm_map.c:9061` and, for a read fault on a `needs_copy` entry, demoted at `vm_map.c:9139`
(`prot &= (~VM_PROT_WRITE);`). So `out_prot` **is** the effective entry protection that governs
the CS-block decision — and it is already a materialized local at the call site, so **no
`vm_map_entry` deref and no offsets are needed.**

- `vm_map_lookup_locked` is linker-visible and wrap-able: global definition `vm_map.c:8705-8706`
  (not static), `extern` at `vm_map.h:503`; in the linked image
  `out/stage90/xnu_arm_entry_kernsyms.txt` → `vm_map_lookup_locked T b1c`. The precedent is the
  existing `--wrap=vm_fault` (`entry_trace.c:5810`).
- **Coverage.** `--wrap` rewrites linker-visible cross-object calls. The two call sites that
  matter are both in `vm_fault.o`: `vm_fault.c:2830` (first lookup) and `vm_fault.c:3769` (the
  `RetryFault` verify branch, `fault_type & ~VM_PROT_WRITE`). Both are wrapped; both must be
  covered. Intra-`vm_map.o` calls are not wrapped — acceptable (not the storm path).
- **The edit:** add to `src/entry/entry_trace.c`, beside the `vm_fault` wrapper, under a new
  switch matching the existing gate spelling:

```c
#if defined(STAGE90_XNU_VME_PROT) && STAGE90_XNU_VME_PROT
extern void entry_note_abt_vme(uint32_t kr, uint32_t prot, uint32_t fault_type, uint32_t vaddr);

uint32_t __real_vm_map_lookup_locked(void *var_map, uint32_t vaddr, uint32_t fault_type,
                                     uint32_t object_lock_type, void *out_version, void **object,
                                     void *offset, uint32_t *out_prot, uint32_t *wired,
                                     void *fault_info, void **real_map);

uint32_t __wrap_vm_map_lookup_locked(void *var_map, uint32_t vaddr, uint32_t fault_type,
                                     uint32_t object_lock_type, void *out_version, void **object,
                                     void *offset, uint32_t *out_prot, uint32_t *wired,
                                     void *fault_info, void **real_map)
{
    uint32_t kr = __real_vm_map_lookup_locked(var_map, vaddr, fault_type, object_lock_type,
                                              out_version, object, offset, out_prot, wired,
                                              fault_info, real_map);   /* unconditional pass-through */
    if (out_prot != 0)
        entry_note_abt_vme(kr, *out_prot, fault_type, vaddr);          /* gated by the latch */
    return kr;
}
#endif
```

  and in `entry_stubs.c`, `entry_note_abt_vme` publishes behind the SAME `g_abtpte_latch` on the
  same ladder, with a change-count: `xnu_live_abt_vmekr`, `xnu_live_abt_vmeprot`,
  `xnu_live_abt_vmefault`, `xnu_live_abt_vmeva`, and `xnu_live_abt_vmenx`.
- **Build wiring:** add `-DSTAGE90_XNU_VME_PROT=1` to both `STUB_DEFINES` and
  `STUB_DEFINES_TRACE` and add `--wrap=vm_map_lookup_locked` to `TRACE_LDFLAGS` **only when
  `STAGE90_XNU_VME_PROT=1`**. OFF ⇒ no wrap, Apple's bytes.
- **No `tools/patch_d13_*.py` is required for (E′)**: there is no source anchor and no OFF-identity
  text problem, because nothing in the tree changes. If a `--check` half is wanted, provide a
  checker that asserts the wrapper and the `--wrap` are present-by-value in the hidden elf and
  absent from the OFF/1009 elf.

#### (E) patch `vm_map.c` to store the raw entry — ONLY if max_protection / needs_copy / entry identity are wanted

`out_prot` does NOT carry `max_protection` or `needs_copy` (needs_copy reaches `out_prot` only as
the read-fault demotion at `vm_map.c:9139`). If those are wanted, patch the tree instead:

- **Patch tool:** `tools/patch_d13_vme_prot.py`, structured exactly on the F4 draft: `(old, new)`
  text pairs, `apply`/`--dry-run`/`--check` verbs, idempotent refusal, insertions wrapped in
  `#if defined(STAGE90_XNU_VME_PROT) && STAGE90_XNU_VME_PROT ... #endif` so OFF is Apple's bytes.
- **Source anchor — the REAL D13 line** (`vm_map.c:9061`, read by value):

```c
    prot = entry->protection;
```

  Insert after it (entry is live here; PRE-branch):
  `stage90_note_vme_entry(entry);` publishing `entry->vme_start` and `*(uint32_t*)((char*)entry+40)`.
- **Keys:** `xnu_live_abt_vmeprot` = `(word >> 7) & 7`, `xnu_live_abt_vmemax` = `(word >> 10) & 7`,
  `xnu_live_abt_vmenct` = `(word >> 6) & 1`, `xnu_live_abt_vmeva` = `*(uint32_t*)entry` (`vme_start`).
- **OFF identity:** a by-value checker confirms the inserted form is present in the hidden elf and
  absent from the frozen 1009 elf.

**Ranking:** (E′) is simpler, needs no offset, cannot drift from Apple's layout, and reads exactly
the value that governs — prefer it. (E) is justified only when raw entry fields are needed.

### 2 (A) — CORROBORATING — make the walk read the faulting thread's table

The walk (`stage90_read_l2_pte`, `entry_stubs.c:2227-2324`) reads TTBR0 itself and derives the base
at `:2263` (`base &= 0xffffc000`). The base mask is not the defect (1010b fixed it). The open
question is **which L1 the abort-time TTBR0 points at** — and per §0.2 that is not settled by the
4570 locore citation.

- **(A-i) read the current thread's map/pmap L1 base — SIMPLER, preferred.** Path, offsets
  validated by `tools/check_saved_state_offsets.py`: `thread + STAGE90_ACT_MAP (956)` → `map`;
  `map + STAGE90_MAP_PMAP (44)` → `pmap`; `pmap + 0` → `pm_l1_phys` (`osfmk/arm/pmap.h:99`, first
  field of `struct pmap`). Publish this base as `xnu_live_abt_ptethr` beside `_ptettbr`; the
  comparison `_ptethr` vs `_ptettbr` is the reading. No tree patch.
- **(A-ii) stash the pre-switch TTBR0 in the vector — NOT justified yet.** On the built image the
  handler (`traps_lo.s:411`) does not switch TTBR0 (§0.2), so there is nothing to stash. Rank below
  (A-i); only if (A-i) shows the map/pmap base differs from the live TTBR0.

**ASID:** neither needs kernel-vs-user ASID handling — the walk reads the L1 by physical address
inside the identity window, so only **which physical L1 base** answers is in question.

**Also required by (A):** the `[A]` VERDICT text must be keyed to the observable
(`ptel1 & 3 != 0b01` ⇒ the walk read a table with no coarse descriptor for this VA), and the
`_ptenx` change-count site (never fired) must be fixed.

---

## 3. How F1a/F1b is decided by the cells

Inputs: **(B)** `xnu_live_abt_vmfret` (already measured = 0 at the storm); **(E′)**
`(vmekr, vmeprot, vmefault, vmeva)` for the storm's `vaddr`; **(A)** `(ptel1 type, ptethr, ptettbr)`.

1. **`vmeprot & VM_PROT_WRITE` AND `vmefault & VM_PROT_WRITE` AND the fault recurs** ⇒ the
   effective entry protection GRANTS write and it still faults ⇒ **F1a** (the write is lost on the
   way from `prot` to the PTE — a pmap/enter bug). (A) should then show a COARSE L1 and an L2 leaf
   with APX=1 once the table is right.
2. **`!(vmeprot & VM_PROT_WRITE)`** ⇒ the lookup returned READ-only ⇒ the accept path's
   unconditional `kr = KERN_SUCCESS` (`vm_fault.c:2478`) is the wrong condition ⇒ **F1b** — then
   F4 bound (`tools/patch_d13_abt_retry_bound.py`) + escalation.
3. **`vmekr != KERN_SUCCESS`** ⇒ the lookup refused (anti-grant → `KERN_PROTECTION_FAILURE`); but
   1009 showed ZERO `usermode data abort, EXC_BAD_ACCESS` lines over 2.1 M faults. If this fires,
   the storm is a *different* class and the reading is re-opened (§4 falsifier).
4. **`!(vmefault & VM_PROT_WRITE)`** ⇒ the "write" is the unreliable `DFSR.WnR` on Krait; the fault
   was a READ on a read-only page — re-rank as a CPU/DFSR reading, not a prot bug.

**The audit's caveat, kept explicit:** (B)'s `vmfret=0` **already implies** the entry grants write
(given `vmefault`=WRITE), because a denying entry returns `KERN_PROTECTION_FAILURE` and the handler
would have taken the exception arm. So **(E) is the confirming MEASUREMENT of an inference** — its
value is replacing "the entry must grant write" with the actual `(prot, fault_type)` word pair.

---

## 4. Verification — what a 1011 press must show, and the falsifiers

**Positive:**
- The new keys are present and paired to the storm ordinal (`pcn >= 0x400`):
  `xnu_live_abt_vmekr/vmeprot/vmefault/vmeva`. `vmeva` must equal `vmfva=0x9a000`.
- `vmeprot` is a legal `vm_prot_t` (one of 1..7), and the `[CFFIX]` verdict resolves to exactly one
  of F1a / F1b / re-open — never "TOO SHORT TO CLASSIFY" for a storm that ran to `0x200000`.
- (A): `_ptethr` (or the corrected walk) reads a COARSE L1 (`ptel1 & 3 == 0b01`) for `far=0x9a000`,
  and the L2 leaf is published with a readable APX bit.

**Falsifiers (each is a finding, not a retry):**
- `_ptel1` still `0xfeedface`-class after (A) ⇒ the table base was not the issue; re-read §0.2.
- `vmekr != KERN_SUCCESS` ⇒ the storm is not the F1a/F1b write case.
- `!(vmefault & VM_PROT_WRITE)` ⇒ the "write" is `DFSR.WnR` unreliability.
- The same `(pc=0x2be8a1a0, fsr=0x80f, far≈0x9a000)` storm after the chosen fix ⇒ the fix did not
  reach the object (the by-value check should have refused the build — do not press).
- `_ptenx`/`_vmenx` absent while the ladder is present ⇒ the change-count site did not fire.

**OFF / negative controls:** the OFF elf must be byte-identical to the frozen reference; the 1009
elf is the negative control and must carry NONE of the new strings/immediates.

**Tooling that reads the new keys:**
- `tools/check_1011_capture.py` must gain: (1) a `[CFFIX]` sub-read that parses
  `xnu_live_abt_vmeprot/vmefault/vmekr/vmeva` on the tail rows using the existing helpers; (2) a
  classifier implementing §3's tree; (3) the existing F1b console-line branch retained as
  corroboration; (4) the re-keyed `[A]` VERDICT text and the `_ptenx`/`_vmenx` gates.
- `tools/check_1010_capture.py`: the `[A]` VERDICT re-key (done, `02cd3b4`).
- `tools/check_d13_*.py`: if (E) is taken, a `check_d13_vme_prot.py`; if only (E′), a checker that
  the wrapper + `--wrap` are absent from the OFF/1009 elf.

---

## 5. Explicitly out of scope

- **No fix is built or pressed.** F1a's strip search, F1b's `kr` rewrite, F1c (COW), and F2
  (shared-region) wait on the (E′) word.
- **F4's implementation is not re-designed** — `tools/patch_d13_abt_retry_bound.py` is the standing
  draft.
- **The `__ARM_USER_PROTECT__` path is not implemented** — §0.2 shows it is not the live mechanism.
- **`conf/MASTER`, `pmap.c`, shared-cache, and the commpage alias** are untouched.