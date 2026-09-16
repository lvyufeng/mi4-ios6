# MSM8974 ↔ XNU Porting Map

This is the current technical map for moving from “Xiaomi Mi 4 can boot custom Android boot images” toward “run an XNU/Darwin/iOS-like kernel on the hardware”.

It is not a promise that stock iOS 6 can run. Stock iOS 6 remains tied to Apple SoCs, Apple boot chain, Apple drivers, Apple code signing, and proprietary userspace. This map identifies the concrete engineering gap for a legal XNU-derived or XNU-inspired bring-up.

## Known-good boot path

Already proven on the actual device:

- Standard Android boot images are accepted.
- `sudo fastboot boot <image>` works non-persistently.
- A cmdline-only boot image patch boots normal Android.
- `/proc/cmdline` confirms injected bootargs.
- `/proc/last_kmsg` and `/dev/kmsg` provide USB-only debugging.

Therefore the first XNU-like payload should be packaged as an Android boot image and tested with `fastboot boot`, not flashed.

## Cancro / MSM8974 hardware facts

### Boot image parameters

From `external/android_device_xiaomi_cancro/BoardConfig.mk` and local image parsing:

- Platform: `msm8974`
- Bootloader board: `MSM8974`
- Kernel base: `0x00000000`
- Kernel entry/load: `0x00008000`
- Page size: `2048`
- Ramdisk offset: `0x02000000`
- Tags offset: `0x01E00000`
- Separated DT/QCDT: true
- DTB tool args: `-2`
- Android cmdline baseline: `console=none vmalloc=340M androidboot.hardware=qcom msm_rtb.filter=0x3b7 ehci-hcd.park=3 androidboot.bootdevice=msm_sdcc.1`

Relevant files:

- `external/android_device_xiaomi_cancro/BoardConfig.mk`
- `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/Makefile.boot`
- `external/android_kernel_xiaomi_cancro/arch/arm/configs/cancro_user_defconfig`

### QCDT / board variants

The boot image contains an appended/separate Qualcomm device-tree table. Android chooses a DTB using Qualcomm IDs.

Likely cancro DTBs include:

- `msm8974pro-ac-pm8941-mtp.dtb`
- `msm8974pro-ac-pm8941-mtp-v4.dtb`
- `msm8974pro-ac-pm8941-mtp-v5.dtb`

Board IDs in source:

- base AC MTP: `qcom,board-id = <8 0x100>`
- v4: `qcom,board-id = <8 0x400>`
- v5: `qcom,board-id = <8 0x500>`

Relevant files:

- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974pro-ac-pm8941-mtp.dts`
- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974pro-ac-pm8941-mtp-v4.dts`
- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974pro-ac-pm8941-mtp-v5.dts`

### UART

Primary low-speed serial:

- Linux alias: `serial0 = &blsp1_uart1`
- Linux device: `ttyHSL0`
- MMIO: `0xf991e000`
- Size: `0x1000`
- IRQ: `108`
- Compatible: `qcom,msm-lsuart-v14`

Relevant files:

- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974-mtp.dtsi`
- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi`
- `external/android_kernel_xiaomi_cancro/drivers/tty/serial/msm_serial_hs_lite.c`

### Persistent log memory

This kernel uses Android ram_console, not upstream pstore:

- Config: `CONFIG_ANDROID_RAM_CONSOLE=y`
- Reserved region: top-of-DRAM minus 2 MiB
- Size: `2 MiB`
- Exposed as `/proc/last_kmsg`

Relevant files:

- `external/android_kernel_xiaomi_cancro/arch/arm/configs/cancro_user_defconfig`
- `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/board-8974.c`

### Interrupt controller

MSM8974 uses a GICv2-style controller:

- Distributor/control regions:
  - `0xF9000000`, size `0x1000`
  - `0xF9002000`, size `0x1000`
- DT compatible: `qcom,msm-qgic2`
- Interrupt cells: 3

Relevant file:

- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi`

### Timer

Useful timer options:

- ARMv7 architectural timer
- Frequency: `19200000` Hz
- Memory timer block: `0xf9020000`
- Frame 0: `0xf9021000` / `0xf9022000`
- IRQs: `8` and `7`

Relevant file:

- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi`

### Display / framebuffer

Initial bring-up should not initialize the full display stack. Use a headless/log-only path first or preserve bootloader framebuffer.

Known display facts:

- Primary framebuffer reservation: `0x03200000`, size `0x01E00000`
- MDSS MDP: `0xfd900000`, IRQ `72`
- DSI0: `0xfd922800`
- Panel: Sharp FHD DSI, 1080x1920, 60 Hz

Relevant files:

- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974-mdss.dtsi`
- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/dsi-panel-sharp-fhd-video.dtsi`
- `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974pro-ac-pm8941-mtp-v5.dts`

## XNU ARM interface facts

### Public source limitation

- `external/xnu-2050.18.24` is useful as iOS 6 / Darwin 12-era structure, but it does not include a complete public ARMv7 iOS kernel tree.
- `external/xnu-4570.1.46` includes public ARM code and is the practical legal reference for ARM boot interfaces.

### Boot arguments

ARM `boot_args` is defined in:

- `external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h`

Minimum fields XNU expects:

- `virtBase`
- `physBase`
- `memSize`
- `topOfKernelData`
- `Video`
- `machineType`
- `deviceTreeP`
- `deviceTreeLength`
- `CommandLine`
- `bootFlags`
- `memSizeActual`

The kernel consumes boot args in:

- `external/xnu-4570.1.46/osfmk/arm/arm_init.c`
- `external/xnu-4570.1.46/pexpert/arm/pe_init.c`

### Device tree format

XNU does not consume Linux FDT directly in this path. It uses Apple’s flattened device tree parser:

- `external/xnu-4570.1.46/pexpert/gen/device_tree.c`
- `external/xnu-4570.1.46/pexpert/pexpert/device_tree.h`

IOKit imports that device tree through:

- `external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp`
- `external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp`

Minimum useful Apple-style DT content:

- root properties: `name`, `compatible`, `model`, `target-type`
- `/cpus` with `state`, `reg`, `timebase-frequency`, `clock-frequency`
- `arm-io` node with `device_type`, `ranges`, optional `chip-revision`
- interrupt controller node
- timer node

### Machine identification

ARM platform expert machine identification is centered in:

- `external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c`

Existing code expects Apple-style SoC descriptions such as `arm-io`. For MSM8974, we need a new `msm8974-io` path or a carefully constructed `arm-io` node plus new board-class handling.

### Interrupts and timer

The later public ARM XNU code assumes Apple AIC/PMGR-style semantics in many paths:

- `external/xnu-4570.1.46/pexpert/pexpert/arm/AIC.h`
- `external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c`

MSM8974 instead uses ARM GIC and Qualcomm timer wiring. Required replacements:

- GIC distributor/CPU interface mapping
- interrupt acknowledge/EOI/mask/unmask
- IPI via GIC SGIs
- ARMv7 generic timer or MSM memory timer path
- timebase registration through `ml_init_timebase()`

Relevant XNU timebase files:

- `external/xnu-4570.1.46/osfmk/arm/machine_routines.c`
- `external/xnu-4570.1.46/osfmk/arm/rtclock.c`

### SMP / CPU startup

Relevant XNU paths:

- `external/xnu-4570.1.46/osfmk/arm/cpu.c`
- `external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp`

For MSM8974/Krait, a platform CPU provider would need:

- CPU topology under `/cpus`
- secondary CPU release mechanism
- IPI delivery through GIC SGIs
- platform halt/restart hooks

## Minimal bring-up target

The next realistic payload is not iOS userspace. It is a minimal XNU-like kernel or shim that proves:

1. It can be packaged as an Android boot image and entered by the cancro bootloader.
2. It can parse or synthesize boot arguments.
3. It can preserve/parse an Apple-style device tree or convert Linux FDT/QCDT into one.
4. It can reserve top-of-DRAM 2 MiB and write a persistent log readable after reboot.
5. It can set up MMU enough for C code.
6. It can initialize GIC.
7. It can initialize the 19.2 MHz timer.
8. It can print a persistent log marker and reboot.

Only after that should we attempt to boot more XNU subsystems.

## Work packages

### WP1 — Boot wrapper / loader

Build a small ARMv7 boot wrapper packaged as Android `boot.img` kernel payload.

Responsibilities:

- Accept Android bootloader entry conditions.
- Locate ramdisk and QCDT if needed.
- Build XNU-style `boot_args` in memory.
- Build or point to an Apple-format device tree.
- Jump into a test kernel entry.

### WP2 — Persistent log shim

Implement a tiny writer for the Android `ram_console` / top-of-DRAM 2 MiB region.

Success criterion:

- `fastboot boot` the shim.
- Shim writes `MI4IOS6_XNU_STAGE0` to persistent RAM.
- Device reboots.
- Android `/proc/last_kmsg` or a raw memory dump path shows the marker.

### WP3 — DT conversion

Convert Linux/QCDT facts into the Apple-style flattened tree XNU expects.

Minimum nodes:

- root
- `/cpus`
- `arm-io` or `msm8974-io`
- interrupt controller
- timer
- chosen/memory/video-like nodes as needed

### WP4 — XNU pexpert MSM8974 path

Add or prototype:

- `msm8974-io` machine identification
- GIC interrupt controller
- 19.2 MHz timer path
- persistent log debug output

### WP5 — Userland reality gate

Do not attempt iOS SpringBoard/UIKit yet. A legal userland target comes much later and would start with Darwin-like pieces, not proprietary iOS 6 frameworks.

## Hard blockers for “stock iOS 6”

Even after kernel bring-up, stock iOS 6 still requires:

- Apple iBoot/SecureROM-style boot environment or an equivalent loader contract
- Apple SoC platform devices and drivers, or replacement drivers for Qualcomm hardware
- AMFI/code-signing/entitlement behavior
- dyld shared cache compatibility
- UIKit, SpringBoard, CoreAnimation, and private frameworks
- Apple graphics/touch/audio/power userspace stacks

So the practical path remains: XNU/Darwin-like bring-up first, iOS userspace only after a much deeper compatibility layer exists.
