# Experiment 04 — Stage1 Boot Wrapper / XNU Boot Args Stub

Date: 2026-06-04

Goal: move beyond Stage0's one-shot marker payload and prove a minimal XNU-style boot-wrapper contract on real Xiaomi Mi 4 (`cancro`) hardware.

Stage1 still does **not** run XNU or iOS. It is a self-contained ARMv7 payload packaged as an Android boot image `kernel` and tested through non-persistent `fastboot boot`.

## What Stage1 adds over Stage0

Stage0 proved that the bootloader executes our raw ARMv7 code, that QCDT is mandatory, that Android ram_console at `0xde500000` works, and that MSM8974 PS_HOLD reset works.

Stage1 adds:

- a payload-local stack,
- `.bss` zeroing,
- a reusable append-style Android `ram_console` writer,
- a synthetic public-XNU-style ARM `boot_args` structure,
- a minimal Apple flattened device tree stub,
- a test kernel entry stub called with `r0 = boot_args*`, matching the future XNU ARM handoff shape,
- validation of boot_args revision/version and deviceTree pointer/length,
- automatic reboot through PS_HOLD.

## Sources

Stage1 source files:

```text
stage1/stage1.S
stage1/linker.ld
stage1/build.sh
```

Public XNU reference for the boot argument layout:

```text
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h
external/xnu-4570.1.46/pexpert/pexpert/device_tree.h
```

Relevant XNU ARM fields:

```text
Revision
Version
virtBase
physBase
memSize
topOfKernelData
Video
machineType
deviceTreeP
deviceTreeLength
CommandLine
bootFlags
memSizeActual
```

## Built image

Build command:

```bash
./stage1/build.sh
```

The build produces:

```text
out/stage1/stage1.img       # negative-control image without QCDT
out/stage1/stage1-qcdt.img  # cancro-accepted image with legacy v0 dt_size/QCDT
```

Successful local build:

```text
kernel_size=1232
ramdisk_size=0
second_size=0
dt_size=2521088
page_size=2048
stage1-qcdt sha256=67f5744ac397bf548ce3efeeb9d428ea148f86555042378f64be9b09ee51274f
```

Parsed boot image fields:

```text
magic=ANDROID!
page_size=2048
kernel_size=1232 (0x4d0)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage1 mi4ios6=stage1 boot-wrapper
```

Important symbol addresses from the linked ELF:

```text
00008000 T _start
0000839c t apple_dt_blob
00008474 t apple_dt_blob_end
00008474 t apple_dt_blob_len
000084d0 B boot_args_struct
00009610 b stage1_stack_top
0000a000 B __stage1_image_end
```

## Static handoff checks

The Stage1 handoff stub validates:

- `boot_args.Revision == 2`
- `boot_args.Version == 2`
- `boot_args.deviceTreeP != 0`
- `boot_args.deviceTreeLength != 0`
- first word of the Apple DT root node (`nProperties`) is non-zero

Disassembly confirmed that the XNU ARM structure offsets are correct:

```text
machineType        stored at offset 44 (0x2c)
deviceTreeP        stored/read at offset 48 (0x30)
deviceTreeLength   stored/read at offset 52 (0x34)
CommandLine        starts at offset 56 (0x38)
bootFlags          stored at offset 312 (0x138)
memSizeActual      stored at offset 316 (0x13c)
```

## Expected persistent log

After `fastboot boot out/stage1/stage1-qcdt.img`, Android `/proc/last_kmsg` should contain lines like:

```text
MI4IOS6_STAGE1 v1 entered; stack+bss ready; ram_console live
MI4IOS6_STAGE1 boot_args built (rev2); apple-dt stub attached
MI4IOS6_STAGE1 handoff -> test_kernel_entry(boot_args*)
MI4IOS6_STAGE1 handoff ok: rev/ver/dt validated
MI4IOS6_STAGE1 returned from test kernel; preparing reset
MI4IOS6_STAGE1 attempting MSM8974 PS_HOLD reset
```

## Run result

Hardware run succeeded.

Command used:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage1/stage1-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2466 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

After about 25 seconds, the phone returned to normal Android automatically. The ADB transport id changed from 19 to 20, confirming a reboot cycle.

Persistent log pull:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage1-last_kmsg.txt
```

Full recovered `/proc/last_kmsg` content:

```text
MI4IOS6_STAGE1 v1 entered; stack+bss ready; ram_console live
MI4IOS6_STAGE1 boot_args built (rev2); apple-dt stub attached
MI4IOS6_STAGE1 handoff -> test_kernel_entry(boot_args*)
MI4IOS6_STAGE1 handoff ok: rev/ver/dt validated
MI4IOS6_STAGE1 returned from test kernel; preparing reset
MI4IOS6_STAGE1 attempting MSM8974 PS_HOLD reset

No errors detected
```

The file size was 353 bytes and contained six `MI4IOS6_STAGE1` markers.

## Success criteria

Stage1 is successful if:

1. bootloader accepts `stage1-qcdt.img`,
2. phone automatically reboots,
3. `/proc/last_kmsg` contains `MI4IOS6_STAGE1`,
4. `/proc/last_kmsg` contains `handoff ok`.

If successful, the next stage is to replace the hand-coded structure-only wrapper with an early runtime capable of executing more C-like code: real append log routines, generated Apple-DT nodes for CPUs/GIC/timer/memory, and eventually a public-XNU-adjacent entry stub.
