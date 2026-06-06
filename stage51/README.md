# Stage51 — minimal public-XNU object subset compile

Stage51 keeps the previous-stage no-XNU runtime safety envelope and adds the first real
public Apple OSS XNU object-subset compile for the Xiaomi Mi 4 cancro / MSM8974
ARMv7 target. It compiles a tiny dependency-light subset from the selected public
Darwin 12 / iOS 6-era baseline, records the compile result in target-side logs,
and still does not link or execute public-XNU code.

## Tracked scaffold

- `targets/cancro.mk` records the declarative cancro/MSM8974 ARMv7 target facts
  and Stage51 object-subset policy.
- `targets/cancro.stage51.objects` records the public-XNU inputs and boundary.
- `xnu_workspace_validate.sh` validates ignored public checkouts read-only and
  emits generated workspace status under `out/stage51/`.
- `xnu_object_subset_compile.sh` compiles the minimal public-XNU object subset
  into ignored `out/stage51/xnu-objects/`.
- `shims/` contains Stage51-owned compatibility headers needed to compile the
  selected public sources without mutating `external/`.
- `xnu_object_shims.c` provides compile-only shim support symbols such as
  `PE_boot_args`, `kalloc`, `kfree`, and `IODTGetDefault`.
- `xnu_workspace.*` and `xnu_object_subset.*` expose target-side status ABIs used
  by the Stage51 loader preflight.

## Selected public baseline

Stage51 expects the ignored checkout `external/xnu-upstream` to be detached at
public Apple OSS tag `xnu-2050.22.13`, commit
`cc8a9b0ce917bb7115f5c97a78b38db871557db0`, with
`config/MasterVersion` equal to `12.3.0`.

The public 2050 tree is intentionally treated as incomplete for a full iOS ARMv7
XNU build. Stage51 uses Stage51-owned shims to compile only selected public
pexpert/device-tree/boot-argument-adjacent files and uses the ignored public
`external/xnu-4570.1.46` checkout only as a later ARM implementation reference.

## Public-XNU object subset

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
```

Compile-only support object:

```text
stage51/xnu_object_shims.c
```

Successful compile status:

```text
stage51_xnu_object_subset_status=0x51000001
stage51_xnu_object_subset_satisfied_mask=0x00003fff
stage51_xnu_object_subset_failure_mask=0x00000000
stage51_xnu_object_count=0x00000003
stage51_xnu_object_device_tree_sha32=0xc507eca7
stage51_xnu_object_bootargs_sha32=0x2f9e47cb
```

## Safety contract

- Non-persistent `fastboot boot` only for hardware validation.
- No partition flash/erase/write and no bootloader changes.
- No full public `mach_kernel` build attempt.
- No public-XNU Mach-O link.
- No public-XNU object execution.
- No Mach-O fixture execution and no XNU `_start` / `arm_init` jump.
- No writes to proposed XNU physical load or TTE workspace addresses.
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- Inherited controlled TTBR0 round-trip remains Stage-owned, restored, and cache-preserving.
- Generated validation/build outputs stay ignored under `out/stage51/`.
