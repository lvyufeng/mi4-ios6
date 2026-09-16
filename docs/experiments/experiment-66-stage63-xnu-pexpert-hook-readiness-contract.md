# Experiment 66 — Stage63 MSM8974 XNU pexpert interrupt/timer hook readiness contract

## Goal

Stage63 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage62 pmap safe live-table transition prerequisite dry-run toward the next real-XNU blocker: MSM8974 pexpert interrupt/timer hook readiness.

The goal is not to run XNU, not to execute public ARM pexpert/platform code, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the existing PE_state, Apple-DT, GIC, SGI IRQ, timer IRQ, and 19.2 MHz timebase facts are coherent inputs for a future MSM8974 XNU platform-expert implementation.

## Safety boundary

Stage63 preserves all Stage62 boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No generated Mach-O execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The public object subset remains bounded to:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage63/xnu_object_shims.c
```

Public ARM VM/pmap files remain reference-only.

## Implementation

Stage63 adds:

```text
stage63/xnu_pexpert_hook_readiness_contract.c
struct stage63_xnu_pexpert_hook_readiness_contract
```

The contract imports source facts from:

- `stage63_xnu_compile_graph_result()` / loader copy,
- `stage63_xnu_object_subset_result()` / loader copy,
- `stage63_xnu_link_result()` / loader copy,
- Stage63 retained pmap transition dry-run contract,
- loader safety mask,
- PE_state populated from `boot_args` and Apple-DT,
- GIC snapshot and SGI/timer IRQ selftest observations,
- Stage-owned and XNU-adjacent timebase functions.

The contract proves:

- `/chosen/boot-args` contains `mi4ios6.stage=63` and `pexpert-hook-ready`.
- Apple-DT semantic mask remains complete.
- PE_state points at the live boot args and device-tree block.
- PE_state records memory base/size, CPU count, machine type, vector base, GIC bases, timer base, and timer frequency exactly.
- GICv2 distributor/CPU bases are `0xf9000000` and `0xf9002000`.
- GIC snapshot remains valid with 288 IRQs and four CPU interfaces.
- SGI0 selftest delivered an IRQ through the returnable handler.
- Timer IRQ selftest delivered a generic physical timer PPI through the same handler.
- Timer PPI IDs remain 18 and 19, mask `0x000c0000`.
- Timebase frequency is 19,200,000 Hz through both Stage-owned and `ml_*` interfaces.
- Public pexpert objects are compile/link proof inputs only; runtime-heavy public ARM pexpert paths stay blocked.
- Public pmap runtime execution counts remain zero.
- Proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidations, cache changes, generated Mach-O execution, XNU entry execution, and persistent writes remain zero.

Because the 32-bit loader satisfied mask was fully consumed by Stage62, Stage63 does not add a 33rd top-level loader bit. Instead, it records the pexpert hook readiness as a contract roll-up and uses that result to refine the existing platform/interruption readiness gates. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x63000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage63/build.sh
```

Final build size:

```text
text=255824 data=0 bss=398060 dec=653884 hex=9fa3c
```

Hashes:

```text
f5a2acf33446dd1625c007af8596bbf85c1a180bf92c1ee464028868c58ffe4d  out/stage63/stage63_fixture.macho
d991ec3f6aa54fa09653dfe5274d01e09084d1b28007e64ea2dc687393fc0e0f  out/stage63/stage63.elf
9632f2f988f7706fd9f0891ffb2d32e6ed67d42e608264d0b15c8345fae538df  out/stage63/stage63.bin
cf2b9a46fd633bfda7bbe5ac7ba0b63950f4ae4579e239e37d476be3245c4029  out/stage63/stage63.img
5fbeedcae7628aa7c0f72bcf6ef1454eba6b1dd00ad364a5d11c219ff73e996a  out/stage63/stage63-qcdt.img
```

Boot image parse checks confirmed:

```text
stage63.img:      page_size=2048 kernel_size=255824 dt_size=0
stage63-qcdt.img: page_size=2048 kernel_size=255824 dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage63/stage63.elf
out/stage63/xnu-link/stage63-xnu-link.elf
```

Static validation facts:

```text
stage63/boot_args.c command_line_len_with_nul=182 fits256=True
stage63/stage63_main.c command_line_len_with_nul=182 fits256=True
stage63/xnu_object_shims.c command_line_len_with_nul=159 fits256=True
stale_marker_violations=0
public_vm_pmap_command_violations=0
out/stage63 ignored by .gitignore
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage63/stage63-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage63-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage63-last_kmsg.txt
size=178383 bytes
stage63_marker_count=2549
stage63_xnu_marker_count=2523
```

Key contract markers:

```text
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_status=0x63000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_required_mask=0x07ffffff
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_checksum=0x6cabcf99
MI4IOS6_STAGE63_XNU loader_xnu_pexpert_hook_readiness_contract_status=0x63000001
MI4IOS6_STAGE63_XNU loader_xnu_pexpert_hook_readiness_contract_status_rollup=0x63000001
```

Key PE/GIC/timer readiness facts:

```text
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_boot_args_stage63_marker=0x00000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_boot_args_pexpert_marker=0x00000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_pe_cpu_count=0x00000004
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_pe_gic_dist_base=0xf9000000
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_pe_gic_cpu_base=0xf9002000
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_pe_timer_base=0xf9020000
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_pe_timer_frequency=0x0124f800
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_gic_irq_count=0x00000120
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_gic_cpu_interface_count=0x00000004
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_sgi_irq_count=0x00000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_sgi_sgi0_count=0x00000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_timer_ppi_mask=0x000c0000
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_timer_irq_count=0x00000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_timer_timer_count=0x00000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_timer_last_irq_id=0x00000013
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_timebase_freq_hz=0x0124f800
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_platform_gap_mask=0x0000001f
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_interrupt_ready_mask=0x0000000f
```

Final success markers:

```text
MI4IOS6_STAGE63_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE63_XNU loader_status=0x63000001
MI4IOS6_STAGE63_XNU kernel_entry ok
MI4IOS6_STAGE63 kernel_entry returned success
```

## Result

Stage63 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned MSM8974 pexpert interrupt/timer hook readiness contract while preserving every no-XNU/no-public-runtime/no-live-pmap-install/no-persistent-write boundary from Stage62.

The next safe direction is IOKit/platform-driver scaffolding, a deeper MSM8974 pexpert implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof before any attempt to jump into XNU or install proposed live pmap tables.
