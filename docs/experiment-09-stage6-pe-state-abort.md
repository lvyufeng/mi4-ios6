# Experiment 09 — Stage6 PE_state Skeleton + Data-Abort Recovery

Date: 2026-06-04

Goal: extend the Stage5 XNU-adjacent skeleton with a PE_state-like platform state block and verify that the custom exception vectors can catch a deliberate data abort, not just an undefined instruction.

Stage6 still does **not** run XNU or iOS. It moves the skeleton closer to XNU pexpert shape by collecting platform discovery results into a persistent state object.

## What Stage6 adds over Stage5

Stage5 proved:

- boot wrapper -> `kernel_entry(struct boot_args *)`,
- pexpert-like Apple-DT discovery,
- XNU-like `ml_*` timebase stubs.

Stage6 adds:

- `struct pe_platform_state PE_state_stage6`, analogous in spirit to XNU platform expert state,
- `pe_state_init_from_boot_args()` to populate state from `boot_args` and Apple-DT,
- `pe_state_log()` and `pe_state_validate()`,
- a deliberate data-abort trigger through `trigger_stage6_data_abort_test()`.

The data-abort trigger is executed only after `kernel_entry` succeeds. The handler should log:

```text
MI4IOS6_STAGE6 exception: data-abort lr=... spsr=...
```

and then reboot through PS_HOLD.

## New source file

```text
stage6/pe_state.c
```

Stage6 also extends:

```text
stage6/vectors.S
stage6/stage6.h
stage6/xnu_kernel.c
stage6/stage6_main.c
```

## Built image

```bash
./stage6/build.sh
```

Successful local build:

```text
out/stage6/stage6-qcdt.img
sha256=5ed4b22511867adb37214d8a3599760f52836ac55f89de3b45ce54c8632c44c8
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=15144 (0x3b28)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage6 mi4ios6=stage6 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage6_vectors
000081c0 T trigger_stage6_data_abort_test
00009bd4 T pe_state_init_from_boot_args
00009e74 T pe_state_log
00009f68 T pe_state_validate
0000a054 T kernel_entry
0000a43c T stage6_main
00010b50 B PE_state_stage6
00012000 B __stage6_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage6/stage6-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2480 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

The phone returned to Android automatically after ~30 seconds; ADB transport id changed from 29 to 30.

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage6-last_kmsg.txt
```

The recovered log was 3405 bytes and contained:

```text
71 MI4IOS6_STAGE6 markers
48 MI4IOS6_STAGE6_XNU markers
```

## Key recovered markers

Stage6 retained Stage5 kernel-entry success:

```text
MI4IOS6_STAGE6 boot-wrapper handoff -> kernel_entry(boot_args*)
MI4IOS6_STAGE6_XNU kernel_entry(struct boot_args*) entered
MI4IOS6_STAGE6_XNU pexpert discovery ok
MI4IOS6_STAGE6_XNU ml timebase ok
MI4IOS6_STAGE6_XNU kernel_entry ok
MI4IOS6_STAGE6 kernel_entry returned success
```

PE_state-like summary:

```text
MI4IOS6_STAGE6_XNU PE_state summary begin
MI4IOS6_STAGE6_XNU PE_bootArgs=0x00010b80
MI4IOS6_STAGE6_XNU PE_deviceTreeHead=0x00010cc0
MI4IOS6_STAGE6_XNU PE_deviceTreeLength=0x00000a10
MI4IOS6_STAGE6_XNU PE_memoryBase=0x80000000
MI4IOS6_STAGE6_XNU PE_memorySize=0x5e500000
MI4IOS6_STAGE6_XNU PE_cpuCount=0x00000004
MI4IOS6_STAGE6_XNU PE_gicDistributorBase=0xf9000000
MI4IOS6_STAGE6_XNU PE_gicCpuBase=0xf9002000
MI4IOS6_STAGE6_XNU PE_timerBase=0xf9020000
MI4IOS6_STAGE6_XNU PE_timerFrequency=0x0124f800
MI4IOS6_STAGE6_XNU PE_machineType=0x00008974
MI4IOS6_STAGE6_XNU PE_vectorBase=0x000080a0
MI4IOS6_STAGE6_XNU PE_state summary end
MI4IOS6_STAGE6_XNU PE_state validate ok
```

Data-abort recovery:

```text
MI4IOS6_STAGE6 triggering deliberate data-abort exception
MI4IOS6_STAGE6 exception: data-abort lr=0x000081c4 spsr=0x60000193
MI4IOS6_STAGE6 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage6 confirms that the XNU-adjacent platform state can be populated from `boot_args` and the Apple-style device tree, then validated inside `kernel_entry`.

It also proves the custom exception safety net catches data aborts. This matters more than the Stage4 undefined-instruction test because future bring-up work will commonly hit data aborts when MMIO ranges, mappings, or pointers are wrong.

## Success criteria — met

1. bootloader accepted `stage6-qcdt.img`: yes
2. `kernel_entry(struct boot_args*)` still succeeded: yes
3. pexpert-like discovery still succeeded: yes
4. PE_state-like platform state was populated and logged: yes
5. PE_state validation passed: yes
6. deliberate data abort was caught and logged: yes
7. exception handler reset the phone through PS_HOLD: yes

## Next stage

Stage7 can start modeling interrupt-controller behavior more seriously, while still staying safe:

- add a GIC driver skeleton that reads and caches distributor/CPU-interface state,
- log enabled/pending/target registers for a small IRQ range without modifying them,
- add mask/unmask routines but do not enable external IRQ delivery yet,
- optionally test SGI/IPI only after vector/IRQ stack handling is ready,
- continue keeping data-abort logging active.
