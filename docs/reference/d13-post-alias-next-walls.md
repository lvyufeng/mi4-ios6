# D13 post-alias next walls — 993 capture-triage memo

Read-only recon; one file written. Sources read by value: `/tmp/cancro-last_kmsg-991.txt` (the 991 capture),
`external/xnu-hd2-darwin13/xnu` (OUR build tree; paths below are relative to it), the reference
`external/xnu-4570.1.46`, the IOS7MIN rootfs inside `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs`
(extracted this session to the fresh dir `/tmp/cp993-x/`; list at `/tmp/cp992-data/rootfs-7z-list.txt`).
Companion studies are cited, not duplicated: `d13-commpage-content-study.md` (field/writer/reader census),
`d13-syscall-abi-study.md` (deltas D0-D3), `d13-dyld-shared-cache-study.md` (gates G1-G4),
`d13-launchd-early-abi-study.md`, `d13-ios7-compat-platform-study.md`. Each claim is a machine reading with a
file:line / log-line, or explicitly marked **inference**.

## 1. What the 991 capture pins (measured)

- First abort log:4095 `launchd[1]: usermode data abort, EXC_BAD_ACCESS at 0xffff4090 in map ... (Translation
  fault on page)`; register block log:4096-4102; **231 identical blocks** through log:~5942
  (`grep -c 0xffff4090` = 231; `grep -c 'usermode data abort'` = 231). After that the kernel is alive
  (periodic `xnu_live_*` dumps): `grep -c 'Debugger called'` = 0, `grep -c 'panic('` = 0, `grep -c IOS7LAB` = 0.
  So 991 is **not** a panic and **not** a completed kill — the same fault repeats. What stops the loop at 231
  is not established (**inference**: a deferred/retry path in this tree's abort handling; no exit or panic
  print follows).
- pc 0x2be6a2c8 = dyld base 0x2be4f000 (slide +0x4f000; the three independent proofs are in
  `d13-syscall-abi-study.md` §4) + **0x1b2c8** = dyld `_mach_absolute_time+8` (file 0x1b2c0:
  `88 c0 04 e3 | ff cf 4f e3 | 08 00 dc e5 | 00 00 50 e3` = `movw ip,#0x4088; movt ip,#0xffff;
  ldrb r0,[ip,#8]; cmp r0,#0`). r12 = 0xffff4088, fault = 0xffff4090 ⇒ the faulting access is that `ldrb`.
- **lr 0x2be58a9f** (unslid 0x2be09a9f) resolves in dyld's own symtab to
  `ImageLoader::link(LinkContext const&, bool, bool, bool, RPathChain const&)+0x37` (symbol at VA 0x2be09a68):
  the Thumb `blx` is at file 0x9a9a = link+0x32. This **corrects** `d13-syscall-abi-study.md` §4's
  "0x2be19a9f = __platform_memchr+0x2b" (an arithmetic slip of 0x10000); everything else in that section stands.
- `ImageLoader::link` runs only after the main executable and its dependents are instantiated. Measured:
  `/sbin/launchd` (extracted; 154,736 B) has `LC_MAIN entryoff 0x30dd` and loads
  `/usr/lib/libbsm.0.dylib` + `/usr/lib/libSystem.B.dylib`; **neither is on disk** (7z list: `/usr/lib` holds
  only dyld + the two ios7lab witness dylibs, and the witnesses load libSystem too). Both must therefore come
  from the on-disk cache `IOS7MIN/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7`
  (315,952,666 B) whose image table (rootfs.hfs+0x455d000, magic `dyld_v1   armv7`) places
  `/usr/lib/libSystem.B.dylib` @0x37267000, `libsystem_kernel` @0x381c5000, `libdispatch` @0x380fc000.
  **Inference (strong): the cache was mapped and readable at those VAs before link() aborted.** Which route
  mapped it — the kernel's syscall 438 or dyld's private per-segment `mmap` shim (dyld VA 0x2be05878,
  `dyld::_shared_region_map_and_slide_np+0xdc`; loop at file 0x5810-0x5890) — is **not established**; §3 W6
  records the one measured constraint on it.
- Stack: sp 0x2fdb0870 (log:4100). USRSTACK = 0x2FE00000 (`osfmk/arm/status.c:59`) and the stack slides down by
  the exec slide (`bsd/kern/mach_loader.c:1096-1097 result->user_stack -= slide`; region created at
  `bsd/kern/kern_exec.c:3619-3622`) ⇒ top = 0x2fe00000 − 0x4f000 = **0x2fdb1000**; the exec's initial sp
  0x2fdb0f54 (log:4092,4094 — the "user pc was" print reads the saved-state field, `src/entry/entry_trace.c:3886`)
  is 0xac below it. dyld had used ~0x790 of stack at the abort.

## 2. What the alias changes, by value (the immediate next event)

With the alias mapped, the `ldrb` at 0xffff4090 now hits the alias page. D13's `commpage/commpage.c` never writes +0x90
(0 grep hits; the value is deliberately 0) ⇒ reads **0** ⇒ `beq` → `mvn ip,#2` (r12 = −3) → `svc #0x80` →
`fleh_swi` → `cmn r12,#3; beq swi_trap_tb` (`osfmk/arm/traps_lo.s:116-119`, compiled in on CANCRO) →
`swi_trap_tb: movs pc, lr` (`traps_lo.s:260-262`) → **returns with r0:r1 untouched: r0 = 0 (just loaded),
r1 = stale**. dyld stores r0/r1 as link()'s start time (file 0x9a9e-0x9aa2). No printf, no fault, no marker.
**Expected 993 signature: the pre-registered alias/vecl2 markers present, the 231× burst ABSENT, and no new
event at the old pc.** The wall moves from a crash to silence.

## 3. Ranked next walls (most likely first)

Format: (a) address/class (b) mechanism (c) the exact 993 log marker (d) new userspace abort vs kernel panic
vs silent.

1. **W1 — the −3 SWI silent return (the wall the alias uncovers).** (a) no address; user-space time values
   (b) every userspace `mach_absolute_time` (dyld file 0x1b2c0; libsystem_kernel same shape @0x381c5784) now
   takes the −3 route and gets r0 = 0 / r1 = stale (`traps_lo.s:260-262`); the correct arm
   (`:162-168`/`:264-268`) is compiled out on CANCRO (c) marker = the burst absent and **no** new fault at
   0xffff4090 (d) silent — if anything, a later hang with no print.
2. **W2 — launchd dies of an unhandled exception → kernel panic naming it.** (a) any fatal signal/exception
   reaching `exception_triage` with no handler → `task_terminate` (`osfmk/kern/exception.c:301-352`)
   (b) `bsd/kern/kern_exit.c:372-380`: `printf("pid 1 exited (signal %d, exit %d)")` then
   `panic("%s died\nState at Last Exception:\n\n%s")` (c) markers = `pid 1 exited` **and** `launchd died`
   (d) kernel panic (the first panic of the series, if it happens).
3. **W3 — the fatal-five mach traps (D1) as the trigger of W2.** (a) traps −15/−24/−25/−41/−42
   (b) slots are `kern_invalid` on CANCRO (`osfmk/kern/syscall_sw.c:126-170`; `ios7_compat.h:5`), served by
   `swi_mach_error` (`traps_lo.s:232-240`) which raises `EXC_SYSCALL` via `doexception` **with no printf**
   (c) no marker of its own; the visible event is W2's panic (d) silent-then-panic. Reachability **not**
   established: dyld's own fatal-five stubs have zero call sites (this session's census: dyld's only stub
   callers are −89/−28/−26/−19/−18/−16) — this corrects `d13-syscall-abi-study.md` §5's "D1 as dyld maps
   images and builds its port set". Exposure is via libsystem/libdispatch (their cache slices exist; census
   not completed).
4. **W4 — `panic("setup_wqthread")`.** (a) any first workqueue-thread creation
   (b) `bsd/kern/pthread_synch.c:2205` (wq_runreq) → `:2336 panic("setup_wqthread")` — the ARM path is a bare
   `#else panic` on CANCRO (c) marker = `panic(cpu N caller ...): setup_wqthread` (d) kernel panic.
   Reachability needs a libdispatch/workqueue thread in launchd's early life — unproven.
5. **W5 — first pthread created with a junk cpsr.** (a) a new thread's start pc/cpsr
   (b) bsdthread_create's ARM state gates are compiled out (`pthread_synch.c:358-380`): cpsr is never set;
   `thread_set_wq_state32` bzeroes the state but takes `cpsr = sanitise_cpsr(uninitialized)` (`status.c:547-583`)
   and `sanitise_cpsr` keeps bit 5/T (`status.c:112-123`) — the first thread can start in the wrong instruction
   set with an unmasked pc (c) marker = a NEW `usermode data abort`/prefetch abort with a wild or odd pc in
   launchd (d) userspace abort.
6. **W6 — cache/stack VA collision, if the cache mapping route ever changes.** (a) 0x2fdb0xxx
   (b) cache map[0] = [0x2c000000, 0x38266000) (cache header) contains the slid stack top 0x2fdb1000; this
   tree's `mmap(MAP_FIXED)` is `VM_FLAGS_FIXED|VM_FLAGS_OVERWRITE` (`bsd/kern/kern_mman.c:446-465`, comment
   ":457 will replace any existing mappings"), and dyld's private shim maps each segment with
   `prot = sfm_init_prot` (5 = r-x for map[0]; dyld file 0x5864-0x5876) ⇒ the stack page would be replaced by
   cache-file pages (c) marker = new `usermode data abort ... (Permission fault on page)` at ~0x2fdb0xxx
   (d) userspace abort. **Measured constraint: this did NOT happen in 991** — sp 0x2fdb0870 stayed writable
   through link+0x32 — so **inference: 991's mapping did not take that route for map[0]**; the hazard returns
   if a later rung changes the mapping.
7. **W7 — frozen-clock silent-wrong.** (a) commpage +0x40/+0x4C vs +0x58/0x60/0x64 (b) the first
   kernel-serviced `gettimeofday` writes TimeBase (`osfmk/arm/rtclock.c:341→350`) while
   magic/add/shift stay 0 → userspace's fast-time multiply is by zero (`d13-commpage-content-study.md` §3(g))
   (c) no marker (d) silent.
8. **W8 — BSD syscalls ≥440 return ENOSYS.** (a) 440-455 (b) `NUM_SYSENT 440` (`bsd/sys/sysent.h:60`);
   `bsd/dev/arm/unix_syscalls.c:335` → `nosys` (c) no marker (d) silent errno
   (memorystatus_control 440, guarded_* 441-444, proc_rlimit_control 446).
   Not walls (verified satisfied by the same alias page, so already true in 991): +0x1E VERSION = 3
   (`commpage.c:92`), +0x20 = kUP, +0x22/+0x34/+0x35/+0x36 = 1 (libdispatch's heap sizing),
   +0x26 = 64, +0x38 = max_mem, +0x80 = CPUFAMILY_ARM_13 (libvDSP takes the ARM_13 path).

## 4. The 993-capture triage recipe (minutes)

1. `grep IOS7LAB` — expect the pre-registered lines (verbatim, `arm_vm_init.c:434/438/445`, `pmap.c:2863/2866/2874/2878`, `commpage.c:108`; script `scripts/build_993.sh:37-44`):
   `IOS7LAB vecl2: %u nonzero slot(s) in the stolen high-vector L2 table before zeroing` (+ one
   `IOS7LAB vecl2: slot 0x%02x = 0x%08x` per nonzero slot), `... after zeroing (VECTORS_BASE slot written next)`,
   `IOS7LAB commpage alias: L2 slot pre = 0x00000000` (or `... stale slot cleared (was 0x%08x)`),
   `IOS7LAB commpage alias: L2 slot post = 0x%08x (commpage pa 0x%08x)`,
   `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN`,
   `IOS7LAB commpage cpus: configured=1 active=1 physical=1 logical=1`. Missing ⇒ the alias did not land.
2. `grep -c 0xffff4090` — expect **0**. Non-zero ⇒ falsification; the wall is still +0x90.
3. `grep -n 'usermode data abort\|prefetch abort\|panic(\|pid 1 exited'` — the FIRST new event names the wall:
   the address/class table in §3 (a 0x2c000000-0x3c235000 fault = cache; ~0x2fdb0xxx = W6; a wild pc = W5;
   `setup_wqthread` = W4; `launchd died` = W2/W3).
4. No new event and the log continues with `xnu_live_*` dumps ⇒ the silent class (W1/W7/W8): read the last
   pid-1 activity lines and the live dump to see where it parked.
5. Do not read the absence of the burst as success by itself: the alias markers (1) are the success reading.

## 5. Corrections and open items

- Corrections: `d13-syscall-abi-study.md` §4 lr attribution (now ImageLoader::link+0x37) and §5's D1-on-dyld
  claim (dyld calls none of the five); the task premise "commpage fields still ZERO (VERSION, ...)" — VERSION
  (+0x1E) **is** written = 3 (`commpage.c:92`); the zero fields with proven readers are +0x88/+0x90 and the
  TOD half-fields (`d13-commpage-content-study.md` §2/§4). `d13-commpage-content-study.md`'s +0x90 =
  `_COMM_PAGE_USER_TIMEBASE` is confirmed by value twice: 4570 `osfmk/arm/cpu_capabilities.h:191-192`
  (u64 +0x88 "for constructing mach_absolute_time", u8 +0x90 "is userspace mach_absolute_time supported") and
  the artifact's own instruction shape (u8 gate at [ip+8] selecting an mrrc/TIMEBASE_OFFSET path at [ip]/[ip+4]).
- Open: which route mapped the cache in 991 (§1/W6's constraint); what stopped the 231-retry loop;
  whether launchd's next step is fork/exec of a LaunchDaemon (the plists exist in the rootfs) or an idle loop.