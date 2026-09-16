# Experiment 82: Stage79 PE_init_platform(FALSE,args)-shaped pre-VM pexpert/platform micro-sequence

## Summary

Stage79 is Method-C Level 3 after the hardware-validated Stage78 early pmap/platform-init result (`21fb079`).  It keeps the Stage-owned `_start` / `arm_init`-shaped path and adds a Stage-owned `PE_init_platform(FALSE, args)`-shaped pre-VM pexpert/platform micro-sequence after the Stage78 early-init handoff:

```text
loader -> stage79_xnu_start_stub(args, result)
       -> stage79_arm_init_stub(args, result)
       -> stage79_xnu_early_pmap_platform_init_run(args, result)
       -> stage79_xnu_pe_init_platform_false_run(args, result)
       -> stage79_arm_init_stub
       -> loader
```

This models the public ARM XNU pre-VM pexpert handoff shape on Xiaomi Mi 4 `cancro` while still avoiding public XNU `_start` / `arm_init`, public `PE_init_platform`, public `DTInit`, public `pe_identify_machine`, public pexpert/pmap/IOKit runtime, generated Mach-O execution, live pmap installation, TTBR0/TTBCR/DACR/SCTLR writes, TLB invalidation, cache-policy changes, and persistent writes.

## New sources

```text
stage79/xnu_pe_init_platform_false.c
```

Retained/extended entry sources:

```text
stage79/xnu_entry_start.S
stage79/xnu_entry_stub.c
stage79/xnu_early_pmap_platform_init.c
```

Main symbols:

```c
uint32_t stage79_xnu_start_stub(struct boot_args *args,
                                struct stage79_xnu_entry_stub_result *result);
uint32_t stage79_arm_init_stub(struct boot_args *args,
                               struct stage79_xnu_entry_stub_result *result);
int stage79_xnu_entry_stub_run(struct boot_args *args);
const struct stage79_xnu_entry_stub_result *stage79_xnu_entry_stub_result(void);

int stage79_xnu_early_pmap_platform_init_run(
    struct boot_args *args,
    struct stage79_xnu_entry_stub_result *entry_result);
const struct stage79_xnu_early_pmap_platform_init_result *
stage79_xnu_early_pmap_platform_init_result(void);

int stage79_xnu_pe_init_platform_false_run(
    struct boot_args *args,
    struct stage79_xnu_entry_stub_result *entry_result);
const struct stage79_xnu_pe_init_platform_false_result *
stage79_xnu_pe_init_platform_false_result(void);
```

## Execution path

The retained boot path is:

```text
start.S -> stage79_main() -> kernel_entry(&boot_args) -> stage79_loader_preflight_run(args)
```

Inside `stage79_loader_preflight_run()`:

1. Existing Mach-O fixture checks run.
2. Existing workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x79000001`.
4. Stage79 calls `stage79_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR before and after calling `stage79_xnu_start_stub(args, result)`.
6. The assembly stub marks the start-shaped entry as reached and performs a real `bl stage79_arm_init_stub`.
7. The arm-init-shaped C stub validates `boot_args`, calls `stage79_xnu_early_pmap_platform_init_run(args, result)`, then calls `stage79_xnu_pe_init_platform_false_run(args, result)`.
8. The PE-init-platform-false micro-sequence imports the early-init result, revalidates boot args and command-line markers, models `PE_state.bootArgs = args` and `PE_state.deviceTreeHead = args->deviceTreeP`, records DTInit-shaped and identify-machine-shaped facts via Stage-owned Apple-DT helpers, samples TTBR0/TTBCR/DACR/SCTLR, records safety counters, and returns.
9. The loader copies the entry-stub, early-init, and PE-init-platform-false result ABIs into `struct stage79_loader_preflight` and requires all roll-ups to be OK.

## Entry-stub result ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required satisfied mask:

```text
0x00003fff
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
```

The assembly ABI prefix remains compatible with Stage77/Stage78 offsets:

```text
start_entered=48
arm_init_called=52
arm_init_returned=56
arm_init_status=64
```

Stage79 appends PE-init-platform-false summary fields after the retained early-init summary fields and before the entry checksum:

```c
uint32_t early_pmap_platform_init_called;
uint32_t early_pmap_platform_init_returned;
uint32_t early_pmap_platform_init_status;
uint32_t early_pmap_platform_init_checksum;
uint32_t pe_init_platform_false_called;
uint32_t pe_init_platform_false_returned;
uint32_t pe_init_platform_false_status;
uint32_t pe_init_platform_false_checksum;
```

## PE-init-platform-false result ABI

Required satisfied mask:

```text
0x01ffffff
```

The PE-init-platform-false ABI records:

- source early-init status/masks/checksum;
- independent boot-args and device-tree validation;
- compact fixed/chosen command-line marker validation for `xnu-pe-init-false`, `prevm-pexpert`, `dtinit-facts`, `peid-machine`, `no-pub-peinit`, `no-pub-dtinit`, and `no-pub-peid`;
- PE_state boot args pointer, device-tree head/length, memory, CPU count, machine type, vector base, GIC bases, timer base, and timer frequency;
- DTInit-shaped facts: DT head/length captured, root parseable, and public `DTInit` not called;
- Apple-DT facts for `/device-tree`, `/chosen`, `/memory`, `/cpus`, `/interrupt-controller`, and `/timer`;
- pe_identify_machine-shaped facts: model, compatible, target-type, MSM8974, and cancro matches, with public `pe_identify_machine` not called;
- TTBR0/TTBCR/DACR/SCTLR before/after samples;
- explicit public-runtime/generated-Mach-O/live-pmap/control-register/TLB/cache/persistent-write counters;
- an XOR checksum over the structure prefix.

Required satisfied bits:

```text
0x00000001 source early-init OK
0x00000002 boot_args captured/valid
0x00000004 device tree captured/valid
0x00000008 PE_state captured
0x00000010 PE_state matched boot_args/device tree/platform facts
0x00000020 DTInit-shaped facts recorded
0x00000040 Apple-DT root/device-tree facts present
0x00000080 /chosen and boot-args present
0x00000100 /memory facts match
0x00000200 /cpus facts match
0x00000400 /interrupt-controller/GIC facts match
0x00000800 /timer facts match
0x00001000 pe_identify_machine-shaped facts recorded
0x00002000 platform facts matched
0x00004000 public pexpert runtime blocked
0x00008000 control registers sampled
0x00010000 control registers unchanged
0x00020000 local Stage-owned sequence only
0x00040000 no public XNU start/arm_init execution
0x00080000 no public pmap runtime execution
0x00100000 no public pexpert runtime execution
0x00200000 no public IOKit runtime execution
0x00400000 no Mach-O fixture execution
0x00800000 no pmap/control-register/TLB/cache/persistent mutation
0x01000000 safety boundary preserved
```

## Loader roll-up

The top-level loader mask is already saturated:

```text
loader_satisfied_mask=0xffffffff
```

Stage79 therefore keeps the loader satisfied-mask ABI unchanged.  The PE-init-platform-false result is instead included by:

- copying `struct stage79_xnu_pe_init_platform_false_result` into the loader preflight block;
- logging `loader_xnu_pe_init_platform_false_*` status/mask/checksum/roll-up markers;
- ORing `xnu_pe_init_platform_false_failure_mask` into the final loader failure mask;
- requiring `xnu_pe_init_platform_false_status_rollup == 0x79000001` for final loader success.

## Boundary update

Stage79 active command-line markers include:

```text
xnu-entry-stub xnu-early-init xnu-pe-init-false prevm-pexpert dtinit-facts peid-machine arm-init-stub no-pub-start no-pub-arm-init no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid
```

Interpretation:

- Stage79 executes Stage-owned entry-stub, arm-init-stub, early-init, and PE-init-platform-false code.
- Stage79 does not execute public XNU `_start` / `arm_init`.
- Stage79 does not call or execute public `PE_init_platform`, `DTInit`, or `pe_identify_machine`.
- Stage79 does not execute public pexpert/pmap/IOKit proof objects.
- Stage79 does not execute the generated Mach-O fixture.
- Stage79 does not install live pmap tables, write control registers for a pmap transition, invalidate TLBs, change cache policy, or persistently write storage.

## Local validation

Local validation passed:

```text
stage79/build.sh: success
stage79-qcdt.img dt_size=2521088 (0x267800)
stage79.elf undefined symbols: none
stage79-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 238 bytes including NUL
chosen boot-args string: 153 bytes including NUL
Android boot-image cmdlines: 1283 bytes including NUL
stale Stage78 tokens in stage79 code/build files: none
```

Disassembly confirms real calls:

```text
bl stage79_xnu_start_stub
bl stage79_arm_init_stub
bl stage79_xnu_early_pmap_platform_init_run
bl stage79_xnu_pe_init_platform_false_run
```

Build outputs:

```text
stage79.elf: 492264 bytes
stage79.bin: 439788 bytes
stage79.img: 442368 bytes
stage79-qcdt.img: 2963456 bytes
stage79-qcdt.img sha256=fc5139d928cf179bb57c7e4a84d8f828176715ccdd13e0d2aba2a01547818c7d
```

## Hardware validation result

Hardware validation passed and remained non-persistent:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage79/stage79-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage79-pe-init-platform-false-last_kmsg.txt
```

Captured log:

```text
/tmp/cancro-stage79-pe-init-platform-false-last_kmsg.txt
263638 bytes
```

Confirmed hardware markers:

```text
MI4IOS6_STAGE79_XNU stage79_xnu_entry_stub_status=0x79000001
MI4IOS6_STAGE79_XNU stage79_xnu_entry_stub_satisfied_mask=0x00003fff
MI4IOS6_STAGE79_XNU stage79_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_early_init_status=0x79000001
MI4IOS6_STAGE79_XNU stage79_xnu_early_init_satisfied_mask=0x01ffffff
MI4IOS6_STAGE79_XNU stage79_xnu_early_init_failure_mask=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_status=0x79000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_satisfied_mask=0x01ffffff
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_failure_mask=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_checksum=0xab05a29f
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_source_early_init_status=0x79000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_pe_state_matched=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_dtinit_root_parseable=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_dtinit_no_public_call=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_identify_msm8974_matched=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_identify_cancro_matched=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_identify_machine_no_public_call=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_control_registers_unchanged=0x00000001
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_public_pe_init_platform_executed=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_public_dtinit_executed=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_public_pe_identify_machine_executed=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_live_pmap_installed=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_ttbr_written=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_ttbcr_written=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_dacr_written=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_sctlr_written=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_tlb_invalidated=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_cache_policy_changed=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_persistent_write_attempted=0x00000000
MI4IOS6_STAGE79_XNU stage79_xnu_pe_init_platform_false_safety_boundary_preserved=0x00000001
MI4IOS6_STAGE79_XNU loader_xnu_entry_stub_status_rollup=0x79000001
MI4IOS6_STAGE79_XNU loader_xnu_early_init_status_rollup=0x79000001
MI4IOS6_STAGE79_XNU loader_xnu_pe_init_platform_false_status_rollup=0x79000001
MI4IOS6_STAGE79_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE79_XNU loader_status=0x79000001
MI4IOS6_STAGE79 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change was performed.
