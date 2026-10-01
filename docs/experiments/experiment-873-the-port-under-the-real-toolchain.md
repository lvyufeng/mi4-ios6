# 873 — THE PORT UNDER THE REAL TOOLCHAIN: 38/38 ON THE APPLE TARGET TOO

**A host-side measurement: no device, no boot, no arm, no park, no switch.** Nothing outside `tools/` and
this document is written. The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE
OPERATOR'S; the goal is NOT met.**

In one paragraph: 871 measured the HFS+ port at 38/38 compile and a zero link gap — but under *this probe's*
invocation, a **kernel-like ELF** target (`arm_target.sh`). The kernel this project actually builds the
manifest with is compiled by `tools/build_xnu_arm_macho.sh --compile` under a **different** invocation:
`--target=armv7-apple-darwin -mabi=aapcs -DMACH_KERNEL=1`. A port that compiles under one and not the other
does not build. This document adds that invocation as a **third block** in the probe and measures it:
**38/38 on the apple target too.** The port's compile half is real, not an artifact of the harness.

## 1. The two invocations, and why both matter

| | this probe's ELF block | the real build (`build_xnu_arm_macho.sh --compile`) |
| --- | --- | --- |
| target | `arm_target.sh`'s triple + `-mcpu=cortex-a15` | `--target=armv7-apple-darwin` |
| ABI | (none) | **`-mabi=aapcs`** |
| `MACH_KERNEL` | per-component (BSD files do not get it) | global `-DMACH_KERNEL=1` |
| output | ELF `.o` | Mach-O `.o` |

`-mabi=aapcs` is load-bearing, not cosmetic: without it XNU's own `IF_DATA_REQUIRE_ALIGNED_64` assertion
(`bsd/net/dlil.c:1419`) fails, because clang 14's plain Darwin ARM ABI aligns `long long` to 4 and XNU
asserts 8 (`build_xnu_arm_macho.sh`'s comment measures exactly this). And `MACH_KERNEL=1` global is the one
macro the per-component code in this project is careful about (`kern/misc_protos.h`'s `ffs` vs
`bsd/libkern/libkern.h`'s) — so running the port under the global set is a genuinely different compile, not a
re-spelling of the same one.

## 2. The measurement

The probe gains a third block, and recompiles all 38 files under the apple invocation:

```
CONFIG_PROTECT=0                                  ok=38  fail=0  (ELF target)
CONFIG_PROTECT=1                                  ok=34  fail=4  (ELF target)
CONFIG_PROTECT=0, APPLE TARGET                    ok=38  fail=0  (armv7-apple-darwin -mabi=aapcs)
```

**38/38 under both.** The `CONFIG_PROTECT=1` failures are unchanged and are the same four `cp_*` files.

## 3. What this does and does not close

- **It closes the last way 871's number could have been a harness artifact.** "38/38 and 0 external symbols"
  now holds under the invocation the kernel is *actually* built with, so the compile half of the filesystem
  clause is measured against the real toolchain — not a look-alike.
- **It does not build anything into the kernel.** The port is compiled in a `/tmp` sandbox from 2050's tree;
  `out/xnu_arm_manifest.txt` (733 files) does not name a single HFS file. A build still needs the sources
  staged, the `hfs` option set, the `files` rows moved, and the root row added (870) by the carrier 872 chose.
- **It does not touch the medium.** Even a wired-in, mounted HFS+ needs the eMMC driver clause (867).

## 4. What changed, in the tool

`tools/hfs_port_probe.sh`:

- Two target arrays: `TARGET_ELF` (unchanged) and `TARGET_MACHO` (`armv7-apple-darwin -mabi=aapcs`), with the
  reason each exists in a comment.
- `run_configuration` takes a third argument (`elf`/`macho`) selecting the target, the `MACH_KERNEL=1` global,
  and a separate object directory (`obj-macho/`).
- A third block runs the port under the apple invocation; the summary text is corrected.

## 5. What this changes, and what it does not

- **The park backlog is not touched.** Rungs 57/58/59/60 stay parked and unpressed.
- **THE GOAL IS NOT MET.** No HFS+ volume is mounted; the medium has never been read by the mounting OS; the
  eMMC driver clause is still open.

## 6. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press, no kernel build.** The probe is
host-only and writes nothing in the repository outside `tools/` and this document; `make check` is exit 0.