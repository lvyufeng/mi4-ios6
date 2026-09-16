# Experiment 17 — Stage14 High-Virtual PE/XNU State Bootstrap

Date: 2026-06-04

Goal: move more platform-state work into the high-virtual bootstrap path by reading high-virtual boot arguments and high-virtual `PE_state`, emitting explicit high-virtual log markers, and writing a richer PE/XNU-like bootstrap state block.

Stage14 still does **not** run XNU or iOS. It is another incremental XNU-adjacent bootstrap step: the high-virtual kernel bootstrap path now owns a richer platform-state copy/checksum instead of only a few scalar arguments.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after high-virtual bootstrap state validation.

## What Stage14 adds over Stage13

Stage13 proved:

- named high-virtual bootstrap entry,
- high-virtual boot-args pointer access,
- high-virtual bootstrap-state writes,
- post-bootstrap SGI/timer IRQ success.

Stage14 adds:

- a high-virtual `PE_state` pointer into `stage14_kernel_bootstrap`,
- high-virtual log calls from inside the high bootstrap function,
- richer bootstrap state copied from boot args and PE_state:
  - boot args revision/version,
  - machine type,
  - device-tree length,
  - memory base/size,
  - CPU count,
  - GIC distributor/CPU-interface bases,
  - timer base/frequency,
  - vector base,
  - boot flags,
  - checksum.

## High-bootstrap mechanics

The high alias is still:

```text
0xc0000000 -> 0x00000000
```

The high bootstrap function was linked at:

```text
stage14_kernel_bootstrap = 0x0000ab68
```

Stage14 called it at:

```text
0xc000ab68
```

Important pointers:

```text
boot_args identity: 0x00020000
boot_args alias:    0xc0020000
PE_state identity:  0x00015020
PE_state alias:     0xc0015020
state identity:     0x00018018
state alias:        0xc0018018
```

The high bootstrap function itself emitted these log markers while executing through the high alias:

```text
MI4IOS6_STAGE14_XNU high virtual kernel_bootstrap entered
MI4IOS6_STAGE14_XNU high_bootstrap_checksum=0x3274cfc2
MI4IOS6_STAGE14_XNU high virtual kernel_bootstrap leaving
```

## Built image

```bash
./stage14/build.sh
```

Successful local build:

```text
out/stage14/stage14-qcdt.img
sha256=bb4ba85f00fee7579bd15a6b885abd35177d0d4f59e445cfe2f5ff7573f94c71
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=27120 (0x69f0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage14 mi4ios6=stage14 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage14_vectors
0000a36c T stage14_irq_c_handler
0000a784 T gic_timer_selftest
0000ab68 t stage14_kernel_bootstrap
0000b204 T mmu_high_call_selftest
0000b428 T mmu_high_bootstrap_selftest
0000b810 T kernel_entry
0000bcd0 T stage14_main
0001c000 b stage14_l1_table
00022000 B __stage14_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage14/stage14-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2492 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage14-last_kmsg.txt
```

The recovered log was 11667 bytes and contained:

```text
227 MI4IOS6_STAGE14 markers
204 MI4IOS6_STAGE14_XNU markers
```

## Key recovered high-PE-state markers

```text
MI4IOS6_STAGE14_XNU mmu high bootstrap selftest begin
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_fn_phys=0x0000ab68
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_fn_virt=0xc000ab68
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_args_phys=0x00020000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_args_virt=0xc0020000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_pe_phys=0x00015020
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_pe_virt=0xc0015020
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_state_phys=0x00018018
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_state_virt=0xc0018018
MI4IOS6_STAGE14_XNU high virtual kernel_bootstrap entered
MI4IOS6_STAGE14_XNU high_bootstrap_checksum=0x3274cfc2
MI4IOS6_STAGE14_XNU high virtual kernel_bootstrap leaving
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_result=0x3274cfc2
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_expected=0x3274cfc2
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_magic_id=0x14001400
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_rev_ver_id=0x00020002
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_machine_id=0x00008974
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_dt_len_id=0x00000a10
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_mem_base_id=0x80000000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_mem_size_id=0x5e500000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_cpu_count_id=0x00000004
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_gic_dist_id=0xf9000000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_gic_cpu_id=0xf9002000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_timer_base_id=0xf9020000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_timer_freq_id=0x0124f800
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_vector_id=0x000080a0
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_boot_flags_id=0x00000000
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_checksum_id=0x3274cfc2
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_magic_alias=0x14001400
MI4IOS6_STAGE14_XNU mmu_high_bootstrap_checksum_alias=0x3274cfc2
MI4IOS6_STAGE14_XNU mmu high bootstrap selftest ok
```

## Post-bootstrap IRQ retests

SGI0 still delivered after the high PE-state bootstrap:

```text
MI4IOS6_STAGE14 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE14_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the high PE-state bootstrap:

```text
MI4IOS6_STAGE14 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE14_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE14_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE14_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE14_XNU kernel_entry ok
MI4IOS6_STAGE14 kernel_entry returned success
MI4IOS6_STAGE14 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage14 proves that high-virtual bootstrap code can call the existing logging path, consume both high-virtual boot args and high-virtual PE_state, and populate a richer XNU-like platform state block. The payload then returns to the identity path and still handles SGI and timer interrupts.

This is a useful stepping stone toward making the high-virtual path own more of the `kernel_entry` flow instead of only being a validation side-call.

## Success criteria — met

1. bootloader accepted `stage14-qcdt.img`: yes
2. identity/high-alias/high-call tests still succeeded: yes
3. high bootstrap function logged while executing through the high alias: yes
4. high PE_state pointer `0xc0015020` was consumed: yes
5. richer state fields matched expected memory/CPU/GIC/timer/vector facts: yes
6. checksum matched through identity and alias views: yes
7. SGI and timer IRQ paths still worked after bootstrap validation: yes
8. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage15 can start letting the high-virtual bootstrap path own a larger slice of the XNU-adjacent kernel flow:

- move selected pexpert/PE_state validation into the high bootstrap function,
- return a structured success/failure status from high virtual execution,
- keep identity/recovery mappings and caches disabled,
- keep SGI/timer IRQ retests after the high path,
- avoid persistent writes.
