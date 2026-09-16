# Experiment 84: Stage81 arm_vm_init live-pmap installation window

## Summary

Stage81 is the next Method-C step after the hardware-validated Stage80 post-`PE_init_platform(FALSE,args)` bootstrap/timebase-registration boundary. Stage80 intentionally stopped before `arm_vm_init`; Stage81 crosses that boundary in the narrowest useful way: a Stage-owned `arm_vm_init`-shaped live-pmap installation window.

The new path remains Stage-owned and fail-closed:

```text
loader -> stage81_xnu_start_stub(args, result)
       -> stage81_arm_init_stub(args, result)
       -> stage81_xnu_early_pmap_platform_init_run(args, result)
       -> stage81_xnu_pe_init_platform_false_run(args, result)
       -> stage81_xnu_arm_init_post_pe_bootstrap_run(args, result)
       -> stage81_xnu_arm_vm_init_live_pmap_run(args, result)
       -> stage81_arm_init_stub
       -> loader
```

`stage81_xnu_arm_vm_init_live_pmap_run()` builds a dedicated 16 KiB-aligned candidate L1 table, writes TTBR0 to install it, invalidates TLBs, verifies live translations, restores the original TTBR0, invalidates TLBs again, and returns to the loader only after restore verification. This is the first Method-C stage in this branch that intentionally installs a Stage-owned live pmap and invalidates TLBs for that install/restore sequence.

This is still not a public XNU boot. Stage81 does not execute public XNU `_start`, public `arm_init`, public `arm_vm_init`, public `pmap_bootstrap`, public pmap runtime, public pexpert/IOKit runtime, or generated Mach-O bytes. It does not build a full public `mach_kernel`, does not mutate external XNU checkouts, does not change cache policy, and does not perform persistent writes.

Public ARM XNU references are reference-only:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
```

## New sources

```text
stage81/xnu_arm_vm_init_live_pmap.c
```

Retained/extended entry sources:

```text
stage81/xnu_entry_start.S
stage81/xnu_entry_stub.c
stage81/xnu_early_pmap_platform_init.c
stage81/xnu_pe_init_platform_false.c
stage81/xnu_arm_init_post_pe_bootstrap.c
stage81/macho_probe.c
stage81/stage81_main.c
stage81/build.sh
```

Main new symbols:

```c
int stage81_xnu_arm_vm_init_live_pmap_run(
    struct boot_args *args,
    struct stage81_xnu_entry_stub_result *entry_result);

const struct stage81_xnu_arm_vm_init_live_pmap_result *
stage81_xnu_arm_vm_init_live_pmap_result(void);
```

## Execution path

The retained boot path is:

```text
start.S -> stage81_main() -> kernel_entry(&boot_args) -> stage81_loader_preflight_run(args)
```

Inside `stage81_loader_preflight_run()`:

1. Existing Mach-O fixture checks run.
2. Existing workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x81000001`.
4. Stage81 calls `stage81_xnu_entry_stub_run(args)`.
5. The wrapper records TTBR0/SCTLR before and after calling `stage81_xnu_start_stub(args, result)`.
6. The assembly stub marks the start-shaped entry as reached and performs a real `bl stage81_arm_init_stub`.
7. The arm-init-shaped C stub validates `boot_args`, calls the retained early pmap/platform init, PE-init-platform-false, and post-PE bootstrap/timebase micro-sequences, then calls the new live-pmap install window.
8. The loader copies the entry-stub, early-init, PE-init-platform-false, post-PE bootstrap, and live-pmap result ABIs into `struct stage81_loader_preflight` and requires all roll-ups to be OK.

## Entry-stub result ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required satisfied mask:

```text
0x0000ffff
```

The assembly ABI prefix remains compatible with Stage77/Stage78/Stage79/Stage80 offsets:

```text
start_entered=48
arm_init_called=52
arm_init_returned=56
arm_init_status=64
```

Stage81 appends the live-pmap summary fields after the retained early-init, PE-init, and post-PE summary fields and before the entry checksum:

```c
uint32_t arm_vm_init_live_pmap_called;
uint32_t arm_vm_init_live_pmap_returned;
uint32_t arm_vm_init_live_pmap_status;
uint32_t arm_vm_init_live_pmap_checksum;
```

New entry-stub satisfied/fail bit:

```c
#define STAGE81_XNU_ENTRY_STUB_SAT_ARM_VM_INIT_LIVE_PMAP_OK 0x00008000u
#define STAGE81_XNU_ENTRY_STUB_FAIL_ARM_VM_INIT_LIVE_PMAP   0x00008000u
```

The entry-stub safety check now requires `live_pmap_installed == 1` and at least one TLB invalidation while still requiring public XNU/start/arm-init/pmap/IOKit runtime counters, generated Mach-O execution, cache-policy change, and persistent write counters to stay zero.

## Live-pmap result ABI

Required satisfied mask:

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

- source post-PE bootstrap status/checksum;
- boot args pointer/validity, memory size, and `physBase`;
- candidate L1 base/checksum;
- original, live, and restored TTBR0/TTBCR/DACR/SCTLR samples;
- TTBR0 write count and TLB invalidation count;
- live-pmap installed/verified/restored booleans;
- high-alias, RAM-console, and GIC verification booleans;
- explicit zero counters for public `arm_vm_init`, public pmap runtime, and persistent writes;
- safety-boundary flag and XOR checksum.

## Candidate L1 table

`stage81/xnu_arm_vm_init_live_pmap.c` allocates a dedicated 4096-entry, 16 KiB-aligned candidate L1 table:

```text
static uint32_t stage81_live_pmap_candidate_l1[4096] __attribute__((aligned(16384)));
```

The table maps only the sections required to survive and verify the bounded window:

```text
0x00000000 -> 0x00000000      low identity section for Stage81 code/data/probe
0xc0000000 -> 0x00000000      high alias for Stage81 low payload data
0xc0100000 -> RAM_CONSOLE_BASE RAM-console alias
0xc0200000 -> 0xf9000000      GIC alias
0x0fa00000 -> 0x0fa00000      MSM IMEM / restart-reason section
RAM_CONSOLE_BASE identity section
RAM_CONSOLE_BASE + 1 MiB identity section
0xf9000000 -> 0xf9000000      GIC identity section
0xfc400000 -> 0xfc400000      PS_HOLD/device section
candidate_l1_base section -> itself
```

Descriptors use the same conservative ARMv7 section word shape used by the prior dry-run proofs:

```text
(pa & 0xfff00000) | 0x00010c02
```

## Live install/verify/restore sequence

The bounded live-pmap window is:

```text
dsb; isb
write TTBR0 = candidate_l1_base
invalidate unified TLB
dsb; isb
read TTBR0/TTBCR/DACR/SCTLR
verify TTBR0 base and unchanged TTBCR/DACR
write/read Stage-owned probe through 0xc0000000 high alias
verify RAM-console signature through 0xc0100000 alias
verify GIC distributor register through 0xc0200000 alias
dsb; isb
write TTBR0 = original_ttbr0
invalidate unified TLB
dsb; isb
read TTBR0/TTBCR/DACR/SCTLR
verify original state restored
```

A fail-closed `finish:` path attempts the same original-TTBR0 restore if any check fails after the switch.

## Loader roll-up

The top-level loader mask is already saturated:

```text
loader_satisfied_mask=0xffffffff
```

Stage81 therefore keeps the loader satisfied-mask ABI unchanged. The live-pmap result is included by:

- copying `struct stage81_xnu_arm_vm_init_live_pmap_result` into the loader preflight block;
- logging `loader_xnu_arm_vm_init_live_pmap_*` status/mask/checksum/roll-up markers;
- ORing `xnu_arm_vm_init_live_pmap_failure_mask` into the final loader failure mask;
- requiring `xnu_arm_vm_init_live_pmap_status_rollup == 0x81000001` for final loader success.

Dedicated loader markers:

```text
loader_xnu_arm_vm_init_live_pmap_status
loader_xnu_arm_vm_init_live_pmap_satisfied_mask
loader_xnu_arm_vm_init_live_pmap_failure_mask
loader_xnu_arm_vm_init_live_pmap_checksum
loader_xnu_arm_vm_init_live_pmap_status_rollup
```

## Boundary update

Fixed `CommandLine[256]` Stage81 markers include:

```text
xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock
```

Interpretation:

- Stage81 executes Stage-owned entry-stub, arm-init-stub, early-init, PE-init-platform-false, post-PE bootstrap/timebase, and live-pmap code.
- Stage81 writes TTBR0 only for the Stage-owned candidate pmap and restores the original TTBR0 before returning.
- Stage81 invalidates TLBs only for the Stage-owned install/restore sequence.
- Stage81 does not write TTBCR/DACR/SCTLR or change cache policy.
- Stage81 does not execute public XNU `_start` / `arm_init` / `arm_vm_init` / `pmap_bootstrap` / pmap runtime.
- Stage81 does not call or execute public `PE_init_platform`, `DTInit`, or `pe_identify_machine`.
- Stage81 does not execute public pexpert/pmap/IOKit proof objects.
- Stage81 does not execute the generated Mach-O fixture.
- Stage81 does not write persistent storage.

## Local validation

Local validation passed after the live-pmap log-string update:

```text
stage81/build.sh: success
stage81-qcdt.img dt_size=2521088 (0x267800)
stage81.elf undefined symbols: none
stage81-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 249 bytes including NUL
chosen boot-args string: 241 bytes including NUL
Android boot-image cmdlines: 1389 bytes including NUL
stale Stage80 tokens in stage81 code/build files: none
git diff --check: clean
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

Disassembly confirms real calls:

```text
bl stage81_xnu_start_stub
bl stage81_arm_init_stub
bl stage81_xnu_early_pmap_platform_init_run
bl stage81_xnu_pe_init_platform_false_run
bl stage81_xnu_arm_init_post_pe_bootstrap_run
bl stage81_xnu_arm_vm_init_live_pmap_run
```

Build outputs:

```text
stage81_fixture.macho: 1744 bytes
stage81.elf:          509688 bytes
stage81.bin:          456236 bytes
stage81.img:          458752 bytes
stage81-qcdt.img:    2979840 bytes
```

SHA256:

```text
34fcb0a4b0699a999b5b3225da20558fa4ad05d77d28f86d15e88e26264fbbc7  stage81_fixture.macho
9bce42f97d93f86c146d9f9fc044aafc26f73aeb7e7446b30fc90f5e2bf987ca  stage81.elf
b64ca73adc5a396cc55c7b4cd9b04787a7fd6732e6fb84ca32abac7ced4f001b  stage81.bin
6ab1c9dc43e5b69bd867ee70994e17b6de053bc0507953936e85bd7f6d1c33a7  stage81.img
9850d88663607d4519f5a92b20569fa04f1b70781f2c197f11a55d7f7178ccc5  stage81-qcdt.img
```

## Hardware validation result

Passed on real hardware (`cancro`, serial `4a2fe00b`) via non-persistent `fastboot boot`. The device booted Stage81, ran the full loader preflight plus the Stage-owned entry/early-init/PE-init-false/post-PE bootstrap/live-pmap install-verify-restore path, returned success, and recovered cleanly to Android with no persistent change. Command sequence used:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage81/stage81-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage81-arm-vm-init-live-pmap-last_kmsg.txt
```

Recovered log size:

```text
275864 /tmp/cancro-stage81-arm-vm-init-live-pmap-last_kmsg.txt
```

Observed pass markers:

```text
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_status=0x81000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_satisfied_mask=0x01007fff
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_failure_mask=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_candidate_l1_base=0x000e4000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_candidate_l1_checksum=0x2d810c02
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_original_ttbr0=0x000b0000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_ttbr0=0x000e4000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_restored_ttbr0=0x000b0000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_original_ttbcr=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_ttbcr=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_restored_ttbcr=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_original_dacr=0x00000003
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_dacr=0x00000003
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_restored_dacr=0x00000003
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_original_sctlr=0x00c5487b
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_sctlr=0x00c5487b
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_restored_sctlr=0x00c5487b
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_ttbr0_write_count=0x00000002
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_tlb_invalidate_count=0x00000002
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_pmap_installed=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_pmap_verified=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_high_alias_verified=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_ram_console_verified=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_gic_verified=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_live_pmap_restored=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_public_arm_vm_init_executed=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_public_pmap_runtime_executed=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_persistent_write_attempted=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_safety_boundary_preserved=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_arm_vm_init_live_pmap_checksum=0xac2b7182
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_status=0x81000001
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_satisfied_mask=0x0000ffff
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_ttbr0_before=0x000b0000
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_ttbr0_after=0x000b0000
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_sctlr_before=0x00c5487b
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_sctlr_after=0x00c5487b
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_mmu_unchanged=0x00000001
MI4IOS6_STAGE81_XNU stage81_xnu_entry_stub_checksum=0x300fb4f0
MI4IOS6_STAGE81_XNU loader_xnu_arm_vm_init_live_pmap_status_rollup=0x81000001
MI4IOS6_STAGE81_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE81_XNU loader_checksum=0xf194020c
MI4IOS6_STAGE81_XNU loader_status=0x81000001
MI4IOS6_STAGE81 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change was performed.
