# Stage80 — Stage-owned arm_init post-PE_FALSE bootstrap/timebase-registration boundary

Stage80 continues the Method-C live bring-up path after the hardware-validated Stage79 `PE_init_platform(FALSE,args)` result (`ee6f5b9`). Stage79 proved that the controlled Stage-owned `_start` / `arm_init`-shaped handoff can invoke a Stage-owned early pmap/platform-init micro-sequence and then a Stage-owned `PE_init_platform(FALSE,args)`-shaped pre-VM pexpert/platform micro-sequence. Stage80 adds the next still-Stage-owned public ARM XNU `arm_init` region: CPU topology/master-CPU facts, BootCpuData/CpuDataEntries-shaped facts, bootstrap ordering facts, rtclock/timebase-registration facts, selected pre-VM boot-arg parsing, and an explicit stop before `arm_vm_init()`.

This is still not a public XNU boot. Stage80 does not enter public XNU `_start` or `arm_init`, does not execute public `thread_bootstrap`, `cpu_bootstrap`, `rtclock_early_init`, `kernel_early_bootstrap`, `cpu_init`, `processor_bootstrap`, or `arm_vm_init`, does not call public `PE_init_platform`, `DTInit`, or `pe_identify_machine`, does not execute public pexpert/pmap/IOKit runtime objects, does not execute the generated Mach-O fixture, does not install live pmap tables, does not write TTBR0/TTBCR/DACR/SCTLR for the proposed XNU pmap plan, does not invalidate TLBs, does not change cache policy, and does not perform persistent writes.

## New / changed sources

```text
stage80/xnu_entry_start.S
stage80/xnu_entry_stub.c
stage80/xnu_early_pmap_platform_init.c
stage80/xnu_pe_init_platform_false.c
stage80/xnu_arm_init_post_pe_bootstrap.c
stage80/macho_probe.c
stage80/build.sh
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

The Stage80 target path remains:

```text
start.S -> stage80_main() -> kernel_entry(&boot_args) -> stage80_loader_preflight_run(args)
```

Inside `stage80_loader_preflight_run()`:

1. Retained Mach-O fixture and XNU loader dry-run checks run.
2. Retained workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x80000001`.
4. Stage80 calls `stage80_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR and calls `stage80_xnu_start_stub(args, &g_stage80_xnu_entry_stub_result)`.
6. The assembly stub marks entry and performs a real branch-and-link to `stage80_arm_init_stub(args, result)`.
7. The C `arm_init`-shaped stub validates `boot_args`, invokes `stage80_xnu_early_pmap_platform_init_run(args, result)`, invokes `stage80_xnu_pe_init_platform_false_run(args, result)`, then invokes `stage80_xnu_arm_init_post_pe_bootstrap_run(args, result)`.
8. The post-PE_FALSE bootstrap/timebase micro-sequence imports the PE-init result, validates boot args/command-line markers, inherits PE_state/device-tree/platform facts, models CPU topology, BootCpuData, CpuDataEntries, bootstrap ordering, timebase callback registration, and selected boot-arg parse facts, samples TTBR0/TTBCR/DACR/SCTLR unchanged, records forbidden-operation counters, and stops before `arm_vm_init`.
9. The loader copies the entry-stub, early-init, PE-init-platform-false, and post-PE bootstrap result ABIs into `struct stage80_loader_preflight` and requires all roll-ups to be OK.

```text
entry stub -> arm-init stub -> early pmap/platform init -> PE_init_platform(FALSE)-shaped pre-VM init -> post-PE_FALSE bootstrap/timebase-registration-shaped init -> loader
```

## Entry-stub ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required mask:

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

The Stage80 entry-result prefix keeps the Stage77/Stage78/Stage79 assembly offsets stable:

```text
start_entered:     48
arm_init_called:   52
arm_init_returned: 56
arm_init_status:   64
```

The Stage80-only summary fields are appended before the entry checksum:

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

## PE_init_platform(FALSE)-shaped result ABI

PE-init-platform-false required mask:

```text
0x01ffffff
```

The retained PE-init-platform-false result records:

- source early-init status/masks/checksum;
- independent `boot_args` validation and compact command-line marker checks;
- PE_state boot args pointer, device-tree head/length, memory, CPU count, machine type, vector base, GIC bases, timer base, and timer frequency;
- DTInit-shaped facts without calling public `DTInit`;
- Apple-DT facts for `/device-tree`, `/chosen`, `/memory`, `/cpus`, `/interrupt-controller`, and `/timer`;
- identify-machine-shaped model/compatible/target-type checks without calling public `pe_identify_machine`;
- explicit public-runtime/generated-Mach-O/live-pmap/control-register/TLB/cache/persistent-write counters;
- TTBR0/TTBCR/DACR/SCTLR before/after samples;
- an XOR checksum over all words before `checksum`.

## Post-PE_FALSE bootstrap/timebase result ABI

Post-PE bootstrap required mask:

```text
0x01ffffff
```

Satisfied bits:

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

The post-PE bootstrap result records:

- source PE-init-platform-false and early-init status/masks/checksum;
- independent `boot_args`, fixed `CommandLine[256]`, and Stage80 marker checks;
- inherited PE_state/platform facts: CPU count, machine type, vector base, GIC bases, timer base, and 19.2 MHz timer frequency;
- Stage-owned `ml_parse_cpu_topology()` facts for cancro: CPU count `4`, boot CPU `0`, master CPU `0`, max CPU number `3`;
- Stage-owned BootCpuData-shaped fields including CPU number/count, machine type, vector base, interrupt/FIQ stack tops, timer base, and timebase frequency;
- Stage-owned CpuDataEntries-shaped fields for master CPU index `0`;
- ordered bootstrap facts for thread, cpu, rtclock, kernel-early, cpu-init, and processor-bootstrap shapes without public calls;
- a Stage-owned `PE_register_timebase_callback`-shaped callback pointer/checksum and `0x0124f800` frequency fact;
- boot-arg parse facts for `diag`, `maxmem`, `debug=0x144`, and `immediate_NMI`;
- explicit `stop_before_arm_vm_init` and `arm_vm_init_not_called` facts;
- explicit zero counters for public bootstrap/runtime routines, public `arm_vm_init`, public pmap/pexpert/IOKit runtime, generated Mach-O execution, live pmap install, control-register writes, TLB invalidation, cache-policy change, and persistent writes;
- TTBR0/TTBCR/DACR/SCTLR before/after samples and unchanged flags;
- an XOR checksum over all words before `checksum`.

## Loader integration

The top-level loader satisfied mask remains saturated at:

```text
0xffffffff
```

Stage80 therefore does not add a new top-level loader satisfied bit. Instead the loader copies the full post-PE bootstrap/timebase result into `struct stage80_loader_preflight`, logs a dedicated status roll-up, ORs the post-PE failure mask into the final loader failure mask, and requires:

```text
loader_xnu_entry_stub_status_rollup=0x80000001
loader_xnu_early_init_status_rollup=0x80000001
loader_xnu_pe_init_platform_false_status_rollup=0x80000001
loader_xnu_arm_init_post_pe_bootstrap_status_rollup=0x80000001
```

## Boundary

Stage80 executes only Stage-owned entry/arm-init/early-init/PE-init-platform-false/post-PE bootstrap code. It still does **not**:

- execute public XNU `_start` / `arm_init`,
- execute public `thread_bootstrap`, `cpu_bootstrap`, `rtclock_early_init`, `kernel_early_bootstrap`, `cpu_init`, `processor_bootstrap`, or `arm_vm_init`,
- call or execute public `PE_init_platform`,
- call or execute public `DTInit`,
- call or execute public `pe_identify_machine`,
- execute public XNU object-subset proof objects,
- execute public pexpert/platform runtime,
- execute public ARM VM/pmap runtime,
- execute public IOKit runtime,
- execute the generated Mach-O fixture,
- install live XNU/pmap tables,
- write TTBR0/TTBCR/DACR/SCTLR for the proposed pmap plan,
- invalidate TLBs for a pmap install,
- change cache policy,
- perform persistent writes.

Compact fixed `CommandLine[256]` Stage80 markers include:

```text
xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock no-armvm prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock
```

The Android boot-image cmdline also keeps higher-level audit markers such as `xnu-entry-stub`, `xnu-early-init`, `arm-init-stub`, `no-pub-start`, `no-pub-arm-init`, `no-pub-pmap`, `no-pub-pexpert`, `no-iokit-runtime-exec`, `no-macho-exec`, `no-live-pmap-install`, `no-tlb-invalidate`, `no-cache-change`, `no-persist-write`, and `no-external-mutation`.

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

## Hardware validation

Passed on real hardware (`cancro`, serial 4a2fe00b) via non-persistent `fastboot boot`. Stage80 booted, completed the full loader preflight plus the Stage-owned entry/early-init/PE-init-false/post-PE bootstrap path, returned success, and the device recovered cleanly to Android with no persistent change. Command sequence:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage80/stage80-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage80-arm-init-post-pe-bootstrap-last_kmsg.txt
```

Observed pass markers include:

```text
MI4IOS6_STAGE80_XNU stage80_xnu_entry_stub_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_entry_stub_satisfied_mask=0x00007fff
MI4IOS6_STAGE80_XNU stage80_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE80_XNU stage80_xnu_early_init_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_pe_init_platform_false_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_status=0x80000001
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_satisfied_mask=0x01ffffff
MI4IOS6_STAGE80_XNU stage80_xnu_arm_init_post_pe_bootstrap_failure_mask=0x00000000
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

No flash, erase, partition write, or persistent bootloader/storage change is part of Stage80 validation.
