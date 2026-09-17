# Experiment 131 — the build defined `MONOTONIC` and the manifest never built the file

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/device_table.py` (new), `tools/xnu_config/list_sources.py`,
`tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-130) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 402 of 420 | **405 of 423** |
| `RELEASE` | 592 of 609 | **595 of 612** |
| undefined after linking | 454 | **444** |

No line of XNU's code was changed.

## The defect: one value, two definitions, and nothing compared them

`osfmk/conf/files:296` is

```
osfmk/kern/kern_monotonic.c		optional monotonic
```

and `tools/build_xnu_arm_kernel.sh` defines `-DMONOTONIC=1` **by hand** — it is one of the flags the
ARM layer needs, and experiment-112 established it as the one genuinely-absent per-SoC value that
`MONOTONIC_BASE` would have supplied.

`list_sources.py` matched `optional <cond>` against `config/MASTER`'s expanded options only.
`monotonic` is not one of those. So:

- every file that *uses* `MONOTONIC` was compiled with it defined = 1;
- `kern_monotonic.c`, the only file that *implements* it, was **excluded from the manifest**.

Ten undefined symbols at link time, and the mechanism is the seventh instance of this project's
recurring defect — with a new one again: **two halves of the build reading two different sources of
truth for the same condition**, and nothing comparing them.

## Where the condition can come from, which is the thing worth writing down

A `*/conf/files` condition has three possible origins in Apple's build, and this project only knew
about the first:

1. **an option** — `config/MASTER`, expanded by doconf. `list_sources.py` reads these.
2. **an `OPTIONS/` line** — `SETUP/config/mkheaders.c` writes `<COND>.h` with a `0` or a `1`, and the
   same name is what `optional` matches against. `gen_option_headers.py` reproduces it; its values
   agree with (1), so it needed no separate handling.
3. **a `device` / `pseudo-device` declaration** — becomes `#define NLOOP 1` in a generated header.
   **4570 publishes no `device` or `pseudo-device` lines at all**: `grep -c '^pseudo-device'
   */conf/files` is 0 in every component. So this third of the configuration is simply absent.

Of the 139 distinct conditions across the three components' file lists, **89 are not in `RELEASE`**:
60 of those have an `OPTIONS/` line (so they are handled and correctly off), and **29 have nothing** —
`loop`, `pty`, `ptmx`, `monotonic`, `nullfs`, `mockfs`, `skywalk`, `stf`, `pgo`, and eleven
`config_*` names that are the per-SoC layer.

## The fix, and its shape

`tools/xnu_config/device_table.py` is a table of `condition → 0|1` for the conditions this project
chooses, and **both** the manifest and the compile flags read it:

```
monotonic	1	the build script defines it
xpr_debug	0	the build script defines it
```

- `list_sources.py` takes it via `--device-table` and treats `1` as satisfied.
- `build_xnu_arm_kernel.sh` **runs the tool as a gate before every build.** The tool compares the
  table against the script's own hand-set `-D` flags — reading the *value*, because `-DXPR_DEBUG=0`
  mentions the name and means the condition is off — and exits 1 on a disagreement in either
  direction. Perturbation-tested: setting `monotonic` to 0 in the table makes the build stop with

  ```
  DISAGREEMENT monotonic: the build script defines it =1, table says 0
  ```

  A first version of the gate wrote the table before checking it, which overwrote the file under
  test; the check has to come first and the comment says so.

`pty` and `ptmx` are deliberately **not** in the table. `NPTY 0` was measured not to compile
(experiment-130 — `conf.c` then fails on `ptsselect`, which exists nowhere in the tree), so they need
a real device table rather than another one-line value, and `--unknown` keeps reporting them.

## What is left

11 files and 42 symbols on the boot path, unchanged by this stage — `kern_monotonic.c` was
attributed to *files that fail to compile*, and it compiled on the first try; the ten symbols it
defines were simply absent from the link. That is why the link moved and the work list did not.

## How to reproduce

```bash
./tools/xnu_config/device_table.py --write out/device_table.txt   # 2 conditions
./tools/xnu_config/device_table.py --unknown                      # 89 with no entry, 2 UNRESOLVED
MANIFEST=$PWD/out/xnu_arm_manifest.txt ./tools/xnu_config/list_sources.py RELEASE --write $PWD/out/xnu_arm_manifest.txt
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 595 of 612
./tools/link_xnu_arm.sh                    # 444 undefined
```
