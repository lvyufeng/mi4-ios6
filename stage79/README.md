# Stage79 — Stage-owned PE_init_platform(FALSE,args)-shaped pre-VM pexpert/platform micro-sequence

Stage79 continues the Method-C live bring-up path after the hardware-validated Stage78 early pmap/platform-init result (`21fb079`).  Stage78 proved the controlled Stage-owned `_start` / `arm_init`-shaped handoff can invoke a Stage-owned early pmap/platform-init micro-sequence.  Stage79 adds the next still-Stage-owned step: a `PE_init_platform(FALSE, args)`-shaped pre-VM pexpert/platform micro-sequence invoked from the Stage-owned `arm_init` stub after the early-init path.

This is still not a public XNU boot.  Stage79 does not enter public XNU `_start` or `arm_init`, does not call public `PE_init_platform`, does not call public `DTInit`, does not call public `pe_identify_machine`, does not execute public pexpert/pmap/IOKit runtime objects, does not execute the generated Mach-O fixture, does not install live pmap tables, does not write TTBR0/TTBCR/DACR/SCTLR for the proposed plan, does not invalidate TLBs, does not change cache policy, and does not perform persistent writes.

## New / changed sources

```text
stage79/xnu_entry_start.S
stage79/xnu_entry_stub.c
stage79/xnu_early_pmap_platform_init.c
stage79/xnu_pe_init_platform_false.c
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

The Stage79 target path remains:

```text
start.S -> stage79_main() -> kernel_entry(&boot_args) -> stage79_loader_preflight_run(args)
```

Inside `stage79_loader_preflight_run()`:

1. Retained Mach-O fixture and XNU loader dry-run checks run.
2. Retained workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x79000001`.
4. Stage79 calls `stage79_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR and calls `stage79_xnu_start_stub(args, &g_stage79_xnu_entry_stub_result)`.
6. The assembly stub marks entry and performs a real branch-and-link to `stage79_arm_init_stub(args, result)`.
7. The C `arm_init`-shaped stub validates `boot_args`, invokes `stage79_xnu_early_pmap_platform_init_run(args, result)`, then invokes `stage79_xnu_pe_init_platform_false_run(args, result)`.
8. The PE-init-platform-false micro-sequence imports the early-init result, revalidates `boot_args`, captures `PE_state` / `deviceTreeHead`, records DTInit-shaped and identify-machine-shaped facts, samples TTBR0/TTBCR/DACR/SCTLR unchanged, records forbidden-operation counters, and returns to the Stage-owned arm-init stub.
9. The loader copies the entry-stub, early-init, and PE-init-platform-false result ABIs into `struct stage79_loader_preflight` and requires all roll-ups to be OK.

```text
entry stub -> arm-init stub -> early pmap/platform init -> PE_init_platform(FALSE)-shaped pre-VM init -> loader
```

## Entry-stub ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required mask:

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

The Stage79 entry-result prefix keeps the Stage77/Stage78 assembly offsets stable:

```text
start_entered:     48
arm_init_called:   52
arm_init_returned: 56
arm_init_status:   64
```

The Stage79-only summary fields are appended before the entry checksum:

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

## PE_init_platform(FALSE)-shaped result ABI

PE-init-platform-false required mask:

```text
0x01ffffff
```

Satisfied bits:

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

The PE-init-platform-false result records:

- source early-init status/masks/checksum;
- independent `boot_args` validation and compact command-line marker checks;
- PE_state boot args pointer, device-tree head/length, memory, CPU count, machine type, vector base, GIC bases, timer base, and timer frequency;
- DTInit-shaped facts without calling public `DTInit`;
- Apple-DT facts for `/device-tree`, `/chosen`, `/memory`, `/cpus`, `/interrupt-controller`, and `/timer`;
- identify-machine-shaped model/compatible/target-type checks without calling public `pe_identify_machine`;
- explicit public-runtime/generated-Mach-O/live-pmap/control-register/TLB/cache/persistent-write counters;
- TTBR0/TTBCR/DACR/SCTLR before/after samples;
- an XOR checksum over all words before `checksum`.

## Loader integration

The top-level loader satisfied mask remains saturated at:

```text
0xffffffff
```

Stage79 therefore does not add a new top-level loader satisfied bit.  Instead the loader copies the full PE-init-platform-false result into `struct stage79_loader_preflight`, logs a dedicated status roll-up, ORs the PE-init failure mask into the final loader failure mask, and requires:

```text
loader_xnu_entry_stub_status_rollup=0x79000001
loader_xnu_early_init_status_rollup=0x79000001
loader_xnu_pe_init_platform_false_status_rollup=0x79000001
```

## Boundary

Stage79 executes only Stage-owned entry/arm-init/early-init/PE-init-platform-false code. It still does **not**:

- execute public XNU `_start` / `arm_init`,
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

Compact Stage79 markers include:

```text
xnu-entry-stub xnu-early-init xnu-pe-init-false prevm-pexpert dtinit-facts peid-machine arm-init-stub no-pub-start no-pub-arm-init no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid
```

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
disassembly contains: bl stage79_xnu_start_stub
disassembly contains: bl stage79_arm_init_stub
disassembly contains: bl stage79_xnu_early_pmap_platform_init_run
disassembly contains: bl stage79_xnu_pe_init_platform_false_run
stale Stage78 tokens in stage79 code/build files: none
```

Build outputs:

```text
stage79.elf: 492264 bytes
stage79.bin: 439788 bytes
stage79.img: 442368 bytes
stage79-qcdt.img: 2963456 bytes
stage79-qcdt.img sha256=fc5139d928cf179bb57c7e4a84d8f828176715ccdd13e0d2aba2a01547818c7d
```

## Hardware validation

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
