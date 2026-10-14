# 1010 — WHY the user write permission-fault is never dismissed (design)

## 1. The frontier statement (what 1009 leaves open)

1009 (`records/1009-press-read.txt`, arm `armed-d13-24997fc5`) named the storm **by value**: from
ordinal `0x400` through `0x200000` (~2.09 M aborts) the ladder is **constant** —

| key | value | decoded |
|---|---|---|
| `xnu_live_abt_pc` (`pcn 0x400…0x200000`) | `0x2be8a1a0` | the faulting instruction, in the dyld shared cache (`0x2be…`) |
| `xnu_live_abt_fsr` | `0x0000080f` | **WnR=1 (write) \| fault-status 7 = PERMISSION fault** |
| `xnu_live_abt_abtcpsr` | `0x60000030` | **USER mode (0x10)**, IRQ enabled |
| `xnu_live_abt_pcfar` | `0x0009a000` | a **low user VA**, below the TTBR0 split `0x40000000` (and below the `0x80000000` address-space boundary) |

So the store at `pc=0x2be8a1a0` to user VA `far=0x9a000` takes a **WRITE permission fault
(`dfsr=0x80f`)** and is **re-executed ~2 M times**. The fault address slides between runs (1008 read
`0x7b000`) — the *class* is invariant, the *low address* is not.

**What 1009 does NOT say is WHY the fault is never dismissed.** The port's handler
(`external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:812 sleh_abort`, user data-abort branch) is:

```
:829  code = vm_fault(map, trunc(dfar),
                       (dfsr & 0x800) ? (VM_PROT_READ|VM_PROT_WRITE) : VM_PROT_READ,
                       FALSE, THREAD_UNINT, NULL, trunc(0));
:834  if (code != KERN_SUCCESS && code != KERN_ABORTED) {
:871      printf("usermode data abort, EXC_BAD_ACCESS at 0x%08x …");     /* ← PRINTS */
          ...
      } else {
:906      ml_set_interrupts_enabled(TRUE); return;                      /* ← SILENT, retry */
      }
```

The handler **prints on the failure branch and is silent on the retry branch**. And the capture
proves the run took the silent branch: the **last XNU-console line in the whole 30,463-line capture is
`CODE SIGNING: cs_invalid_page(0x2e1a9000): p=1[launchd] clearing CS_VALID` (`:4130`)** — XNU's
`printf` *does* reach this log (kext-scan, `IOS7SHREG`, `IOS7LAB`, `mini4:` lines are all there), yet
**not one `usermode data abort` line appears over 2.1 M faults**. (The wrapper's own `sleh` keys are
*silent by construction* — they are written only at `g_sleh_armed`'s bounded sites, exhausted long
before the storm; `nz` counts distinct, not occurrences. So the silence is not a plumbing gap: the
handler chose the retry.)

**Therefore `code == KERN_SUCCESS || code == KERN_ABORTED`.** `vm_fault` claims it satisfied the
fault; the hardware keeps permission-faulting the same writable-requested store. That is a
**vm_map-vs-PTE disagreement** — one of the two says the page is writable, the other does not — and
it is the frontier. (Or `KERN_ABORTED`, which the handler also retries: not a dismissal either.)

## 2. The two readings (one measurement each)

The disagreement has exactly two sides; 1010 reads **one cell from each** so the reader can say *which
side is wrong*, not just that they disagree.

### 2.1 (A) the PTE side — the L2 descriptor the MMU faulted on

The wrapper runs in kernel/SVC mode with the MMU on and (for a fault taken in USER mode) **TTBR0 = the
faulting process's page table**. Between the cp15 reads and `__real_sleh_abort`, walk **TTBR0** to the
**L2 small-page descriptor for `far_`** and publish its raw word. The walk keys the table on
**TTBCR.N = 2** (TTBR0 owns `[0, 0x40000000)` — **not** the `0x80000000` *address-space* boundary;
`docs/1010-pte-walk.md §2`), reads TTBR0/TTBCR/TTBR1 by cp15, and the descriptor's bits decide the
PTE side:

- bits `[1:0]` — `0b10` = small page (any other = absent/section → **no mapping**: the PTE side is
  `fault`, vm_map must be wrong).
- `L2_ACCESS_APX` (bit 9) — **APX=1 ⇒ user is READ-ONLY**; APX=0 ⇒ user RW. The fault asks for a
  **write** (`dfsr` WnR=1); a descriptor with **APX=1** is *exactly* "vm_fault said success, the PTE
  is read-only" — side (A) named.
- `L2_ACCESS_USER` (bit 5) — 0 ⇒ not user-accessible at all (a stronger fault).
- the physical address (`bits[31:12]`) — proves the walk landed on a real page.
- `L2_NX_BIT` — the XN state (context).

### 2.2 (B) the return-code side — what `vm_fault` decided

`vm_fault` is called inside `sleh_abort`, which the entry code cannot edit from the wrapper. Wrap it
(`--wrap=vm_fault`, the same linkage the project already uses for `sleh_abort`): the wrapper records
the **return code** and the two arguments that name the fault (`va`, `prot`) for the last call, then
calls `__real_vm_fault`. Publish `xnu_live_abt_vmf_{ret,va,prot}`.

(A) and (B) together are decisive:

| (A) L2 APX | (B) `vm_fault` ret | reading |
|---|---|---|
| **1 (RO)** | `KERN_SUCCESS` | **the PTE is read-only though vm_fault said success — the pmap/map prot disagreement is on the PTE side** (a `pmap_enter` that did not clear APX, or a `pmap_protect`/COW downgrade that did not re-map). |
| 0 (RW) | `KERN_SUCCESS` | the PTE is writable — the fault is **transitional** (a TLB/CB/DSB ordering or an ASID/cache-maint gap), not a prot disagreement. |
| section / absent | `KERN_SUCCESS` | the **walk found no L2 entry** — vm_map maps it, the page table does not (a missed `pmap_enter`, or the wrong TTBR0 at the fault). |
| don't care | `KERN_ABORTED` | **`vm_fault` aborted** (`THREAD_UNINT` interrupted): the handler retries an aborted fault forever — the defect is that a user fault can abort at all, or its abort is not delivered. |

Only **one** of these is the machine's — 1010 names it.

## 3. The change (one switch, two recorders)

### 3.1 The arm switch — a NEW `STAGE90_XNU_ABT_PTE`

Not reusing `ABT_FAR`/`ABT_PC` keeps each prior arm byte-identical (the 1007…1009 precedent) and keeps
the OFF image provably inert. The guard is the **conjunction** `ABT_FAR && ABT_PC && ABT_PTE`: 1010 sits
after 1009, is inert unless 1008 **and** 1009 are also on, and the build refuses `ABT_PTE=1` with
`ABT_FAR=0` or `ABT_PC=0`.

### 3.2 Recorder (A) — the L2 walk (`src/entry/entry_stubs.c`, after 1009's `#endif`)

```c
void entry_note_abt_pte(uint32_t va);   /* walks TTBR0, publishes the L2 descriptor */
```

Publishes, on the same powers-of-two ladder 1008/1009 use (the tail is already sampled) so the rows
line up by ordinal:
`xnu_live_abt_pten` (ordinal), `_pteva` (`far_`), `_ptel2` (the raw L2 descriptor),
`_ptel1` (the raw L1 descriptor — the coarse base, so a bad walk is visible), `_ptettbr` (the TTBR0
actually used), `_ptenx` (a bounded **change-count** for `_ptel2`, so the reader decides
CONSTANT-vs-CYCLING **on the tail rows**, never on a whole-run aggregate — the 1009 defect, not
repeated).

The **walk itself** is a pure function of the VA and the coprocessor registers; the exact constants
(`L2_ADDR_MASK`, `L2_SMALL_PAGE`, `L2_ACCESS_AP*`, `L2_ACCESS_APX`, `L2_ACCESS_USER`, `L2_NX_BIT`,
the `0x80000000` TTBR0/TTBR1 split) are **read off this port's `pmap.c`/`arm_vm_init.c` by value**, not
assumed — the design's one dependency on a sub-agent extraction (`docs/1010-pte-walk.md`, to be
committed with the design).

### 3.3 Recorder (B) — the `vm_fault` return (`src/entry/entry_stubs.c`)

```c
void entry_note_abt_vmf(uint32_t ret, uint32_t va, uint32_t prot);
```

Called from a new `__wrap_vm_fault` (`src/entry/entry_trace.c`), gated by the same conjunction, on the
first four calls plus a bounded change-count on `ret`. It writes **only when the last abort was a USER
write permission fault** (the recorder checks a latch the abort wrapper sets), so 2.1 M retries do not
overflow the channel: the values published once are the ones that matter.

### 3.4 Call sites

- **(A)** in `__wrap_sleh_abort`, after 1009's call, guarded
  `#if defined(ABT_FAR)&&ABT_FAR&&defined(ABT_PC)&&ABT_PC&&defined(ABT_PTE)&&ABT_PTE`:
  `entry_note_abt_pte(far_);`
- **(B)** a new `__wrap_vm_fault(vm_map_t map, vm_map_offset_t vaddr, vm_prot_t fault_type,
  boolean_t change_wiring, int interruptible, pmap_t pmap, vm_map_offset_t pmap_addr)` — signature
  read off `vm_fault.h:90-97` by value — calling `__real_vm_fault`, then `entry_note_abt_vmf(ret,
  (uint32_t)vaddr, (uint32_t)fault_type)`.

## 4. Build refusals

- `nm`/linked-image clauses: `entry_note_abt_pte` **DEFINED** (`T`/`t`), the L2-walk helper **inlined
  or present**, `__wrap_vm_fault` **DEFINED**, and its `__real_vm_fault` reference present; the literal
  count for the arm's new keys; the conjunction guard present **twice** in `entry_trace.c` and once in
  `entry_stubs.c`; OFF image byte-identical to `armed-d13-24997fc5`.
- **Frozen negative control**: the frozen 1009 elf (`armed-d13-24997fc5`) must show **no**
  `entry_note_abt_pte`/`__wrap_vm_fault` symbol — the by-value refusal, as 1009 used the frozen 1008 elf.
- A refusal that `ABT_PTE=1` requires `ABT_FAR=1 && ABT_PC=1` (a claim made a check, not a comment).
- `make check` RC 0; four-ref verify on push.

## 5. Classifier (`tools/check_1010_capture.py`, `[ABT4]`)

- **(A)** decode each tail row's `_ptel2`: descriptor type, AP bits, APX, USER, NX, PA; decide
  CONSTANT-vs-CYCLING **on the tail rows' `_ptel2`** (never a whole-run aggregate).
- **(B)** read `_vmf_ret`; map to `KERN_SUCCESS`/`KERN_ABORTED`/other.
- **Print the §2.2 table row the machine occupies**, in one line, e.g.
  `[ABT4] VERDICT: vm_fault KERN_SUCCESS + L2 APX=1 => the PTE is read-only though vm_fault said success`.
- Report all three of (A)-value, (B)-value, and the conjunction as `OBS` so a "0 anomalies" run still
  carries the named mechanism.

## 6. Falsifiers

- If (A) reads a **writable** (APX=0) small page **and** (B) reads `KERN_SUCCESS`, the fault is not a
  prot disagreement and the next rung moves to the TLB/cache-maint ordering on this port — recorded,
  not papered over.
- If the L2 walk yields a **section** or **absent** descriptor for `far_`, the walk itself is suspect
  (wrong TTBR0 at the fault, or the port uses sections) and the extraction in §3.2 was wrong — the
  rung re-reads the port's descriptor format by value before re-pressing.
- The measured `pc`/`far`/`fsr` must **reproduce 1009** (`pc=0x2be8a1a0`, `fsr=0x80f`); a different
  class means 1009's reading was run-specific and is itself re-opened.

## 7. What a fix would then be (out of scope here, named for the record)

- **(A) PTE-side**: the site whose `pmap_enter`/`pmap_protect` leaves APX set on a page the map calls
  RW — most likely the **shared-cache / `cs_invalid_page` path** (1009's last console line) or the
  port's `pmap_vm_prot_to_page_flags` (`pmap.c:1095`).
- **(B) abort-side**: `vm_fault`'s `KERN_ABORTED` reaching the retry branch — the handler should
  **deliver** an aborted user fault (signal/return), never spin.

Rung 1010 is **instrumentation only** (no new device behaviour, low blast radius); the fix is 1011+.

## 8. Implementation notes (verified by value this session)

- **`build_entry.sh` threading** — mirror `STAGE90_XNU_ABT_PC` at its five sites
  (`:290-310` `STUB_DEFINES_TRACE` append; `:1220-1230` the validate/refuse block; `:1707-1751`
  `ENTRY_ARM_KEYS`; `:1983-2002` the record-writer `case`; `:2031-2041` the record `echo`). The
  refusal is `ABT_PTE=1` requires `ABT_FAR=1 && ABT_PC=1` (the conjunction guard).
- **`scripts/preflight_boot_check.sh`** — add `STAGE90_XNU_ABT_PTE` to `ENTRY_CFG_KEYS`
  (`:716-753`), or the CONVERSE check (`:1175`) refuses the record.
- **`vm_fault` signature** — `vm_fault.h:90-97`, seven args ending `pmap_t pmap, vm_map_offset_t
  pmap_addr`. The `--wrap=vm_fault` flag belongs wherever `--wrap=sleh_abort` is wired (the build
  agent must find it; the OFF image must not carry it).
- **The `sleh` keys' silence is BY CONSTRUCTION** (`entry_note_sleh`, `entry_stubs.c:1744-1818`):
  rows write only while `g_sleh_seq <= SLEH_LIVE_MAX`, then one `_storm` marker, then only
  `_seen <= SLEH_LIVE_SEEN`. So the capture's `sleh` rows describe the run's FIRST ~64 aborts, never
  the tail — the wrapper's own keys are **not** an independent witness of handler behaviour over the
  storm. The console (`cs_invalid_page` last at `:4130`) is. Do not read the `sleh` rows as the
  storm's behaviour.
- **The L2 walk is extracted and committed as `docs/1010-pte-walk.md`** — its constants
  (`L2_ADDR_MASK` 0xfffff000, `L2_SMALL_PAGE` 0x2, `L2_ACCESS_USER` bit 5, `L2_ACCESS_APX` bit 9,
  `L2_ACCESS_PRW` 0x10, `L2_NX_BIT` 0x1, `L1_TYPE_COARSE` 1), its **TTBR split `0x40000000`
  (TTBCR.N=2) — NOT `0x80000000`**, and its walk are all cited to `file:line` there. The walk must
  key on **TTBCR.N**, never on `0x80000000`, and must not use `entry_live_ttb_base` (it returns TTBR1
  for any `n != 0`, which is wrong for `far=0x9a000`).

### 8.1 (B)'s wiring — the surface risk, and the rule

`--wrap=vm_fault` rewrites **every linker-visible call** to `vm_fault` in the whole kernel (the
`--wrap=sleh_abort` precedent only wraps a symbol called from a handful of sites; `vm_fault` is
called from hundreds of objects). That is a real surface, and it violates "instrumentation only" if
the wrapper is anything but a pure pass-through on the non-abort case. Two rules:

1. **Prefer a call-site hook over a global wrap.** The one call that matters is `trap.c:829`. This is
   **our port tree** (`external/xnu-hd2-darwin13`), patched by the same staged-edit mechanism as the
   other XNU rows; adding a `entry_note_abt_vmf(code, dfar, prot)` call under a `#if STAGE90_XNU_ABT_PTE`
   right after `:833` is narrower than a kernel-wide wrap and touches nothing else. If the patch
   mechanism is used, the OFF tree must be byte-identical (the staged-edit checker enforces it).
2. **If a `--wrap=vm_fault` is used instead**, the OFF image must carry **no** `--wrap=vm_fault` (the
   `TRACE_LDFLAGS` block, `build_entry.sh:169-224`, is only built under `ENTRY_TRACE`), and the ON
   wrapper must be a pure pass-through (`return __real_vm_fault(...)`) **except** when the latched
   "last abort was a USER write permission fault" flag is set — proven by a build clause that the
   wrapper's only `entry_note_*` call is behind that latch.

(A) is the **primary** measurement and needs neither: it reads the page table directly and, combined
with the handler-silence fact (`vm_fault` returned success/aborted), already names the PTE side for
three of the four §2.2 rows. (B) only splits `KERN_SUCCESS` from `KERN_ABORTED`; if wiring it proves
to broaden the surface, it is dropped and the reader reports what (A) + the silence alone decide.