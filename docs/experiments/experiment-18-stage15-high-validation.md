# Experiment 18 — Stage15 High-Virtual Bootstrap Validation Status

Date: 2026-06-04

Goal: let the high-virtual bootstrap path own selected PE_state/boot_args validation and return a structured status word instead of only a checksum.

Stage15 still does **not** run XNU or iOS. It makes the high-virtual path more kernel-like by giving it validation responsibility and an explicit success/failure result.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after high-virtual validation.

## What Stage15 adds over Stage14

Stage14 proved:

- high-virtual bootstrap logging,
- high-virtual boot_args and PE_state access,
- richer PE/XNU-like state copied through high aliases,
- post-bootstrap SGI/timer IRQ success.

Stage15 adds:

- validation inside `stage15_kernel_bootstrap`,
- a failure bitmask for boot/platform facts,
- a structured status word:
  - `0x15000001` means success,
  - `0x15000000 | failure_mask` would represent failure,
- checksum over both state and status fields,
- identity/alias verification of the status and validation fields after returning.

Validated high-virtual facts:

- boot args revision/version,
- machine type,
- device-tree pointer/length,
- memory base/size,
- CPU count,
- GIC distributor/CPU-interface bases,
- timer base/frequency,
- vector base.

## Built image

```bash
./stage15/build.sh
```

Successful local build:

```text
out/stage15/stage15-qcdt.img
sha256=43e38da4db43848a143b63cca34220ebda14b44079ca17b632764736e2cd80a6
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=27776 (0x6c80)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage15 mi4ios6=stage15 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage15_vectors
0000a36c T stage15_irq_c_handler
0000a784 T gic_timer_selftest
0000ab68 t stage15_kernel_bootstrap
0000b574 T mmu_high_bootstrap_selftest
0000b9e0 T kernel_entry
0000bea0 T stage15_main
0001c000 b stage15_l1_table
00022000 B __stage15_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage15/stage15-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2492 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage15-last_kmsg.txt
```

The recovered log was 11917 bytes and contained:

```text
231 MI4IOS6_STAGE15 markers
208 MI4IOS6_STAGE15_XNU markers
```

## Key recovered high-validation markers

```text
MI4IOS6_STAGE15_XNU mmu high bootstrap selftest begin
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_fn_phys=0x0000ab68
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_fn_virt=0xc000ab68
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_args_phys=0x00020000
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_args_virt=0xc0020000
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_pe_phys=0x00015020
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_pe_virt=0xc0015020
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_state_phys=0x00018018
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_state_virt=0xc0018018
MI4IOS6_STAGE15_XNU high virtual kernel_bootstrap entered
MI4IOS6_STAGE15_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE15_XNU high_bootstrap_status=0x15000001
MI4IOS6_STAGE15_XNU high_bootstrap_checksum=0x2674cec3
MI4IOS6_STAGE15_XNU high virtual kernel_bootstrap leaving
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_result=0x15000001
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_expected_checksum=0x2674cec3
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_status_id=0x15000001
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_checksum_id=0x2674cec3
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_magic_alias=0x15001500
MI4IOS6_STAGE15_XNU mmu_high_bootstrap_checksum_alias=0x2674cec3
MI4IOS6_STAGE15_XNU mmu high bootstrap selftest ok
```

## Post-validation IRQ retests

SGI0 still delivered after high-virtual validation:

```text
MI4IOS6_STAGE15 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE15_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after high-virtual validation:

```text
MI4IOS6_STAGE15 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE15_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE15_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE15_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE15_XNU kernel_entry ok
MI4IOS6_STAGE15 kernel_entry returned success
MI4IOS6_STAGE15 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage15 makes the high-virtual bootstrap path responsible for selected platform validation and structured status reporting. The high path now consumes boot args and PE_state, validates core platform facts, logs its status, returns a success value, and the identity path verifies status/checksum through both identity and high alias views.

This moves the experiment closer to an XNU-style handoff where the higher-level kernel path owns platform validation rather than merely being a side-call.

## Success criteria — met

1. bootloader accepted `stage15-qcdt.img`: yes
2. identity/high-alias/high-call tests still succeeded: yes
3. high bootstrap validation mask was zero: yes
4. high bootstrap returned status `0x15000001`: yes
5. checksum matched through identity and alias views: yes
6. SGI and timer IRQ paths still worked after validation: yes
7. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage16 can begin moving the flow from “high path returns to identity for everything” toward “high path runs a small XNU-like init sequence”:

- keep identity/recovery mappings and caches disabled,
- build a tiny ordered high-virtual init sequence,
- include timebase check, PE_state validation, and interrupt-controller state summary in that sequence,
- return a structured status and keep post-run IRQ retests,
- avoid persistent writes.
