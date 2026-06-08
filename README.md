# Xiaomi Mi 4 iOS 6 / Darwin Experiment

This repository tracks an experimental, owner-controlled research project around the Xiaomi Mi 4 LTE (`cancro`) and the feasibility of running iOS 6 / Darwin / XNU-like components on non-Apple ARMv7 hardware.

## Current device

Connected device observed via ADB:

- Model: Xiaomi MI 4LTE
- Device codename: `cancro`
- Hardware: Qualcomm / `qcom`
- CPU ABI: `armeabi-v7a`
- Current Android: Android 10 userdebug-style build
- Kernel: Linux 3.4.113, ARMv7
- ADB shell: root-capable (`uid=0` observed)

## Important technical reality

The goal is experimental research, not a normal ROM port.

A stock Apple iOS 6 image cannot simply be flashed to this phone. iOS depends on Apple-specific hardware, boot chain, device tree, XNU platform support, IOKit drivers, graphics stack, code-signing infrastructure, and proprietary userland components. Xiaomi Mi 4 uses a Qualcomm MSM8974-family platform and Android/Linux boot images.

A realistic research path is incremental:

1. Keep the original device recoverable.
2. Understand the existing Android boot image and partition layout.
3. Prove that custom boot/recovery images can be booted safely.
4. Study whether an open-source Darwin/XNU-derived or XNU-like minimal kernel experiment can be adapted to the Qualcomm platform.
5. Treat full iOS 6 userspace as a separate, much harder problem because SpringBoard/UIKit/CoreAnimation and related Apple frameworks are proprietary and platform-specific.

## Local backup status

Before any write/flash operation, key boot-critical partitions were backed up locally from the connected phone.

Backup directory currently present in the working tree but intentionally not committed:

```text
xiaomi4-cancro-backup-20260604-112053/
```

The backup contains images such as:

- `sbl1.img`
- `rpm.img`
- `tz.img`
- `aboot.img`
- `boot.img`
- `recovery.img`
- `persist.img`
- `modem.img`
- `SHA256SUMS.txt`

The SHA256 manifest was verified successfully after backup.

## Observed boot image layout

Backed-up `boot.img` is a standard Android boot image:

- Page size: 2048
- Kernel load address: `0x8000`
- Ramdisk load address: `0x2000000`
- Tags address: `0x1e00000`
- Device tree blob / Qualcomm QCDT present in the boot image (`dt_size=2521088`)
- Command line includes `androidboot.hardware=qcom` and `androidboot.bootdevice=msm_sdcc.1`
- The cancro bootloader requires the legacy Android v0 `dt_size`/QCDT field for custom `fastboot boot` payloads (`dtb not found` without it)

Backed-up `recovery.img` is also a standard Android boot image and uses a serial-console-oriented command line (`console=ttyHSL0,115200,n8`).

## Confirmed milestones

- Non-persistent `sudo fastboot boot <img>` works on this device.
- USB-only debugging works through Android `/proc/last_kmsg` / `ram_console`; no teardown/UART was required for Stage0/Stage1.
- Stage0 executed a raw non-Linux ARMv7 payload at `0x00008000`, wrote `MI4IOS6_STAGE0` into persistent RAM at `0xde500000`, and reset the phone through MSM8974 PS_HOLD at `0xfc4ab000`.
- Stage1 executed a boot-wrapper payload, set up stack and `.bss`, built a public-XNU-style ARM `boot_args` structure, attached a minimal Apple flattened device tree stub, called `test_kernel_entry(boot_args*)`, validated the handoff, logged `MI4IOS6_STAGE1 handoff ok`, and reset back to Android.
- Stage2 entered a freestanding C runtime, built a fuller Apple-style device tree (`/chosen`, `/memory`, `/cpus`, `/msm8974-io`, `/interrupt-controller`, `/timer`), walked/validated the tree, logged `apple_dt selftest ok` and `handoff ok`, and reset back to Android.
- Stage3 performed read-only hardware probes from the C runtime: CP15 state, ARM generic timer (`CNTFRQ=19.2 MHz`, `CNTPCT` advancing), and GIC distributor/CPU-interface IDs at `0xf9000000` / `0xf9002000`, then reset back to Android.
- Stage4 installed a custom ARMv7 exception vector table at VBAR `0x000080a0`, implemented a `CNTPCT/CNTFRQ` timebase with `delay_us`, verified 1000us/5000us delays, caught a deliberate undefined-instruction exception, logged LR/SPSR, and reset through PS_HOLD.
- Stage5 split the payload into a boot-wrapper path and `kernel_entry(struct boot_args*)`, added `MI4IOS6_STAGE5_XNU` skeleton logs, implemented pexpert-like Apple-DT discovery for memory/CPUs/GIC/timer, initialized XNU-like `ml_*` timebase stubs, and returned success before PS_HOLD reset.
- Stage6 added a `PE_state`-like platform state block populated from `boot_args`/Apple-DT, validated memory/CPU/GIC/timer/vector facts, then deliberately triggered and recovered from a data abort through the custom exception handler.
- Stage7 added a read-only MSM8974 GIC driver skeleton, captured distributor/CPU-interface state, enable/pending/priority/target registers for the first IRQ group, validated IRQ count 288 and 4 CPU interfaces, and returned through `kernel_entry` successfully.
- Stage8 installed a returnable IRQ vector path, generated SGI0 to the current CPU through `GICD_SGIR`, handled it by reading `GICC_IAR` / writing `GICC_EOIR`, logged interrupt ID 0, and returned through `kernel_entry` successfully.
- Stage9 programmed the ARM generic physical timer, enabled the local timer PPI path, handled timer interrupt ID 19 through the same IRQ/EOIR path, masked/disabled the timer again, and returned through `kernel_entry` successfully.
- Stage10 built an ARMv7 section identity map, enabled `SCTLR.M` with caches still disabled, verified ram_console/IMEM/GIC/timer MMIO access under translation, then re-ran SGI and timer IRQ selftests successfully.
- Stage11 added high virtual aliases for the low payload section, ram_console, and GIC/timer MMIO, validated alias-vs-identity data/vector/MMIO reads with caches disabled, and re-ran SGI/timer IRQ selftests successfully.
- Stage12 called a tiny function through its high virtual alias, passed a high virtual state pointer, validated identity/alias state coherence, returned safely to the identity path, and re-ran SGI/timer IRQ selftests successfully.
- Stage13 called a named high-virtual `kernel_bootstrap`-style entry, consumed high-virtual boot args, wrote PE/XNU-like bootstrap state through a high alias, validated it through identity/alias views, and re-ran SGI/timer IRQ selftests successfully.
- Stage14 moved richer PE/XNU-like state handling into the high-virtual bootstrap path, consumed high-virtual `PE_state`, emitted high-virtual log markers, validated memory/CPU/GIC/timer/vector facts, and re-ran SGI/timer IRQ selftests successfully.
- Stage15 moved selected pexpert/PE_state validation into the high-virtual bootstrap path, returned structured status `0x15000001`, verified validation mask `0x00000000`, and re-ran SGI/timer IRQ selftests successfully.
- Stage16 ran a small ordered XNU-like init sequence from high-virtual bootstrap, validated step mask `0x0000000f`, returned structured status `0x16000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage17 moved that flow into a high-virtual `kernel_root` shape, validated root/init step masks `0x0000000f`, returned structured root status `0x17000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage18 moved high-virtual boot_args/Apple-DT consumption into `kernel_root`, validated root step mask `0x0000001f`, recorded memory/timer DT facts, returned status `0x18000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage19 built a versioned high-root platform-result object, validated consistency mask `0x0000000f`, root step mask `0x0000003f`, returned status `0x19000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage20 added a high-root phase table, validated phase mask `0x0000000f`, phase checksum `0x0000000b`, root step mask `0x0000007f`, returned status `0x20000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage21 added a high-root service table for logging/timebase/platform/interrupts, validated service mask `0x0000000f`, service checksum `0x0000000b`, root step mask `0x000000ff`, returned status `0x21000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage22 made the high-root phase table consume service-table state explicitly, validated phase-service dependency mask `0x0000000f`, satisfied phase mask `0x0000000f`, phase-service checksum `0x00000008`, root step mask `0x000001ff`, returned status `0x22000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage23 replaced open-coded phase completion with a descriptor-driven high-root phase dispatcher, validated dispatcher order/handler masks `0x0000000f`, dispatcher checksum `0x00000004`, root step mask `0x000003ff`, returned status `0x23000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage24 replaced open-coded service availability with a descriptor-driven high-root service dispatcher, validated service dispatcher order/handler masks `0x0000000f`, service dispatcher checksum `0x00000004`, root step mask `0x000007ff`, returned status `0x24000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage25 added a high-root bootstrap registry over service and phase descriptors, validated service/phase descriptor masks `0x0000000f`, dependency coverage `0x0000000f`, dispatch coverage `0x000f000f`, registry checksum `0x000f0191`, root step mask `0x00000fff`, returned status `0x25000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage26 added a registry-gated high-root boot policy object, validated required/observed root mask `0x00000ff3`, service/phase/dependency masks `0x0000000f`, dispatch coverage `0x000f000f`, policy satisfied mask `0x0000003f`, policy checksum `0x000001ea`, root step mask `0x00001fff`, returned status `0x26000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage27 added a descriptor-driven bootstrap manifest over services, phases, dependencies, dispatcher coverage, boot policy, and final status, validated manifest order/satisfied masks `0x0000003f`, manifest root-step observation `0x00001ff3`, manifest checksum `0x00000038`, root step mask `0x00003fff`, returned status `0x27000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage28 added a manifest-driven launch contract for the next kernel bootstrap boundary, validated required/observed launch root steps `0x00003ff3`, root/manifest/policy statuses `0x28000001`, MMU state `0x00000001`, timebase `0x0124f800`, interrupt mask `0x0000000f`, launch satisfied mask `0x0000007f`, root step mask `0x00007fff`, returned status `0x28000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage29 added a contract-consuming startup boundary, validated required/observed startup root steps `0x00007ff3`, launch/root/manifest statuses `0x29000001`, boot args pointer `0xc0028000`, DT pointer `0xc0028140`, timebase `0x0124f800`, interrupt mask `0x0000000f`, startup satisfied mask `0x000000ff`, root step mask `0x0000ffff`, returned status `0x29000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage30 added a minimal startup routine driven by the startup boundary, validated required/observed routine root steps `0x0000fff3`, startup/launch/root statuses `0x30000001`, boot args pointer `0xc002c000`, DT pointer `0xc002c140`, timebase `0x0124f800`, interrupt mask `0x0000000f`, routine satisfied mask `0x000000ff`, root step mask `0x0001ffff`, returned status `0x30000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage31 split the startup routine into a separately called high-virtual startup entry, validated a compact handoff object (`0x00000030`, checksum `0x3025068c`), startup-entry required/observed root steps `0x0001fff3`, routine/startup/root statuses `0x31000001`, boot args pointer `0xc002c000`, DT pointer `0xc002c140`, timebase `0x0124f800`, interrupt mask `0x0000000f`, entry satisfied mask `0x000000ff`, root step mask `0x0003ffff`, returned status `0x31000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage32 made the high-virtual startup entry dispatch a minimal descriptor-driven kernel-start callout table, validated callout order/handler/service masks `0x0000000f`, callout checksum `0x00000005`, callout status `0x32000001`, root step mask `0x0007ffff`, returned status `0x32000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage33 made the startup-entry callouts populate a versioned kernel-start context object, validated context required/satisfied mask `0x0000003f`, context checksum `0x012506ba`, context status `0x33000001`, boot args pointer `0xc002c000`, DT pointer `0xc002c140`, platform consistency `0x0000000f`, timebase `0x0124f800`, interrupt mask `0x0000000f`, callout mask `0x0000000f`, root step mask `0x000fffff`, returned status `0x33000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage34 made the kernel-start context produce a versioned VM bootstrap plan object, validated VM plan required/satisfied mask `0x0000007f`, VM plan checksum `0xeb540a8a`, VM plan status `0x34000001`, low/high alias bases `0x00000000`/`0xc0000000`, ram_console/GIC aliases `0xc0100000`/`0xc0200000`, L1 table `0x00028000`/`0xc0028000`, memory range `0x80000000`/`0x5e500000`, section policy `0x00100000`/`0x00010c02`, MMU/cache policy `0x00000001`/`0x00000000`, root step mask `0x001fffff`, returned status `0x34000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage35 made the accepted VM bootstrap plan drive a versioned VM bootstrap state object, validated VM state required/satisfied mask `0x0000003f`, VM plan checksum/status `0xea540a8a`/`0x35000001`, VM state checksum/status `0x1f4506e6`/`0x35000001`, kernel map `0xc0000000`-`0xc0100000`, available-memory cursor `0x80000000`->`0x80100000`, bootstrap allocation span `0x80000000`+`0x00100000`, pmap L1 table `0x0002c000`/`0xc002c000`, section policy `0x00100000`/`0x00010c02`, MMU/cache policy `0x00000001`/`0x00000000`, root step mask `0x003fffff`, returned status `0x35000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage36 made the VM bootstrap state drive a versioned bootstrap allocator descriptor, validated allocator required/satisfied mask `0x0000003f`, VM state checksum/status `0x1f4506e6`/`0x36000001`, allocator checksum/status `0x68195ad2`/`0x36000001`, allocator span `0x80000000`/`0x00100000`/`0x80100000`, cursor `0x80000000`->`0x80100000`, remaining bytes `0x00000000`, first allocation tag `0x414c4c43`, alignment `0x00001000`, root step mask `0x007fffff`, returned status `0x36000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage37 made the bootstrap allocator descriptor drive a versioned pmap bootstrap workspace descriptor, validated workspace required/satisfied mask `0x0000003f`, allocator checksum/status `0x69195ad2`/`0x37000001`, pmap workspace checksum/status `0xce451213`/`0x37000001`, workspace range `0x80000000`/`0x80100000`/`0x00100000`, section count `0x000005e5`, L1 table `0x0002c000`/`0xc002c000`, section policy `0x00010c02`/`0x00100000`, allocation tag `0x504d4150`, MMU/cache policy `0x00000001`/`0x00000000`, root step mask `0x00ffffff`, returned status `0x37000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage38 made the pmap workspace descriptor drive a versioned kernel object table descriptor, validated object-table required/satisfied mask `0x0000003f`, object coverage mask `0x0000007f`, pmap workspace checksum/status `0xce451213`/`0x38000001`, object table checksum/status `0xa61ac796`/`0x38000001`, object slots for boot args `0xc0034000`, device tree `0xc0034140`, PE state `0xc0029020`, VM plan/state `0xc002c000`/`0xc002c04c`, allocator `0xc002c09c`, pmap workspace `0xc002c0e4`, L1 table `0x00030000`/`0xc0030000`, root step mask `0x01ffffff`, returned status `0x38000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage39 made the kernel object table descriptor drive a versioned kernel collection handoff descriptor, validated collection-handoff required/satisfied mask `0x0000003f`, handoff entry/object mask `0x00000007`/`0x0000007f`, object table checksum/status `0xa71ac796`/`0x39000001`, collection handoff checksum/status `0xce451213`/`0x39000001`, object slots for boot args `0xc0034000`, device tree `0xc0034140`, PE state `0xc0029020`, VM plan/state `0xc002c000`/`0xc002c04c`, allocator `0xc002c09c`, pmap workspace `0xc002c0e4`, L1 table `0x00030000`/`0xc0030000`, root step mask `0x03ffffff`, returned status `0x39000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage40 made the kernel collection handoff descriptor drive a versioned kernel collection entry-table descriptor, validated entry-table required/satisfied mask `0x0000007f`, object/order masks `0x0000007f`/`0x0000007f`, class mask `0x0000000f`, handoff checksum/status `0xce451213`/`0x40000001`, entry-table checksum/status `0xde1ac77d`/`0x40000001`, ordered object slots `0x00000001` through `0x00000040`, object pointers for boot args `0xc0034000`, device tree `0xc0034140`, PE state `0xc0029020`, VM plan/state `0xc002c000`/`0xc002c04c`, allocator `0xc002c09c`, pmap workspace `0xc002c0e4`, L1 table `0x00030000`/`0xc0030000`, root step mask `0x07ffffff`, returned status `0x40000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage41 made the kernel collection entry-table descriptor drive a versioned kernel collection object-graph descriptor, validated object-graph required/satisfied mask `0x0000003f`, node/edge masks `0x0000007f`/`0x0000007f`, class mask `0x0000000f`, entry-table checksum/status `0xdf1a877d`/`0x41000001`, object-graph checksum/status `0xce451242`/`0x41000001`, node dependency sequence `0x00000000`, `0x00000001`, `0x00000003`, `0x00000007`, `0x00000008`, `0x00000010`, `0x00000020`, object pointers for boot args `0xc0038000`, device tree `0xc0038140`, PE state `0xc002d020`, VM plan/state `0xc0030000`/`0xc003004c`, allocator `0xc003009c`, pmap workspace `0xc00300e4`, L1 table `0x00034000`/`0xc0034000`, root step mask `0x0fffffff`, returned status `0x41000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage42 made the kernel collection object-graph descriptor drive a versioned kernel collection dependency-resolution descriptor, validated dependency-resolution required/satisfied mask `0x0000007f`, resolved-order/dependency/activation masks `0x0000007f`/`0x0000007f`/`0x0000007f`, class mask `0x0000000f`, object-graph checksum/status `0xce451242`/`0x42000001`, dependency-resolution checksum/status `0xdc1b472e`/`0x42000001`, resolved object sequence `0x00000001` through `0x00000040`, dependency sequence `0x00000000`, `0x00000001`, `0x00000003`, `0x00000007`, `0x00000008`, `0x00000010`, `0x00000020`, activation-ready sequence `0x00000001` through `0x00000040`, object pointers for boot args `0xc003c000`, device tree `0xc003c140`, PE state `0xc0031020`, VM plan/state `0xc0034000`/`0xc003404c`, allocator `0xc003409c`, pmap workspace `0xc00340e4`, L1 table `0x00038000`/`0xc0038000`, root step mask `0x1fffffff`, returned status `0x42000001`, and re-ran SGI/timer IRQ selftests successfully.
- Stage43 pivoted toward a real XNU loader path by adding a bounded Mach-O/XNU loader preflight over an inert embedded 32-bit ARM Mach-O-shaped artifact, validated `MH_MAGIC`, `CPU_TYPE_ARM`, `CPU_SUBTYPE_ARM_V7`, `MH_PRELOAD`, `LC_SEGMENT`/`LC_SYMTAB`/`LC_UNIXTHREAD`, required segments `__TEXT`/`__DATA`/`__LINKEDIT`, entry metadata marked not executed, proposed XNU tuple `virtBase=0x80008000`, `physBase=0x80000000`, `memSize=0x5e500000`, `topOfKernelData=0x80010000`, 10-page TTE workspace ending at `0x8001a000`, complete Apple-DT semantic mask `0x00000fff`, recorded remaining platform gaps `0x0000001f`, loader status `0x43000001`, preserved the high-root/MMU checks, and re-ran SGI/timer IRQ selftests successfully.
- Stage44 replaced the hand-written Mach-O bytes with a reproducible non-proprietary host-generated fixture, parsed 32-bit `LC_SEGMENT` section records, reported `__TEXT,__text`, `__DATA,__const`, `__PRELINK_TEXT`, `__PRELINK_INFO`, and `__PRELINK_STATE` readiness, built a proposed physical load-plan (`load_phys_base=0x80000000`, `load_phys_end=0x80009000`), refined proposed `topOfKernelData=0x8000c000` and `avail_start=0x80016000`, returned loader status `0x44000001`, preserved the Stage43 safety gates, and re-ran SGI/timer IRQ selftests successfully.
- Stage45 executed the public Mach-O copy+zero-fill materialization rule into a dedicated local BSS arena only, reparsed the materialized image, verified `ST45-TEXT`/`ST45-DATA`/`ST45-PRELINK-TEXT` marker prefixes and zero-fill tails, recorded `macho_staging_status=0x45000001`, `loader_safety_mask=0x0000007f`, `loader_satisfied_mask=0x000001ff`, returned loader status `0x45000001`, preserved the Stage44 safety gates, and re-ran SGI/timer IRQ selftests successfully.
- Stage46 modeled the early ARM XNU TTE workspace derived from Stage45's materialized image and `topOfKernelData=0x8000c000`, recorded a 10-page workspace split into 16 KiB L1, 4 KiB L2/coarse-table, and 20 KiB scratch reservations, reported kernel/RAM/ram_console/GIC section indices, proved `xnu_tte_dryrun_status=0x46000001`, `loader_safety_mask=0x000003ff`, `loader_satisfied_mask=0x000003ff`, returned loader status `0x46000001`, preserved the Stage45 safety gates, and re-ran SGI/timer IRQ selftests successfully.
- Stage47 verified the local simulated ARMv7 L1 section descriptor words emitted by the Stage46 TTE dry-run model, checked descriptor type/attribute masks for loaded kernel, low RAM, ram_console, and GIC/timer MMIO descriptors, added a software-only identity L1 section VTOP dry-run, proved `xnu_tte_descriptor_verify_mask=0x0000007f`, `xnu_tte_translation_check_mask=0x0000001f`, `xnu_tte_satisfied_mask=0x0001ffff`, `loader_safety_mask=0x00000fff`, `loader_satisfied_mask=0x00000fff`, returned loader status `0x47000001`, preserved the Stage46 safety gates, and re-ran SGI/timer IRQ selftests successfully.
- Stage48 added a PA-base-aware high-VA L1 section descriptor model and a Stage-owned safe-table materialization buffer, proved `xnu_tte_descriptor_verify_mask=0x000001ff`, `xnu_tte_translation_check_mask=0x0000007f`, `xnu_tte_satisfied_mask=0x001fffff`, `loader_safety_mask=0x00007fff`, `loader_satisfied_mask=0x00007fff`, returned loader status `0x48000001`, preserved no-XNU/no-TTBR/no-cache/no-proposed-physical-write safety, and re-ran SGI/timer IRQ selftests successfully.
- Stage49 performed a controlled no-XNU live TTBR0 round-trip using only a Stage49-owned 16 KiB recovery L1 table at `0x00068000`, restored original TTBR0 `0x0006c000`, preserved TTBCR/DACR/SCTLR and cache bits (`0x00000000` before/during/after), proved `stage49_ttbr_roundtrip_satisfied_mask=0x0000ffff`, `loader_satisfied_mask=0x0007ffff`, returned loader status `0x49000001`, avoided XNU/Mach-O execution and proposed physical/workspace writes, and reset through PS_HOLD successfully.
- Stage50 created the public-XNU workspace and cancro/MSM8974 ARMv7 target scaffold, validated `external/xnu-upstream` at public `xnu-2050.22.13` commit `cc8a9b0c` / `MasterVersion 12.3.0`, acknowledged the incomplete public 2050 ARM source gap, validated later public ARM references from `external/xnu-4570.1.46`, prepared the Stage51 object plan, proved `stage50_xnu_workspace_satisfied_mask=0x0007ffff`, `loader_safety_mask=0x0003ffff`, `loader_satisfied_mask=0x003fffff`, returned loader status `0x50000001`, preserved the Stage-owned TTBR0 restore/cache safety, avoided public-XNU execution/full `mach_kernel` build/external mutation, and reset through PS_HOLD successfully.
- Stage51 compiled the first minimal public-XNU ARMv7 object subset from public `xnu-2050.22.13` sources (`pexpert/gen/device_tree.c` and `pexpert/gen/bootargs.c`) using Stage51-owned compatibility shims, emitted ignored objects under `out/stage51/xnu-objects/`, proved `stage51_xnu_object_subset_satisfied_mask=0x00003fff`, `stage51_xnu_object_count=0x00000003`, `loader_safety_mask=0x0007ffff`, `loader_satisfied_mask=0x007fffff`, returned loader status `0x51000001`, preserved the Stage-owned TTBR0 restore/cache safety, avoided public-XNU Mach-O link/public-XNU execution/full `mach_kernel` build/external mutation, and reset through PS_HOLD successfully.
- Stage52 linked that minimal public-XNU object subset into a controlled host-only ARM ELF proof artifact with Stage52-owned support objects, embedded only link metadata into the inert generated Mach-O fixture, proved zero undefined symbols, `stage52_xnu_link_satisfied_mask=0x0000ffff`, `loader_safety_mask=0x0007ffff`, `loader_satisfied_mask=0x00ffffff`, and loader status `0x52000001`, preserved no public-XNU execution/no Mach-O execution/no proposed physical or TTE workspace writes/no external mutation/no cache changes, and reset through PS_HOLD successfully.
- Stage53 formalized public-XNU compile migration with a host-side compile graph scanner over selected public XNU candidates, graph-gated the first bounded expansion to public `pexpert/gen/pe_gen.c`, compiled three public objects plus Stage53-owned shims, linked them into a controlled host-only ARM ELF proof with zero undefined symbols, proved `stage53_xnu_compile_graph_satisfied_mask=0x0003ffff`, `stage53_xnu_object_count=0x00000004`, `stage53_xnu_link_object_count=0x00000004`, `loader_safety_mask=0x000fffff`, `loader_satisfied_mask=0x01ffffff`, and loader status `0x53000001`, preserved no public-XNU execution/no Mach-O execution/no proposed physical or TTE workspace writes/no external mutation/no persistent writes/no cache changes, and reset through PS_HOLD successfully.
- Stage54 expanded the public-XNU graph toward pexpert/platform ARM sources by graph-gating later-public `external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c`, removing the Stage-owned `PE_boot_args()` shim so the symbol comes from public `arm_pe_bootargs.o`, adding Stage-owned `PE_state` ABI backing, proving duplicate symbols are closed, compiling four public objects plus one Stage54-owned shim object, linking a controlled host-only ARM ELF proof with zero undefined symbols, proving `stage54_xnu_compile_graph_satisfied_mask=0x00ffffff`, `stage54_xnu_object_count=0x00000005`, `stage54_xnu_link_object_count=0x00000005`, `loader_safety_mask=0x001fffff`, `loader_satisfied_mask=0x01ffffff`, and loader status `0x54000001`, preserving no public-XNU execution/no platform runtime execution/no Mach-O execution/no proposed physical or TTE workspace writes/no external mutation/no persistent writes/no cache changes, and reset through PS_HOLD successfully.
- Stage55 expanded the bounded public ARM pexpert/platform proof to `external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c`, added minimal Stage-owned consistent-debug, OSAtomic, and machine-routine ABI shims, compiled five public objects plus one Stage55-owned shim object, linked a controlled host-only ARM ELF proof with zero undefined symbols, proved `stage55_xnu_compile_graph_satisfied_mask=0x07ffffff`, `stage55_xnu_object_count=0x00000006`, `stage55_xnu_object_public_arm_pexpert_count=0x00000002`, `stage55_xnu_link_object_count=0x00000006`, `loader_safety_mask=0x001fffff`, `loader_satisfied_mask=0x01ffffff`, and loader status `0x55000001`, preserved no public-XNU execution/no platform runtime execution/no Mach-O execution/no proposed physical or TTE workspace writes/no external mutation/no persistent writes/no cache changes, and reset through PS_HOLD successfully.
- Stage56 keeps the Stage55 public object/link proof stable and adds a Stage-owned XNU bootstrap mapping contract over inert Mach-O loader, staging, TTE dry-run, high-VA, safe-table, TTBR0-restore, cache-preservation, compile-graph, object-subset, and controlled link-proof facts. It explicitly blocks runtime-heavy public ARM pexpert sources (`pe_kprintf.c`, `pe_serial.c`, `pe_identify_machine.c`, `pe_init.c`), proves `stage56_xnu_compile_graph_satisfied_mask=0x1fffffff`, `stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001`, local object/link counts remain six/zero-undefined, emits `stage56_xnu_bootstrap_contract_status=0x56000001` and `loader_xnu_bootstrap_contract_status_rollup=0x56000001` as target-side gates, and preserves no public-XNU execution/no platform runtime execution/no Mach-O execution/no proposed physical or TTE workspace writes/no external mutation/no persistent writes/no cache changes. Hardware validation through non-persistent `fastboot boot` recovered 130283 bytes from `/proc/last_kmsg`, confirmed loader status `0x56000001`, and returned to Android.
- Stage57 keeps the Stage55/Stage56 public object/link proof stable and adds a Stage-owned XNU pmap/bootstrap allocation contract layered on the Stage56 bootstrap mapping contract. It expands the fail-closed compile graph to twenty candidates, treats public ARM `arm_vm_init.c`, `pmap.c`, `pmap.h`, `proc_reg.h`, and `vm_param.h` as reference-only, keeps public pmap compile/link/execute counts at zero, models public VM/pmap arithmetic (`gVirtBase`, `gPhysBase`, `boot_ttep`, `cpu_ttep`, `initial_avail_start`, `avail_end`, `vstart`, `virtual_space_end`) inside Stage-owned code, proves `stage57_xnu_pmap_bootstrap_contract_status=0x57000001`, `stage57_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff`, `loader_xnu_pmap_bootstrap_contract_status_rollup=0x57000001`, and loader status `0x57000001`, and preserves no public-XNU/platform/VM-pmap/Mach-O execution, no proposed physical/TTE writes, no live pmap table install, no external mutation, no persistent writes, and no cache changes. Hardware validation through non-persistent `fastboot boot` recovered 138383 bytes from `/proc/last_kmsg`, confirmed all Stage57 contract/status markers, and returned to Android.
- Stage58 keeps the Stage57 public object/link and pmap/bootstrap allocation proofs stable and adds a Stage-owned XNU pmap table population dry-run contract. It constructs a simulated ARMv7 4096-entry L1 section table in a local Stage-owned 16 KiB buffer at `0x00080000`-`0x00084000`, distinct from the proposed pmap workspace L1 at `0x00074000`, populates low-memory, kernel, workspace, and ram_console section descriptors with `0x00010c02`, verifies descriptor readback/type/attribute masks and four software translations, proves `stage58_xnu_pmap_table_dryrun_contract_status=0x58000001`, `stage58_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff`, `stage58_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000`, `loader_xnu_pmap_table_dryrun_contract_status_rollup=0x58000001`, and loader status `0x58000001`, and preserves no proposed workspace writes, no live pmap table install, no TTBR/TTBCR/DACR/SCTLR writes, no TLB invalidation for pmap install, no public VM/pmap execution, no persistent writes, and no cache changes. Hardware validation through non-persistent `fastboot boot` recovered 145908 bytes from `/proc/last_kmsg`, confirmed all Stage58 table dry-run contract/status markers, and returned to Android.

## Repository contents

- `docs/local-device-findings.md` — detailed local observations, partition map, backup status, and parsed boot/recovery image fields.
- `docs/recovery-and-rollback.md` — required recovery checklist and rollback procedure before any persistent write.
- `docs/boot-tooling.md` — local boot image tooling plan and no-op round-trip results.
- `docs/cancro-platform.md` — Xiaomi Mi 4 / MSM8974 platform source pointers and bootloader notes.
- `docs/darwin-xnu-research.md` — open Darwin/XNU research notes and milestone framing.
- `docs/source-baseline.md` — external source checkout baseline for cancro kernel/device tree and public XNU references.
- `docs/upstream-xnu-analysis.md` — selected iOS 6-era public XNU tag analysis, Stage42 gap review, and revised Stage43 loader direction.
- `docs/experiment-01-cmdline.md` — first successful experiment: custom kernel cmdline via non-persistent boot.
- `docs/no-teardown-debugging.md` — USB-only debugging channels (`/proc/last_kmsg`, `/dev/kmsg`, ramoops) that avoid soldering a UART.
- `docs/experiment-02-usb-log-loop.md` — verified printk/kmsg markers survive reboot into `/proc/last_kmsg`.
- `docs/msm8974-xnu-porting-map.md` — concrete MSM8974 ↔ XNU platform interface and work-package map.
- `docs/stage0-payload-plan.md` — plan for first non-Linux ARMv7 payload executed via `fastboot boot`.
- `docs/experiment-03-stage0-bare-metal.md` — successful Stage0 bare-metal payload execution and ram_console/PS_HOLD proof.
- `docs/stage1-boot-wrapper-plan.md` — Stage1 boot-wrapper design for XNU-style `boot_args` and Apple-DT handoff.
- `docs/experiment-04-stage1-boot-wrapper.md` — successful Stage1 boot-wrapper hardware test and recovered persistent log.
- `stage0/` — tiny bare-metal ARMv7 payload that writes a ram_console marker and attempts MSM8974 reset.
- `stage1/` — ARMv7 boot-wrapper payload that builds a public-XNU-style `boot_args` structure and validates `test_kernel_entry(boot_args*)` handoff.
- `stage2/` — freestanding C runtime payload that builds and walks a fuller Apple-style device tree for the MSM8974 bring-up contract.
- `stage3/` — C runtime payload that performs read-only CP15, ARM generic timer, and GIC probes on the MSM8974 hardware.
- `stage4/` — C runtime payload with custom ARMv7 exception vectors, early timebase, and deliberate exception recovery test.
- `stage5/` — XNU-adjacent skeleton with `kernel_entry(struct boot_args*)`, pexpert-like Apple-DT discovery, and `ml_*` timebase stubs.
- `stage6/` — XNU-adjacent skeleton with `PE_state`-like platform state and deliberate data-abort recovery test.
- `stage7/` — XNU-adjacent skeleton with a read-only MSM8974 GIC driver/state snapshot.
- `stage8/` — XNU-adjacent skeleton with controlled SGI0 delivery and a returnable GIC IRQ handler path.
- `stage9/` — XNU-adjacent skeleton with ARM generic timer one-shot IRQ delivery.
- `stage10/` — XNU-adjacent skeleton with first ARMv7 MMU identity-map enable.
- `stage11/` — XNU-adjacent skeleton with controlled high virtual aliases over the identity map.
- `stage12/` — XNU-adjacent skeleton with high-virtual function call and state-pointer validation.
- `stage13/` — XNU-adjacent skeleton with a high-virtual kernel bootstrap handoff and state block.
- `stage14/` — XNU-adjacent skeleton with richer PE/XNU-like high-virtual bootstrap state handling.
- `stage15/` — XNU-adjacent skeleton with high-virtual bootstrap validation and structured status reporting.
- `stage16/` — XNU-adjacent skeleton with a small ordered high-virtual init sequence.
- `stage17/` — XNU-adjacent skeleton with a high-virtual root kernel entry shape.
- `stage18/` — XNU-adjacent skeleton with high-virtual boot_args and Apple-DT summary inside the root path.
- `stage19/` — XNU-adjacent skeleton with a versioned high-root platform-result object.
- `stage20/` — XNU-adjacent skeleton with an ordered high-root phase table.
- `stage21/` — XNU-adjacent skeleton with a high-root early service table.
- `stage22/` — XNU-adjacent skeleton with high-root phase-service dependency validation.
- `stage23/` — XNU-adjacent skeleton with a descriptor-driven high-root phase dispatcher.
- `stage24/` — XNU-adjacent skeleton with a descriptor-driven high-root service dispatcher.
- `stage25/` — XNU-adjacent skeleton with a high-root bootstrap registry that cross-checks service and phase descriptor coverage.
- `stage26/` — XNU-adjacent skeleton with registry-gated high-root boot policy checks before final root success.
- `stage27/` — XNU-adjacent skeleton with a descriptor-driven bootstrap manifest over registry and boot-policy state.
- `stage28/` — XNU-adjacent skeleton with a manifest-driven launch contract for the next kernel bootstrap boundary.
- `stage29/` — XNU-adjacent skeleton with a contract-consuming startup boundary after the launch contract.
- `stage30/` — XNU-adjacent skeleton with a minimal startup routine driven by the startup boundary.
- `stage31/` — XNU-adjacent skeleton with a separately called high-virtual startup entry and explicit startup handoff object.
- `stage32/` — XNU-adjacent skeleton with a startup-entry-owned descriptor-driven kernel-start callout table.
- `stage33/` — XNU-adjacent skeleton with startup-entry-populated kernel-start context object.
- `stage34/` — XNU-adjacent skeleton with kernel-context-produced VM bootstrap plan object.
- `stage35/` — XNU-adjacent skeleton with VM-plan-driven VM bootstrap state object.
- `stage36/` — XNU-adjacent skeleton with VM-state-driven bootstrap allocator descriptor.
- `stage37/` — XNU-adjacent skeleton with allocator-driven pmap bootstrap workspace descriptor.
- `stage38/` — XNU-adjacent skeleton with pmap-workspace-driven kernel object table descriptor.
- `stage39/` — XNU-adjacent skeleton with object-table-driven kernel collection handoff descriptor.
- `stage40/` — XNU-adjacent skeleton with collection-handoff-driven kernel collection entry-table descriptor.
- `stage41/` — XNU-adjacent skeleton with entry-table-driven kernel collection object-graph descriptor.
- `stage42/` — XNU-adjacent skeleton with object-graph-driven kernel collection dependency-resolution descriptor.
- `stage43/` — XNU-adjacent skeleton with a bounded Mach-O/XNU loader preflight, Apple-DT semantic readiness check, and proposed XNU boot tuple/workspace report.
- `stage44/` — XNU-adjacent skeleton with a generated non-proprietary Mach-O fixture, section/prelink reporting, and proposed physical load-plan/topOfKernelData refinement.
- `stage45/` — XNU-adjacent skeleton with local BSS-only Mach-O copy/zero-fill materialization, materialized-image reparse, marker checks, and zero-fill validation.
- `stage46/` — XNU-adjacent skeleton with a local BSS-only ARM XNU TTE workspace dry-run descriptor derived from the materialized Mach-O load plan and proposed `topOfKernelData`.
- `stage47/` — XNU-adjacent skeleton with local simulated ARMv7 L1 descriptor readback verification and software-only identity VTOP dry-run checks over the TTE arena.
- `stage48/` — XNU-adjacent skeleton with PA-base-aware high-VA L1 descriptor modeling and Stage-owned safe-table materialization.
- `stage49/` — XNU-adjacent skeleton with a controlled no-XNU TTBR0 round-trip using a Stage-owned recovery L1 table and restored live MMU state.
- `stage50/` — public-XNU workspace and cancro/MSM8974 ARMv7 target scaffold that validates public source refs and prepares the Stage51 object-subset plan without executing XNU.
- `stage51/` — minimal public-XNU object-subset compile scaffold that builds selected public `xnu-2050.22.13` pexpert/device-tree objects with Stage51-owned shims while avoiding public-XNU Mach-O link or execution.
- `stage52/` — controlled public-XNU link-proof scaffold that links the Stage51 object subset into a host-only ARM ELF artifact with Stage52-owned support code, imports link facts into the target ABI, and keeps the generated Mach-O fixture inert/no-exec.
- `stage53/` — public-XNU compile graph migration scaffold that classifies selected public XNU candidates, graph-gates bounded `pexpert/gen/pe_gen.c` inclusion, compiles three public objects plus Stage-owned shims, links a host-only ARM ELF proof with zero undefined symbols, imports graph/object/link facts into the target ABI, and keeps public-XNU/Mach-O execution disabled.
- `stage54/` — public-XNU pexpert/platform compile graph scaffold that graph-gates later-public ARM `pexpert/arm/pe_bootargs.c`, sources `PE_boot_args()` from public `arm_pe_bootargs.o`, supplies only Stage-owned `PE_state` ABI backing, proves duplicate symbols are closed, links a five-object host-only ARM ELF proof with zero undefined symbols, imports graph/object/link facts into the target ABI, and keeps public-XNU/platform-runtime/Mach-O execution disabled.
- `stage55/` — public-XNU pexpert/platform compile graph scaffold that adds later-public ARM `pexpert/arm/pe_consistent_debug.c`, supplies minimal Stage-owned consistent-debug/OSAtomic/machine-routine ABI shims, links a six-object host-only ARM ELF proof with zero undefined symbols, imports graph/object/link facts into the target ABI, and keeps public-XNU/platform-runtime/Mach-O execution disabled.
- `stage56/` — Stage-owned XNU bootstrap mapping contract scaffold that keeps the Stage55 public object/link set stable, blocks runtime-heavy public ARM pexpert sources, derives page-granular bootstrap mapping facts from loader/TTE/highVA/safe-table/TTBR/cache facts, imports contract status into the loader roll-up, and keeps public-XNU/platform-runtime/Mach-O execution plus proposed physical/TTE writes disabled.
- `stage57/` — Stage-owned XNU pmap/bootstrap allocation contract scaffold that keeps the bounded public object/link set stable, treats public ARM VM/pmap sources as reference-only, derives public-style pmap arithmetic and allocator/workspace facts from Stage-owned contracts, imports contract status into the loader roll-up, and keeps public-XNU/platform-runtime/VM-pmap/Mach-O execution plus proposed physical/TTE writes and live pmap table installs disabled.
- `stage58/` — Stage-owned XNU pmap table population dry-run contract scaffold that keeps the Stage57 pmap/bootstrap tuple stable, populates and validates a local-only simulated ARMv7 L1 section table, imports dry-run status into the loader roll-up, and keeps proposed workspace writes, live pmap table installs, control-register writes, TLB invalidation, public VM/pmap execution, persistent writes, and cache changes disabled.
- `docs/ios-613-oss-baseline.md` — notes on Apple OSS `distribution-iOS@ios-613` and public XNU baseline implications.
- `docs/experiment-05-stage2-c-runtime.md` — successful Stage2 C runtime + Apple-DT builder/walker hardware test.
- `docs/experiment-06-stage3-hardware-probes.md` — successful Stage3 read-only CP15/timer/GIC hardware probes.
- `docs/experiment-07-stage4-timebase-vectors.md` — successful Stage4 VBAR/vector + `CNTPCT` timebase/delay test.
- `docs/experiment-08-stage5-xnu-skeleton.md` — successful Stage5 XNU-adjacent kernel-entry skeleton test.
- `docs/experiment-09-stage6-pe-state-abort.md` — successful Stage6 PE_state-like platform state and data-abort recovery test.
- `docs/experiment-10-stage7-gic-skeleton.md` — successful Stage7 read-only GIC driver skeleton test.
- `docs/experiment-11-stage8-sgi-irq.md` — successful Stage8 controlled SGI0 IRQ delivery test.
- `docs/experiment-12-stage9-timer-irq.md` — successful Stage9 ARM generic timer IRQ delivery test.
- `docs/experiment-13-stage10-mmu-identity.md` — successful Stage10 ARMv7 MMU identity-map enable test.
- `docs/experiment-14-stage11-high-alias.md` — successful Stage11 high virtual alias mapping test.
- `docs/experiment-15-stage12-high-call.md` — successful Stage12 high-virtual function call test.
- `docs/experiment-16-stage13-high-bootstrap.md` — successful Stage13 high-virtual kernel bootstrap handoff test.
- `docs/experiment-17-stage14-high-pe-state.md` — successful Stage14 high-virtual PE/XNU state bootstrap test.
- `docs/experiment-18-stage15-high-validation.md` — successful Stage15 high-virtual bootstrap validation/status test.
- `docs/experiment-19-stage16-high-init-sequence.md` — successful Stage16 high-virtual init-sequence test.
- `docs/experiment-20-stage17-high-root.md` — successful Stage17 high-virtual root kernel path test.
- `docs/experiment-21-stage18-high-dt-summary.md` — successful Stage18 high-virtual boot_args and Apple-DT summary test.
- `docs/experiment-22-stage19-platform-result.md` — successful Stage19 versioned high-root platform-result test.
- `docs/experiment-23-stage20-phase-table.md` — successful Stage20 ordered high-root phase-table test.
- `docs/experiment-24-stage21-service-table.md` — successful Stage21 high-root service-table test.
- `docs/experiment-25-stage22-phase-service-deps.md` — successful Stage22 high-root phase-service dependency test.
- `docs/experiment-26-stage23-phase-dispatcher.md` — successful Stage23 descriptor-driven high-root phase dispatcher test.
- `docs/experiment-27-stage24-service-dispatcher.md` — successful Stage24 descriptor-driven high-root service dispatcher test.
- `docs/experiment-28-stage25-bootstrap-registry.md` — successful Stage25 high-root bootstrap registry test.
- `docs/experiment-29-stage26-boot-policy.md` — successful Stage26 registry-gated boot policy test.
- `docs/experiment-30-stage27-bootstrap-manifest.md` — successful Stage27 descriptor-driven bootstrap manifest test.
- `docs/experiment-31-stage28-launch-contract.md` — successful Stage28 manifest-driven launch contract test.
- `docs/experiment-32-stage29-startup-boundary.md` — successful Stage29 contract-consuming startup boundary test.
- `docs/experiment-33-stage30-startup-routine.md` — successful Stage30 startup-routine handoff test.
- `docs/experiment-34-stage31-startup-entry.md` — successful Stage31 high-virtual startup-entry handoff test.
- `docs/experiment-35-stage32-kernel-callouts.md` — successful Stage32 startup-entry kernel-callout table test.
- `docs/experiment-36-stage33-kernel-context.md` — successful Stage33 startup-entry kernel-context object test.
- `docs/experiment-37-stage34-vm-plan.md` — successful Stage34 VM bootstrap plan object test.
- `docs/experiment-38-stage35-vm-state.md` — successful Stage35 VM bootstrap state object test.
- `docs/experiment-39-stage36-bootstrap-allocator.md` — successful Stage36 bootstrap allocator descriptor test.
- `docs/experiment-40-stage37-pmap-workspace.md` — successful Stage37 pmap bootstrap workspace descriptor test.
- `docs/experiment-41-stage38-kernel-object-table.md` — successful Stage38 kernel object table descriptor test.
- `docs/experiment-42-stage39-kernel-collection-handoff.md` — successful Stage39 kernel collection handoff descriptor test.
- `docs/experiment-43-stage40-kernel-collection-entry-table.md` — successful Stage40 kernel collection entry-table descriptor test.
- `docs/experiment-44-stage41-kernel-collection-object-graph.md` — successful Stage41 kernel collection object-graph descriptor test.
- `docs/experiment-45-stage42-kernel-collection-dependency-resolution.md` — successful Stage42 kernel collection dependency-resolution descriptor test.
- `docs/experiment-46-stage43-mach-o-xnu-loader-probe.md` — successful Stage43 Mach-O/XNU loader-preflight probe test.
- `docs/experiment-47-stage44-mach-o-load-plan.md` — successful Stage44 generated Mach-O fixture, section/prelink, and physical load-plan test.
- `docs/experiment-48-stage45-macho-materialization.md` — successful Stage45 local Mach-O materialization, reparse, marker, and zero-fill test.
- `docs/experiment-49-stage46-xnu-tte-dryrun.md` — successful Stage46 local ARM XNU TTE workspace dry-run test.
- `docs/experiment-50-stage47-tte-verify-vtop-dryrun.md` — successful Stage47 local TTE descriptor verification and VTOP dry-run test.
- `docs/experiment-51-stage48-high-va-safe-table-materialization.md` — successful Stage48 high-VA descriptor model and Stage-owned safe-table materialization test.
- `docs/experiment-52-stage49-controlled-ttbr-roundtrip.md` — successful Stage49 controlled no-XNU TTBR0 round-trip and restore test.
- `docs/experiment-53-stage50-public-xnu-workspace-cancro-scaffold.md` — successful Stage50 public-XNU workspace and cancro target scaffold validation.
- `docs/experiment-54-stage51-public-xnu-object-subset-compile.md` — successful Stage51 minimal public-XNU object-subset compile and no-execution loader roll-up validation.
- `docs/experiment-55-stage52-public-xnu-controlled-link-proof.md` — successful Stage52 controlled public-XNU ARM ELF link proof, inert Mach-O metadata wrapper, and no-execution loader roll-up validation.
- `docs/experiment-56-stage53-xnu-compile-graph.md` — successful Stage53 public-XNU compile graph migration proof with bounded `pe_gen.c` object expansion, controlled ARM ELF link proof, and no-execution loader roll-up validation.
- `docs/experiment-57-stage54-pexpert-platform-compile-graph.md` — successful Stage54 public-XNU pexpert/platform compile graph proof with bounded public ARM `pe_bootargs.c`, PE_state ABI shim backing, duplicate-symbol closure, controlled ARM ELF link proof, and no-platform-runtime-execution loader roll-up validation.
- `docs/experiment-58-stage55-pexpert-consistent-debug-compile-graph.md` — Stage55 public-XNU pexpert consistent-debug compile/link proof with bounded public ARM `pe_consistent_debug.c`, minimal consistent-debug ABI shims, six-object host-only ARM ELF link proof, and no-platform-runtime-execution boundary.
- `docs/experiment-59-stage56-xnu-bootstrap-mapping-contract.md` — Stage56 Stage-owned XNU bootstrap mapping contract proof over loader/TTE/highVA/safe-table/TTBR/cache facts, stable public object/link proof, blocked runtime-heavy ARM pexpert sources, and no-public-XNU/platform/Mach-O execution boundary.
- `docs/experiment-60-stage57-xnu-pmap-bootstrap-contract.md` — Stage57 Stage-owned XNU pmap/bootstrap allocation contract proof over the Stage56 mapping tuple plus allocator/workspace snapshot facts, public ARM VM/pmap arithmetic modeled reference-only, zero public pmap compile/link/execute counts, and no live pmap table install boundary.
- `docs/experiment-61-stage58-xnu-pmap-table-dryrun-contract.md` — Stage58 Stage-owned XNU pmap table population dry-run contract proof over the Stage57/Stage58 pmap/bootstrap tuple and snapshot, local-only simulated ARMv7 L1 descriptor population/readback/translation, and no proposed pmap workspace write/live table install/control-register/TLB/cache boundary.
- `tools/parse_android_bootimg.py` — dependency-free parser/extractor for Android boot image v0/v1-style files.
- `tools/patch_bootimg_cmdline.py` — surgical editor that changes only the kernel command line, preserving kernel/ramdisk/QCDT and the boot `id`.
- `tools/mkbootimg_v0_qcdt.py` — legacy Android boot image v0 packer that populates the Qualcomm QCDT `dt_size` field required by cancro bootloader.
- `tools/mkmacho_fixture.py` — deterministic host generator for the non-proprietary inert Stage44+ Mach-O fixture, including optional metadata embedding used by Stage52.

Example parser usage:

```bash
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/boot.img
```

The backup directory is ignored by git, so this command only works on a host where the local backup exists.

## Safety rules for this repo

- Do not flash or erase partitions without an explicit confirmation for that specific operation.
- Prefer `fastboot boot` or other non-persistent tests before persistent writes.
- Never include personal data or large partition backup images in normal source commits.
- Keep recovery instructions and hashes close to any experimental boot image work.
- Focus on open-source, owned-device research. Do not rely on leaked proprietary Apple code.

## Next milestones

Stage0 through Stage58 are complete through non-persistent hardware validation. Stage58 validates a Stage-owned XNU pmap table population dry-run contract over the Stage57/Stage58 bootstrap mapping tuple, pmap/bootstrap allocation contract, and Stage-owned allocator/workspace snapshot facts while keeping the bounded public pexpert object/link set stable. The Stage58 graph consumes `external/xnu-upstream` detached at public `xnu-2050.22.13` (`cc8a9b0c`, `MasterVersion 12.3.0`) read-only, uses `external/xnu-4570.1.46` only as a public ARM reference, classifies twenty candidates, allows only `pexpert/gen/device_tree.c`, `pexpert/gen/bootargs.c`, `pexpert/gen/pe_gen.c`, `pexpert/arm/pe_bootargs.c`, and `pexpert/arm/pe_consistent_debug.c`, treats public ARM `arm_vm_init.c`, `pmap.c`, `pmap.h`, `proc_reg.h`, and `vm_param.h` as reference-only, links the six-object host-only ARM ELF proof with zero undefined symbols, and records pmap public compile/link/execute counts as zero. The target-side dry-run contract proves `stage58_xnu_pmap_table_dryrun_contract_status=0x58000001`, `stage58_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff`, `stage58_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000`, local L1 dry-run buffer `0x00080000`-`0x00084000`, proposed workspace L1 `0x00074000`/`0xc0074000`, descriptor type/attribute masks `0x00000002`/`0x00010c02`, four software translations, `loader_xnu_pmap_table_dryrun_contract_status_rollup=0x58000001`, and `loader_status=0x58000001`. Hardware validation recovered 145908 bytes from `/proc/last_kmsg` with 2162 `MI4IOS6_STAGE58` markers and 2136 `MI4IOS6_STAGE58_XNU` markers after non-persistent `fastboot boot`. Stage58 explicitly avoids any full public `mach_kernel` build, public-XNU object execution, public pexpert/platform runtime execution, public ARM VM/pmap runtime execution, generated Mach-O execution, XNU `_start` / `arm_init` jump, proposed physical/TTE/pmap workspace write, live proposed pmap table install, TTBR/TTBCR/DACR/SCTLR write for pmap install, TLB invalidation for pmap install, external checkout mutation, persistent write, or cache change.

Near-term work should proceed from hardware-validated Stage58 and refine the real-XNU loader/mapping/pmap path while keeping the proven no-execution safety boundary:

- Keep using non-persistent `sudo fastboot boot` for hardware validation; do not flash without explicit per-operation confirmation.
- Preserve identity/recovery mappings for ram_console, PS_HOLD, GIC, timer, abort logging, and early recovery paths.
- Keep descriptor-driven service/phase dispatchers plus registry, policy, manifest, launch-contract, startup-boundary, startup-routine, startup-entry, callout-table, kernel-context, VM-plan, VM-state, allocator, pmap-workspace, object-table, collection-handoff, entry-table, object-graph, dependency-resolution, loader-preflight, load-plan, materialization, TTE dry-run, descriptor verification, high-VA VTOP dry-run, safe-table materialization, TTBR0 restore checks, workspace validation, compile-graph validation, object-subset compile validation, controlled link-proof validation, bootstrap mapping contract, pmap/bootstrap allocation contract, and pmap table dry-run contract as safety gates.
- Use the Stage49/Stage58 TTBR0 restore proof as the live-MMU safety baseline; do not install proposed XNU tables yet.
- Expand from the Stage58 pmap table dry-run contract toward deeper page-granular pmap table modeling, exact XNU cache/MMU attributes, MSM8974 pexpert support, XNU interrupt/timer hooks, or another bounded public-XNU source proof instead of jumping directly to a full kernel.
- Keep large source checkouts under ignored `external/` and build outputs under the next ignored stage output directory.
- Continue refining real XNU high-virtual `virtBase`/`physBase` mapping toward sub-section/page-granular behavior instead of only section-envelope translations.
- Continue refining proposed `virtBase`/`physBase`/`topOfKernelData`, `cpu_ttep`, `avail_start`, and pmap bootstrap allocation facts from concrete loader/build facts.
- Use `xnu-2050.22.13` for iOS 6 / Darwin 12-era public context and `xnu-4570.1.46` as the public ARM implementation reference where the 2050 tree lacks ARMv7 files.
- Do not build a full public `mach_kernel`, jump into XNU, execute public-XNU object code, execute public platform runtime code, execute public VM/pmap runtime code, or install proposed XNU tables yet; record compile/link/readiness facts first.
- Keep caches disabled unless a later stage explicitly validates a safe cache policy.
- Preserve SGI/timer IRQ retests plus custom abort logging.
