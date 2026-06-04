# Experiment 14 — Stage11 High Virtual Alias Mapping

Date: 2026-06-04

Goal: extend the Stage10 identity MMU setup with controlled high virtual aliases, while keeping caches disabled and preserving identity-mapped recovery/debug paths.

Stage11 still does **not** run XNU or iOS. It is a small step toward an XNU-like bootstrap virtual-memory layout: prove that the payload can keep identity mappings while also using selected high virtual aliases.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- Identity mappings from Stage10 remain in place for recovery paths.
- Caches remain disabled.
- High aliases are tested only for simple reads/writes and MMIO reads.
- `ram_console`, GIC, timer, IMEM, and PS_HOLD remain identity-mapped.
- SGI/timer IRQ selftests are rerun after alias validation.

## What Stage11 adds over Stage10

Stage10 proved:

- first ARMv7 MMU enable,
- identity mapping of low payload, ram_console, IMEM, GIC/timer, and PS_HOLD,
- SGI/timer IRQ paths still work post-MMU.

Stage11 adds:

- high alias `0xc0000000 -> 0x00000000` for the low payload/code/data section,
- high alias `0xc0100000 -> 0xde500000` for ram_console,
- high alias `0xc0200000 -> 0xf9000000` for GIC/timer MMIO,
- alias-vs-identity data write/read validation,
- alias-vs-identity vector table read validation,
- high-alias ram_console and GIC MMIO read validation.

## Mapped sections

Stage11 keeps all Stage10 identity mappings and adds:

```text
0xc0000000  -> 0x00000000  low payload/code/data/BSS/VBAR section
0xc0100000  -> 0xde500000  ram_console section
0xc0200000  -> 0xf9000000  GIC/timer MMIO section
```

The descriptors are still conservative ARMv7 section descriptors with caches disabled:

```text
base attributes: 0x00010c02
```

## Built image

```bash
./stage11/build.sh
```

Successful local build:

```text
out/stage11/stage11-qcdt.img
sha256=a7d05576a37083d2259fd786c6fdb0f973430fd0ed3d7a5714a4f525e78e5672
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=23124 (0x5a54)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage11 mi4ios6=stage11 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage11_vectors
0000a36c T stage11_irq_c_handler
0000a784 T gic_timer_selftest
0000ab20 T mmu_identity_selftest
0000ae48 T mmu_high_alias_selftest
0000b08c T kernel_entry
0000b514 T stage11_main
0001c000 b stage11_l1_table
00022000 B __stage11_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage11/stage11-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2488 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage11-last_kmsg.txt
```

The recovered log was 8994 bytes and contained:

```text
181 MI4IOS6_STAGE11 markers
158 MI4IOS6_STAGE11_XNU markers
```

## Key recovered MMU alias markers

```text
MI4IOS6_STAGE11_XNU mmu_entry_high_alias=0x00010c02
MI4IOS6_STAGE11_XNU mmu_entry_high_ram_console=0xde510c02
MI4IOS6_STAGE11_XNU mmu_entry_high_gic=0xf9010c02
MI4IOS6_STAGE11_XNU mmu_sctlr_after=0x00c5487b
MI4IOS6_STAGE11_XNU mmu high alias selftest begin
MI4IOS6_STAGE11_XNU mmu_alias_base=0xc0000000
MI4IOS6_STAGE11_XNU mmu_alias_entry=0x00010c02
MI4IOS6_STAGE11_XNU mmu_alias_probe_phys=0x00018004
MI4IOS6_STAGE11_XNU mmu_alias_vector_phys=0x000080a0
MI4IOS6_STAGE11_XNU mmu_alias_read_after_identity_write=0x11112222
MI4IOS6_STAGE11_XNU mmu_identity_read_after_alias_write=0x33334444
MI4IOS6_STAGE11_XNU mmu_alias_vector_word=0xea000006
MI4IOS6_STAGE11_XNU mmu_identity_vector_word=0xea000006
MI4IOS6_STAGE11_XNU mmu_alias_ram_console_sig=0x43474244
MI4IOS6_STAGE11_XNU mmu_identity_ram_console_sig=0x43474244
MI4IOS6_STAGE11_XNU mmu_alias_gicd_ctlr=0x00000001
MI4IOS6_STAGE11_XNU mmu_identity_gicd_ctlr=0x00000001
MI4IOS6_STAGE11_XNU mmu high alias selftest ok
```

## Post-alias IRQ retests

SGI0 still delivered after high alias validation:

```text
MI4IOS6_STAGE11 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE11_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after high alias validation:

```text
MI4IOS6_STAGE11 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE11_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE11_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE11_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE11_XNU kernel_entry ok
MI4IOS6_STAGE11 kernel_entry returned success
MI4IOS6_STAGE11 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage11 proves the payload can run with both identity mappings and selected high virtual aliases. The high alias of low memory is coherent enough with caches disabled for direct data and vector-table reads/writes, and separate aliases for ram_console and GIC MMIO work for debug/MMIO reads.

This is the first step away from a flat identity-only address space toward a bootstrap VM layout closer to what an XNU platform port eventually needs.

## Success criteria — met

1. bootloader accepted `stage11-qcdt.img`: yes
2. Stage10 identity MMU selftest still succeeded: yes
3. high alias `0xc0000000 -> 0x00000000` was installed: yes
4. data write/read through identity and alias matched: yes
5. vector-table read through identity and alias matched: yes
6. ram_console high alias read returned `0x43474244`: yes
7. GIC high alias read matched identity read: yes
8. SGI and timer IRQ paths still worked after alias validation: yes
9. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage12 can begin an actual virtual-call/virtual-data bootstrap experiment:

- keep identity mappings for recovery,
- add high aliases for code and data,
- call a tiny function through its high virtual alias,
- use a high virtual data pointer for a state block,
- keep caches disabled and preserve MMIO identity paths,
- verify that the system can return safely to the identity-mapped path and reboot.
