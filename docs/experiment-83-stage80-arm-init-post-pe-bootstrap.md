# Experiment 83: Stage80 arm_init post-PE_FALSE bootstrap/timebase-registration boundary

## Summary

Stage80 is the next Method-C step after the hardware-validated Stage79 `PE_init_platform(FALSE,args)` handoff (`ee6f5b9`). It keeps the Stage-owned `_start` / `arm_init`-shaped path and extends the Stage-owned `arm_init` stub with the next in-order public ARM XNU region after `PE_init_platform(FALSE,args)` and before `arm_vm_init(xmaxmem,args)`:

```text
loader -> stage80_xnu_start_stub(args, result)
       -> stage80_arm_init_stub(args, result)
       -> stage80_xnu_early_pmap_platform_init_run(args, result)
       -> stage80_xnu_pe_init_platform_false_run(args, result)
       -> stage80_xnu_arm_init_post_pe_bootstrap_run(args, result)
       -> stage80_arm_init_stub
       -> loader
```

The new Stage-owned post-PE_FALSE micro-sequence records CPU topology/master-CPU facts, BootCpuData/CpuDataEntries-shaped facts, bootstrap ordering facts, rtclock/timebase-registration facts, selected pre-VM boot-arg parse facts, unchanged control-register samples, forbidden-operation counters, and an explicit stop before `arm_vm_init`.

This models the public ARM XNU `arm_init` sequence on Xiaomi Mi 4 `cancro` while still avoiding public XNU `_start` / `arm_init`, public `thread_bootstrap`, `cpu_bootstrap`, `rtclock_early_init`, `kernel_early_bootstrap`, `cpu_init`, `processor_bootstrap`, public `arm_vm_init`, public `PE_init_platform`, public `DTInit`, public `pe_identify_machine`, public pexpert/pmap/IOKit runtime, generated Mach-O execution, live pmap installation, TTBR0/TTBCR/DACR/SCTLR writes, TLB invalidation, cache-policy changes, and persistent writes.

## New sources

```text
stage80/xnu_arm_init_post_pe_bootstrap.c
```

Retained/extended entry sources:

```text
stage80/xnu_entry_start.S
stage80/xnu_entry_stub.c
stage80/xnu_early_pmap_platform_init.c
stage80/xnu_pe_init_platform_false.c
stage80/macho_probe.c
```

Main symbols:

```c
uint32_t stage80_xnu_start_stub(struct boot_args *args,
                                struct stage80_xnu_entry_stub_result *result);
uint32_t stage80_arm_init_stub(struct boot_args *args,
                               struct stage80_xnu_entry_stub_result *result);
int stage80_xnu_entry_stub_run(struct boot_args *args);
const struct stage80_xnu_entry_stub_result *stage80_xnu_entry_stub_result(void);

int stage80_xnu_early_pmap_platform_init_run(
    struct boot_args *args,
    struct stage80_xnu_entry_stub_result *entry_result);
const struct stage80_xnu_early_pmap_platform_init_result *
stage80_xnu_early_pmap_platform_init_result(void);

int stage80_xnu_pe_init_platform_false_run(
    struct boot_args *args,
    struct stage80_xnu_entry_stub_result *entry_result);
const struct stage80_xnu_pe_init_platform_false_result *
stage80_xnu_pe_init_platform_false_result(void);

int stage80_xnu_arm_init_post_pe_bootstrap_run(
    struct boot_args *args,
    struct stage80_xnu_entry_stub_result *entry_result);
const struct stage80_xnu_arm_init_post_pe_bootstrap_result *
stage80_xnu_arm_init_post_pe_bootstrap_result(void);
```

## Execution path

The retained boot path is:

```text
start.S -> stage80_main() -> kernel_entry(&boot_args) -> stage80_loader_preflight_run(args)
```

Inside `stage80_loader_preflight_run()`:

1. Existing Mach-O fixture checks run.
2. Existing workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x80000001`.
4. Stage80 calls `stage80_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR before and after calling `stage80_xnu_start_stub(args, result)`.
6. The assembly stub marks the start-shaped entry as reached and performs a real `bl stage80_arm_init_stub`.
7. The arm-init-shaped C stub validates `boot_args`, calls `stage80_xnu_early_pmap_platform_init_run(args, result)`, calls `stage80_xnu_pe_init_platform_false_run(args, result)`, then calls `stage80_xnu_arm_init_post_pe_bootstrap_run(args, result)`.
8. The post-PE_FALSE bootstrap/timebase micro-sequence imports the PE-init result, revalidates boot args and command-line markers, inherits `PE_state` / device-tree / platform facts, models CPU topology, BootCpuData, CpuDataEntries, bootstrap ordering, and timebase registration, parses selected boot args, samples TTBR0/TTBCR/DACR/SCTLR, records safety counters, and returns before `arm_vm_init`.
9. The loader copies the entry-stub, early-init, PE-init-platform-false, and post-PE bootstrap result ABIs into `struct stage80_loader_preflight` and requires all roll-ups to be OK.

## Entry-stub result ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required satisfied mask:

```text
0x00007fff
```

Satisfied bits:

```text
0x00000001 start stub called
0x00000002 start stub entered
0x00000004 arm_init stub called
0x00000008 arm_init stub returned
0x00000010 return status OK
0x00000020 boot_args valid
0x00000040 device tree valid
0x00000080 magic OK
0x00000100 output OK
0x00000200 TTBR0/SCTLR unchanged
0x00000400 no exception observed
0x00000800 safety boundary preserved
0x00001000 early pmap/platform init OK
0x00002000 PE_init_platform(FALSE)-shaped init OK
0x00004000 post-PE bootstrap/timebase-registration init OK
```

The assembly ABI prefix remains compatible with Stage77/Stage78/Stage79 offsets:

```text
start_entered=48
arm_init_called=52
arm_init_returned=56
arm_init_status=64
```

Stage80 appends the post-PE bootstrap summary fields after the retained early-init and PE-init summary fields and before the entry checksum:

```c
uint32_t early_pmap_platform_init_called;
uint32_t early_pmap_platform_init_returned;
uint32_t early_pmap_platform_init_status;
uint32_t early_pmap_platform_init_checksum;
uint32_t pe_init_platform_false_called;
uint32_t pe_init_platform_false_returned;
uint32_t pe_init_platform_false_status;
uint32_t pe_init_platform_false_checksum;
uint32_t arm_init_post_pe_bootstrap_called;
uint32_t arm_init_post_pe_bootstrap_returned;
uint32_t arm_init_post_pe_bootstrap_status;
uint32_t arm_init_post_pe_bootstrap_checksum;
```

## Post-PE bootstrap/timebase result ABI

Required satisfied mask:

```text
0x01ffffff
```

The post-PE bootstrap ABI records:

- source PE-init-platform-false status/masks/checksum and source early-init status/checksum;
- independent boot-args, device-tree, and compact command-line marker validation;
- inherited PE_state/device-tree/platform facts from Stage79's `PE_init_platform(FALSE,args)`-shaped result;
- CPU topology facts: cancro CPU count `4`, boot CPU `0`, master CPU `0`, max CPU number `3`, and master CPU validity;
- BootCpuData-shaped facts: local pointer/checksum, CPU number/count, machine type, vector base, interrupt/FIQ stack-top facts, timer pointer/readiness, and timer/timebase facts;
- CpuDataEntries-shaped facts for selected index `0`, including local entry pointer/checksum and self/virtual/physical consistency booleans;
- ordered bootstrap facts for thread, cpu, rtclock, kernel-early, cpu-init, and processor-bootstrap shapes;
- rtclock/timebase facts: Stage-owned callback pointer/checksum, callback registered, `0x0124f800` timebase frequency, initial timebase sample, and PE-register-timebase-callback-shaped status;
- boot-arg parse facts for `diag`, `maxmem`, `debug=0x144`, and `immediate_NMI`;
- explicit stop-before-`arm_vm_init` and public `arm_vm_init`-not-called facts;
- TTBR0/TTBCR/DACR/SCTLR before/after samples;
- explicit public-runtime/generated-Mach-O/live-pmap/control-register/TLB/cache/persistent-write counters;
- an XOR checksum over the structure prefix.

Required satisfied bits:

```text
0x00000001 source PE_init_platform(FALSE)-shaped result OK
0x00000002 boot_args captured/valid
0x00000004 PE_state/device-tree/platform facts inherited
0x00000008 CPU topology parsed
0x00000010 master CPU selected and valid
0x00000020 BootCpuData-shaped facts populated
0x00000040 CpuDataEntries-shaped facts populated
0x00000080 vector/interrupt/FIQ stack facts recorded
0x00000100 thread_bootstrap-shaped order recorded
0x00000200 cpu_bootstrap-shaped order recorded
0x00000400 rtclock_early_init/timebase callback registered in Stage-owned form
0x00000800 kernel_early_bootstrap/cpu_init/processor_bootstrap order recorded
0x00001000 pre-VM boot-arg parse facts recorded
0x00002000 explicit stop-before-arm_vm_init recorded
0x00004000 public bootstrap/runtime routines blocked
0x00008000 public rtclock/timebase runtime blocked
0x00010000 control registers sampled
0x00020000 control registers unchanged
0x00040000 local Stage-owned sequence only
0x00080000 no public XNU start/arm_init execution
0x00100000 no public arm_vm_init/pmap runtime execution
0x00200000 no public pexpert/IOKit runtime execution
0x00400000 no Mach-O fixture execution
0x00800000 no pmap/control-register/TLB/cache/persistent mutation
0x01000000 safety boundary preserved
```

## Loader roll-up

The top-level loader mask is already saturated:

```text
loader_satisfied_mask=0xffffffff
```

Stage80 therefore keeps the loader satisfied-mask ABI unchanged. The post-PE bootstrap/timebase result is included by:

- copying `struct stage80_xnu_arm_init_post_pe_bootstrap_result` into the loader preflight block;
- logging `loader_xnu_arm_init_post_pe_bootstrap_*` status/mask/checksum/roll-up markers;
- ORing `xnu_arm_init_post_pe_bootstrap_failure_mask` into the final loader failure mask;
- requiring `xnu_arm_init_post_pe_bootstrap_status_rollup == 0x80000001` for final loader success.

## Boundary update

Fixed `CommandLine[256]` Stage80 markers include:

```text
xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock no-armvm prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock
```

Interpretation:

- Stage80 executes Stage-owned entry-stub, arm-init-stub, early-init, PE-init-platform-false, and post-PE bootstrap/timebase code.
- Stage80 does not execute public XNU `_start` / `arm_init`.
- Stage80 does not execute public `thread_bootstrap`, `cpu_bootstrap`, `rtclock_early_init`, `kernel_early_bootstrap`, `cpu_init`, `processor_bootstrap`, or `arm_vm_init`.
- Stage80 does not call or execute public `PE_init_platform`, `DTInit`, or `pe_identify_machine`.
- Stage80 does not execute public pexpert/pmap/IOKit proof objects.
- Stage80 does not execute the generated Mach-O fixture.
- Stage80 does not install live pmap tables, write control registers for a pmap transition, invalidate TLBs, change cache policy, or persistently write storage.

## Local validation

Local validation passed:

```text
stage80/build.sh: success
stage80-qcdt.img dt_size=2521088 (0x267800)
stage80.elf undefined symbols: none
stage80-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 227 bytes including NUL
chosen boot-args string: 198 bytes including NUL
Android boot-image cmdlines: 1372 bytes including NUL
stale Stage79 tokens in stage80 code/build files: none
legacy failure status OR collisions: none outside STAGE80_STATUS_FAIL macro
```

Disassembly confirms real calls:

```text
bl stage80_xnu_start_stub
bl stage80_arm_init_stub
bl stage80_xnu_early_pmap_platform_init_run
bl stage80_xnu_pe_init_platform_false_run
bl stage80_xnu_arm_init_post_pe_bootstrap_run
```

Build outputs:

```text
stage80.elf: 501608 bytes
stage80.bin: 448540 bytes
stage80.img: 452608 bytes
stage80-qcdt.img: 2973696 bytes
stage80-qcdt.img sha256=4fe4cc4bbf4a56acbdc933dbc010f656d3d70a0924f209233af47a891e288349
```

## Hardware validation result

Passed on real hardware (`cancro`, serial 4a2fe00b) via non-persistent `fastboot boot`. The device booted Stage80, ran the full loader preflight and the Stage-owned entry/early-init/PE-init-false/post-PE bootstrap path, returned success, and recovered cleanly to Android with no persistent change. Command sequence used:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage80/stage80-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage80-arm-init-post-pe-bootstrap-last_kmsg.txt
```

Observed pass markers:

```text
MI4IOS6_STAGE80_XNU stage80_xnu_entry_stub_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_entry_stub_satisfied_mask=0x00007fff
MI4IOS6_STAGE80_XNU stage80_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_early_init_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_pe_init_platform_false_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_satisfied_mask=0x01ffffff
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_failure_mask=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_source_pe_init_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_cpu_topology_parsed=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_cpu_count=0x00000004
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_master_cpu_valid=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_boot_cpu_data_populated=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_cpu_data_entries_populated=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_timebase_callback_registered=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_timebase_frequency=0x0124f800
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_boot_arg_parse_ok=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_stop_before_arm_vm_init=0x00000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_public_thread_bootstrap_executed=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_public_cpu_bootstrap_executed=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_public_rtclock_early_init_executed=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_public_arm_vm_init_executed=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_live_pmap_installed=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_tlb_invalidated=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_cache_policy_changed=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_persistent_write_attempted=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_safety_boundary_preserved=0x00000001
MI4IOS6_STAGE80_XNU loader_xnu_entry_stub_status_rollup=0x80000001
MI4IOS6_STAGE80_XNU loader_xnu_early_init_status_rollup=0x80000001
MI4IOS6_STAGE80_XNU loader_xnu_pe_init_platform_false_status_rollup=0x80000001
MI4IOS6_STAGE80_XNU loader_xnu_arm_init_post_pe_bootstrap_status_rollup=0x80000001
MI4IOS6_STAGE80_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE80_XNU loader_status=0x80000001
MI4IOS6_STAGE80 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change is part of the validation.
