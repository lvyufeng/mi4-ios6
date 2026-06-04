# Experiment 05 — Stage2 C Runtime + Fuller Apple Device Tree

Date: 2026-06-04

Goal: move the bring-up payload from hand-written assembly structures (Stage1) to a small bare-metal C runtime that builds and walks a fuller Apple-style flattened device tree, while keeping the test non-persistent through `fastboot boot`.

Stage2 still does **not** run XNU or iOS. It proves the next layer of the boot contract: a C-capable runtime, a reusable persistent log, a real Apple-DT builder, and a self-test walker that an eventual XNU pexpert path would need.

## What Stage2 adds over Stage1

Stage1 (assembly) proved we can construct an XNU-style `boot_args` and hand off to a kernel-entry stub.

Stage2 adds:

- an assembly trampoline that masks interrupts, sets a stack, zeroes `.bss`, and calls C,
- a freestanding C runtime (`memset`/`memcpy`/`strlen`/`strcmp`/`align4`),
- a reusable Android `ram_console` log module with `log_puts`/`log_hex32`/`log_kv32`,
- an Apple flattened device tree builder (`apple_dt_*`),
- a fuller device tree with root, `/chosen`, `/memory`, `/cpus` (4 Krait CPUs), `/msm8974-io`, `/interrupt-controller`, `/timer`,
- a device tree self-test walker that re-parses the tree and confirms node structure,
- a C `test_kernel_entry(boot_args*)` that logs and validates the handoff.

## Source files

```text
stage2/start.S         # assembly entry trampoline into C
stage2/stage2.h        # types, platform constants, boot_args, interfaces
stage2/runtime.c       # freestanding mem/str helpers
stage2/ram_console.c   # Android persistent_ram_buffer writer
stage2/apple_dt.c      # Apple flattened DT builder + walker/self-test
stage2/boot_args.c     # XNU ARM boot_args builder
stage2/stage2_main.c   # device tree assembly, handoff, reboot
stage2/linker.ld
stage2/build.sh
```

The C toolchain used was `arm-none-eabi-gcc` (Ubuntu `gcc-arm-none-eabi`, 10.3-2021.07), built `-ffreestanding -nostdlib -mcpu=cortex-a15 -marm -O2 -Werror`.

## Built image

```bash
./stage2/build.sh
```

Output:

```text
out/stage2/stage2.img       # negative-control image without QCDT
out/stage2/stage2-qcdt.img  # cancro-accepted image with QCDT
```

Local build facts:

```text
text=6868 data=0 bss=20800
kernel_size=6868
dt_size=2521088
page_size=2048
stage2-qcdt sha256=b23cd529623c692b397991f63b4ea86d561123a19987a3d182728b13e36493c4
```

Key symbols:

```text
00008000 T _start
00008f30 T stage2_main
00008d98 T test_kernel_entry
00008948 T apple_dt_selftest_and_log
0000dae0 B stage2_stack_top
0000dc20 b g_apple_dt
0000f000 B __stage2_image_end
```

## Non-persistent boot test

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage2/stage2-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2472 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

The phone returned to Android automatically after ~25 seconds; the ADB transport id changed from 21 to 22, confirming a reboot.

## Recovered persistent log

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage2-last_kmsg.txt
```

Full recovered content (1168 bytes, 25 Stage2 markers):

```text
MI4IOS6_STAGE2 v1 entered; C runtime active; ram_console live
MI4IOS6_STAGE2 stage2_image_end=0x0000f000
MI4IOS6_STAGE2 built_apple_dt_len=0x000009f4
MI4IOS6_STAGE2 boot_args built (rev2); fuller apple_dt attached
MI4IOS6_STAGE2 handoff -> test_kernel_entry(boot_args*)
MI4IOS6_STAGE2 boot_args_ptr=0x0000dae0
MI4IOS6_STAGE2 boot_args_rev_ver=0x00020002
MI4IOS6_STAGE2 boot_args_physBase=0x00008000
MI4IOS6_STAGE2 boot_args_memSize=0x5e500000
MI4IOS6_STAGE2 boot_args_topOfKernelData=0x0000f000
MI4IOS6_STAGE2 boot_args_deviceTreeP=0x0000dc20
MI4IOS6_STAGE2 boot_args_deviceTreeLength=0x000009f4
MI4IOS6_STAGE2 apple_dt_len=0x000009f4
MI4IOS6_STAGE2 apple_dt_root_props=0x00000004
MI4IOS6_STAGE2 apple_dt_root_children=0x00000006
MI4IOS6_STAGE2 find /chosen ... ok
MI4IOS6_STAGE2 find /memory ... ok
MI4IOS6_STAGE2 find /cpus ... ok
MI4IOS6_STAGE2 find /msm8974-io ... ok
MI4IOS6_STAGE2 find /interrupt-controller ... ok
MI4IOS6_STAGE2 find /timer ... ok
MI4IOS6_STAGE2 apple_dt selftest ok
MI4IOS6_STAGE2 handoff ok: boot_args + fuller apple_dt validated
MI4IOS6_STAGE2 test kernel returned success
MI4IOS6_STAGE2 attempting MSM8974 PS_HOLD reset

No errors detected
```

## Interpretation

- `C runtime active` proves the assembly trampoline entered C with a working stack and zeroed `.bss`.
- `built_apple_dt_len=0x9f4` (2548 bytes) proves the C builder emitted a multi-node tree.
- `boot_args_memSize=0x5e500000` equals `0xde500000 - 0x80000000`, the usable RAM below the ram_console reservation, computed at runtime.
- The walker found all six expected children and the pre-order traversal length matched the tree length exactly (`apple_dt selftest ok`). This is structural validation, not just a string match: the Apple-DT binary layout is correct enough to be re-parsed node by node.
- `handoff ok` and `test kernel returned success` confirm the full boot-wrapper contract executed.
- PS_HOLD reset still works from C, and the whole test stayed non-persistent.

## Success criteria — met

1. bootloader accepted `stage2-qcdt.img`: yes
2. phone auto-rebooted: yes
3. `/proc/last_kmsg` contains `MI4IOS6_STAGE2`: yes (25 markers)
4. Apple-DT walker found `/chosen`, `/memory`, `/cpus`, `/msm8974-io`, `/interrupt-controller`, `/timer`: yes
5. `apple_dt selftest ok` and `handoff ok`: yes

## Next stage

Stage3 should begin using the device tree for real early hardware bring-up rather than only self-testing it:

- map and program the MSM8974 GIC distributor/CPU interface (`0xf9000000` / `0xf9002000`),
- enable the ARM/MSM 19.2 MHz timer and read a monotonic tick,
- optionally set up a minimal identity MMU mapping for cached C execution,
- print live timer ticks into the persistent log to prove interrupts/timekeeping,
- only after that, attempt to link against a public-XNU-adjacent early entry path.
