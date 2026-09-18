# Experiment 254 — The Stop Is `ledger_credit`, Inside the pmap's Page-Table Expansion

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`bsd_kern_kern_cs.o` — **2120 bytes of text**, 21 references — the code-signing subsystem
(`cs_init`, `cs_enforcement`, `cs_invalid_page`, `cs_debug`, `panic_on_cs_killed`, the trust-cache
machinery). Resolved 5, added 13:

```
resolved   cs_debug  cs_enforcement  cs_init  cs_invalid_page  panic_on_cs_killed
added      csblob_find_blob  csblob_get_entitlements  cs_hash_type  osobject_retain
           proc_lock  proc_unlock  sysctl__vm_children  threadsignal  ubc_cs_blob_get
           UBCINFOEXISTS  vn_getpath  vnode_lock  vnode_unlock
```

605 → **613** undefined, 523 → **532** function stubs, 82 → **81** storage stubs, text 682353 →
**684993** (+2640). The 13 added are the BSD layer's own door out — `proc_lock`, `vnode_lock`,
`threadsignal`, `vn_getpath`, the UBC code-signing blob — and they arrive together, which is what
entering `bsd/kern` for the first time looks like.

## The prediction, and how it was wrong

`cs_init` is short and calls no stub, so it should complete:

```
80091838  bl PE_parse_boot_argn       ; "cs_" + a global at 0x800ecfac
8009183c  bl lck_grp_attr_alloc_init
80091850  bl lck_grp_alloc_init
80091868  b  lck_grp_attr_free        ; a tail call (again)
```

The prediction came from what follows in `kernel_bootstrap`:

```
8000db80  bl vm_mem_init        <- real
8000dbc8  bl oslog_init         <- reaches a stub
8000dbd8  bl telemetry_init     <- STUB
```

and the walker's answer for `oslog_init` was `__firehose_buffer_create`. **The prediction written
before the run was `stub_hit=__firehose_buffer_create`, caller `oslog_init+0x70` — and it was wrong.**
Two things caused that, and both are worth recording because they are the same defect class this
project has hit before:

1. **I read the walker's output truncated.** `xnu_entry_callwalk.py` prints the straight-line answer
   *and then* the list of conditional alternatives; I had piped it through `head -3` and read only the
   first paragraph, so the alternatives — the ones that actually happened — were never on screen.
2. **The walker's closure is not the executed path here.** It treats `kmem_alloc_flags` (the call
   `oslog_init` makes at `+0x3c`, *before* the firehose call at `+0x70`) as clean, because inside the
   pmap the ledger calls sit behind conditionals the enumerator does not attribute to that root. A
   "no stub on the straight-line path" answer is a statement about the model, not about the run.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003a
 xnu_entry_kv_in_dram=0x0000003a
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ledger_credit
 xnu_entry_stub_caller=0x80029cfc

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x3a` (58 bytes
= 24 + 34, the name being 13 characters), and `0x80029cfc`'s `caller - 4` is
`80029cf8: bl 80093454 <ledger_credit>` — a `bl`, so the call site is the address itself.

The call site is inside the function that starts at `0x800298ac`, which `nm` labels **`pmap_expand`**,
in the ARM pmap's page-table path:

```
80029cbc  beq 80029ce8            <- TAKEN (the first two credits below were skipped)
80029cd0  bl ledger_credit        ; task_ledgers.phys_footprint
80029ce4  bl ledger_credit        ; task_ledgers.page_table
80029ce8  ...
80029cf8  bl ledger_credit        <- the stop
```

The three calls are the inlined `pmap_tt_ledger_credit(pmap, size)` sites from `pmap.c:3631`/`3639`/
`3647`, the free-page-table-list reuse branches of the page-table allocator. **The `beq` at `+0x410`
was taken and the first two were skipped** — a measured fact, not an inference: `ledger_credit` is a
stub, so the run could not have passed the first one.

One caution about that address, recorded because the resolver's output is easy to misread:
`nm -S` reports `pmap_expand` as 538 bytes, but the stop is 0x450 bytes into it, and the disassembly
shows no `pop {…, pc}` between `0x80029a00` and `0x80029d40` — one function runs through all of it.
The symbol's size field is not the function's extent here, so the arithmetic "address minus base" is
what identifies the function, not the size.

## What this measurement means

**`cs_init` completed** (all four of its calls are real), `kernel_bootstrap` continued, and then:

- **`vm_mem_init` is a single `b vm_object_init`** — and **`vm_object_init` is `bx lr`**, four bytes.
  The function `kernel_bootstrap` calls after `cs_init` does nothing at all on this build.
- **`oslog_init` ran** and its `kmem_alloc_flags` call (`+0x3c`) is where the run went: allocating the
  OS-log buffer is the first kernel-map allocation that has ever needed *new page tables*, so it went
  into the pmap's expansion path, and the pmap's ledger accounting is what stopped it.

That is a change in the character of the frontier worth naming. For thirty-odd steps the stop has been
the *next initialisation function* in a boot sequence — `kalloc_init`, `vm_fault_init`,
`memory_manager_default_init`. This one is inside a **runtime memory allocation**, several frames deep
in the ARM pmap, reached because the kernel is now actually allocating memory rather than setting
subsystems up. The path from here on runs through code that will be executed again and again.

## Cost

| | exp-253 | now |
| --- | --- | --- |
| undefined | 605 | **613** (5 resolved, 13 added) |
| function stubs | 523 | **532** |
| storage stubs | 82 | **81** |
| entry text | 682353 B | **684993 B** (+2640) |
| entry image | 785456 B | **785648 B** (+192) |
| entry `.bss` | 0x800bf500–0x800ee688 | **0x800bf5c0–0x800ee708** |
| layout | args +983040, headroom 1120632 B | **args +983040, headroom 1120504 B** |
| payload text | 1277674 B | **1277866 B** |

Another step that did not move the layout: 192 bytes of image growth, `boot_args` still at 983040.

## What is next

`ledger_credit` is in `out/xnu_kernel_obj/osfmk_kern_ledger.o` — **8324 bytes of text, 35 references**
— the ledger subsystem (`ledger_credit`, `ledger_debit`, `ledger_alloc`, `ledger_reference`,
`ledger_template_*`, `ledger_ast`). It is a *runtime* object rather than an initialization one, which
fits where the frontier now is: the ledger is entered from the pmap's accounting on this boot and from
`pmap_enter_options` (which is what experiment 250's `kalloc.o` was referring to when it added
`ledger_credit`/`ledger_debit`/`task_ledgers` as undefined), so its closure is the thing to read next.

Predicting 255 well means reading the *whole* output of `xnu_entry_callwalk.py --root pmap_expand`
this time — the conditional list included — and treating its "no stub on the straight-line path" as a
statement about the model rather than about the run.

## Reproduce

```bash
grep -n 'BSD_KERN_KERN_CS_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'

# the prediction: cs_init, then the rest of kernel_bootstrap
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<cs_init>:/{f=1} f{print} f&&/^$/{exit}'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<kernel_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' \
  | sed -n '/8000db70:/,/8000dbdc:/p' | grep -E '\tbl\t'
./tools/xnu_entry_callwalk.py --root oslog_init        # read ALL of it, not the first three lines

# ... and what actually ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x80029cfc          # -> 0x450 bytes into `pmap_expand`

# the three credits, and the branch that skipped the first two
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<pmap_expand>:/{f=1} f{print} f&&/^$/{exit}' \
  | sed -n '/80029cb0:/,/80029d00:/p'
sed -n '1573,1585p' external/xnu-4570.1.46/osfmk/arm/pmap.c   # pmap_tt_ledger_credit
sed -n '3625,3650p' external/xnu-4570.1.46/osfmk/arm/pmap.c   # the three free-list branches

# the two empty functions on the way there
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address=0x8004057c --stop-address=0x80040584
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address=0x80074f08 --stop-address=0x80074f0c
```
