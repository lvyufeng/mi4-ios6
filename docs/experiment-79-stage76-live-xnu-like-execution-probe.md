# Experiment 79: Stage76 live XNU-like execution probe

## Summary

Stage76 pivots from the Stage56-Stage75 no-execution dry-run ladder to a controlled Level-0 live execution probe.

The user explicitly approved breaking the previous `no-xnu-jump` / `no-public-xnu-exec` direction for the next step toward making XNU run. Stage76 therefore keeps the Stage75 IOKit client-open / provider-claim / close-readiness dry-run prerequisite chain, but then executes a new Stage-owned XNU-like probe function and returns to the loader.

This is not a full XNU boot. It does not enter public XNU `_start` or `arm_init`, does not execute public XNU pexpert/pmap/IOKit objects, and does not install live pmap tables. It proves one concrete thing that the previous dry-run stages did not prove: a kernel-shaped function can actually be called, run, write ram_console output, and return on the MSM8974 boot path.

## New source

```text
stage76/xnu_live_probe.c
```

Main symbols:

```c
uint32_t stage76_xnu_minimal_live_probe(struct stage76_xnu_live_execution_probe_result *result);
int stage76_xnu_live_execution_probe_run(void);
const struct stage76_xnu_live_execution_probe_result *stage76_xnu_live_execution_probe_result(void);
```

The minimal probe is marked `noinline` so the final ARM disassembly contains a real branch-and-link call into the probe body.

## Execution path

The Stage76 target path remains:

```text
start.S -> stage76_main() -> kernel_entry(&boot_args) -> stage76_loader_preflight_run(args)
```

Inside `stage76_loader_preflight_run()`:

1. Existing Mach-O fixture checks run.
2. Existing workspace, compile-graph, object-subset, link-proof, bootstrap, pmap dry-run, pexpert, and IOKit dry-run contracts run.
3. The retained Stage75-derived client-open / provider-claim / close-readiness dry-run contract must report `0x76000001`.
4. Then Stage76 calls:

```c
stage76_xnu_live_execution_probe_run();
```

5. The wrapper calls:

```c
stage76_xnu_minimal_live_probe(&g_stage76_xnu_live_probe_result);
```

6. The live probe logs two lines, fills the result object, and returns to the loader.

## Probe result ABI

```c
struct stage76_xnu_live_execution_probe_result {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t probe_function_ptr;
    uint32_t probe_result_ptr;
    uint32_t probe_magic_expected;
    uint32_t probe_magic;
    uint32_t probe_called;
    uint32_t probe_entered;
    uint32_t probe_returned;
    uint32_t probe_return_value;
    uint32_t probe_status_from_probe;
    uint32_t probe_output_lines;
    uint32_t probe_output_bytes;
    uint32_t enter_count_before;
    uint32_t enter_count_after;
    uint32_t ttbr0_before;
    uint32_t ttbr0_after;
    uint32_t sctlr_before;
    uint32_t sctlr_after;
    uint32_t mmu_state_unchanged;
    uint32_t no_exception_observed;
    uint32_t checksum;
};
```

Magic:

```text
0x584c5052 /* 'XLPR' */
```

Required mask:

```text
0x000000ff
```

Satisfied bits:

```text
0x00000001 called
0x00000002 entered
0x00000004 returned
0x00000008 return status OK
0x00000010 magic OK
0x00000020 output OK
0x00000040 TTBR0/SCTLR unchanged
0x00000080 no exception observed
```

## Expected hardware markers

```text
stage76_xnu_minimal_live_probe: first live XNU-like code execution entered
stage76_xnu_minimal_live_probe: returning to stage loader
stage76_xnu_live_probe_status=0x76000001
stage76_xnu_live_probe_satisfied_mask=0x000000ff
stage76_xnu_live_probe_failure_mask=0x00000000
stage76_xnu_live_probe_required_mask=0x000000ff
stage76_xnu_live_probe_return_value=0x76000001
stage76_xnu_live_probe_magic=0x584c5052
stage76_xnu_live_probe_output_lines=0x00000002
stage76_xnu_live_probe_mmu_unchanged=0x00000001
stage76_xnu_live_probe_no_exception=0x00000001
loader_xnu_live_execution_probe_status=0x76000001
loader_xnu_live_execution_probe_status_rollup=0x76000001
loader_satisfied_mask=0xffffffff
loader_status=0x76000001
kernel_entry returned success
```

## Local validation

Local validation passed after implementation:

```text
stage76/build.sh: success
stage76-qcdt.img dt_size=2521088 (0x267800)
stage76.elf undefined symbols: none
stage76-xnu-link.elf undefined symbols: none
fixed CommandLine[256] strings: 237 bytes including NUL
Android boot-image cmdlines: 1520 bytes including NUL
stale Stage75 marker scan under stage76/: clean
git diff --check: clean
```

Live probe symbols are present:

```text
00042174 T stage76_xnu_minimal_live_probe
00042218 T stage76_xnu_live_execution_probe_run
0004258c T stage76_xnu_live_execution_probe_result
```

Disassembly confirms a real call:

```text
bl stage76_xnu_minimal_live_probe
```

## Boundary update

Stage76 intentionally removes these boot-image command-line claims from the active Stage76 image:

```text
no-xnu-jump
no-public-xnu-exec
```

and replaces them with:

```text
xnu-live-probe
live-stage-owned-xnu-probe
no-xnu-start
```

Interpretation:

- Stage76 does execute a Stage-owned XNU-like probe function.
- Stage76 still does not enter public XNU `_start` / `arm_init`.
- Stage76 still does not execute public XNU pexpert/pmap/IOKit proof objects.
- Stage76 still does not execute the generated Mach-O fixture.
- Stage76 still does not install live pmap tables, invalidate TLBs, change cache policy, or persistently write storage.

The retained dry-run contracts still use the old no-execution safety facts as a pre-probe prerequisite chain. The live probe is deliberately after that chain, as the first controlled escape from pure dry-run verification.

## Hardware validation result

Hardware validation passed and remained non-persistent:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage76/stage76-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage76-live-probe-last_kmsg.txt
```

Captured log:

```text
/tmp/cancro-stage76-live-probe-last_kmsg.txt
248306 bytes
```

Confirmed hardware markers:

```text
MI4IOS6_STAGE76_XNU stage76_xnu_minimal_live_probe: first live XNU-like code execution entered
MI4IOS6_STAGE76_XNU stage76_xnu_minimal_live_probe: returning to stage loader
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_status=0x76000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_satisfied_mask=0x000000ff
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_failure_mask=0x00000000
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_required_mask=0x000000ff
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_function_ptr=0x00042174
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_result_ptr=0x000d57c4
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_called=0x00000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_entered=0x00000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_returned=0x00000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_return_value=0x76000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_magic=0x584c5052
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_output_lines=0x00000002
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_output_bytes=0x00000085
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_ttbr0_before=0x000a8000
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_ttbr0_after=0x000a8000
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_sctlr_before=0x00c5487b
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_sctlr_after=0x00c5487b
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_mmu_unchanged=0x00000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_no_exception=0x00000001
MI4IOS6_STAGE76_XNU stage76_xnu_live_probe_checksum=0x7609765f
MI4IOS6_STAGE76_XNU loader_xnu_live_execution_probe_status=0x76000001
MI4IOS6_STAGE76_XNU loader_xnu_live_execution_probe_satisfied_mask=0x000000ff
MI4IOS6_STAGE76_XNU loader_xnu_live_execution_probe_failure_mask=0x00000000
MI4IOS6_STAGE76_XNU loader_xnu_live_execution_probe_checksum=0x7609765f
MI4IOS6_STAGE76_XNU loader_xnu_live_execution_probe_status_rollup=0x76000001
MI4IOS6_STAGE76_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE76_XNU loader_status=0x76000001
MI4IOS6_STAGE76 kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change was performed.

## Next stage direction

If Stage76 validates on hardware, Stage77 should not return to IOKit release/unclaim dry-runs. It should continue the method-C bring-up path:

```text
Stage77: minimal _start/arm_init-shaped entry stub
```

That stage should still avoid full pmap/IOKit runtime, but it should exercise a more realistic early-XNU entry calling convention than Stage76's single returned function call.
