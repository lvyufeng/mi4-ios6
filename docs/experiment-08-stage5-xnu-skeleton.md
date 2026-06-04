# Experiment 08 — Stage5 XNU-Adjacent Kernel Skeleton

Date: 2026-06-04

Goal: split the Stage4 boot-wrapper test flow into a more XNU-like handoff:

```c
kernel_entry(struct boot_args *args)
```

Stage5 still does **not** run XNU or iOS. It is a legal XNU-adjacent skeleton that consumes the already proven `boot_args` and Apple flattened device tree contract, then performs pexpert-like discovery and XNU-like timebase initialization.

## Apple OSS baseline context

The user identified Apple's Open Source GitHub repositories:

```text
https://github.com/apple-oss-distributions/distribution-iOS/tree/ios-613
https://github.com/apple-oss-distributions/xnu
```

Local verification found:

- `distribution-iOS@ios-613` is a tag for iOS 6.1.3.
- Its `release.json` lists selected OSS projects (`JavaScriptCore`, `WebCore`, `cctools`, `ld64`, etc.) but does not list XNU.
- Apple GitHub `xnu` exposes `rel/xnu-2050`, `xnu-2050.*` tags, and later public ARM-capable references such as `rel/xnu-4570`.

Therefore Stage5 uses the practical two-source strategy documented in `docs/ios-613-oss-baseline.md`:

1. `xnu-2050.*` / iOS 6.1.3 OSS notes for era context.
2. Later public ARM XNU source (`xnu-4570.1.46`) to shape the ARM boot contract legally.

## What Stage5 adds over Stage4

Stage4 proved:

- custom VBAR/vector safety net,
- `CNTPCT/CNTFRQ` timebase and `delay_us`,
- deliberate exception recovery.

Stage5 adds:

- separate `kernel_entry(struct boot_args *)`,
- `xnu_log_*` helpers that mark kernel-skeleton logs separately,
- public Apple-DT parser helpers (`apple_dt_find_child`, `apple_dt_get_prop`, etc.),
- `pexpert_discover_and_log()` that consumes `/chosen`, `/memory`, `/cpus`, `/interrupt-controller`, and `/timer`,
- XNU-like timebase wrappers:
  - `ml_init_timebase()`
  - `ml_get_timebase()`
  - `ml_get_timebase_frequency()`

## Source files added

```text
stage5/xnu_log.c
stage5/xnu_timebase.c
stage5/pexpert.c
stage5/xnu_kernel.c
```

Stage5 is otherwise based on Stage4.

## Built image

```bash
./stage5/build.sh
```

Successful local build:

```text
out/stage5/stage5-qcdt.img
sha256=1c1dc110ff28effcfda4891377976410b80093c4c68b7b396341294847fe1b6f
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=13500 (0x34bc)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage5 mi4ios6=stage5 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage5_vectors
00009718 T ml_init_timebase
00009834 T pexpert_discover_and_log
00009bc8 T kernel_entry
00009f88 T stage5_main
00012000 B __stage5_image_end
```

## Expected hardware log

Expected new markers include:

```text
MI4IOS6_STAGE5 boot-wrapper handoff -> kernel_entry(boot_args*)
MI4IOS6_STAGE5_XNU kernel_entry(struct boot_args*) entered
MI4IOS6_STAGE5_XNU pexpert discovery begin
MI4IOS6_STAGE5_XNU pexpert found /memory
MI4IOS6_STAGE5_XNU pexpert found /cpus
MI4IOS6_STAGE5_XNU pexpert found /interrupt-controller
MI4IOS6_STAGE5_XNU pexpert found /timer
MI4IOS6_STAGE5_XNU pexpert discovery ok
MI4IOS6_STAGE5_XNU ml_timebase_freq=0x0124f800
MI4IOS6_STAGE5_XNU ml timebase ok
MI4IOS6_STAGE5_XNU kernel_entry ok
MI4IOS6_STAGE5 kernel_entry returned success
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage5/stage5-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2478 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

The phone returned to Android automatically after ~30 seconds; ADB transport id changed from 27 to 28.

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage5-last_kmsg.txt
```

The recovered log was 2598 bytes and contained:

```text
54 MI4IOS6_STAGE5 markers
33 MI4IOS6_STAGE5_XNU markers
```

## Key recovered markers

```text
MI4IOS6_STAGE5 boot-wrapper handoff -> kernel_entry(boot_args*)
MI4IOS6_STAGE5_XNU kernel_entry(struct boot_args*) entered
MI4IOS6_STAGE5_XNU boot_args_rev_ver=0x00020002
MI4IOS6_STAGE5_XNU physBase=0x00008000
MI4IOS6_STAGE5_XNU memSize=0x5e500000
MI4IOS6_STAGE5_XNU topOfKernelData=0x00012000
MI4IOS6_STAGE5_XNU deviceTreeP=0x00010620
MI4IOS6_STAGE5_XNU deviceTreeLength=0x00000a04
```

Pexpert-like discovery succeeded:

```text
MI4IOS6_STAGE5_XNU pexpert discovery begin
MI4IOS6_STAGE5_XNU pexpert found /chosen
MI4IOS6_STAGE5_XNU pexpert found /memory
MI4IOS6_STAGE5_XNU pexpert found /cpus
MI4IOS6_STAGE5_XNU pexpert found /interrupt-controller
MI4IOS6_STAGE5_XNU pexpert found /timer
MI4IOS6_STAGE5_XNU chosen boot-args: debug=0x144 serial=0x1 mi4ios6.stage=5 msm8974=cancro xnu-skeleton=1
MI4IOS6_STAGE5_XNU memory.reg base=0x80000000 size=0x5e500000
MI4IOS6_STAGE5_XNU memory matches boot_args
MI4IOS6_STAGE5_XNU cpu_count=0x00000004
MI4IOS6_STAGE5_XNU gicd.reg base=0xf9000000 size=0x00001000
MI4IOS6_STAGE5_XNU gicc.reg base=0xf9002000 size=0x00001000
MI4IOS6_STAGE5_XNU gic_interrupt_cells=0x00000003
MI4IOS6_STAGE5_XNU timer_frequency=0x0124f800
MI4IOS6_STAGE5_XNU timer.reg0 base=0xf9020000 size=0x00001000
MI4IOS6_STAGE5_XNU pexpert discovery ok
```

XNU-like timebase stubs succeeded:

```text
MI4IOS6_STAGE5_XNU ml_timebase_freq=0x0124f800
MI4IOS6_STAGE5_XNU ml_timebase_start=0x0000000005458a53
MI4IOS6_STAGE5_XNU ml_timebase_freq_check=0x0124f800
MI4IOS6_STAGE5_XNU ml_timebase_delta_ticks=0x0000000000009633
MI4IOS6_STAGE5_XNU ml_timebase_delta_us=0x000007d2
MI4IOS6_STAGE5_XNU ml timebase ok
```

Final success markers:

```text
MI4IOS6_STAGE5_XNU kernel_entry ok
MI4IOS6_STAGE5 kernel_entry returned success
MI4IOS6_STAGE5 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage5 confirms the first real XNU-adjacent split:

```text
Android boot image kernel payload
  -> boot wrapper builds boot_args + Apple-DT
  -> kernel_entry(struct boot_args *)
      -> validate boot_args
      -> validate Apple-DT
      -> pexpert-like platform discovery
      -> XNU-like ml timebase init/read
  -> return to boot wrapper
  -> PS_HOLD reset
```

This is still not XNU, but it is now structurally closer to XNU early boot than the earlier one-piece test payloads.

## Success criteria — met

1. bootloader accepted `stage5-qcdt.img`: yes
2. boot wrapper called `kernel_entry(struct boot_args*)`: yes
3. `kernel_entry` validated `boot_args`: yes
4. Apple-DT self-test passed: yes
5. pexpert-like discovery found `/memory`, `/cpus`, `/interrupt-controller`, `/timer`: yes
6. XNU-like `ml_init_timebase` / `ml_get_timebase` stubs worked: yes
7. payload reset the phone through PS_HOLD: yes

## Next stage

Stage6 should begin connecting this skeleton to public XNU source concepts:

- compare Stage5 `kernel_entry` and pexpert stubs against public `xnu-4570.1.46` ARM entry points,
- mirror selected names/types from public XNU headers where legally available,
- add a small Mach/XNU-style platform state object,
- add explicit exception tests for data abort and prefetch abort now that vector logging is reliable,
- optionally prototype a tiny GIC driver skeleton that only models state and masks/unmasks software-defined lines, not full interrupt enablement yet.
