# Stage78 — Stage-owned early pmap/platform-init micro-sequence

Stage78 continues the Method-C live bring-up path after the hardware-validated Stage77 entry stub.  Stage77 proved a controlled Level-1 Stage-owned `_start` / `arm_init`-shaped handoff.  Stage78 adds the next still-Stage-owned step: an early pmap/platform-init micro-sequence invoked from the Stage-owned `arm_init` stub.

This is still not a public XNU boot.  Stage78 does not enter public XNU `_start` or `arm_init`, does not execute public pexpert/pmap/IOKit runtime objects, does not execute the generated Mach-O fixture, does not install live pmap tables, does not write TTBR0/TTBCR/DACR/SCTLR for the proposed plan, does not invalidate TLBs, does not change cache policy, and does not perform persistent writes.

## New / changed sources

```text
stage78/xnu_entry_start.S
stage78/xnu_entry_stub.c
stage78/xnu_early_pmap_platform_init.c
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

The Stage78 target path remains:

```text
start.S -> stage78_main() -> kernel_entry(&boot_args) -> stage78_loader_preflight_run(args)
```

Inside `stage78_loader_preflight_run()`:

1. Retained Mach-O fixture and XNU loader dry-run checks run.
2. Retained workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x78000001`.
4. Stage78 calls:

```c
stage78_xnu_entry_stub_run(args);
```

5. The wrapper records TTBR0/SCTLR and calls:

```c
stage78_xnu_start_stub(args, &g_stage78_xnu_entry_stub_result);
```

6. The assembly stub marks entry and performs a real branch-and-link to:

```c
stage78_arm_init_stub(args, result);
```

7. The C `arm_init`-shaped stub validates `boot_args`, then invokes:

```c
stage78_xnu_early_pmap_platform_init_run(args, result);
```

8. The early-init micro-sequence samples TTBR0/TTBCR/DACR/SCTLR before and after the sequence, imports the Stage78 pmap-transition dry-run plan, imports and validates PE_state/platform facts, records forbidden-operation counters, returns to the Stage-owned arm-init stub, and then returns to the loader.

## Entry-stub ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required mask:

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

The Stage78 entry-result prefix keeps the Stage77 assembly offsets stable:

```text
start_entered:     48
arm_init_called:   52
arm_init_returned: 56
arm_init_status:   64
```

The Stage78-only summary fields are appended before the entry checksum:

```c
uint32_t early_pmap_platform_init_called;
uint32_t early_pmap_platform_init_returned;
uint32_t early_pmap_platform_init_status;
uint32_t early_pmap_platform_init_checksum;
```

## Early-init ABI

Early-init required mask:

```text
0x01ffffff
```

Satisfied bits:

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

The early-init result records:

- independent `boot_args` and device-tree validation;
- TTBR0/TTBCR/DACR/SCTLR before/after samples;
- imported pmap-transition status, masks, checksum, candidate L1 base/limit/alignment, proposed TTBR0/TTBCR/DACR/SCTLR plan, translation proof PAs, and recovery-continuity facts;
- imported PE_state memory/CPU/GIC/timer/machine/vector facts;
- explicit public-runtime/generated-Mach-O/live-pmap/control-register/TLB/cache/persistent-write counters;
- an XOR checksum over all words before `checksum`.

## Loader integration

The top-level loader satisfied mask remains saturated at:

```text
0xffffffff
```

Stage78 therefore does not add a new top-level loader satisfied bit.  Instead the loader copies the full early-init result into `struct stage78_loader_preflight`, logs a dedicated status roll-up, ORs the early-init failure mask into the final loader failure mask, and requires:

```text
loader_xnu_entry_stub_status_rollup=0x78000001
loader_xnu_early_init_status_rollup=0x78000001
```

## Boundary

Stage78 executes only Stage-owned entry/arm-init/early-init code. It still does **not**:

- execute public XNU `_start` / `arm_init`,
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

The active Android boot-image command line uses compact Stage78 markers:

```text
xnu-entry-stub xnu-early-init early-pmap-platform stage-owned-early-init arm-init-stub no-pub-start no-pub-arm-init no-pub-pmap no-pub-pexpert
```

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
disassembly contains: bl stage78_xnu_start_stub
disassembly contains: bl stage78_arm_init_stub
disassembly contains: bl stage78_xnu_early_pmap_platform_init_run
stale Stage77 tokens in stage78/: none
```

Build outputs:

```text
stage78.elf: 476144 bytes
stage78.bin: 423968 bytes
stage78.img: 428032 bytes
stage78-qcdt.img: 2949120 bytes
stage78-qcdt.img sha256=98abb7127d7f67358e7aad072c67bf2df5b95d7d50a6db462bd40de402e4b2be
```

## Hardware validation

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
