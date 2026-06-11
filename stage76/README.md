# Stage76 — First live XNU-like execution probe

Stage76 pivots away from adding another IOKit dry-run layer and implements the first deliberately live XNU-like code execution probe in the cancro boot-wrapper path.

## What changed

Stage75 proved an IOKit client-open / provider-claim / close-readiness dry-run contract, but it still preserved the old no-execution boundary. Stage76 keeps that validated prerequisite chain and then adds one explicit live probe after the Stage75-derived client-open contract succeeds.

New Stage76 file:

```text
stage76/xnu_live_probe.c
```

New live entry:

```c
uint32_t stage76_xnu_minimal_live_probe(struct stage76_xnu_live_execution_probe_result *result);
```

The loader calls:

```c
stage76_xnu_live_execution_probe_run();
```

which then calls the minimal live probe, records the result, and returns to the stage loader.

## Probe behavior

The probe intentionally does almost nothing:

- increments a volatile entry counter,
- logs `stage76_xnu_minimal_live_probe: first live XNU-like code execution entered`,
- writes the magic `0x584c5052` (`XLPR`),
- writes status `0x76000001`,
- logs `stage76_xnu_minimal_live_probe: returning to stage loader`,
- returns `0x76000001` to the loader.

The wrapper records:

```text
stage76_xnu_live_probe_status
stage76_xnu_live_probe_satisfied_mask
stage76_xnu_live_probe_failure_mask
stage76_xnu_live_probe_required_mask
stage76_xnu_live_probe_function_ptr
stage76_xnu_live_probe_result_ptr
stage76_xnu_live_probe_called
stage76_xnu_live_probe_entered
stage76_xnu_live_probe_returned
stage76_xnu_live_probe_return_value
stage76_xnu_live_probe_magic
stage76_xnu_live_probe_output_lines
stage76_xnu_live_probe_output_bytes
stage76_xnu_live_probe_ttbr0_before
stage76_xnu_live_probe_ttbr0_after
stage76_xnu_live_probe_sctlr_before
stage76_xnu_live_probe_sctlr_after
stage76_xnu_live_probe_mmu_unchanged
stage76_xnu_live_probe_no_exception
stage76_xnu_live_probe_checksum
loader_xnu_live_execution_probe_status
loader_xnu_live_execution_probe_status_rollup
```

Required satisfied mask:

```text
0x000000ff
```

Success status:

```text
0x76000001
```

## Boundary

Stage76 does execute the new Stage-owned XNU-like probe function. It still does **not**:

- execute the public XNU object-subset proof objects,
- execute the generated Mach-O fixture,
- enter XNU `_start` or `arm_init`,
- install live XNU/pmap tables,
- invalidate TLBs for a pmap install,
- change cache policy,
- execute public IOKit or public ARM pmap runtime code,
- execute IOKit match/registry/provider/catalog/attach/start/register/notification/open/claim/close runtime code,
- perform persistent writes.

The old boot-image markers `no-xnu-jump` and `no-public-xnu-exec` are removed from the Stage76 Android boot command line and replaced with:

```text
xnu-live-probe live-stage-owned-xnu-probe no-xnu-start
```

This means Stage76 is no longer a pure no-execution dry-run stage. It is a controlled Level-0 live execution probe.

## Local validation

Local validation passed:

- `stage76/build.sh` succeeds.
- `out/stage76/stage76-qcdt.img` preserves Qualcomm QCDT `dt_size=2521088 (0x267800)`.
- `arm-none-eabi-nm -u out/stage76/stage76.elf` has no undefined symbols.
- `arm-none-eabi-nm -u out/stage76/xnu-link/stage76-xnu-link.elf` has no undefined symbols.
- Fixed public ARM `CommandLine[256]` strings are 237 bytes including NUL.
- Android boot-image cmdlines are 1520 bytes including NUL, under the legacy 1536-byte limit.
- Live probe symbols are present:

```text
stage76_xnu_minimal_live_probe
stage76_xnu_live_execution_probe_run
stage76_xnu_live_execution_probe_result
```

- Disassembly contains a real branch-and-link from the probe wrapper to the minimal probe:

```text
bl stage76_xnu_minimal_live_probe
```

- No stale Stage75 markers were found under `stage76/`.
- `git diff --check` is clean.

## Hardware validation

Hardware validation passed with non-persistent `fastboot boot` only:

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

Confirmed pass markers:

```text
stage76_xnu_minimal_live_probe: first live XNU-like code execution entered
stage76_xnu_minimal_live_probe: returning to stage loader
stage76_xnu_live_probe_status=0x76000001
stage76_xnu_live_probe_satisfied_mask=0x000000ff
stage76_xnu_live_probe_failure_mask=0x00000000
stage76_xnu_live_probe_required_mask=0x000000ff
stage76_xnu_live_probe_function_ptr=0x00042174
stage76_xnu_live_probe_result_ptr=0x000d57c4
stage76_xnu_live_probe_called=0x00000001
stage76_xnu_live_probe_entered=0x00000001
stage76_xnu_live_probe_returned=0x00000001
stage76_xnu_live_probe_return_value=0x76000001
stage76_xnu_live_probe_magic=0x584c5052
stage76_xnu_live_probe_output_lines=0x00000002
stage76_xnu_live_probe_output_bytes=0x00000085
stage76_xnu_live_probe_ttbr0_before=0x000a8000
stage76_xnu_live_probe_ttbr0_after=0x000a8000
stage76_xnu_live_probe_sctlr_before=0x00c5487b
stage76_xnu_live_probe_sctlr_after=0x00c5487b
stage76_xnu_live_probe_mmu_unchanged=0x00000001
stage76_xnu_live_probe_no_exception=0x00000001
stage76_xnu_live_probe_checksum=0x7609765f
loader_xnu_live_execution_probe_status=0x76000001
loader_xnu_live_execution_probe_satisfied_mask=0x000000ff
loader_xnu_live_execution_probe_failure_mask=0x00000000
loader_xnu_live_execution_probe_checksum=0x7609765f
loader_xnu_live_execution_probe_status_rollup=0x76000001
loader_satisfied_mask=0xffffffff
loader_status=0x76000001
kernel_entry returned success
```

No flash, erase, partition write, or persistent bootloader/storage change was performed.

## Next step

If Stage76 validates on hardware, Stage77 should move from one returned stage-owned function call to a slightly more realistic XNU-like startup entry: a minimal `_start`/`arm_init`-shaped stub that still avoids full pmap/IOKit runtime, but starts exercising the real early-kernel calling convention and boot argument handoff.
