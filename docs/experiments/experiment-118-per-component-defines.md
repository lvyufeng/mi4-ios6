# Experiment 118 — the per-component defines, and the 127-file defect they were

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/component_defines.sh` (new), `tools/check_component_defines.py` (new),
`tools/build_xnu_arm_kernel.sh`

| Configuration | files tried | before | after |
| --- | --- | --- | --- |
| `STAGE90_BOOT` (minimal boot) | 401 | 196 | **288** |
| `RELEASE` (Apple's full iOS kernel) | 569 | 204 | **329** |

No line of XNU's code was changed. The whole movement is which `-D` flags each component is
compiled with.

## The finding

`experiment-117` closed with "the remaining blockers are the Mach-view-versus-BSD-view collisions —
`uthread_t`, `copyinstr`, `ORDINARY`/`struct tty`, `fls`/`ffs`" and named Apple's exported-header set
as the next step. That experiment's own commit then *measured* the export roots and found them worse
(167 against 196), so the collisions were still unexplained.

The explanation is in each component's own Makefile template, which this project had never read:

```
osfmk/conf/Makefile.template:19     CFLAGS+= -include meta_features.h -DMACH_KERNEL_PRIVATE -DMACH_KERNEL
bsd/conf/Makefile.template:41-43    CFLAGS+= -include meta_features.h -DDRIVER_PRIVATE -D_KERNEL_BUILD \
                                            -DKERNEL_BUILD -DMACH_KERNEL -DBSD_BUILD -DBSD_KERNEL_PRIVATE \
                                            -DLP64_DEBUG=0
libkern/conf/Makefile.template:19   CFLAGS+= -include meta_features.h -DLIBKERN_KERNEL_PRIVATE -DOSALLOCDEBUG=1
iokit/conf/Makefile.template:19-20  CFLAGS+= -include meta_features.h -DDRIVER_PRIVATE -DIOKIT_KERNEL_PRIVATE \
                                            -DIOMATCHDEBUG=1 -DIOALLOCDEBUG=1
pexpert/conf/Makefile.template:19   CFLAGS+= -include meta_features.h -DPEXPERT_KERNEL_PRIVATE
libsa/conf/Makefile.template:19     CFLAGS+= -include meta_features.h -DLIBSA_KERNEL_PRIVATE
security/conf/Makefile.template:19  CFLAGS+= -include meta_features.h -DBSD_KERNEL_PRIVATE
san/conf/Makefile.template:16       CFLAGS+=
```

**`MACH_KERNEL_PRIVATE` is set for `osfmk` and for nothing else.** This build set it globally, for
every file in every component. The consequence is precise:

- `MACH_KERNEL_PRIVATE` is what makes `osfmk/kern/kern_types.h:192` include `kern/misc_protos.h`.
- `misc_protos.h:70,76,107` declare `ffs(unsigned int)`, `fls(unsigned int)` and
  `copyinstr(const user_addr_t, char *, vm_size_t, vm_size_t *)`.
- `bsd/sys/systm.h:113` includes `<libkern/libkern.h>` — `bsd/libkern/libkern.h`, which is the only
  `libkern.h` in the tree — and that declares `ffs(int)`, `fls(int)` and
  `copyinstr(const user_addr_t, void *, size_t, size_t *)` at `:145,147,183`.

Two declarations of the same name with different types in one translation unit is a hard error, so
**every BSD file that includes `<sys/systm.h>` failed on it** — 127 files, 62 % of the minimal
configuration's 205 failures. Three shapes of the same defect (`ffs`, `fls`, `copyinstr`) and one
cause.

## How it was found, including the search that failed first

Grepping for `#define MACH_KERNEL_PRIVATE` returns nothing anywhere in the tarball, and so does
grepping for any definition of it. The first conclusion drawn from that was the wrong one — that
Apple leaves it undefined and the kernel builds in "public" mode — and `-DMACH_KERNEL_PRIVATE` was
dropped globally to test it: **196 → 127**, much worse, because the Mach-private view is genuinely
needed by the Mach side. That is the measurement that said "per-component" rather than "absent",
before anything pointed at the templates.

The confirmation is `makedefs/MakeInc.def:586`:

```make
XNU_PRIVATE_UNIFDEF = -UMACH_KERNEL_PRIVATE -UBSD_KERNEL_PRIVATE -UIOKIT_KERNEL_PRIVATE \
                      -ULIBKERN_KERNEL_PRIVATE -ULIBSA_KERNEL_PRIVATE -UPEXPERT_KERNEL_PRIVATE \
                      -UXNU_KERNEL_PRIVATE
```

That line exists to *undefine* exactly seven `*_KERNEL_PRIVATE` macros, and it is used to build the
public SDK headers (`MakeInc.def:591-594`, `SPINCFRAME_UNIFDEF` and friends). A set that has to be
undefined together to produce a public view is a set that is defined together *per component* to
produce the private one. Following that to its definition — rather than to where the error message
points — is what found the templates, and it is the fifth time in this project that "not available"
meant "looked in one directory".

## What was measured

Five builds, one variable at a time, `STAGE90_BOOT` unless noted:

| Build | compile | fail |
| --- | --- | --- |
| baseline, `-DMACH_KERNEL_PRIVATE=1` global | 196 | 205 |
| plus Apple's standard defines (`-DAPPLE -D__MACHO__=1 -Dvolatile=__volatile`, `MakeInc.def:78`) | 196 | 205 |
| minus `-DMACH_KERNEL_PRIVATE` globally | 127 | 274 |
| per-component table, `-DMACH_KERNEL=1` still global | **288** | 113 |
| per-component table including `MACH_KERNEL`, as Apple has it (adopted) | **288** | 113 |
| …the same, `RELEASE` | **329** | 240 |

Two notes on the table. Apple's standard `DEFINES` were tested and are inert here, so they are not
in the script — recording them as a change would overstate what was done. And `MACH_KERNEL` is
given by Apple to `osfmk` and `bsd` only; moving it out of the global list changed nothing, so the
faithful version is the one in use.

## What it does not mean

- **`osfmk` did not move at all: 59 failing files before and after.** The fix is entirely on the BSD
  and libkern side (122 → 40 and 17 → 10, with pexpert's 3 → 0). That is the expected shape: those
  components were being compiled with a flag their template does not set.
- **The remainder is not one thing.** By first error, the 113 failures are 36 missing generated
  headers, 18 unknown type names, 14 incomplete field types, 14 conflicting types, 8 typedef
  redefinitions and small tails. The largest single item left is `mach/memory_object_control.h`
  (10 files) — which `tools/gen_mach_headers.sh` already tries to generate and fails on, because the
  `.defs` needs `upl_size_t` from `mach_types.defs` and MIG is not being fed it. That is a real
  next step, not a wall.
- **Compiling is not linking.** 288 objects is not a kernel; nothing here says the bounded set links,
  and the undefined-symbol list after `build_xnu_arm_layer.sh` (443, for `osfmk/arm` alone) still
  stands as the shape of the next problem.

## Two smaller things this run found

**Stale per-file logs.** `build_xnu_arm_kernel.sh` deleted `$OUT/$key.log` on success and left it on
failure, and removed nothing at the start. So `out/xnu_min_obj/` accumulated every failing file's log
across every run: after this change the directory still held 127 `ffs` logs from the baseline, and a
"first error per file" sweep read as if the per-component fix had done nothing. This is the same
class as the defect `experiment-116` fixed in the same script — a result that is a count of two runs
— one level down. The script now clears `*.log` before recreating `all.log`.

**The table is checked, not transcribed.** A hand-copied value is this project's recurring defect,
and a define table copied out of a Makefile is exactly that shape. `tools/check_component_defines.py`
parses each `<component>/conf/Makefile.template`, joins its `CFLAGS+=` continuation lines, drops the
generated `meta_features.h` force-include, and compares the remaining `-D` flags to the table as
sets. Its own perturbation test — removing `-DBSD_BUILD=1` from the table — reports
`bsd MISMATCH` and exits 1, so it is not a check that always passes.

## How to reproduce

```bash
./tools/check_component_defines.py                      # the table still matches the templates

MANIFEST=out/xnu_arm_manifest_min.txt
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $MANIFEST

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/$MANIFEST XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh

MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh                       # RELEASE, 329
```

`XNU_KERNEL_OBJ_OUT` must be absolute: the script `cd`s to `tools/`, so a relative path lands in
`tools/out/` and the run looks successful while the directory you are inspecting is untouched. That
happened once while writing this up.
