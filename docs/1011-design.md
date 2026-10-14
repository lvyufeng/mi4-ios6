# 1011 — the FIX for the user write permission-fault storm (design)

**Scope.** 1010 (`docs/1010-design.md`, `docs/1010-pte-walk.md`) is *instrumentation only*: it
publishes the L2 descriptor at the storm `far` (A) and `vm_fault`'s return (B). 1011 is the **fix**,
and it is only *decidable* once 1010's cells are read — because, as §1.5 shows, the correct edit
**depends on the verdict**, and the verdict is a choice between two mechanisms that produce the
identical 1009 signature.

## 1. The mechanism, verified — and two corrections

### 1.1 The CS block's control flow, quoted (`osfmk/vm/vm_fault.c`)

The code-signing block runs only when the page qualifies (`:2389-2400`, `VM_FAULT_NEED_CS_VALIDATION`
at `:2291-2295`). The deciding branch (`:2435-2494`):

```
2435  if (m->cs_tainted ||
2436      (( !cs_enforcement_disable && !cs_bypass ) &&
2437       ( (!m->cs_validated && (prot & VM_PROT_EXECUTE)) ||
2452         (page_immutable(m, prot) && ((prot & VM_PROT_WRITE) || m->wpmapped)) )))
      {
2472      if (reject_page) { kr = KERN_CODESIGN_ERROR; ... }
2478      else             { kr = KERN_SUCCESS; must_disconnect = !m->cs_tainted; m->cs_tainted = TRUE; ... }
2492  } else {
2494      kr = KERN_SUCCESS;
      }
```

`page_immutable(m,prot) ((m)->cs_validated)` (`:2402`) — **`prot` is ignored**: it is a claim, not a
test. `cs_enforcement_disable = 0` on this board (`CONFIG_ENFORCE_SIGNED_CODE`, `osfmk/conf/MASTER:256`
+ `bsd/conf/MASTER:435`); `cs_bypass` is FALSE for an ordinary fault (`vm_map.c:9180`).

### 1.2 **Correction 1 — the discriminator is `reject_page` = `CS_HARD`, not `cs_tainted`**

- `m->cs_tainted` gates whether the block at `:2435` is *entered* (first term).
- `reject_page` — the success-vs-error outcome — is the **return value of `cs_invalid_page()`.**
  `cs_invalid_page` returns 1 **iff** `p->p_csflags & CS_HARD`; otherwise it clears `CS_VALID`,
  prints `CODE SIGNING: cs_invalid_page(0x…): p=N[proc] clearing CS_VALID`, and returns 0. For
  `map_is_switched` it is hard-wired FALSE (`:2467`).

So on the storm's process `reject_page == FALSE` because launchd's `csflags` lack `CS_HARD` — **not**
because the page is tainted. `reject_page == TRUE` returns `KERN_CODESIGN_ERROR` (→ the handler
delivers SIGBUS); `FALSE` returns `KERN_SUCCESS` (`:2478`).

### 1.3 The asymmetry, verified

- `reject_page==TRUE` ⇒ `kr = KERN_CODESIGN_ERROR` ⇒ no `PMAP_ENTER` (`:2565`) ⇒ `vm_fault` returns an
  error ⇒ handler's error branch ⇒ `EXC_BAD_ACCESS` ⇒ SIGBUS.
- `reject_page==FALSE` ⇒ `kr = KERN_SUCCESS` ⇒ `:2503` block runs ⇒ on a WRITE fault `must_disconnect`
  is consumed (`:2540-2559`; it touches only `prot &= ~VM_PROT_EXECUTE`, `:2557`) ⇒
  `PMAP_ENTER_OPTIONS(pmap, vaddr, m, prot, fault_type, …)` (`:2565`).

**Crucially: no write-stripping site fires for a *write* fault.** The demotions `vm_fault.c:2353`,
`:1881`, `:1904` are all `prot &= ~VM_PROT_WRITE` under `(fault_type & VM_PROT_WRITE) == 0` — i.e. they
fire only for a **non-write** fault; `:3855` fires when the object's `copy` moved. **None strips a write
from a write fault.** So if the page is entered read-only, the `prot` reaching `:2565` **already lacked
`VM_PROT_WRITE` when it came out of `vm_map_lookup_locked`** — the `vm_map_entry` denies the write. The
APX=1 consequence is as §7 says: `pmap_vm_prot_to_page_flags` (`arm/pmap.c:1110-1113`), written at
`pmap.c:3254/3260/3280`.

### 1.4 The handler's silent retry, verified

`arm/trap.c:829-833` (`dfsr & 0x800` selects the write prot), the error branch `:834-901`
(`exception_type = EXC_BAD_ACCESS`, `:865`), the silent retry `:902-908`. `dfsr=0x0000080f` = WnR=1,
**FS=15 "permission fault, level 2"** — the 1009 decode is correct. The exception path (`:926` →
`doexception` → `exception_triage` → `ux_exception` → SIGBUS → `task_terminate`) is **never reached**
because `code == KERN_SUCCESS`.

### 1.5 **Correction 2 — the storm is NOT (necessarily) the CS-taint branch, and `far=0x9a000` is not launchd's data**

`docs/1010-design.md` §7 asserts the storm "is the CS-taint path". That is **one** of two mechanisms,
**not established by the silence**: because `prot` out of `vm_map_lookup_locked` lacks WRITE (§1.3), the
identical signature is produced **with the `:2435` block never entered** (the `:2492` else,
`kr = KERN_SUCCESS` at `:2494`). They are distinguished only by 1010's (A)+(B) **plus** the entry's
`protection`.

`far=0x9a000` is **neither** the shared cache (`0x2c000000..0x40000000`, capture `:4114`) **nor** a
launchd/dyld image mapping: launchd runs at `0x2be…` (entry `0x2be7a028`, capture `:21808`), dyld at
`0x2be00000`+slide. It is a page **below** the shared region, **present** (FS=15 ⇒ an L2 descriptor
exists) and **user-read-only**. `pc=0x2be8a1a0` is in **dyld's `__TEXT`** (`0x2be00000` + slide
`0x7c000`), and the store there computes its target from a register (`mov.w r2, sl, lsr #16` at link
`0x2be0a1a0`) — consistent with a **stale/unrebased dyld pointer**, not with the shared cache.

## 2. The fix, ranked (conditional on 1010's verdict)

**Switch discipline.** Every candidate must be a **new** `tools/patch_d13_*.py` applying the text under
`#if defined(STAGE90_XNU_*) && STAGE90_XNU_*` so OFF == Apple's bytes, proven by a by-value checker that
the string/immediate is absent from the frozen 1009 elf. **Do not edit `vm_fault.c` directly.**

- **F1a — if the entry's `protection` grants `VM_PROT_WRITE` but the PTE lands APX=1:** the `prot` was
  stripped on the way in (a pmap/prot bug). Read `prot` at `vm_fault_enter` entry and, under the switch,
  make the mismatch loud (printf/counter). **Diagnostic until the strip site is found; do not guess it.**
- **F1b — if the entry's `protection` denies WRITE:** the `vm_map` genuinely denies the write, and the
  accept path's unconditional `kr = KERN_SUCCESS` (`:2478`) is the wrong condition. Minimal honest edit:
  `kr = (fault_type & VM_PROT_WRITE) ? KERN_CODESIGN_ERROR : KERN_SUCCESS;` (gated). **Consequence
  (measured, not hand-waved):** `KERN_CODESIGN_ERROR` ⇒ SIGBUS ⇒ `task_terminate(initproc)` ⇒
  `panic_plain("unexpected SIGKILL of …")` (`bsd/kern/kern_sig.c:2132-2142`, the landmine the build
  notes already record). F1b alone converts a 24.4 s freeze into an immediate **panic** — *correct*
  semantics, *fatal* outcome, survivable only if the write is genuinely illegal and the process is not
  initproc. **If 1010 shows F1b, 1011 is a decision, not a patch** (see F4).
- **F1c — the COW form:** if the entry is `needs_copy` and the page reached the CS block instead of a
  shadow. The COW that should service an intentional write lives at `vm_map.c:9112-9132` and
  `vm_fault.c:2915/3216/3378`. Only after 1010 shows `needs_copy`; **highest blast radius** (general VM
  COW path).
- **F2 — the shared-region/COW path:** `vm_fault.c:1880/1903`, `vm_map.c:9103-9141`. **Unlikely** —
  §1.5 measured `0x9a000` is not shared-cache. Rank below F1; design only if 1010's PA lands in the
  cache's `[0x2e1a9000, 0x3aaef000)`.
- **F3 — the port's own additions — checked; NOT the site.** `vm_fault.c` has **no** `STAGE90`/`BOARD`
  patch (grep → 0 hits). `arm/pmap.c`'s `QSD8250_LEO`/`ARMPBA8` blocks compile out on Mi4 (only
  `-DBOARD_CONFIG_MSM8974_CANCRO` is defined). The live VM edits are `STAGE90_XNU_SUBMAP_PMAP_SKIP`
  (`vm_map.c`) and the commpage alias (`pmap.c`, `kernel_pmap && va==0xffff4000` only) — neither touches
  an ordinary user page's `prot`/APX. **The defect is Apple's semantics, not a local patch:**
  `mi4-one-value-two-definitions` — the `vm_map`/`prot` says one thing about writability, the L2 PTE
  (APX) says another, and `vm_fault` returns success regardless.
- **F4 — the safety net:** if F1b is the verdict and initproc must not be killed, a ~2 M identical
  retry is never right — bound it. `arm/trap.c:902-908` (the retry arm) under `STAGE90_XNU_ABT_RETRY_BOUND`:
  count consecutive identical `(pc, far, fsr)`, and on exceeding a small N, `printf` once and escalate.
  Same "a measurement must not silently succeed forever" discipline the 1009 reader defect embodied.

**Ranked order:** read 1010 → if the entry grants WRITE, **F1a**; if not, **F1b + F4**; only then F1c/F2.

## 3. Verification — how a 1011 press proves the fix

**Positive (the fix took):** the storm stops — `xnu_live_abt_pcn` does **not** reach `0x200000`, and
`xnu_live_tick_sum_maxd` ≪ `0x1be8…` (24.4 s; 1009: `0x1be8767b`). **F1b also produces a NEW console
line that is absent today:** the handler's error branch prints `usermode data abort, EXC_BAD_ACCESS at
0x… (…)` (`trap.c:871-875`) — 1009's capture has **zero** over 2.1 M faults. One such line == the fix
routed the unresolvable write to the exception path. **Its absence, over a 1011 claiming F1b, is a FAIL
of the fix, not a quiet success.** F1b's falsifier is the panic: if the next line is `panic` from
`kern_sig.c`, the fix worked *as designed* and the outcome is fatal — report it, do not paper it over.
F1a/F1c/F2: no storm and the boot advances past the last 1009 line to a NEW line — the measurable "new
line" is the whole proof.

**Falsifiers (each is a finding, not a retry):**

- The same `(pc=0x2be8a1a0, fsr=0x80f, far≈low)` storm after **F1a** ⇒ the strip site was wrong; re-read
  1010.
- The same storm after **F1b** ⇒ impossible in principle; if it happens, the (B) wrapper missed the
  call or the class is not the data-abort case — the reading is re-opened.
- A different `far`/`pc`/`fsr` class ⇒ the fix exposed the next wall; **name it, do not widen 1011.**
- `xnu_live_abt_pcn` still ~2 M ⇒ the fix did not reach the object (the by-value checker should have
  refused the build) — do not press.

**Readers:** `tools/check_1009_capture.py` (expect a SHORT abort ladder), `tools/check_1010_capture.py`
(the (A)/(B) cells), and a new `tools/check_1011_capture.py` printing one `[CFFIX]` verdict line.

**OFF identity:** the OFF elf must be **byte-identical to `armed-d13-24997fc5`**; the frozen 1009 elf is
the negative control (must show **no** `CS_FAULT_FIX` string/immediate).

## Summary of the two corrections to 1010 §7

1. The success-vs-error discriminator is **`reject_page` = `CS_HARD`**, not `cs_tainted`.
2. **"The storm IS the CS-taint path" is unproven**, and `far=0x9a000` is not launchd's data. Read by
   value, **no write-strip site fires for a write fault**, so the `prot` reaching `pmap_enter_options`
   already lacked WRITE out of `vm_map_lookup_locked` — the storm is the retry of a write the **`vm_map`**
   denies (fix F1b+F4), *or* the CS accept path lied (F1a) *or* a missing COW (F1c). **Build nothing in
   `vm_fault.c` until 1010's (A)+(B) and the entry's protection are read.**