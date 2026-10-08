# 916 — The whole kernel manifest on Darwin-13: the harness, retargeted (2026-10-08)

913 §6 measured the ARM *layer* (`osfmk/arm` files) and the PE against the Darwin-13 tree. That is
not a kernel. This step points the **whole-kernel** build (`tools/build_xnu_arm_kernel.sh`) at the
same tree and reports the first honest whole-kernel number, then fixes the harness assumptions it
exposes — each chosen by the tree's own files, not by its name, and each measured zero-change for
4570.

## 1. Why the layer measurement was not the kernel

`build_xnu_arm_layer.sh` compiles every `.c` in `osfmk/arm`. A kernel is what Apple's `*/conf/files`
manifest selects **across all components** — 406 files for Darwin-13 RELEASE (`list_sources.py`
`--xnu <d13>`: 367 present + 39 MIG-generated + **0 absent**). That set is the thing to build.

## 2. The four harness assumptions the whole-kernel build exposed

Each was a place one tree's layout was baked in. The fix in every case is to **read the tree's own
file** (the `select_master.sh` by-layout rule), never to branch on the tree's name.

| where | assumption | fix | 4570 effect |
|---|---|---|---|
| `build_xnu_arm_kernel.sh:208` | regenerated the manifest with `list_sources.py`'s **default** 4570 path, ignoring `XNU_TREE` | pass `--xnu "$XNU"` | none (same tree) |
| `build_xnu_arm_kernel.sh:408` | hardcoded `-DMONOTONIC=1` | derive it from `grep kern_monotonic.c $XNU/osfmk/conf/files` | none (grep matches) |
| `xnu_config/component_defines.sh` | a hand-written **transcription** of Apple's per-component `CFLAGS` | derive the flags from the selected tree's `<component>/conf/Makefile.template` | **byte-identical table** (verified 8/8) |
| `xnu_config/device_table.py` | `monotonic` override unconditional | bind it to the tree's own file lists | none (condition present) |
| `gen_option_headers.py` | device-header walker read only `conf/files`, missing `conf/files.arm` | read both (Apple's `mkmakefile` reads the union) | none (the 6 extra 4570 `files.arm` conditions are neither devices nor options) |
| `check_device_conditions.py:262` | options-header dir hardcoded `out/xnu_options` | honor `XNU_OPTION_HEADERS_OUT` | none (default path) |

The `component_defines.sh` rewrite is the most valuable: it **deletes a transcription**, which is the
project's most-repeated defect class. The templates are the source of truth Apple's own build reads;
a derived table cannot drift from them. `check_component_defines.py` still cross-checks a second,
independent parse of the same templates, so a bug in the derivation is still caught. Measured: the
derivation reproduces the old 4570 table exactly, and both trees now pass the check.

The `files.arm` fix was a **real latent defect on any tree**: `vc.h` was generated but
force-included by nothing, and `check_device_conditions.py` correctly refused — the walker, not the
checker, was wrong. `check_device_conditions.conditions()` and `list_sources.py` already read both
files, so `gen_option_headers.py` was the one walker with a second definition of "the file list".

New path shims for the 2013 tree's missing `sys/_types/` fragments: `_caddr_t.h`, `_u_char.h`,
`_u_short.h` — same one-line typedefs under Darwin's own guards, so they are substitutions, not new
declarations, and are inert on 4570.

## 3. The number, and the walls

**Darwin-13 RELEASE manifest: 195 of 367 sources compile** (`out/xnu_kernel_obj_d13/failed.txt`, this
session). The two dominant walls, both structural rather than per-file:

1. **BSD legacy typedefs** — `unknown type name`: `gid_t` (407), `segsz_t` (375), `off_t` (300),
   `u_quad_t` (249), `uid_t` (162), `fixpt_t` (126), `mode_t`, `pid_t`, `quad_t`, … ~1,800 sites.
   The 2013 tree's public headers do not surface these to the kernel the way 4570's do.
2. **MIG-generated headers** — `ipc/ipc_voucher.h` (51 sites), `default_pager/*_server.h`,
   `mach/security.h`. The `out/mach_headers` currently on the include path was generated from
   **4570's** `.defs`; Darwin-13's MIG set (and its `Makefile`-listed `.defs`) differs and must be
   generated from the selected tree, the same way the option headers now are.

Both are the shape of everything else in this pivot: a value/asset that is defined once per tree and
was pinned to 4570. They are host-side and reversible; no press.

## 4. What is NOT done

- The BSD-typedef and MIG walls above (the next two rungs).
- The `-DMONOTONIC`-style assignment to a **config** rather than a tree grep is left as-is: it is
  measured-correct for both trees and changing it would move the 4570 manifest the 916 step must
  keep byte-identical.
- Option/device headers are regenerated per tree into `out/xnu_options_d13/` and
  `out/xnu_device_d13/`; the **default** `out/` roots stay 4570's. A D13 build must pass both
  `XNU_OPTION_HEADERS_OUT` and `XNU_DEVICE_HEADERS_OUT` (and `XNU_DEVICE_TABLE`) or it will pollute
  the 4570 output tree — observed this session and reverted.

*Provenance: `out/xnu_kernel_obj_d13/{failed.txt,all.log}` and `out/d13_kernel_build.log` (this
session); 4570 controls re-run after every edit (`make check` 0, `check_device_conditions` 0,
`device_table` 0, `osfmk/meta_features.h` byte-identical). Device untouched — no press.*