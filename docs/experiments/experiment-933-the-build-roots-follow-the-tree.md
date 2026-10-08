# 933 — the build tools' out-roots follow the tree (2026-10-08)

932 cleared the D13 entry link's `data.o` wall and unmasked the next stop: the **platform block**
(`out/xnu_platform_obj_d13/MSM8974PlatformExpert.o`). Rebuilding it failed on

    out/mach_headers/mach/mach_host.h:351:2: error: unknown type name 'mach_voucher_attr_raw_recipe_array_t'

`mach_host.h` at that path is **4570's** (34 voucher references, 30 KB); the D13 one
(`out/mach_headers_d13/mach/mach_host.h`, 24 KB, 1 `#if __has_include` note and nothing else) has none.
`tools/build_xnu_arm_kernel.sh`'s `MIG_HEADERS` defaulted to `$REPO_ROOT/out/mach_headers` — a
tree-independent literal — so the platform block compiled against 4570's MIG output while the D13
pool sat beside it. The wall was not a MIG-generation problem at all; it was **the wrong root**.

## The defect, at full breadth

This is the pivot's recurring class: *an asset pinned to 4570 that must follow the selected tree*
([[mi4-913-ios7-rebase-decision]]). `build_entry.sh` closed its half in 925/926 by deriving
`XNU_OBJ_SUFFIX` from the tree's own discriminator (Darwin 13 ships `osfmk/sys/types.h`; Darwin 17
does not), so `XNU_TREE=<d13>` alone selects the `_d13` pools. **The build tools did not.** Six of them
each defaulted their output root to a tree-independent literal:

| tool | literal root(s) |
|---|---|
| `build_xnu_arm_kernel.sh` | `xnu_kernel_obj`, `xnu_asm_obj`, `xnu_platform_obj`, `mach_headers`, `xnu_generated`, `xnu_options`, `xnu_device`, `device_table.txt`, `xnu_arm_manifest.txt` |
| `gen_mach_headers.sh` | `mach_headers` (+ its `kserver`) |
| `gen_assym.sh` | `xnu_assym`, `xnu_generated`, `xnu_options`, `xnu_device`, `mach_headers` |
| `build_xnu_arm_layer.sh` | `xnu_arm_obj`, `xnu_generated`, `xnu_options`, `mach_headers` |
| `build_xnu_arm_macho.sh` | `xnu_macho_obj`, `xnu_generated`, `xnu_options`, `xnu_device`, `mach_headers`, `xnu_assym`, `xnu_arm_manifest.txt` |
| `assemble_arm_layer.sh` | `xnu_asm_obj`, `xnu_assym`, `xnu_options`, `xnu_device`, `mach_headers`, `xnu_generated` |

On 4570 that is correct, because 4570 *is* the default tree. On Darwin 13 it means the build writes —
and, worse, **reads** — the 4570 outputs. The measured cost was a D13 kernel build driven by a
hand-typed **twelve-variable environment**, recorded nowhere; when it was re-run without that
environment, the platform block silently read 4570's `mach_host.h`. The header exists, so nothing says
it is the wrong one: this is the *silent* shape of the class.

## The fix: one definition

The rule is written down **once**, in a new sourced file `tools/xnu_tree_roots.sh`:

    XNU_TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
    if [[ -z ${XNU_OBJ_SUFFIX:-} ]]; then
        if [[ -f $XNU_TREE/osfmk/sys/types.h ]]; then XNU_OBJ_SUFFIX=_d13; else XNU_OBJ_SUFFIX=; fi
    fi
    export XNU_TREE XNU_OBJ_SUFFIX
    _OUT_BASE=$REPO_ROOT/out
    XNU_KERNEL_OBJ_OUT=${XNU_KERNEL_OBJ_OUT:-$_OUT_BASE/xnu_kernel_obj$XNU_OBJ_SUFFIX}
    ... (every tree-derived root) ...

Every builder sources it right after it sets `REPO_ROOT`. `build_entry.sh` **stops spelling its own
copy** and sources the same file, so an entry link and a kernel build cannot disagree about which pool
belongs to which tree. The two roots that are *this project's* sources and belong to no tree
(`xnu_firehose_obj`, `xnu_rt_obj`) stay unsuffixed and are still set there.

**The export is load-bearing.** A builder does not only *name* these paths, it *passes them down*:
`build_xnu_arm_kernel.sh` runs `gen_option_headers.py`, `gen_device_headers.py` and `list_sources.py`
as children, and those read `XNU_TREE`/`XNU_*_OUT` **from the environment**. A variable set but not
exported is invisible to them, and each falls back to its own 4570 default — the same silent failure,
one level down. That is also exactly why the D13 build's command-line variables worked: command-line
environment is exported.

## Verification

| check | result |
|---|---|
| `bash -n` on all 11 files | ok |
| rules file, 4570 default | `XNU_OBJ_SUFFIX=''`; every root the old literal |
| rules file, `XNU_TREE=<d13>` | `_d13`; `xnu_kernel_obj_d13`, `mach_headers_d13`, `device_table_d13.txt`, … |
| D13 platform block, **`XNU_TREE` alone** (no hand-typed roots) | 9 objects, **0 error logs** (was: the voucher wall) |
| D13 entry link, `XNU_TREE` alone | selects `_d13`; advances to the pool arm agreement (`arm_vm_init.o`), i.e. past the platform block |
| `tools/gen_assym.sh` default (4570) | 266 defines, `assym.s` **byte-identical** (`2a9728c1…`) |
| `tools/gen_assym.sh XNU_TREE=<d13>` | 142 defines into `xnu_assym_d13`; the 4570 `assym.s` is **untouched** afterward |
| `make check` | 0 |

4570-neutrality is **structural**: 4570 ships no `osfmk/sys/types.h`, so `XNU_OBJ_SUFFIX` is empty and
every root is character-for-character the literal it replaced. The `gen_assym.sh` rebuild checks it in
the one place a rebuild is cheap.

## The check widened

`tools/check_entry_tree_pools.sh` (in `make check`) had two blind spots the fix exposed:

1. It read `build_entry.sh` for the derivation, which has **moved** to the rules file. It now guards
   the rules file directly — (a) the `_d13` suffix keyed on the D13 header, (d) the 4570 default —
   and requires `build_entry.sh` to **source** it. As a **rules file**, all six builders now pass the
   same import check; the widening derives from the shared file's membership and so covers all seven.
2. Its rule-(b) literal regex named only `xnu_(kernel|asm|platform)_obj`. It is now
   `xnu_(kernel|asm|platform|arm|macho)_obj|xnu_assym|xnu_generated|mach_headers|device_table|xnu_arm_manifest`,
   and it is applied to the six builders as well as `build_entry.sh`. It immediately caught two real
   survivors: `PL_OUT=${...:-$REPO_ROOT/out/xnu_platform_obj}` in the kernel build, and an include
   path `-I"$REPO_ROOT/out/xnu_generated" -I"$REPO_ROOT/out/mach_headers"` in `assemble_arm_layer.sh`
   — the latter reads a **second tree's generated headers** as literals while `$GENERATED`/`$MIG_HEADERS`
   sat derived three lines above.

The `--selftest` gains a **rule-less rules file** (a file that keeps the default tree but drops the
`XNU_OBJ_SUFFIX` derivation) and asserts it is refused, so the derivation cannot be deleted from the
one file that now owns it.

## What moved

`src/entry/build_entry.sh` (its derivation replaced by a source), six builders, two generators
(`gen_bsd_headers.sh`, `gen_libkern_version.sh`), the check, and the new `tools/xnu_tree_roots.sh`. No
tree edit, no device. The arm in `out/stage90` is `cb4e17f1…`, unchanged.

## The next wall

With the roots following the tree, the D13 link stops at the **kernel-pool arm agreement**: the pool
`out/xnu_kernel_obj_d13` was built with `XNU_KERNEL_CONFIG=RELEASE` and no `STAGE90_XNU_*` defines,
while the entry asks for `STAGE90_XNU_MEM_SIZE_MAX=0x5e500000`; `osfmk_arm_arm_vm_init.o` therefore
carries the ceiling arm `unset`. Rebuilding the D13 pool with the arm switch set is a whole-pool
config change — rung 934.

*Provenance: `out/xnu_platform_obj_d13/*.log`, `out/xnu_assym{,_d13}/RELEASE/assym.s`, this session's
`/tmp/d13_entry_link{2,3}.log`. Host-side, reversible, no press.*