# D13 tree — census of dormant HD2/LEO board gates (recon for rungs after 992)

Read-only census taken 2026-10-12, while 992 (`STAGE90_XNU_COMMPAGE_ALIAS`, opening the six
`IOS7LAB_COMMPAGE_ALIAS` gates) was being built. Root: `external/xnu-hd2-darwin13/xnu` (our build
tree). Method: `grep -rn` for the two board macros + `IOS7LAB`, machine-counted; a "dormant" line
carries a board macro and no `STAGE90_` escape on that line. Mi4 itself is
`BOARD_CONFIG_MSM8974_CANCRO` (`osfmk/arm/PlatformConfigs.h:42`, Krait/A9 class), so every
`(ARMPBA8 || LEO)` gate takes the non-HD2 arm today.

## Totals (lines / distinct files)

| marker | lines | files | opened by a STAGE90_ switch | dormant |
|---|---|---|---|---|
| `BOARD_CONFIG_ARMPBA8` | 122 | 29 | 6 | 116 |
| `BOARD_CONFIG_QSD8250_LEO` | 208 | 53 | 6 | 202 |
| `IOS7LAB` | 368 | 42 | 0 | 368 |

Dormant board gates: **307 lines in 60 files**. Line kinds: 303 `#if`, 1 `#elif`, 1 `#endif`
comment, 1 `#define`, 1 comment-only (`hw_lock.s:464`). `IOS7LAB` is not itself a gate: 57 defines/
include-guards, 311 `printf`/`IOLog("IOS7LAB …")` trace strings; the only behavioral IOS7LAB gates
are the six 992 just opened. Existing escapes: `SP_ALIGN` (pcb.c ×4), `COMMPAGE_ALIAS` (pmap.c ×4 +
commpage.c ×2), `MEM_TOTAL` (arm_vm_init.c ×2), `VMHINT_GUARD` (vm_resident.c:1376), `KEXTB_LOGIN`
(×3), `ENTRY_TABLE_BYTES` (×1), plus `STAGE90_HFS_ROOT_RW` (hfs_vfsops.c:186).

## The master switch

`IOS7_COMPAT_PLATFORM` = `(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)` —
`osfmk/arm/ios7_compat.h:5`, **31 uses in 13 files, no STAGE90 escape**. This is the umbrella for
most userspace-compat behavior below; a future rung likely adds one STAGE90 escape here rather than
to each site.

## Dormant gates by subsystem (307 lines)

- **osfmk — 155**: arm 79 (boot/pmap/trap/timebase/commpage/serial), kern 41 (sched_prim 13,
  sync_sema 11, thread_act 8, debug 5, ipc_kobject 4), ipc 27 (mach_msg 21, mach_kernelrpc 6),
  vm 6, mach 1 (`vm_param.h` VM_MAX_ADDRESS), console 1.
- **bsd — 86**: kern 72 (kern_exec 23, kern_sig 14, pthread_support 13, pthread_synch 8,
  kern_exit 7, bsd_init 6, kern_fork 1), dev 12, vfs 2.
- **iokit — 57**: Kernel 30 (IOUserClient 16, IOService 14), Drivers 20 (Leo/Lab backends),
  bsddev 5, KernelConfigTables 2.
- **pexpert — 9** (leo timer, pe_realview, kprintf/init/identify/bringup, semihost).

~2/3 of dormant lines are lab trace/telemetry (`IOS7LAB …`, `LEO_LOG_PAYLOAD`, `ios7leo_*_note`);
the rest are HD2 functional alternates and ABI shims.

## Ranked candidates for iOS 7 userspace bring-up after 992

1. `osfmk/kern/syscall_sw.c:126,139,144` — mach trap table 15 (`ios7lab_mach_vm_map_trap`), 24/25
   (`mach_port_construct/destruct`) are `kern_invalid` unless IOS7_COMPAT_PLATFORM. libsystem/
   libdispatch call these — top candidate (needs the compat-macro escape).
2. `osfmk/vm/vm_shared_region.c:626` — iOS7 dyld shared cache at **0x2C000000** (64 MiB prefix in
   `vm_shared_region_enter`) gated by IOS7_COMPAT_PLATFORM.
3. `osfmk/vm/vm_map.c:11440,11539` + `bsd/vm/vm_unix.c:1130,1174` — `VM_FLAGS_RETURN_DATA_ADDR` /
   64-bit wire copyout for shared-cache mappings.
4. `bsd/kern/kern_descrip.c:1545` — bounded CodeDirectory blob allowance (1,535,514-byte superblob)
   for `dyld_shared_cache_armv7` code-sign validation.
5. `bsd/kern/kern_fork.c:1098,1146` — reserve early PIDs (launchd keeps real PID 1).
6. `osfmk/arm/status.c:62` — `ios7lab_user_stack()` / USRSTACK 0x2BE00000 stack reporting.
7. `bsd/kern/pthread_synch.c:228,1082` (default stack hint 0x20000000) and `:360-372`
   (zero arm_thread_state, Thumb cpsr bit, `pc &= ~1`) — pthread_create ABI for first threads.
8. `osfmk/arm/traps_lo.s:162,264` — `ios7lab_timebase:` −3 SWI fallback returning the real 64-bit
   clock via `swi_exit64`; negated twin at `:116` currently takes the early `cmn r12,#3` path.
9. `osfmk/arm/commpage/commpage.c:87-103` — remaining nested LEO gates: `_COMM_PAGE_CPUFAMILY`
   and cache-line size (Mi4 falls through to Cortex-A8 constants).
10. `bsd/vfs/vfs_subr.c:966,975` — root-mount RW overrides (`ios7leo_cow_root_writable` /
    boot-arg `ios7lab_root_rw`); `bsd/dev/unix_startup.c:272` — mbuf cluster floor 516.
11. `osfmk/ipc/mach_msg.c` (21 gates) + `ipc_kobject.c:266-525` + `mach_kernelrpc.c:276-457` —
    mostly tracing, but 408-437 include a real `ios7lab_vmabi` boot-arg branch, inside
    launchd's bootstrap-port send/receive path.
12. `osfmk/arm/trap.c:748,805,899,943` — `ios7lab_fault_witness` dyld instruction-fault
    instrumentation; `:611` ARMPBA8 bounded backtrace. `bsd/kern/kern_sig.c:1699,1932` —
    signal-metadata preservation.
13. `osfmk/arm/pmap.c` remaining 17 LEO gates — inert on Mi4 but the same board predicates 992's
    six used; a future pmap rung would touch them.
14. `osfmk/mach/arm/vm_param.h:63` — `VM_MAX_ADDRESS` 0x40000000 vs 0x80000000 (user VA ceiling).

Lower priority (diagnostic-only): kern_exec traces, kern_exit, pthread_support, sync_sema,
sched_prim/thread_act LEO_TS061 tracing, debug.c panic capture, iokit registration logging,
IOKitBSDInit, pexpert LEO putc/timer, bsd_init Leo boot prints.

## What this is for

After 992's press reads the next wall (the 991 log's frontier was userspace's commpage read at
0xffff4090), the first wall named by the log should be matched against this list before any new
probe is designed — several candidate gates sit exactly in launchd's early path (mach traps,
shared-cache prefix, PID reservation, pthread ABI).