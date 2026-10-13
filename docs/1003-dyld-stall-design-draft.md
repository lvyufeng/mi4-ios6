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

## Resolved by value: the running user code is NOT /usr/lib/dyld — it is the shared cache

Extracted the real iOS 7.1.2 `/usr/lib/dyld` from `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs`
(196,576 B; `7z e ... IOS7MIN/usr/lib/dyld`). Its Mach-O: `__TEXT vmaddr=0x2be00000 vmsize=0x21000`
(ends 0x2be21000), `__DATA 0x2be21000`, `__LINKEDIT 0x2be4a000 .. 0x2be58000`. So dyld's whole image
is 0x2be00000..0x2be58000. The 1002 SVC `pc` values (0x2be96c10..0x2be98490) and the `_cerror_nocancel`
symbol from 1001 (vmaddr 0x2be1b8d4, in dyld __TEXT) show: 1001's fault WAS in dyld (offset 0x1b8d4),
but the 1002 SVCs run at 0x2be9xxxx = **past dyld's image end** — the code is the shared-cache /
libSystem region mapped just above dyld. So 1002 advanced the boot OUT of dyld's own code and INTO
the shared-cache library code, where it then stalled on `mach_absolute_time` (pc=0x2be97328).

## Open mechanism questions (the agents are answering these)

1. What is the running code at pc=0x2be97328? It is NOT dyld (dyld ends at 0x2be58000); it is the
   shared-cache / libSystem region mapped just above dyld. The 3 `mach_absolute_time` calls from one
   pc mean the running program is looping on `mach_absolute_time` — a spin.
2. Is pid 1 (a) blocked in the kernel (asleep on a page-in / IPC that never completes) or
   (b) spinning in user code? The capture has no direct probe; a per-thread last-syscall or a
   sampled pid-1 pc over time would settle it.
3. Why only ONE `cs_invalid_page` (page 0) while 537 pages are being paged in? If every shared-cache
   code page were unvalidated it would print ~537×. So validation SUCCEEDS for pages 1..N and fails
   only for page 0 (the cache header, which sits in a separate mapping — cache mapping 0 is DATA
   prot=0/0, while the code mappings carry R+X). So `cs_invalid_page` is a RED HERRING: an
   unvalidated non-executable data/header page faulted for execute, or the header genuinely
   uncovered by the code directory. It did NOT stop the boot.
4. The machine is NOT storage-bound: 537 × ~113 µs ≈ 61 ms of SDHCI time total, yet the reads are
   spread over ~23 min ⇒ ~2.7 s between page-ins. The bottleneck is in the VM/user code BETWEEN
   reads, not in the driver.

## DECISION: 1003 = the STALL INSTRUMENT (low-risk, no behaviour change)

The stall mechanism (user spin vs kernel block) is NOT determined by the existing capture. The
highest-value, lowest-risk next rung is a NEW instrument (mirroring 1002's shape: no new
behaviour, gated by a `STAGE90_XNU_*` switch, verified by value) that samples, for pid 1:

- its current **user pc** at a fixed tick cadence (so the record either advances = user spin, or
  freezes = blocked/looping at one pc);
- the **last syscall number** seen (already have `xnu_live_usvc_*`);
- whether the storage read door advanced between samples (page-in progress vs idle).

Self-testing reading: at the frontier the sampled pc either changes over consecutive samples (the
program is running) or repeats identically (it is wedged at one instruction). Falsifier: if the pc
advances monotonically while the syscall count stays flat, the stall is a pure user-space loop and
the fix is in the shared-cache init path, not the kernel.

This is deliberately NOT a CS-blob structural rung: `cs_invalid_page` is tolerated and singular, so
chasing CS now would be treating a red herring as the frontier ([[mi4-measurement-defects]]).