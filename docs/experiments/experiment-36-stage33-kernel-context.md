# Experiment 36 — Stage33 Kernel-Start Context

Date: 2026-06-05

Goal: make startup-entry callouts populate a minimal kernel-start context object.

Stage33 still does **not** run XNU or iOS. It extends Stage32 by adding a versioned kernel-start context object populated by the high-virtual `startup_entry` after the descriptor-driven callout table succeeds. The context records validated boot-args, Apple-DT, platform, timebase, interrupt, callout, and root-step facts, then is checked through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage33 adds over Stage32

Stage32 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated boot policy,
- descriptor-driven bootstrap manifest,
- manifest-driven launch contract,
- contract-consuming startup boundary,
- startup-boundary-driven startup routine,
- separate high-virtual startup entry,
- explicit startup handoff object,
- startup-entry-owned descriptor-driven kernel-start callout table,
- callout order/handler/service masks `0x0000000f`,
- callout checksum `0x00000005`,
- callout status `0x32000001`,
- root step mask `0x0007ffff`,
- status `0x32000001`.

Stage33 adds a kernel-start context object:

- context version: `1`,
- context size: `0x00000038`,
- required/satisfied context mask: `0x0000003f`,
- context boot args pointer: `0xc002c000`,
- context DT pointer: `0xc002c140`,
- context platform status: `0x33000001`,
- context platform consistency: `0x0000000f`,
- context timebase frequency: `0x0124f800` (`19.2 MHz`),
- context interrupt mask: `0x0000000f`,
- context callout status: `0x33000001`,
- context callout mask: `0x0000000f`,
- context root-step input: `0x0001fff3`,
- context checksum: `0x012506ba`,
- context status: `0x33000001`,
- full root step mask extends to `0x000fffff`,
- final root status `0x33000001`.

Kernel context satisfied bits:

```text
0x00000001 boot args pointer satisfied
0x00000002 DT pointer satisfied
0x00000004 platform status/consistency satisfied
0x00000008 timebase satisfied
0x00000010 interrupt readiness satisfied
0x00000020 callout table satisfied
```

Complete context satisfied mask: `0x0000003f`.

The startup-entry context is populated after the callout table succeeds. The new root-step bit records that the context was accepted:

```text
startup-entry input root steps: 0x0001fff3
final root steps:              0x000fffff
```

Root step bits now include:

```text
0x00000001 enter
0x00000002 init complete
0x00000004 result generated
0x00000008 return-ready
0x00000010 high-DT summary complete
0x00000020 platform-result complete
0x00000040 phase-table complete
0x00000080 service-table complete
0x00000100 phase-service dependencies complete
0x00000200 descriptor phase dispatcher complete
0x00000400 descriptor service dispatcher complete
0x00000800 bootstrap registry complete
0x00001000 boot policy complete
0x00002000 bootstrap manifest complete
0x00004000 launch contract complete
0x00008000 startup boundary complete
0x00010000 startup routine complete
0x00020000 startup entry complete
0x00040000 kernel callout table complete
0x00080000 kernel-start context complete
```

Complete root-step mask: `0x000fffff`.

## Built image

```bash
./stage33/build.sh
```

Successful local build:

```text
out/stage33/stage33-qcdt.img
sha256=3d2404972b75cd28ff5543f1fc429d2ef16b231bd9696671980dde16098863cf
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=77332 (0x12e14)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage33 mi4ios6=stage33 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage33_vectors
0000ab68 t stage33_kernel_root
0000e3d0 t stage33_startup_entry
0000f518 T mmu_high_bootstrap_selftest
00011ed4 T kernel_entry
000121fc T test_kernel_entry
00012394 T stage33_main
00024000 b stage33_startup_handoff_block
00024030 b stage33_kernel_context_block
00024080 b stage33_bootstrap_state_block
00028000 b stage33_l1_table
0002e000 B __stage33_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage33/stage33-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2540 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage33-last_kmsg.txt
```

The recovered log was 49763 bytes and contained:

```text
785 MI4IOS6_STAGE33 markers
759 MI4IOS6_STAGE33_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE33_XNU high root service dispatcher ok
MI4IOS6_STAGE33_XNU high root service table ok
MI4IOS6_STAGE33_XNU high root phase-service dependencies ok
MI4IOS6_STAGE33_XNU high root phase dispatcher ok
MI4IOS6_STAGE33_XNU high root bootstrap registry ok
MI4IOS6_STAGE33_XNU high root phase table ok
MI4IOS6_STAGE33_XNU high init sequence complete
MI4IOS6_STAGE33_XNU high root boot policy ok
MI4IOS6_STAGE33_XNU high root bootstrap manifest ok
MI4IOS6_STAGE33_XNU high root launch contract ok
MI4IOS6_STAGE33_XNU high root startup boundary ok
MI4IOS6_STAGE33_XNU high root startup routine ok
MI4IOS6_STAGE33_XNU high root startup handoff ok
MI4IOS6_STAGE33_XNU high virtual startup_entry ok
MI4IOS6_STAGE33_XNU high startup_entry kernel callout table ok
MI4IOS6_STAGE33_XNU high startup_entry kernel context ok
MI4IOS6_STAGE33_XNU high root startup entry ok
MI4IOS6_STAGE33_XNU high root kernel callout table ok
MI4IOS6_STAGE33_XNU high root kernel context ok
MI4IOS6_STAGE33_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE33_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE33_XNU high_bootstrap_init_status=0x33000001
```

Kernel context markers:

```text
MI4IOS6_STAGE33_XNU high_kernel_context_version=0x00000001
MI4IOS6_STAGE33_XNU high_kernel_context_size=0x00000038
MI4IOS6_STAGE33_XNU high_kernel_context_required_mask=0x0000003f
MI4IOS6_STAGE33_XNU high_kernel_context_satisfied_mask=0x0000003f
MI4IOS6_STAGE33_XNU high_kernel_context_checksum=0x012506ba
MI4IOS6_STAGE33_XNU high_kernel_context_status=0x33000001
MI4IOS6_STAGE33_XNU high_context_boot_args_virt=0xc002c000
MI4IOS6_STAGE33_XNU high_context_dt_virt=0xc002c140
MI4IOS6_STAGE33_XNU high_context_platform_status=0x33000001
MI4IOS6_STAGE33_XNU high_context_platform_consistency=0x0000000f
MI4IOS6_STAGE33_XNU high_context_timebase_freq=0x0124f800
MI4IOS6_STAGE33_XNU high_context_interrupt_mask=0x0000000f
MI4IOS6_STAGE33_XNU high_context_callout_status=0x33000001
MI4IOS6_STAGE33_XNU high_context_callout_mask=0x0000000f
MI4IOS6_STAGE33_XNU high_context_root_steps=0x0001fff3
MI4IOS6_STAGE33_XNU high_context_satisfied_mask=0x0000003f
MI4IOS6_STAGE33_XNU high_context_checksum=0x012506ba
MI4IOS6_STAGE33_XNU high_context_status=0x33000001
MI4IOS6_STAGE33_XNU high_root_steps=0x000fffff
MI4IOS6_STAGE33_XNU high_root_status=0x33000001
MI4IOS6_STAGE33_XNU high_bootstrap_status=0x33000001
MI4IOS6_STAGE33_XNU high_bootstrap_checksum=0x267ac9c6
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_result=0x33000001
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_expected_checksum=0x267ac9c6
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_magic_id=0x33003300
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_root_steps_id=0x000fffff
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_kernel_context_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_kernel_context_checksum_id=0x012506ba
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_kernel_context_status_id=0x33000001
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_context_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_context_checksum_id=0x012506ba
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_context_status_id=0x33000001
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_status_alias=0x33000001
MI4IOS6_STAGE33_XNU mmu_high_bootstrap_checksum_alias=0x267ac9c6
MI4IOS6_STAGE33_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the context-gated root path:

```text
MI4IOS6_STAGE33_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the context-gated root path:

```text
MI4IOS6_STAGE33_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE33_XNU kernel_entry ok
MI4IOS6_STAGE33 kernel_entry returned success
MI4IOS6_STAGE33 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage33 turns the callout result into an explicit context object that later stages can consume. This is still an XNU-adjacent kernel skeleton, but the path now has a realistic sequence of typed handoffs:

1. launch contract,
2. startup boundary,
3. startup routine,
4. startup entry,
5. kernel callout table,
6. kernel-start context.

The next useful step is to let this context drive a minimal VM/bootstrap plan object before scheduler and Mach-like initialization stubs are introduced.

## Success criteria — met

1. bootloader accepted `stage33-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. context boot args pointer matched (`0xc002c000`): yes
7. context DT pointer matched (`0xc002c140`): yes
8. context platform status/consistency matched (`0x33000001` / `0x0000000f`): yes
9. context timebase matched (`0x0124f800`): yes
10. context interrupt mask matched (`0x0000000f`): yes
11. context callout status/mask matched (`0x33000001` / `0x0000000f`): yes
12. context satisfied mask was complete (`0x0000003f`): yes
13. context checksum matched (`0x012506ba`): yes
14. context status was `0x33000001`: yes
15. full root step mask was complete (`0x000fffff`): yes
16. high root returned status `0x33000001`: yes
17. checksum matched through identity and alias views: yes
18. SGI and timer IRQ paths still worked after high root: yes
19. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage34 can make the kernel-start context produce a first minimal VM bootstrap plan object:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context checks,
- add a versioned VM plan object with low/high alias base, L1 table, memory base/size, context status, and MMU/cache policy facts,
- validate VM plan checksum/status through identity and high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
