# Experiment 128 — the manifest was missing a whole component, and it cost 200 symbols

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/list_sources.py`

| Configuration | before (experiment-127) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 400 of 419 | **401 of 420** |
| `RELEASE` | 569 of 587 | **591 of 609** |
| undefined after linking | 699 | **499** |
| …on the boot path | 660 | **460** |

No line of XNU's code was changed. This is the largest single movement since the per-component
defines, and it is not a compile fix — it is a component that was never being compiled.

## The finding: `security` was not in the component list

`list_sources.py` had

```python
DEFAULT_COMPONENTS = ["osfmk", "bsd", "libkern", "iokit", "pexpert"]
```

against Apple's own list, `makedefs/MakeInc.def:46`:

```make
COMPONENT_LIST = osfmk bsd libkern iokit pexpert libsa security san
```

`libsa` is legitimately excluded — it is bootloader-context code the kernel does not link, and its
`<types.h>` is reached as a *header*, which is what `gen_libsa_export.sh` is for. **`security` and
`san` were excluded for no stated reason, and the cost was structural:**

`CONFIG_MACF=1` is set in the `RELEASE` configuration, `security/mac_*.c` are `optional config_macf`
in `security/conf/files`, and the kernel's own sources call into them. So the manifest was selecting
a configuration whose files could never define the symbols the rest of the kernel referenced —
**about 180 of the 699 undefined symbols** came from `security/` alone:

```
81  security/mac_vfs.c          26  security/mac_process.c     15  security/mac_base.c
14  security/mac_mach.c         13  security/mac_socket.c      13  security/mac_file.c
10  security/mac_posix_shm.c     9  security/mac_posix_sem.c    9  security/mac_pipe.c
```

No amount of fixing compile errors would have touched those, because the files were never compiled.
It only became visible once the link was real: every earlier measurement asked "do these files
compile", and the answer for a file that is not in the list is a shrug.

**This is the fourth generator-shaped defect in the project, with a different shape again**: not a
tool with several outputs, and not a file that is generated — a *selection* that silently omits an
input. The check that would have caught it is the one this stage performed: after a link, attribute
every remaining undefined symbol to a source file and ask whether that file was compiled at all.

## `san` measured and excluded, deliberately

Including `san` adds its five files, and they fail:

```
san/kasan.c:54                 'kasan.h' file not found with <angled> include
san/kasan_internal.h:52        #error KASAN undefined
```

They are `standard` in `san/conf/files`, but they are KASAN build machinery for a sanitizer build,
and a non-KASAN kernel compiles none of them. Measured before deciding: including `san` adds 5
failing files and contributes **0** of the link's undefined symbols — `grep -c kasan` on the
undefined list is 0. So it is excluded, with the measurement recorded rather than the omission
implied. The comment now says why, so the next person does not re-add it expecting a free win.

## What is left

18 failures in `RELEASE`, 19 in the minimal configuration, and the boot closure is unchanged at
**12 files / 60 symbols** — because the files that were already failing were already attributing
their symbols. The link is at **499 undefined symbols**, down from 1187 when the measurement was
first taken.

The largest remaining items are still `osfmk/vm/vm_compressor.c` (17), `libkern/gen/OSAtomicOperations.c`
(15), `bsd/dev/arm/conf.c` (7) and `bsd/kern/bsd_init.c` (6) — and 233 of the 499 attributed to a
failing file, with the rest from the 100 `.s`/`.cpp` files the build skips.

## How to reproduce

```bash
./tools/xnu_config/list_sources.py RELEASE --write $PWD/out/xnu_arm_manifest.txt   # 716 file(s)
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh                 # 591 of 609
./tools/link_xnu_arm.sh                           # 0 duplicates, 499 undefined
```
