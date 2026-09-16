# Experiment 81: Stage78 early pmap/platform-init micro-sequence

## Summary

Stage78 is Method-C Level 2 after the Stage77 entry stub.  It keeps the Stage-owned `_start` / `arm_init`-shaped path and adds a Stage-owned early pmap/platform-init micro-sequence after the arm-init-shaped handoff:

```text
loader -> stage78_xnu_start_stub(args, result)
       -> stage78_arm_init_stub(args, result)
       -> stage78_xnu_early_pmap_platform_init_run(args, result)
       -> stage78_arm_init_stub
       -> loader
```

This proves a more realistic post-`arm_init` early-kernel sequence on Xiaomi Mi 4 `cancro` while still avoiding public XNU `_start` / `arm_init`, public pmap/pexpert/IOKit runtime, generated Mach-O execution, live pmap installation, TTBR0/TTBCR/DACR/SCTLR writes, TLB invalidation, cache-policy changes, and persistent writes.

## New sources

```text
stage78/xnu_early_pmap_platform_init.c
```

Retained/extended entry sources:

```text
stage78/xnu_entry_start.S
stage78/xnu_entry_stub.c
```

Main symbols:

```c
uint32_t stage78_xnu_start_stub(struct boot_args *args,
                                struct stage78_xnu_entry_stub_result *result);
uint32_t stage78_arm_init_stub(struct boot_args *args,
                               struct stage78_xnu_entry_stub_result *result);
int stage78_xnu_entry_stub_run(struct boot_args *args);
const struct stage78_xnu_entry_stub_result *stage78_xnu_entry_stub_result(void);

int stage78_xnu_early_pmap_platform_init_run(
    struct boot_args *args,
    struct stage78_xnu_entry_stub_result *entry_result);
const struct stage78_xnu_early_pmap_platform_init_result *
stage78_xnu_early_pmap_platform_init_result(void);
```

## Execution path

The retained boot path is:

```text
start.S -> stage78_main() -> kernel_entry(&boot_args) -> stage78_loader_preflight_run(args)
```

Inside `stage78_loader_preflight_run()`:

1. Existing Mach-O fixture checks run.
2. Existing workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x78000001`.
4. Stage78 calls `stage78_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR before and after calling `stage78_xnu_start_stub(args, result)`.
6. The assembly stub marks the start-shaped entry as reached and performs a real `bl stage78_arm_init_stub`.
7. The arm-init-shaped C stub validates `boot_args` and calls `stage78_xnu_early_pmap_platform_init_run(args, result)`.
8. The early-init micro-sequence samples TTBR0/TTBCR/DACR/SCTLR, imports the pmap-transition dry-run and PE_state platform facts, records safety counters, and returns.
9. The loader copies both the entry-stub and early-init result ABIs into `struct stage78_loader_preflight` and requires both roll-ups to be OK.

## Entry-stub result ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required satisfied mask:

```text
0x00001fff
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
```

The assembly ABI prefix remains compatible with Stage77 offsets:

```text
start_entered=48
arm_init_called=52
arm_init_returned=56
arm_init_status=64
```

Stage78 appends early-init summary fields before the entry checksum:

```c
uint32_t early_pmap_platform_init_called;
uint32_t early_pmap_platform_init_returned;
uint32_t early_pmap_platform_init_status;
uint32_t early_pmap_platform_init_checksum;
```

## Early-init result ABI

Required satisfied mask:

```text
0x01ffffff
```

The early-init ABI records:

- independent `boot_args` and device-tree validation;
- source entry-stub activity;
- pmap-transition dry-run status/masks/checksum;
- candidate L1 base/limit/alignment;
- proposed TTBR0/TTBCR/DACR/SCTLR transition plan as read-only facts;
- representative translation proof PAs for kernel/workspace/ram-console/device cases;
- recovery-continuity inheritance;
- PE_state memory/CPU/GIC/timer/machine/vector facts;
- TTBR0/TTBCR/DACR/SCTLR before/after samples;
- explicit public-runtime/generated-Mach-O/live-pmap/control-register/TLB/cache/persistent-write counters;
- an XOR checksum over the structure prefix.

Required early-init satisfied bits:

```text
0x00000001 boot_args valid
0x00000002 device tree valid
0x00000004 source entry stub active
0x00000008 source pmap-transition dry-run OK
0x00000010 source platform facts valid
0x00000020 control registers sampled
0x00000040 control registers unchanged
0x00000080 candidate L1 plan imported
0x00000100 proposed control-register plan recorded read-only
0x00000200 translation continuity recorded
0x00000400 recovery continuity inherited
0x00000800 PE_state initialized/validated
0x00001000 platform facts matched
0x00002000 local Stage-owned sequence only
0x00004000 no public XNU start/arm_init execution
0x00008000 no public pmap runtime execution
0x00010000 no public pexpert runtime execution
0x00020000 no public IOKit runtime execution
0x00040000 no Mach-O fixture execution
0x00080000 no live pmap install
0x00100000 no TTBR/TTBCR/DACR/SCTLR writes
0x00200000 no TLB invalidate
0x00400000 no cache policy change
0x00800000 no persistent write
0x01000000 safety boundary preserved
```

## Loader roll-up

The top-level loader mask is already saturated:

```text
loader_satisfied_mask=0xffffffff
```

Stage78 therefore keeps the loader satisfied-mask ABI unchanged.  The early-init result is instead included by:

- copying `struct stage78_xnu_early_pmap_platform_init_result` into the loader preflight block;
- logging `loader_xnu_early_init_*` status/mask/checksum/roll-up markers;
- ORing `xnu_early_pmap_platform_init_failure_mask` into the final loader failure mask;
- requiring `xnu_early_pmap_platform_init_status_rollup == 0x78000001` for final loader success.

## Boundary update

Stage78 active command-line markers include:

```text
xnu-entry-stub xnu-early-init early-pmap-platform stage-owned-early-init arm-init-stub no-pub-start no-pub-arm-init no-pub-pmap no-pub-pexpert
```

Interpretation:

- Stage78 executes Stage-owned entry-stub, arm-init-stub, and early-init code.
- Stage78 does not execute public XNU `_start` / `arm_init`.
- Stage78 does not execute public pexpert/pmap/IOKit proof objects.
- Stage78 does not execute the generated Mach-O fixture.
- Stage78 does not install live pmap tables, write control registers for a pmap transition, invalidate TLBs, change cache policy, or persistently write storage.

## Local validation

Local validation passed:

```text
stage78/build.sh: success
stage78-qcdt.img dt_size=2521088 (0x267800)
stage78.elf undefined symbols: none
stage78-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 246 bytes including NUL
chosen boot-args string: 217 bytes including NUL
Android boot-image cmdlines: 1480 bytes including NUL
stale Stage77 tokens in stage78/: none
```

Disassembly confirms real calls:

```text
bl stage78_xnu_start_stub
bl stage78_arm_init_stub
bl stage78_xnu_early_pmap_platform_init_run
```

Build outputs:

```text
stage78.elf: 476144 bytes
stage78.bin: 423968 bytes
stage78.img: 428032 bytes
stage78-qcdt.img: 2949120 bytes
stage78-qcdt.img sha256=98abb7127d7f67358e7aad072c67bf2df5b95d7d50a6db462bd40de402e4b2be
```

## Hardware validation result

Hardware validation passed and remained non-persistent:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage78/stage78-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage78-early-pmap-platform-last_kmsg.txt
```

Captured log:

```text
/tmp/cancro-stage78-early-pmap-platform-last_kmsg.txt
254756 bytes
```

Confirmed hardware markers:

```text
MI4IOS6_STAGE78_XNU stage78_arm_init_stub: entered Stage-owned _start/arm_init-shaped path
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_status=0x78000001
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_satisfied_mask=0x01ffffff
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_failure_mask=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_checksum=0xe2f3b509
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_source_entry_status=0x78000001
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_source_pmap_transition_status=0x78000001
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_source_pmap_transition_satisfied_mask=0x00ffffff
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_source_pmap_transition_failure_mask=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_source_pmap_transition_checksum=0x9bed4bd1
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_control_registers_unchanged=0x00000001
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_platform_facts_valid=0x00000001
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_safety_boundary_preserved=0x00000001
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_live_pmap_installed=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_ttbr_written=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_tlb_invalidated=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_cache_policy_changed=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_early_init_persistent_write_attempted=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_entry_stub_status=0x78000001
MI4IOS6_STAGE78_XNU stage78_xnu_entry_stub_satisfied_mask=0x00001fff
MI4IOS6_STAGE78_XNU stage78_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE78_XNU stage78_xnu_entry_stub_early_init_status=0x78000001
MI4IOS6_STAGE78_XNU loader_xnu_entry_stub_status_rollup=0x78000001
MI4IOS6_STAGE78_XNU loader_xnu_early_init_status=0x78000001
MI4IOS6_STAGE78_XNU loader_xnu_early_init_satisfied_mask=0x01ffffff
MI4IOS6_STAGE78_XNU loader_xnu_early_init_failure_mask=0x00000000
MI4IOS6_STAGE78_XNU loader_xnu_early_init_checksum=0xe2f3b509
MI4IOS6_STAGE78_XNU loader_xnu_early_init_status_rollup=0x78000001
MI4IOS6_STAGE78_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE78_XNU loader_status=0x78000001
MI4IOS6_STAGE78 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change was performed.

## Next stage direction

After Stage78 is hardware-validated, the next step should continue Method-C by expanding the still-Stage-owned early initialization chain.  Good candidates are a more XNU-shaped `PE_init_platform(FALSE, args)` micro-sequence or a stricter pmap handoff verifier that still refuses public pmap runtime, live pmap install, TLB invalidation, cache-policy changes, and persistent writes.
