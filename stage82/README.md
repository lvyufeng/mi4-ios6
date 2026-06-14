# Stage82 — Stage81 renumbering validation baseline

Stage82 is a mechanical renumbering of the hardware-validated Stage81 live-pmap install/verify/restore window. It serves as a validation checkpoint to confirm the renumbering infrastructure works correctly before implementing the next major architectural step (Stage83 full kernel virtual address space with L1+L2 pmap).

**Implementation status**: Stage82 is functionally identical to Stage81, with all `stage81`/`Stage81`/`STAGE81`/`0x81000001` tokens mechanically replaced by `stage82`/`Stage82`/`STAGE82`/`0x82000001`. This includes:
- All source files (.c, .h, .S)
- Build scripts and makefiles
- ABI constants and status codes
- File names (stage81.h → stage82.h, stage81_main.c → stage82_main.c)
- Output paths (out/stage81 → out/stage82)

**Validation purpose**: Confirm that:
1. Renumbering does not introduce compile/link errors
2. Status codes are correctly updated throughout the codebase
3. Hardware validation recovers the same markers with new status codes (`0x82000001` instead of `0x81000001`)
4. The Stage81 → Stage82 renumbering pattern is reliable for future stages

Stage82 retains Stage81's minimal candidate pmap (10 L1 section mappings: low identity, high alias, RAM console, GIC, IMEM, PS_HOLD, self-mapping). It does not yet implement L1+L2 two-level translation or full kernel virtual address space mapping.

This is still not a public XNU boot. Stage82 does not enter public XNU `_start` or `arm_init`, does not execute public pmap runtime, does not call public `PE_init_platform`/`DTInit`/`pe_identify_machine`, does not execute public pexpert/IOKit runtime, does not execute the generated Mach-O fixture, does not change cache policy, and does not perform persistent writes. The live behavior is bounded and Stage-owned: Stage82 builds the same candidate 16 KiB L1 table as Stage81, writes it to TTBR0, invalidates TLBs, verifies translation through the installed table, then restores the original TTBR0 before returning to the loader.

Public ARM XNU references remain reference-only:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
```

## New / changed sources

```text
stage82/xnu_entry_start.S
stage82/xnu_entry_stub.c
stage82/xnu_early_pmap_platform_init.c
stage82/xnu_pe_init_platform_false.c
stage82/xnu_arm_init_post_pe_bootstrap.c
stage82/xnu_arm_vm_init_live_pmap.c
stage82/macho_probe.c
stage82/stage82_main.c
stage82/build.sh
```

Main symbols:

```c
uint32_t stage82_xnu_start_stub(struct boot_args *args,
                                struct stage82_xnu_entry_stub_result *result);
uint32_t stage82_arm_init_stub(struct boot_args *args,
                               struct stage82_xnu_entry_stub_result *result);
int stage82_xnu_entry_stub_run(struct boot_args *args);
const struct stage82_xnu_entry_stub_result *stage82_xnu_entry_stub_result(void);

int stage82_xnu_early_pmap_platform_init_run(
    struct boot_args *args,
    struct stage82_xnu_entry_stub_result *entry_result);
const struct stage82_xnu_early_pmap_platform_init_result *
stage82_xnu_early_pmap_platform_init_result(void);

int stage82_xnu_pe_init_platform_false_run(
    struct boot_args *args,
    struct stage82_xnu_entry_stub_result *entry_result);
const struct stage82_xnu_pe_init_platform_false_result *
stage82_xnu_pe_init_platform_false_result(void);

int stage82_xnu_arm_init_post_pe_bootstrap_run(
    struct boot_args *args,
    struct stage82_xnu_entry_stub_result *entry_result);
const struct stage82_xnu_arm_init_post_pe_bootstrap_result *
stage82_xnu_arm_init_post_pe_bootstrap_result(void);

int stage82_xnu_arm_vm_init_live_pmap_run(
    struct boot_args *args,
    struct stage82_xnu_entry_stub_result *entry_result);
const struct stage82_xnu_arm_vm_init_live_pmap_result *
stage82_xnu_arm_vm_init_live_pmap_result(void);
```

## Execution path

The Stage82 target path remains:

```text
start.S -> stage82_main() -> kernel_entry(&boot_args) -> stage82_loader_preflight_run(args)
```

Inside `stage82_loader_preflight_run()`:

1. Retained Mach-O fixture and XNU loader dry-run checks run.
2. Retained workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x82000001`.
4. Stage82 calls `stage82_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR and calls `stage82_xnu_start_stub(args, &g_stage82_xnu_entry_stub_result)`.
6. The assembly stub marks entry and performs a real branch-and-link to `stage82_arm_init_stub(args, result)`.
7. The C `arm_init`-shaped stub validates `boot_args`, invokes `stage82_xnu_early_pmap_platform_init_run(args, result)`, invokes `stage82_xnu_pe_init_platform_false_run(args, result)`, invokes `stage82_xnu_arm_init_post_pe_bootstrap_run(args, result)`, then invokes `stage82_xnu_arm_vm_init_live_pmap_run(args, result)`.
8. The live-pmap micro-sequence imports the post-PE result, validates boot args, builds the Stage-owned candidate L1 table, saves TTBR0/TTBCR/DACR/SCTLR, writes TTBR0 to the candidate table, invalidates TLBs, verifies installed translation, restores the original TTBR0, invalidates TLBs again, and reports the safety boundary.
9. The loader copies the entry-stub, early-init, PE-init-platform-false, post-PE bootstrap, and live-pmap result ABIs into `struct stage82_loader_preflight` and requires all roll-ups to be OK.

```text
entry stub -> arm-init stub -> early pmap/platform init -> PE_init_platform(FALSE)-shaped pre-VM init -> post-PE_FALSE bootstrap/timebase-registration-shaped init -> arm_vm_init live-pmap install/verify/restore -> loader
```

## Entry-stub ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required mask:

```text
0x0000ffff
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
0x00000200 TTBR0/SCTLR restored/unchanged at wrapper boundary
0x00000400 no exception observed
0x00000800 safety boundary preserved
0x00001000 early pmap/platform init OK
0x00002000 PE_init_platform(FALSE)-shaped init OK
0x00004000 post-PE bootstrap/timebase-registration init OK
0x00008000 arm_vm_init live-pmap install window OK
```

The Stage82 entry-result prefix keeps the Stage77/Stage78/Stage79/Stage80 assembly offsets stable:

```text
start_entered:     48
arm_init_called:   52
arm_init_returned: 56
arm_init_status:   64
```

The Stage82-only summary fields appended before the entry checksum now include:

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
uint32_t arm_vm_init_live_pmap_called;
uint32_t arm_vm_init_live_pmap_returned;
uint32_t arm_vm_init_live_pmap_status;
uint32_t arm_vm_init_live_pmap_checksum;
```

## Live-pmap result ABI

Live-pmap required mask:

```text
0x01007fff
```

Satisfied bits:

```text
0x00000001 source post-PE bootstrap result OK
0x00000002 boot_args captured/valid
0x00000004 candidate L1 base valid and 16 KiB aligned
0x00000008 candidate L1 populated
0x00000010 TTBR0/TTBCR/DACR/SCTLR saved
0x00000020 live pmap installed by TTBR0 write
0x00000040 live pmap verified
0x00000080 high-alias translation verified
0x00000100 RAM-console alias verified
0x00000200 GIC alias verified
0x00000400 live pmap restored away
0x00000800 original control-register state restored
0x00001000 no public arm_vm_init executed
0x00002000 no public pmap runtime executed
0x00004000 no persistent write attempted
0x01000000 safety boundary preserved
```

Failure bits:

```text
0x00000001 source post-PE bootstrap missing/failing
0x00000002 boot_args invalid
0x00000004 candidate L1 not 16 KiB aligned
0x00000008 MMU disabled before the test window
0x00000010 TTBR0 live value mismatch
0x00000020 TTBCR/DACR changed unexpectedly
0x00000040 high-alias verification failed
0x00000080 RAM-console alias verification failed
0x00000100 GIC alias verification failed
0x00000200 original TTBR/control-register restore failed
0x80000000 safety boundary failed
```

The result records:

- source post-PE status/checksum;
- boot-args pointer/validity, memory size, and `physBase`;
- candidate L1 base and checksum;
- original, live, and restored TTBR0/TTBCR/DACR/SCTLR samples;
- TTBR0 write count and TLB invalidation count;
- live-pmap installed/verified/restored booleans;
- high-alias, RAM-console, and GIC verification booleans;
- explicit zero counters for public `arm_vm_init`, public pmap runtime, and persistent writes;
- `safety_boundary_preserved` and XOR checksum.

## Candidate L1 mappings

`stage82/xnu_arm_vm_init_live_pmap.c` allocates a dedicated 4096-entry, 16 KiB-aligned L1 table and populates only the sections needed to keep the Stage82 window alive and verifiable:

```text
0x00000000 -> 0x00000000 identity section for Stage82 low code/data/probe
0xc0000000 -> 0x00000000 high alias for Stage82 low payload data
0xc0100000 -> RAM_CONSOLE_BASE RAM-console alias
0xc0200000 -> 0xf9000000 GIC alias
0x0fa00000 -> 0x0fa00000 MSM IMEM/restart-reason section
RAM_CONSOLE_BASE and RAM_CONSOLE_BASE+1 MiB identity RAM-console sections
0xf9000000 -> 0xf9000000 GIC identity section
0xfc400000 -> 0xfc400000 PS_HOLD/device section
candidate_l1_base section -> itself
```

The live window uses this sequence:

```text
dsb; isb
write TTBR0 = candidate_l1_base
invalidate unified TLB
dsb; isb
verify live TTBR0/control state and alias reads
dsb; isb
write TTBR0 = original_ttbr0
invalidate unified TLB
dsb; isb
verify original TTBR/control state restored
```

Only TTBR0 is intentionally written. TTBCR, DACR, SCTLR, and cache policy are sampled and required to remain unchanged.

## Loader integration

The top-level loader satisfied mask remains saturated at:

```text
0xffffffff
```

Stage82 therefore does not add a new top-level loader satisfied bit. Instead the loader copies the full live-pmap result into `struct stage82_loader_preflight`, logs a dedicated status roll-up, ORs the live-pmap failure mask into the final loader failure mask, and requires:

```text
loader_xnu_entry_stub_status_rollup=0x82000001
loader_xnu_early_init_status_rollup=0x82000001
loader_xnu_pe_init_platform_false_status_rollup=0x82000001
loader_xnu_arm_init_post_pe_bootstrap_status_rollup=0x82000001
loader_xnu_arm_vm_init_live_pmap_status_rollup=0x82000001
```

Dedicated loader markers:

```text
loader_xnu_arm_vm_init_live_pmap_status
loader_xnu_arm_vm_init_live_pmap_satisfied_mask
loader_xnu_arm_vm_init_live_pmap_failure_mask
loader_xnu_arm_vm_init_live_pmap_checksum
loader_xnu_arm_vm_init_live_pmap_status_rollup
```

## Boundary

Stage82 executes only Stage-owned entry/arm-init/early-init/PE-init-platform-false/post-PE bootstrap/live-pmap code. It now **does**:

- build a Stage-owned candidate L1 table;
- write TTBR0 to install that table;
- invalidate TLBs for install and restore;
- execute a small verification window while the candidate pmap is live;
- verify high-alias, RAM-console, and GIC reads through the candidate pmap;
- restore the original TTBR0 before returning to the loader.

It still does **not**:

- execute public XNU `_start` / `arm_init`,
- execute public `thread_bootstrap`, `cpu_bootstrap`, `rtclock_early_init`, `kernel_early_bootstrap`, `cpu_init`, `processor_bootstrap`, or public `arm_vm_init`,
- call or execute public `PE_init_platform`,
- call or execute public `DTInit`,
- call or execute public `pe_identify_machine`,
- execute public XNU object-subset proof objects,
- execute public pexpert/platform runtime,
- execute public ARM VM/pmap runtime,
- execute public IOKit runtime,
- execute the generated Mach-O fixture,
- write the proposed public-XNU pmap workspace,
- write TTBCR/DACR/SCTLR for a split-TTBR or cache-policy transition,
- change cache policy,
- perform persistent writes.

Compact fixed `CommandLine[256]` Stage82 markers include:

```text
xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock
```

The Android boot-image cmdline also keeps higher-level audit markers such as `xnu-entry-stub`, `xnu-early-init`, `arm-init-stub`, `xnu-armvm`, `live-pmap`, `ttbr-live`, `tlb-live`, `pmap-restore`, `no-pub-start`, `no-pub-arm-init`, `no-pub-pmap`, `no-pub-pexpert`, `no-iokit-runtime-exec`, `no-macho-exec`, `no-cache-change`, `no-persist-write`, and `no-external-mutation`.

## Local validation

Local validation passed after the live-pmap log-string update:

```text
stage82/build.sh: success
stage82-qcdt.img dt_size=2521088 (0x267800)
stage82.elf undefined symbols: none
stage82-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 249 bytes including NUL
chosen boot-args string: 241 bytes including NUL
Android boot-image cmdlines: 1389 bytes including NUL
stale Stage80 tokens in stage82 code/build files: none
git diff --check: clean
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

Disassembly confirms real calls:

```text
bl stage82_xnu_start_stub
bl stage82_arm_init_stub
bl stage82_xnu_early_pmap_platform_init_run
bl stage82_xnu_pe_init_platform_false_run
bl stage82_xnu_arm_init_post_pe_bootstrap_run
bl stage82_xnu_arm_vm_init_live_pmap_run
```

Build outputs:

```text
stage82_fixture.macho: 1744 bytes
stage82.elf:          509688 bytes
stage82.bin:          456236 bytes
stage82.img:          458752 bytes
stage82-qcdt.img:    2979840 bytes
```

SHA256:

```text
34fcb0a4b0699a999b5b3225da20558fa4ad05d77d28f86d15e88e26264fbbc7  stage82_fixture.macho
9bce42f97d93f86c146d9f9fc044aafc26f73aeb7e7446b30fc90f5e2bf987ca  stage82.elf
b64ca73adc5a396cc55c7b4cd9b04787a7fd6732e6fb84ca32abac7ced4f001b  stage82.bin
6ab1c9dc43e5b69bd867ee70994e17b6de053bc0507953936e85bd7f6d1c33a7  stage82.img
9850d88663607d4519f5a92b20569fa04f1b70781f2c197f11a55d7f7178ccc5  stage82-qcdt.img
```

## Hardware validation

Passed on real hardware (`cancro`, serial `4a2fe00b`) via non-persistent `fastboot boot`. Stage82 booted, completed the full loader preflight plus the Stage-owned entry/early-init/PE-init-false/post-PE bootstrap/live-pmap install/verify/restore path, returned success, and the device recovered cleanly to Android with no persistent change. Command sequence:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage82/stage82-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage82-arm-vm-init-live-pmap-last_kmsg.txt
```

Recovered log size:

```text
275864 /tmp/cancro-stage82-arm-vm-init-live-pmap-last_kmsg.txt
```

Observed pass markers include:

```text
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_status=0x82000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_satisfied_mask=0x01007fff
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_failure_mask=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_candidate_l1_base=0x000e4000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_candidate_l1_checksum=0x2d810c02
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_original_ttbr0=0x000b0000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_ttbr0=0x000e4000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_restored_ttbr0=0x000b0000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_original_ttbcr=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_ttbcr=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_restored_ttbcr=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_original_dacr=0x00000003
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_dacr=0x00000003
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_restored_dacr=0x00000003
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_original_sctlr=0x00c5487b
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_sctlr=0x00c5487b
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_restored_sctlr=0x00c5487b
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_ttbr0_write_count=0x00000002
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_tlb_invalidate_count=0x00000002
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_pmap_installed=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_pmap_verified=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_high_alias_verified=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_ram_console_verified=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_gic_verified=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_live_pmap_restored=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_public_arm_vm_init_executed=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_public_pmap_runtime_executed=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_persistent_write_attempted=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_safety_boundary_preserved=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_arm_vm_init_live_pmap_checksum=0xac2b7182
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_status=0x82000001
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_satisfied_mask=0x0000ffff
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_ttbr0_before=0x000b0000
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_ttbr0_after=0x000b0000
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_sctlr_before=0x00c5487b
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_sctlr_after=0x00c5487b
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_mmu_unchanged=0x00000001
MI4IOS6_STAGE82_XNU stage82_xnu_entry_stub_checksum=0x300fb4f0
MI4IOS6_STAGE82_XNU loader_xnu_arm_vm_init_live_pmap_status_rollup=0x82000001
MI4IOS6_STAGE82_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE82_XNU loader_checksum=0xf194020c
MI4IOS6_STAGE82_XNU loader_status=0x82000001
MI4IOS6_STAGE82 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change is part of Stage82 validation.
