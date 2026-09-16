# Open Darwin / XNU Research Notes

This document records legal/open-source research relevant to an iOS 6 / Darwin / XNU-like experiment on Xiaomi Mi 4 LTE (`cancro`).

## Summary

The realistic open-source path is not “flash iOS 6”. It is:

1. Study public XNU/Darwin structure.
2. Study later public ARM XNU sources as architecture references.
3. Define a minimal boot ABI and device-tree model.
4. Attempt a narrow XNU-derived or XNU-inspired kernel bring-up milestone.
5. Treat iOS userspace as a separate and much harder problem.

## iOS 6 era and public XNU

- iOS 6 was released in 2012 and ran on ARMv7-era Apple devices.
- OS X 10.8 Mountain Lion was also a 2012 release and maps to Darwin 12.
- Public XNU releases around that era include `xnu-2050.*`; `xnu-2050.18.24` is available in Apple’s public OSS repository.

Important limitation:

- Public `xnu-2050.18.24` contains XNU core areas such as `bsd`, `iokit`, `libkern`, `osfmk`, `pexpert`, and `security`.
- However, its architecture-specific source is desktop-focused. The public tag does not provide a complete iOS 6 ARMv7 kernel source tree.
- This makes it useful for structure, Mach/BSD/IOKit concepts, generic device-tree parsing, and boot-argument behavior, but not as a drop-in iOS 6 ARMv7 port.

## Later public ARM XNU material

Later public XNU tags, such as `xnu-4570.1.46`, include ARM/ARM64 and ARM platform expert code. These are not iOS 6-era code, but they are valuable legal references for:

- ARM boot argument handling
- ARM machine identification
- Platform expert patterns
- Device tree expectations
- Timer/interrupt/device-tree discovery style

Treat later ARM sources as reference material, not as a drop-in base for iOS 6.

## Device tree and boot expectations

Public XNU uses a boot contract involving boot arguments, command-line strings, memory information, and device-tree information.

Useful public XNU 2050 areas:

- `pexpert/gen/device_tree.c` — generic flattened device-tree parser.
- `iokit/Kernel/IODeviceTreeSupport.cpp` — creates/imports the `IODeviceTree` registry plane.

Important device-tree properties and concepts visible in public source include:

- `name`
- `compatible`
- `device_type`
- `model`
- `ranges`
- `AAPL,phandle`
- interrupt-related properties such as `AAPL,interrupts`

Later public ARM platform expert code shows additional expectations around:

- `arm-io`
- `/cpus`
- `timebase-frequency`
- `clock-frequency`
- `interrupt-controller`
- timer nodes with `device_type == "timer"`
- boot arguments exposed through `PE_boot_args()`

## Why iOS userspace is harder

A kernel experiment can target a narrow early-boot milestone:

- early console output
- parsed boot args
- parsed device tree
- accepted memory map
- timer interrupt working

A full iOS 6 userspace would require a tightly coupled Apple environment, including:

- signed Mach-O binaries and entitlement policy
- AMFI/code-signing behavior
- dyld and shared cache compatibility
- launchd job graph
- sandbox profiles
- Objective-C runtime integration
- UIKit, SpringBoard, CoreAnimation, and private frameworks
- Apple-specific display/touch/audio/power/mobile services

Public repositories such as `dyld`, `Libc`, and `launchd` can help with isolated study, but they do not provide a complete legally buildable iOS 6 userland.

## Recommended milestones

### 1. Legal source baseline

Use public Apple OSS only:

- `xnu-2050.*` for OS X 10.8 / Darwin 12-era XNU structure.
- Later public ARM XNU tags for ARM reference patterns.

Avoid leaked or proprietary Apple kernel/userspace code.

### 2. Static source map

Document:

- What is present in public XNU 2050.
- What ARM pieces only appear in later public tags.
- Which parts would need original implementation for Qualcomm MSM8974.

### 3. Boot ABI model

Define a minimal boot handoff contract:

- boot arguments
- command line / boot-args
- memory map
- device tree root
- initial framebuffer or serial console

### 4. Device-tree prototype

Build or emulate a minimal Apple-style device tree with:

- root node
- CPU nodes
- memory
- timer
- interrupt controller
- SoC/IO node similar to `arm-io`

### 5. Kernel bring-up target

Do not start with “boot iOS”. Start with:

> Boot a legally built XNU-derived or XNU-inspired kernel to early init on ARMv7-like hardware/emulation.

Initial success criteria:

- serial output
- parsed boot args
- parsed device tree
- memory map accepted
- timer interrupt working

### 6. Driver boundary

Identify non-Apple hardware devices requiring new code:

- UART
- interrupt controller
- timer
- MMU/cache behavior
- framebuffer
- storage
- USB

For Xiaomi Mi 4, this means Qualcomm MSM8974 support, not Apple SoC support.

### 7. Userspace reality check

Treat full iOS 6 userspace as a separate research track. A more realistic legal userland milestone is Darwin-like userspace:

- `launchd`
- shell/basic BSD tools where available
- public `Libc`
- public `dyld` where compatible

## Sources

- [Apple OSS XNU repository](https://github.com/apple-oss-distributions/xnu)
- [XNU `xnu-2050.18.24` tree](https://github.com/apple-oss-distributions/xnu/tree/xnu-2050.18.24)
- [XNU tags](https://github.com/apple-oss-distributions/xnu/tags)
- [XNU APSL license for `xnu-2050.18.24`](https://github.com/apple-oss-distributions/xnu/blob/xnu-2050.18.24/APPLE_LICENSE)
- [XNU 2050 generic device-tree parser](https://github.com/apple-oss-distributions/xnu/blob/xnu-2050.18.24/pexpert/gen/device_tree.c)
- [XNU 2050 IOKit device-tree support](https://github.com/apple-oss-distributions/xnu/blob/xnu-2050.18.24/iokit/Kernel/IODeviceTreeSupport.cpp)
- [XNU 2050 debug kernel boot-args README](https://github.com/apple-oss-distributions/xnu/blob/xnu-2050.18.24/config/README.DEBUG-kernel.txt)
- [Later public ARM boot args: `pexpert/arm/pe_bootargs.c`](https://github.com/apple-oss-distributions/xnu/blob/xnu-4570.1.46/pexpert/arm/pe_bootargs.c)
- [Later public ARM machine identification: `pexpert/arm/pe_identify_machine.c`](https://github.com/apple-oss-distributions/xnu/blob/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c)
- [Apple OSS dyld repository](https://github.com/apple-oss-distributions/dyld)
- [Apple OSS Libc repository](https://github.com/apple-oss-distributions/Libc)
- [Apple OSS launchd repository](https://github.com/apple-oss-distributions/launchd)
- [Apple Open Source releases page](https://opensource.apple.com/releases/)
