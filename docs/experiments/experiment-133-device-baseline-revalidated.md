# Experiment 133 — the device line, re-validated after fourteen stages of host-side work

Date: 2026-09-17
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/macho_fixture.c` regenerated (it had been stale since June)

**Result: green, and the device returned to Android 10 unattended.**

This session spent fourteen stages (experiments 119–132) entirely on the host side — the XNU
compile graph, its generated headers, its manifests, and the first real link. None of that touches
the Stage90 payload, so none of it was a reason to expect a device regression — and none of it was
a reason to assume there was not one. The project's own rule is that nothing is established without
a hardware run, and the device-side line had not been exercised in this session at all.

## What was run

```
cd stages/stage90
./build.sh                      # the gate refused the previous image as stale, correctly
./preflight_boot_check.sh       # all checks passed, default switch set
./run_and_capture.sh            # gate + fastboot boot + wait + capture, exit 0
```

The gate refused the first attempt — `macho_fixture.c` was newer than the image — which is the
stale-image check doing its job rather than being worked around. After `./build.sh`:

| | |
| --- | --- |
| image | `stage90-qcdt.img`, 3 024 896 bytes, sha256 `10ca78d6…09ff` |
| switch set | `HARD_SKIP`, `SO_ONLY`, caches off, dead-man armed, **HW watchdog armed**, every Phase 2/3 probe off — the default |
| boot | `fastboot boot` — nothing flashed |
| device | came back to Android 10, unattended |

## And the gate's refusal was a real finding, not an inconvenience

`preflight_boot_check.sh` refused the run with

```
== image freshness ==
source newer than the image:
  macho_fixture.c
REFUSING: the image is stale - run ./build.sh, then re-run this gate
```

That is not a timestamp quirk. `stages/stage90/macho_fixture.c` is a **tracked, generated file** —
`tools/mkmacho_fixture.py` writes it, and the build embeds the built ELF's size and checksum into it
(the comment carries `elf_bytes=…` and `elf_sha32=…` alongside its own sha256). It was last committed
in `4d2c938` (the June repository reorganization), and the payload has changed thirteen stages since,
so the checked-in copy had been stale for a long time.

Regenerating it is deterministic — verified by building twice and diffing, which produced no change —
so this is a one-time catch-up rather than a moving target. It is committed with this experiment.
Without the freshness check the run would have proceeded against an image whose source no longer
matches what is checked in, and the mismatch would have been invisible.

## Evidence, from the captured log

```
MI4IOS6_STAGE90_XNU loader_xnu_pe_init_platform_false_failure_mask=0x00000000
MI4IOS6_STAGE90_XNU loader_xnu_arm_init_post_pe_bootstrap_failure_mask=0x00000000
MI4IOS6_STAGE90_XNU loader_xnu_arm_vm_init_full_pmap_failure_mask=0x00000000
MI4IOS6_STAGE90_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE90_XNU loader_status=0x90000001
MI4IOS6_STAGE90_XNU kernel_entry ok
MI4IOS6_STAGE90 kernel_entry returned success
```

The whole `arm_init` ladder passes with an all-ones satisfied mask and a zero failure mask — early
pmap, `PE_init_platform(FALSE)`, the post-PE bootstrap and `arm_vm_init` with the candidate-L1
install/verify/restore. Then the two things that make the payload worth having:

```
stage90_xnu_arm_vm_init_high_va_code_exec_fn_result_correct=0x00000001
stage90_xnu_arm_vm_init_high_va_code_exec_persistent_write_attempted=0x00000000
stage90_xnu_arm_vm_init_high_va_irq_handler_irq_count_before=0x00000001
stage90_xnu_arm_vm_init_high_va_irq_handler_irq_count_after=0x00000003
stage90_xnu_entry_stub_no_exception=0x00000001
```

A real function executed through the candidate L1 at high VA and returned the right value; timer
IRQs were delivered at high VA (`1 → 3`); no exception was taken; and **no persistent write was
attempted** — the property the whole non-persistent method rests on, asserted in the payload rather
than trusted.

And the recovery, which is the reason a hang costs nothing any more:

```
MI4IOS6_STAGE90 platform_reboot entered
MI4IOS6_STAGE90_XNU stage90 hw_watchdog: forcing immediate bite (independent of PS_HOLD)
MI4IOS6_STAGE90_XNU hw_watchdog_bite_sts_before=0x00005864
```

`platform_reboot` wrote the restart reason and PS_HOLD, then forced the SoC watchdog to bite
immediately so the reboot does not depend on the PMIC — and the phone came back with no power press.

## What this does and does not establish

**Does:** the device-side baseline is intact after the host-side work, the safety gate refuses a
stale image, and both recovery nets still work end to end on hardware.

**Does not:** it is not progress toward "XNU loads and runs". The host-side build is at 599 of 615
files compiling and **449 undefined symbols** at link — there is no complete XNU image to boot, so
no device run this session could have been one. The device line and the host line are still
separate, and joining them is the next piece of work rather than a check.

## What the next device-side step would be, concretely

The payload already drives the platform shim through XNU's own interface (experiment-104), and
`xnu_msm8974_shim.c` is the code that will have to replace `pe_arm_init_interrupts` — the function
the spec establishes cannot be configured into working on MSM8974. Extending that shim toward the
rest of `pe_arm_init_interrupts` is device-side work that advances the goal, and it is the piece
that does not depend on the host-side link completing.
