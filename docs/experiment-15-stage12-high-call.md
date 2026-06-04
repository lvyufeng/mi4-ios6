# Experiment 15 — Stage12 High-Virtual Function Call

Date: 2026-06-04

Goal: go beyond high-alias reads/writes and execute a tiny function through a high virtual alias while keeping identity mappings, caches disabled, and all recovery/debug paths intact.

Stage12 still does **not** run XNU or iOS. It proves an early bootstrap VM primitive: a low-loaded payload can keep identity mappings but call selected code and touch selected state through high virtual addresses.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- Stage10 identity mappings remain in place for recovery.
- Stage11 high aliases remain conservative 1 MiB sections.
- Caches remain disabled while alias execution is tested.
- The high-call target is a tiny arithmetic/state function only.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual call.

## What Stage12 adds over Stage11

Stage11 proved:

- high alias for low code/data section,
- high aliases for ram_console and GIC/timer MMIO,
- alias-vs-identity data/vector/MMIO reads,
- post-alias SGI/timer IRQ success.

Stage12 adds:

- `stage12_high_alias_target(input, state)` as a noinline function in the low payload section,
- a high virtual function pointer at `0xc0000000 + physical_function_address`,
- a high virtual state pointer at `0xc0000000 + physical_state_address`,
- function execution via the high virtual address,
- state writes from high-virtual code through the high-virtual state pointer,
- validation through both identity and alias paths after return.

## High-call mechanics

The high alias remains:

```text
0xc0000000 -> 0x00000000
```

The target function was linked at physical/identity address:

```text
stage12_high_alias_target = 0x0000ab20
```

Stage12 called it at:

```text
0xc000ab20
```

The state block was accessed at:

```text
identity: 0x00018008
alias:    0xc0018008
```

The call returns to the identity-mapped caller path, so this proves high-virtual execution can be entered and exited safely while preserving the early identity recovery path.

## Built image

```bash
./stage12/build.sh
```

Successful local build:

```text
out/stage12/stage12-qcdt.img
sha256=448d8eebe91c6e5a7a8da40e45920c9abf2d9d6679b0078422a9c08bd401ddac
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=24464 (0x5f90)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage12 mi4ios6=stage12 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage12_vectors
0000a36c T stage12_irq_c_handler
0000a784 T gic_timer_selftest
0000ab20 t stage12_high_alias_target
0000ab68 T mmu_identity_selftest
0000ae90 T mmu_high_alias_selftest
0000b0d4 T mmu_high_call_selftest
0000b2f8 T kernel_entry
0000b79c T stage12_main
0001c000 b stage12_l1_table
00022000 B __stage12_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage12/stage12-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2488 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage12-last_kmsg.txt
```

The recovered log was 9805 bytes and contained:

```text
196 MI4IOS6_STAGE12 markers
173 MI4IOS6_STAGE12_XNU markers
```

## Key recovered high-call markers

```text
MI4IOS6_STAGE12_XNU mmu high call selftest begin
MI4IOS6_STAGE12_XNU mmu_high_call_fn_phys=0x0000ab20
MI4IOS6_STAGE12_XNU mmu_high_call_fn_virt=0xc000ab20
MI4IOS6_STAGE12_XNU mmu_high_call_state_phys=0x00018008
MI4IOS6_STAGE12_XNU mmu_high_call_state_virt=0xc0018008
MI4IOS6_STAGE12_XNU mmu_high_call_input=0x55667788
MI4IOS6_STAGE12_XNU mmu_high_call_expected=0x47a69a9a
MI4IOS6_STAGE12_XNU mmu_high_call_result=0x47a69a9a
MI4IOS6_STAGE12_XNU mmu_high_call_magic_id=0x12001200
MI4IOS6_STAGE12_XNU mmu_high_call_input_id=0x55667788
MI4IOS6_STAGE12_XNU mmu_high_call_result_id=0x47a69a9a
MI4IOS6_STAGE12_XNU mmu_high_call_checksum_id=0x00c0ff12
MI4IOS6_STAGE12_XNU mmu_high_call_magic_alias=0x12001200
MI4IOS6_STAGE12_XNU mmu_high_call_checksum_alias=0x00c0ff12
MI4IOS6_STAGE12_XNU mmu high call selftest ok
```

Stage12 also retained the Stage11 high-alias checks:

```text
MI4IOS6_STAGE12_XNU mmu high alias selftest ok
MI4IOS6_STAGE12_XNU mmu_alias_ram_console_sig=0x43474244
MI4IOS6_STAGE12_XNU mmu_alias_gicd_ctlr=0x00000001
```

## Post-high-call IRQ retests

SGI0 still delivered after the high-virtual call:

```text
MI4IOS6_STAGE12 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE12_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the high-virtual call:

```text
MI4IOS6_STAGE12 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE12_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE12_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE12_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE12_XNU kernel_entry ok
MI4IOS6_STAGE12 kernel_entry returned success
MI4IOS6_STAGE12 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage12 proves controlled high-virtual execution under the early MMU setup. A function located in the low payload section was invoked through its high alias, wrote to a state block through a high alias pointer, returned to the identity-mapped caller, and left the debug/MMIO/IRQ paths working.

This is an important bridge between the current boot-wrapper skeleton and a future XNU-style virtual bootstrap path, where kernel code and data eventually need stable virtual addresses rather than a flat physical identity map.

## Success criteria — met

1. bootloader accepted `stage12-qcdt.img`: yes
2. Stage10 identity MMU selftest still succeeded: yes
3. Stage11 high-alias selftest still succeeded: yes
4. function pointer `0xc000ab20` executed and returned: yes
5. high-virtual state pointer `0xc0018008` was written and validated: yes
6. identity and alias views of the state matched: yes
7. SGI and timer IRQ paths still worked after high-call validation: yes
8. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage13 can begin organizing this into a more XNU-like bootstrap handoff:

- keep the early identity map for recovery,
- create a named `kernel_bootstrap()` entry that is called through the high virtual alias,
- move a small PE/XNU state update into that high-virtual bootstrap path,
- validate that logging, GIC, timer, and reboot still work after returning,
- keep caches disabled and avoid persistent writes.
