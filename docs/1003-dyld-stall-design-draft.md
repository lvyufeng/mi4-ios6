# 1003 design — the dyld shared-cache stall (the real 1002 frontier)

Status: DRAFT (lead analysis; forensics agents cs-blobs / stall-1003 still converging).
Sources: `records/1002-press-read.txt`, capture `out/stage90/captures/recovered-unknown-arm-20261013-110813-last_kmsg.txt`.

## What the 1002 press actually established (by value)

1002 CONFIRMED its fix (the r3-save in the USVC hook): after `IOS7SHREG check_np ret=12` the
next line is `IOS7SHREG map_slide`, with **0** `usermode data abort at 0x4` (1001: 230), and the
user-SVC frontier advanced **6 → 22**. That is settled and committed (`records/1002-press-read.txt`,
`8531a3d`).

The rung's own goal was FIX, not frontier: it removed the 1001 defect and RESTORED the wall 999
stopped on. This doc is about that wall, re-read against the now-working instruments.

## Correction: `cs_invalid_page` is NOT the frontier

999's record said "frontier = cs_invalid_page then silence". That was a **measurement artifact**:
999 had no USVC probe, so its record simply stopped naming user activity. In 1002:

- `CODE SIGNING: cs_invalid_page(0x2f7f6000): p=1[launchd] clearing CS_VALID` is the LAST kernel
  console PRINT (@:4114) — but the KV live channel shows **22 user SVCs AFTER it**
  (`xnu_live_usvc_n` n=1..22 @:23042..29040). So the boot CONTINUED past `cs_invalid_page`.
- `clearing CS_VALID` is the TOLERATED branch: `kern_proc.c:2791-2794`, `retval=0`, the page fault
  PROCEEDS (NOT `honoring CS_KILL`/`CS_HARD`). It did not kill launchd.
- The address = shared cache's FIRST page: `IOS7LAB cache mapping 0 va=2f7f6000 size=0c266000
  offset=00000000`; base 0x2c000000 + slide 0x37f6000 = 0x2f7f6000 (confirmed by `map_slide`).
- The predicate (`vm_fault.c:2438`): `!m->cs_validated && (prot & VM_PROT_EXECUTE)` — an EXECUTE
  fault on a page not marked CS-validated (`cs_validation=1`, `ubc_subr.c:2380`; `code_signed` IS
  set, so the machinery exists and validation genuinely returned false for page 0).

## THE REAL FRONTIER — a stall, not a crash

After `shared_region_check_np` (n=19, r0=0x2be99251), dyld makes exactly three Mach trap **−3**
calls (n=20/21/22, `num=0xfffffffd`, pc=0x2be97328, r0=0) and then **ZERO further user syscalls**
for the rest of the run. `cmn r11,#3; beq ios7lab_timebase` (`traps_lo.s:186`) → num=−3 =
`ios7lab_timebase` → `_mach_absolute_time` (`traps_lo.s:285`). So the recorded endpoint is three
consecutive `mach_absolute_time` calls from the SAME pc (advancing lr) — a spin of the timebase call.

Meanwhile the kernel stayed alive ~23 min: `xnu_live_wdt_pets=0x61` (97 watchdog feeds, the read
door kept feeding), tick summaries climbing (`sum_count`). No panic, no abort, no SIGKILL line
anywhere — `cs_invalid_page` count is exactly 1 in the whole capture. So pid 1 (launchd/dyld) is
ALIVE but not making syscall progress.

## The machine is paging the shared cache LINEARLY

The storage read door records 537 reads; the last ~40 are a PERFECT linear run
(`xnu_live_storage_rd_lba` 0x4a381b → 0x4a3842, +1 each, 512 B steps) — i.e. the kernel is
page-in-ing the shared cache sequentially. 187 of the 537 reads happen in the usvc region
(@:23042-29040), i.e. during/after the recorded user window, at roughly one 512 B read per ~10
KV lines. The read latency per op is NOT the bottleneck (`xnu_live_storage_dr_ticks=0x87d` ≈ 113 µs
of raw CNTVCT @19.2 MHz — just the SDHCI reset poll; `dr_timeout=0`, `dr_done=1` for all 538).

## The 1002-only change is a PROGRESS, not a regression

999's storage rd ordinal reached 218 and its `wdt_pets` last value was 0 (cap-affected); 1002
reaches 537 with 97 feeds direct-published. The 999→1002 delta (r3-save) removed the 1001 abort,
so 1002 runs FURTHER (22 SVCs where 999 recorded none) into the same shared-cache region.

## CORRECTION (2026-10-13, records/1003-stall-forensics-preliminary.txt): the running code IS dyld

An earlier reading of this doc concluded the 1002 SVC pcs (`0x2be9xxxx`) were "past dyld's image end,
so the running code is the shared cache". **That was wrong — it assumed slide 0.** Dyld is
`MH_DYLINKER`, slid per boot; the 1002 capture MEASURES the slide as **0x7C000** (`@:4099`
`the OS starts the process at 0x2be7d028` − `__dyld_start` link `0x2be01028`). So dyld's runtime
image is **[0x2be7c000, 0x2bed4000)**, and all 22 SVC pcs (0x2be96c10..0x2be98490) fall inside dyld's
own `__TEXT`. Resolved through the slide, all 22 map to exact dyld symbols.

The stall pc **0x2be97328 − 0x7c000 = link 0x2be1b328 = `_mach_absolute_time` + 0x68** — the return
address 4 B after an `svc #0x80` with r12 = −3. By value (disassembling the real dyld, dyld-324.1,
from `rootfs.hfs`), `_mach_absolute_time` reads a commpage structure at `_COMM_PAGE + 0x88`
(0xffff4088), validates it via the byte at **+8 = 0xffff4090**, and **if that byte is 0 it falls
through to `svc #0x80` with r12 = −3** — the kernel `ios7lab_timebase` path. So the −3 trio is
**libsystem's commpage FALLBACK**, i.e. the commpage mach_absolute_time structure's validity byte at
`0xffff4090` reads 0. This ties the frontier straight to 990–993's commpage rungs (991 died at
exactly `_COMM_PAGE + 0x90`). The three calls' `lr` values resolve to `dyld::ImageLoader::link`
+0x37/+0x89/+0xa9 — **dyld is linking an image and polling the clock between steps when it stalls.**

## Open mechanism questions (the agents are answering these)

1. What is the running code at pc=0x2be97328? **RESOLVED** (forensics §6): it is dyld's own
   `_mach_absolute_time` (libsystem commpage shim, slid to 0x2be97328), taking the `svc #0x80`/−3
   fallback because the commpage validity byte at **0xffff4090** reads 0. The caller is
   `dyld::ImageLoader::link` (lr trio +0x37/+0x89/+0xa9).
2. Is pid 1 (a) blocked in the kernel or (b) spinning in user code? The capture has no direct
   probe; **1003's per-tick stall sample decides it** (user pc advancing/repeating = SPINNING;
   every sample a kernel pc = ASLEEP).
3. Why only ONE `cs_invalid_page` (page 0) while 537 pages are being paged in? Because validation
   SUCCEEDS for pages 1..N and fails only for cache mapping m0's first page (0x2f7f6000, the
   `shared_file_mapping_np` m0 header) — so `cs_invalid_page` is a RED HERRING that did not stop
   the boot. Confirmed by value: m0's va − region base = 0x37f6000 = the slide.
4. Is the machine storage-bound? No: §3 of the forensics report shows the storage reads **STOP**
   before pid 1's silent tail (last read `@:28906`, last SVC `@:29033`, nothing between) — the
   kernel serves no page-ins during the stall. So a "blocked on a page-in" story is ruled out.

## DECISION: 1003 = the STALL INSTRUMENT (low-risk, no behaviour change)

The stall mechanism (user spin vs kernel block) is NOT determined by the existing capture. The
highest-value, lowest-risk next rung is a NEW instrument (mirroring 1002's shape: no new
behaviour, gated by a `STAGE90_XNU_STALL` switch, verified by value) that samples the INTERRUPTED
pid-1 frame at a fixed tick cadence. Concretely:

- **The sample** is taken in `msm8974_handle_interrupt` (`src/platform/darwin13/pe_msm8974.c`), on the
  tick's powers-of-two ladder, under `#if STAGE90_XNU_STALL`, calling
  `entry_note_stall_sample((const uint32_t *)context)` with the saved-state frame the timer passes to
  the handler.
- **The recorder** `entry_note_stall_sample(const uint32_t *frame)` lives in
  `src/entry/entry_stubs.c` and publishes, via `entry_write_kv`, the 7 keys `xnu_live_stall_n`
  (counter), `_t` (tick), `_pc`, `_cpsr`, `_sp`, `_lr` and `_nz` (count of non-zero words in the
  frame — a self-check that the offsets in `entry_saved_state.h` are right).

The reading: at the frontier the sampled pc either changes over consecutive samples (the program is
running = SPINNING) or is a KERNEL pc on every sample (pid 1 is ASLEEP/blocked in the kernel), and
the pc says WHERE. Self-testing; the run is EXPECTED to still stall (1003 changes no kernel
behaviour). Falsifiers: no `xnu_live_stall_*` keys at all ⇒ the instrument did not reach the image
(rebuild); all samples `_nz=0` ⇒ the saved-state offset is wrong (a MEASUREMENT defect,
[[mi4-measurement-defects]]).

This is deliberately NOT a CS-blob structural rung: `cs_invalid_page` is tolerated and singular, so
chasing CS now would be treating a red herring as the frontier ([[mi4-measurement-defects]]).

## 1003 artifacts

- `scripts/build_1003.sh` — threads `STAGE90_XNU_STALL=1` into the entry payload + platform object;
  the delta clause asserts the instrument by value (object `U` + linked `T` + 7 key literals + 1
  gated source call site) with the frozen 1002 elf (`armed-d13-b04976fe`) as the negative control.
- `scripts/park_1003.sh` — refuses the pressed arms; re-asserts the 1002 six-word r3-save window
  (regression) and the stall instrument by value before parking.
- `scripts/press_1003.sh` — the frozen pre-registration: `[0]` the instrument in the image, `[0b]` the
  1002 r3-save regression, `[F]` the stall reading, plus the frozen regression guards.
- `tools/check_1003_capture.py` — adds the `[S]` stall section (classify the sampled pcs), keeps the
  `[F]`/`[0]`/`[0b]` sections and the regression guards.
## 1003 PRESS + reconciliation (2026-10-13, armored `armed-d13-7669309b`)

**Press outcome.** build RC=0; instrument confirmed in the parked image (`nm` `T entry_note_stall_sample`,
7 `xnu_live_stall_*` literals, `pe_msm8974.o` carries the `U` reference); `fastboot boot` OKAY
(sha256 29646dfd…, unchanged across the send, never flashed). The device did **not** self-return
(runner exit 2 — the resident-rung outcome, same as 1001), so the log holds in DRAM and the operator's
power press is owed. On return the auto-recovery watcher runs `recover_last_kmsg.sh` (`--read-only` if
Android is up), then `tools/check_1003_capture.py`'s `[S]` section reads the samples.

**Reconciled: the two forensics agents disagreed only in words.** The correct de-slide is the one
ANCHORED on the capture: `@:4099 the OS starts the process at 0x2be7d028` = dyld `__dyld_start` link
`0x2be01028` ⇒ **slide = 0x7C000** (exact). dyld's full image is `[0x2be7C000, 0x2beD4000)`
(`__TEXT` vmaddr 0x2be00000 vmsize 0x21000 + `__DATA` 0x29000 + `__LINKEDIT` 0xe000 = 0x54000). So
the stall pc `0x2be97328` **is inside dyld's own `__TEXT`** — the agent saying "past dyld `__TEXT`"
used a __TEXT-only end (0x2be58000+slide = 0x2beD4000 is the whole image, not __TEXT alone); the
agent saying "inside dyld" is right about the module but its exact `__TEXT` bound was high by 0x30000.
**Both readings agree on the object: the stall pc is dyld's own libsystem-shim code, not a
shared-cache library.**

**`pc=0x2be97328` = the `bx lr` AFTER the `svc`.** The dyld.dis bytes: `0x2be1b320 mvn r12,#2`
(r12=−3), `0x2be1b324 svc #0x80`, `0x2be1b328 bx lr`. De-slid 0x2be97328−0x7C000 = `0x2be1b328` = the
`bx lr`. So the record's pc is the return address 4 B past the SVC — i.e. the `svc #0x80` (num=−3 =
`mach_absolute_time`'s kernel path) was taken and dyld is executing its return. The `lr` trio
(0x2be09a9f/0x2be09af1/0x2be09b11) sits in dyld `ImageLoader::link` — three timing snapshots, not a loop.

**CS-blob (counter to the earlier draft): `code_signed` is FALSE, but it is NOT the frontier.**
The 1,535,514-byte cache code signature exceeds `CS_MAX_BLOB_SIZE` (1280 KiB, `kern_descrip.c:1543`)
and the 2 MiB allowance sits behind `#if IOS7_COMPAT_PLATFORM` (`:1545`), which is 0 on CANCRO, so
dyld's `fcntl(F_ADDFILESIGS)` (USVC #16) gets `E2BIG` and no blob attaches. But (i) `cs_invalid_page`
is structurally capped at one line per process (`kern_proc.c:2783-2797`) and tolerated
(`retval=0`), and (ii) the SHA1 check is a zero-vs-zero no-op anyway (`_NOTYET` undefined). So the CS
gate is a latent correctness item, not the stall.

**The commpage byte at `0xffff4090` is BY DESIGN 0** — this tree ships no `mach_absolute_time`
commpage publisher (`commpage_populate` never touches +0x88/+0x90; only `commpage_set_timestamp`
writes the *gettimeofday* struct at +0x40). Publishing it would (a) risk a PL0 `mrrc …c14` fault
(`CNTKCTL` is set NOWHERE on D13) and (b) return raw 19.2 MHz ticks against the kernel path's ns.
**1004 must NOT publish the byte.**

**1004 = the per-tick pid-1 FRAME instrument, branched on the 1003 verdict.** If 1003 reads
**ASLEEP** (kernel pc every sample), the instrument moves to a RAW, always-run site (the idle
preemption path, `entry_trace.c`'s `Idle_load_context` wrapper, the `context` frame at
`pe_msm8974.c:496`) so the block is *where* the kernel holds pid 1; if it reads **SPINNING** (user pc
recurring), 1004 is a bail-out-error/abort capture in the dyld `link`/`rebase` path (an
always-static-linked-dyld build). The 1003 sampler's user-pc-on-a-kernel-tick has a known false-ASLEEP
bias (a 1 ms tick rarely lands on a user frame unless user mode is nearly continuous), which is why
the verdict chooses between a kernel-block and a kernel-spin capture rather than silently assuming one.

**Ruled out:** large-contiguous allocation. The cache regions are file-backed memory objects
(`vm_shared_region.c:1058/1119`, `VM_FLAGS_FIXED|ALREADY`, file offset, no `phys_contiguous`), demand
paged 4 KiB at a time out of ~1.4 GiB free (`docs/1005-contiguous-alloc-wall.md`).
