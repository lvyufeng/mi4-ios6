# Stage77 — Stage-owned `_start` / `arm_init`-shaped entry stub

Stage77 continues the Method-C live bring-up path after Stage76.  Stage76 proved a controlled Level-0 live call into one Stage-owned XNU-like C function.  Stage77 moves to Level 1: a Stage-owned `_start` / `arm_init`-shaped entry path that receives `boot_args *`, calls an `arm_init`-shaped C stub, validates the handoff, records a result ABI, and returns to the loader.

This is still not a public XNU boot.  Stage77 does not enter public XNU `_start` or `arm_init`, does not execute public pexpert/pmap/IOKit runtime objects, does not execute the generated Mach-O fixture, and does not install live pmap tables.

## New sources

```text
stage77/xnu_entry_start.S
stage77/xnu_entry_stub.c
```

Main symbols:

```c
uint32_t stage77_xnu_start_stub(struct boot_args *args,
                                struct stage77_xnu_entry_stub_result *result);
uint32_t stage77_arm_init_stub(struct boot_args *args,
                               struct stage77_xnu_entry_stub_result *result);
int stage77_xnu_entry_stub_run(struct boot_args *args);
const struct stage77_xnu_entry_stub_result *stage77_xnu_entry_stub_result(void);
```

## Execution path

The Stage77 target path remains:

```text
start.S -> stage77_main() -> kernel_entry(&boot_args) -> stage77_loader_preflight_run(args)
```

Inside `stage77_loader_preflight_run()`:

1. Retained Mach-O fixture and XNU loader dry-run checks run.
2. Retained workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained client-open / provider-claim / close-readiness dry-run contract must report `0x77000001`.
4. Then Stage77 calls:

```c
stage77_xnu_entry_stub_run(args);
```

5. The wrapper records TTBR0/SCTLR and calls:

```c
stage77_xnu_start_stub(args, &g_stage77_xnu_entry_stub_result);
```

6. The assembly stub marks entry and performs a real branch-and-link to:

```c
stage77_arm_init_stub(args, result);
```

7. The C stub validates `boot_args`, logs two lines, fills the result object, and returns to the assembly stub and then to the loader.

## Result ABI

Magic:

```text
0x58535442 /* 'XSTB' */
```

Required mask:

```text
0x00000fff
```

Satisfied bits:

```text
0x00000001 start stub called
0x00000002 start stub entered
0x00000004 arm_init stub called
0x00000008 arm_init stub returned
0x00000010 return status OK
0x00000020 boot_args valid
0x00000040 device tree valid
0x00000080 magic OK
0x00000100 output OK
0x00000200 TTBR0/SCTLR unchanged
0x00000400 no exception observed
0x00000800 safety boundary preserved
```

Key hardware markers:

```text
stage77_xnu_entry_stub_status=0x77000001
stage77_xnu_entry_stub_satisfied_mask=0x00000fff
stage77_xnu_entry_stub_failure_mask=0x00000000
stage77_xnu_entry_stub_magic=0x58535442
stage77_xnu_entry_stub_start_called=0x00000001
stage77_xnu_entry_stub_start_entered=0x00000001
stage77_xnu_entry_stub_arm_init_called=0x00000001
stage77_xnu_entry_stub_arm_init_returned=0x00000001
stage77_xnu_entry_stub_boot_args_valid=0x00000001
stage77_xnu_entry_stub_device_tree_valid=0x00000001
stage77_xnu_entry_stub_mmu_unchanged=0x00000001
stage77_xnu_entry_stub_no_exception=0x00000001
stage77_xnu_entry_stub_safety_boundary_preserved=0x00000001
loader_xnu_entry_stub_status=0x77000001
loader_xnu_entry_stub_status_rollup=0x77000001
loader_satisfied_mask=0xffffffff
loader_status=0x77000001
kernel_entry returned success
```

## Boundary

Stage77 executes only Stage-owned entry-stub code. It still does **not**:

- execute public XNU `_start` / `arm_init`,
- execute public XNU object-subset proof objects,
- execute public pexpert/platform runtime,
- execute public ARM VM/pmap runtime,
- execute public IOKit runtime,
- execute the generated Mach-O fixture,
- install live XNU/pmap tables,
- invalidate TLBs for a pmap install,
- change cache policy,
- perform persistent writes.

The active Android boot-image command line uses compact Stage77 markers:

```text
xnu-entry-stub arm-init-stub no-pub-start no-pub-arm-init
```

## Local validation

Local validation passed:

```text
stage77/build.sh: success
stage77-qcdt.img dt_size=2521088 (0x267800)
stage77.elf undefined symbols: none
stage77-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 251 bytes including NUL
Android boot-image cmdlines: 1523 bytes including NUL
stage77_xnu_start_stub symbol: 0x00042200
stage77_arm_init_stub symbol: 0x00042258
stage77_xnu_entry_stub_run symbol: 0x000423c0
disassembly contains: bl stage77_xnu_start_stub
disassembly contains: bl stage77_arm_init_stub
git diff --check: clean
```

## Hardware validation

Hardware validation passed and remained non-persistent:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage77/stage77-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage77-entry-stub-last_kmsg.txt
```

Captured log:

```text
/tmp/cancro-stage77-entry-stub-last_kmsg.txt
248873 bytes
```

Confirmed hardware markers:

```text
MI4IOS6_STAGE77_XNU stage77_arm_init_stub: entered Stage-owned _start/arm_init-shaped path
MI4IOS6_STAGE77_XNU stage77_arm_init_stub: returning to stage loader
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_status=0x77000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_satisfied_mask=0x00000fff
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_required_mask=0x00000fff
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_start_function_ptr=0x00042200
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_arm_init_function_ptr=0x00042258
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_result_ptr=0x000d57c0
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_magic=0x58535442
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_start_called=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_start_entered=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_arm_init_called=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_arm_init_returned=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_return_value=0x77000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_boot_args_ptr=0x000d5878
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_boot_args_rev_ver=0x00020002
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_device_tree_ptr=0x000d59b8
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_device_tree_length=0x000070f0
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_boot_args_valid=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_device_tree_valid=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_output_lines=0x00000002
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_output_bytes=0x00000078
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_ttbr0_before=0x000a8000
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_ttbr0_after=0x000a8000
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_sctlr_before=0x00c5487b
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_sctlr_after=0x00c5487b
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_mmu_unchanged=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_no_exception=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_safety_boundary_preserved=0x00000001
MI4IOS6_STAGE77_XNU stage77_xnu_entry_stub_checksum=0x2952cf1d
MI4IOS6_STAGE77_XNU loader_xnu_entry_stub_status=0x77000001
MI4IOS6_STAGE77_XNU loader_xnu_entry_stub_satisfied_mask=0x00000fff
MI4IOS6_STAGE77_XNU loader_xnu_entry_stub_failure_mask=0x00000000
MI4IOS6_STAGE77_XNU loader_xnu_entry_stub_checksum=0x2952cf1d
MI4IOS6_STAGE77_XNU loader_xnu_entry_stub_status_rollup=0x77000001
MI4IOS6_STAGE77_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE77_XNU loader_status=0x77000001
MI4IOS6_STAGE77 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change was performed.

## Next direction

Stage78 should move from an entry-shaped returned stub toward the next Method-C level: a still Stage-owned early pmap/platform-init micro-sequence that begins exercising pmap/platform handoff structure without installing public XNU tables or executing public pmap/pexpert/IOKit runtime.
