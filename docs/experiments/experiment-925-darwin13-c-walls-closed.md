# 925 — Darwin-13's last C walls: the kernel block compiles 606/606 + 96/96 (2026-10-08)

923 closed the C++ spine (96/96) and 924 closed the assembly layer (33/33). Five `C` files remained,
and they were the last thing between the D13 object set and a link. All five are closed here:

    C files tried:   606      compile: 606      fail: 0
    C++ files tried:  96      compile:  96      fail: 0

Two of the five were real walls with a single cause each; the other three were the same kxld header.

## Wall 1 — `kxld_object.h:60`, the missing comma (four files)

`libkern/kxld/kxld_object.h:60` reads

    __attribute__((nonnull(1,2,4) visibility("hidden")));

— no comma between the two attribute clauses, so clang stops at `expected ')'` and all four kxld
files (`kxld.c`, `kxld_object.c`, `kxld_kext.c`, `kxld_vtable.c`) fail on the header.

**It is an upstream transcription defect, not a fork one.** `xnu-2050.18.24`, `apple-xnu-rel-2050` and
`xnu-upstream` all carry the un-comma'd line at their own `:59`; Darwin-17 (4570) is the tree that
fixed it to `nonnull(1,2,4), visibility(...)`. So the fix is Apple's own, applied one release early:
add the comma. It is the **fourth** staged tree edit, in `tools/stage_d13_board.sh`, re-derived by
`tools/check_d13_board_staged.sh`.

Left uncontested, this is the kind of wall that looks like a missing header or a wrong toolchain and
is neither: a punctuation mark in a fork's `__attribute__`.

## Wall 2 — `strncasecmp` in `nfs_node.c`

`bsd/nfs/nfs_node.c:347` reads

    cmp = nfs_case_insensitive(mp) ? strncasecmp : strncmp;

It takes the **address** of `strncasecmp`, which is not clang's builtin — so unlike a by-name call to
`strncmp` (which clang's unchecked builtins accept implicitly), this needs a real declaration.

**Why it appears only now.** `nfsclient` is **on** for Darwin-13: `bsd/conf/MASTER.arm:55` declares
`RELEASE = [ BASE NETWORKING NFS VPN FILESYS libdriver ]`, and `NFS = [ nfsclient nfsserver ]` at
`:50`. Our option union produces `-DNFSCLIENT=1 -DNFSSERVER=1` from `bsd/conf/MASTER:213,214`, and
the D13 manifest selects all 17 `bsd/nfs/*.c`. The 4570 line never did — its manifest has **zero** nfs
files — so `nfs_node.c` compiles here for the first time, and it is the first file to need a
declaration of `strncasecmp` that the tree does not carry for a kernel context except in
`osfmk/libsa/string.h:84`.

**The fix is in the shim, not the tree.** `src/shims_arm/string.h` **is** the kernel's `<string.h>` in
this build (`EXTERNAL_HEADERS/` ships no `string.h`), reaching `nfs_node.c` through
`bsd/libkern/libkern.h:76` (`#include <string.h>`) via `sys/systm.h:114`. That shim already declares
17 of `osfmk/libsa/string.h`'s names for exactly this reason; `strncasecmp` was missing. It is added
with Apple's own signature (`osfmk/libsa/string.h:84`), whose definition is
`osfmk/device/subrs.c:241`.

The addition is a **declaration only**, and the declaration matches what clang had been inferring
(`int f(const char *, const char *, size_t)`), so it changes nothing semantically.

**Measured, and it is NOT byte-identical on 4570 — one object moves.** `is_package_name`
(`vfs_subr.c:2787`) **calls** `strncasecmp` by name (`strncasecmp(name_ext, ptr, extlen)`), and
`vfs_subr.c` reaches this shim as its `<string.h>`. Declaring the callee changes clang's inline cost
model for `is_package_name`, which flips whether `vn_path_package_check` inlines it: with the
declaration the object's `vn_path_package_check` is **276 B and calls `is_package_name`**; without it,
**512 B and the callee is inlined**. Exactly **one of 4570's 703 objects** (`bsd_vfs_vfs_subr.o`)
differs, and only by that inlining decision — no call is added, dropped or reordered.

**This is inert, and it is not a blocker.** The 2026-10-08 operator decision
([[mi4-two-branch-split-4570-vs-master]]) makes `4570` a **frozen backup** at `a89cfdc`: the
per-rung 4570 object rebuild+`cmp` was retired, and the backup ships the pre-925 shim. This is the
first measured instance of *why* that rule is the right one — a harness declaration that is correct
for the working tree can perturb an unrelated object's inlining on a tree that only needs the
harness's *scripts* to keep working (`make check`), which they do. Recorded, not chased.

## The staged edits, now four

`tools/stage_d13_board.sh` makes four sibling tree edits, all under the `MSM8974_CANCRO` sentinel,
all idempotent, all re-derived by `tools/check_d13_board_staged.sh` (in `make check`):

1. `osfmk/arm/PlatformConfigs.h` — the board → processor-class map (923).
2. `../nokextd/IOS7NoKextd035.h` — widen the board gate (923).
3. `osfmk/mach/arm/asm.h` — guard the `SLIDABLE` default (924).
4. `libkern/kxld/kxld_object.h` — the attribute comma (925).

## Result

| | before 925 | after 925 |
|---|---|---|
| D13 C compiled | 601/606 | **606/606** |
| D13 C++ compiled | 96/96 | 96/96 |
| D13 objects | 697 | **702** |
| 4570 objects | 703 | 702 identical, 1 (`vfs_subr.o`) inline-flip — inert, see above |
| `make check` | 0 | **0** |

`absent from tarball: 5` is unchanged and is 4570's own list — files the tarball does not ship, not
failures.

## Verification

| check | result |
|---|---|
| D13 kernel build | C 606/606, C++ 96/96, 0 fail |
| `tools/stage_d13_board.sh` | idempotent; the kxld edit reproduced **identically** from the pre-925 text |
| `tools/check_d13_board_staged.sh` | ok, now also re-derives the kxld comma |
| 4570 harness scripts | `make check` 0 (the retired object rebuild would show the one inline-flip) |
| `make check` | 0 |

## What this does not do

It does not link the D13 image. The last step is the **object-pool retarget** in
`src/entry/build_entry.sh`: 289 path literals across `out/xnu_kernel_obj`, `out/xnu_asm_obj` and
`out/xnu_platform_obj` must follow the selected tree, and the entry closure must be re-derived
name-by-name against the D13 object set. That is the next rung.