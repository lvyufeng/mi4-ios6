# Experiment 16 — Stage13 High-Virtual Kernel Bootstrap Handoff

Date: 2026-06-04

Goal: organize the Stage12 high-virtual call primitive into a small named `kernel_bootstrap`-style handoff that reads boot arguments and writes PE/XNU-like bootstrap state through high virtual aliases.

Stage13 still does **not** run XNU or iOS. It proves a slightly more structured virtual bootstrap step: a high-virtual entry can consume boot/platform facts and populate a state block, then return safely to the identity-mapped recovery path.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- The high bootstrap function is tiny and does not perform persistent writes.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high bootstrap handoff.

## What Stage13 adds over Stage12

Stage12 proved:

- calling a tiny function through a high virtual alias,
- writing a high-virtual state pointer,
- returning to the identity path,
- SGI/timer IRQ success after high-call execution.

Stage13 adds:

- `stage13_kernel_bootstrap(...)` as a named high-virtual bootstrap entry,
- high-virtual boot-args pointer access,
- high-virtual bootstrap-state pointer writes,
- PE/XNU-like state fields passed into the high entry,
- checksum validation through both identity and high alias views.

## High-bootstrap mechanics

The main high alias is still:

```text
0xc0000000 -> 0x00000000
```

The bootstrap function was linked at:

```text
stage13_kernel_bootstrap = 0x0000ab68
```

Stage13 called it at:

```text
0xc000ab68
```

The boot arguments and bootstrap state were passed as high virtual pointers:

```text
boot_args identity: 0x00020000
boot_args alias:    0xc0020000
state identity:     0x00018018
state alias:        0xc0018018
```

The high-virtual function copied/recorded:

- boot args revision/version,
- machine type,
- device-tree length,
- PE GIC distributor base,
- PE timer frequency,
- PE vector base,
- checksum.

## Built image

```bash
./stage13/build.sh
```

Successful local build:

```text
out/stage13/stage13-qcdt.img
sha256=83fa7ad59ad2c033855ce9af953a6e6e64d9bf0335d32b72eb440c1a198dd638
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=26332 (0x66dc)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage13 mi4ios6=stage13 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage13_vectors
0000a36c T stage13_irq_c_handler
0000a784 T gic_timer_selftest
0000ab68 t stage13_kernel_bootstrap
0000b158 T mmu_high_call_selftest
0000b37c T mmu_high_bootstrap_selftest
0000b670 T kernel_entry
0000bb30 T stage13_main
0001c000 b stage13_l1_table
00022000 B __stage13_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage13/stage13-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2490 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage13-last_kmsg.txt
```

The recovered log was 10995 bytes and contained:

```text
216 MI4IOS6_STAGE13 markers
193 MI4IOS6_STAGE13_XNU markers
```

## Key recovered high-bootstrap markers

```text
MI4IOS6_STAGE13_XNU mmu high bootstrap selftest begin
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_fn_phys=0x0000ab68
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_fn_virt=0xc000ab68
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_args_phys=0x00020000
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_args_virt=0xc0020000
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_state_phys=0x00018018
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_state_virt=0xc0018018
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_result=0xeb26e8c6
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_expected=0xeb26e8c6
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_magic_id=0x13001300
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_rev_ver_id=0x00020002
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_machine_id=0x00008974
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_dt_len_id=0x00000a10
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_gic_id=0xf9000000
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_timer_id=0x0124f800
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_vector_id=0x000080a0
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_checksum_id=0xeb26e8c6
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_magic_alias=0x13001300
MI4IOS6_STAGE13_XNU mmu_high_bootstrap_checksum_alias=0xeb26e8c6
MI4IOS6_STAGE13_XNU mmu high bootstrap selftest ok
```

Stage13 also retained the Stage12 high-call test:

```text
MI4IOS6_STAGE13_XNU mmu high call selftest ok
```

## Post-bootstrap IRQ retests

SGI0 still delivered after the high bootstrap handoff:

```text
MI4IOS6_STAGE13 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE13_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the high bootstrap handoff:

```text
MI4IOS6_STAGE13 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE13_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE13_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE13_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE13_XNU kernel_entry ok
MI4IOS6_STAGE13 kernel_entry returned success
MI4IOS6_STAGE13 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage13 proves a structured high-virtual bootstrap handoff. The payload can call a named high-virtual entry, pass boot arguments through a high virtual pointer, record PE/XNU-like platform facts into a high virtual state block, return to the identity-mapped caller, and keep debug/reboot/interrupt paths working.

This is closer to the shape needed for an XNU bring-up path: a boot wrapper establishes translation and platform facts, then hands off to a high-virtual kernel bootstrap entry.

## Success criteria — met

1. bootloader accepted `stage13-qcdt.img`: yes
2. identity MMU and high alias tests still succeeded: yes
3. Stage12 high-call test still succeeded: yes
4. high bootstrap function `0xc000ab68` executed and returned: yes
5. high boot-args pointer `0xc0020000` was consumed: yes
6. high bootstrap-state pointer `0xc0018018` was written: yes
7. PE/XNU-like state fields matched expected GIC/timer/vector facts: yes
8. SGI and timer IRQ paths still worked after bootstrap validation: yes
9. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage14 can start reshaping the skeleton so the high-virtual bootstrap path owns more of the XNU-adjacent kernel flow:

- keep identity/recovery paths available,
- move `PE_state` validation or selected pexpert-derived values into a high-virtual kernel state object,
- add explicit high-virtual logging markers before returning,
- keep caches disabled and preserve SGI/timer IRQ retests,
- avoid persistent writes.
