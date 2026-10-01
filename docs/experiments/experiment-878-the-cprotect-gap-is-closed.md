# 878 — THE CPROTECT GAP IS CLOSED: THE HFS+ PORT COMPILES 37/37 THROUGH THE REAL KERNEL BUILD

**A host-side measurement: the staged tree, through `tools/build_xnu_arm_kernel.sh` with the port ON,
compiles every file and the journal. No arm, no park, no switch turned on in the live artifact, no
device.** **THE PRESS IS THE OPERATOR'S; the goal is NOT met.**

In one paragraph: 877 wired the port into the kernel build and measured **32 of 36** `bsd/hfs/` `.c`
files compiling, with four failing in the cprotect API family. This step ports that layer — 2050's
HFS-facing cprotect structures and constants, which 4570 replaced with an opaque kext API — into a
**second tracked force header**, and re-measures: **36/36 `.c` plus `vfs_journal.c`, 0 fail**,
reproducible, through the real pipeline. The port is now source-complete on this tree; what remains is
the root row (874) and the medium (867), plus the honest note that a cprotect *engine* is still owed.

## 1. Why this was a port and not a flag

877 established that `CONFIG_PROTECT` is a `struct bufattr` layout axis and cannot be flipped per file.
So the four failures had to be answered by making the HFS sources *see* the cprotect layer they were
written against. The measurement of that layer:

- 279 errors across the four files: 157 incomplete `struct` types, 88 undeclared identifiers.
- The names are 2050's `bsd/sys/cprotect.h` (204 lines) — which **4570 cut to 191 lines and changed
  from an HFS-facing header into an opaque kext API** (`cpx_alloc`, `cpx_set_*`, …). `struct cprotect`,
  `struct cp_wrap_func`, `struct cp_root_xattr`, `struct cp_xattr_v2/v4` and the `CP_*` flags are **gone
  from 4570**; there was nothing to rename to, and 4570's `bsd/conf/files` has **no HFS rows at all**
  (Apple ships HFS as a closed component), so 2050's source is the only one there is.

## 2. The port, and the two names that could not simply be added

`src/shims/hfs/hfs_cprotect_port.h` restores it, force-included for the HFS files only, **second**
after `hfs_port_force.h`. It is a compile-level restoration — 2050's layouts verbatim, because the HFS
code indexes them and passes them to IOKit through `bufattr`. Two names collided and each has a
measured resolution:

- **`unwrapper_t`** is *already* in 4570's `cprotect.h` (its `cp_cred_t`-first kext signature). A second
  definition is a redefinition error. Measured: **nothing in the staged HFS names 4570's `unwrapper_t`**,
  so a port-local `hfs_unwrapper_t` carries 2050's shape and 4570's name is left alone.
- **`cp_is_valid_class`** is defined *twice* — 4570's global two-argument
  (`vfs_cprotect.c:335`), 2050's static one-argument (`hfs_cprotect.c:1180`, called at `:224/:242/:482/
  :1407`). Both are definitions, so a link-time shim cannot answer it. The header `#define`s the
  identifier to `hfs_cp_is_valid_class` **after** it includes `sys/cprotect.h`, so 4570's declaration
  is read under its own name and only the HFS translation units rename. **No source edit.**

Two smaller facts the same header records: `cp_wrap_func_t` in `hfs_port_force.h` had been a `void *`
placeholder (honest at `CONFIG_PROTECT=0`), and once `cp_register_wraps` dereferences it (`:99-100`) it
must be the real `struct cp_wrap_func *`; and `cp_entry_create_keys`/`cp_entry_destroy` are declared so
the early calls at `:168/:190` are not implicit-`int` calls that "conflict" with their own definitions.

## 3. The measurement

```
STAGE90_XNU_HFS=1 … ./tools/build_xnu_arm_kernel.sh --dir hfs
  C files tried: 36   compile: 36   fail: 0
```

Reproduced twice into distinct object directories; plus `bsd/vfs/vfs_journal.c` (the port's 37th file,
which lives outside `bsd/hfs/`) compiles in the same run. **With the switch off the manifest is still
byte-identical to the committed 733**, so the live arm is untouched.

## 4. The honest bound, restated exactly

**The port is source-complete and still not a working filesystem.**
- The cprotect layer restored here is *declarations*: it makes the HFS code compile, and whether the
  wrap functions are ever populated, whether the xattrs round-trip, and whether the offset-IV path
  runs is **cprotect engine work this port still owes**. `cp_register_wraps` has no caller in this tree.
- The four files compile at the configuration's `CONFIG_PROTECT=1`, so they are consistent with the
  other 704 objects — but the port is still **OFF by default**, because turning it on before a real HFS
  volume can answer would only add objects to the link.
- The **root row** (874 — a static `vfstbllist[]` entry before mockfs) and the **medium** (867 — an
  eMMC driver that moves a byte) are both still owed. **THE GOAL IS NOT MET.**