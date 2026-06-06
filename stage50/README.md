# Stage50 — public-XNU workspace and cancro target scaffold

Stage50 keeps the previous-stage no-XNU runtime safety envelope and adds a tracked
public-XNU workspace scaffold for the Xiaomi Mi 4 cancro / MSM8974 ARMv7 target.
It validates source references and records the next Stage51 object-subset plan;
it does not compile a full public `mach_kernel` and it does not execute public
XNU code.

## Tracked scaffold

- `targets/cancro.mk` records the declarative cancro/MSM8974 ARMv7 target facts.
- `targets/cancro.stage51.objects` records candidate public-XNU inputs for the
  next minimal object-subset compile stage.
- `xnu_workspace_validate.sh` validates ignored public checkouts read-only and
  emits generated status under `out/stage50/`.
- `xnu_workspace.h` / `xnu_workspace.c` expose the target-side status ABI used by
  the Stage50 loader preflight.

## Selected public baseline

Stage50 expects the ignored checkout `external/xnu-upstream` to be detached at
public Apple OSS tag `xnu-2050.22.13`, commit
`cc8a9b0ce917bb7115f5c97a78b38db871557db0`, with
`config/MasterVersion` equal to `12.3.0`.

The public 2050 tree is intentionally treated as incomplete for a full iOS ARMv7
XNU build. Stage50 records that ARM gap and uses the ignored public
`external/xnu-4570.1.46` checkout only as a later ARM implementation reference.

## Safety contract

- Non-persistent `fastboot boot` only for hardware validation.
- No partition flash/erase/write and no bootloader changes.
- No full public `mach_kernel` build attempt.
- No public-XNU object execution.
- No Mach-O fixture execution and no XNU `_start` / `arm_init` jump.
- No writes to proposed XNU physical load or TTE workspace addresses.
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- Inherited controlled TTBR0 round-trip remains Stage-owned, restored, and cache-preserving.
- Generated validation/build outputs stay ignored under `out/stage50/`.
