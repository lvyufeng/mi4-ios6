# 868 — HFS+ COMPILES WHOLE, AND WHAT IT OWES IS 25 SYMBOLS

**A host-side measurement: no device, no boot, no arm, no park, no switch.** Nothing outside `tools/` and
this document is written. The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE
OPERATOR'S; the goal is NOT met.**

In one paragraph: 865 priced the HFS+ port at "34/36 files compile, two failures" and stated plainly that
a compile count **is not the work** — whether it *links* was left unmeasured. This document finishes the
compile and measures the link. **At `CONFIG_PROTECT=0` all 36 files now compile**, because two of the
"failures" were not drifts at all but **unset configuration options** — `HFS_COMPRESSION` and `HFS` are
2050's own `bsd/conf/MASTER` options, and with them unset the `VTOCMP` macro and the entire
`hfscommon/Unicode` family silently compile to nothing. The one remaining compile failure is
`hfs_cprotect.c`, which a root filesystem does not need. And the link, measured for the first time, owes
**25 external symbols in four families**: the transaction journal (its own source file), three `IOBSD*`
calls, and five renamed VFS symbols.

## 1. The two "failures" that were options, not drifts

865 §3 listed eight drifts, and reported:

```
CONFIG_PROTECT=0   ok=34  fail=2  of 36
```

Two of the eight were miscounted, and the probe's own shim framework is what hid it — the drifts were
resolved by *adding definitions*, so the port's missing **options** looked like drift:

- **drift 8, `VTOCMP(vp)->cmp_type`, is not drift.** `VTOCMP(vp)` is `(VTOC(vp))->c_decmp`
  (`hfs_cnode.h:298`), defined only under `#if HFS_COMPRESSION` (`:292`). `HFS_COMPRESSION` is a 2050
  **`bsd/conf/MASTER:193` option**; unset, the macro never defines, so `VTOCMP(vp)` degrades to an
  **implicit-int function call** — and *that* is the `int` the error reported ("member reference type
  'int' is not a pointer"). It is not that "4570 keeps `c_decmp` as an `int`"; 4570 has no `c_decmp` at
  all. It is that the option was never on. Setting `HFS_COMPRESSION=1` resolves it with **no source
  change**.
- **part of drift 1's neighbourhood: the `hfscommon/Unicode` family and `hfs_encodings.c` are `#if HFS`**
  (`UnicodeWrappers.c:35`, `hfs_encodings.c:28`). `HFS` is **`MASTER:188`**. Unset, those files compile
  to nothing and every symbol they define — `FastRelString`, `GetEmbeddedFileID`,
  `ConvertUnicodeToUTF8Mangled`, `hfs_converterinit`, the unicode converters — appears as *undefined*.

Both were set in the probe's forced header (shim 3), and the count went **34/36 → 35/36 → 36/36**. The
last file, `hfs_cprotect.c`, is closed by one placeholder typedef: at `CONFIG_PROTECT=0` its body is a
stub that ignores its argument, and the type it names (`cp_wrap_func_t`) is the one thing 4570's
restructured `cprotect.h` dropped.

## 2. The link gap — measured for the first time

865 said a compile count "is a **compile** count: whether the objects **link** against 4570's `vfs_*` …
[is] untested." The probe now measures it: from the symbols the compiled port leaves undefined it
subtracts (a) everything the built kernel defines — its own object set plus the linked entry image — and
(b) everything the port defines within itself. The remainder, **25 symbols**, is the real external debt:

| family | symbols | what it is |
| --- | --- | --- |
| **journal** | `journal_open/close/create/flush/…` (18) + `fslog_fs_corrupt` | HFS's transaction journal — 2050's own `bsd/vfs/vfs_journal.c`, guarded `#if JOURNALING`. Needed at mount even read-only: the journal is replayed (`hfs_early_journal_init`, `hfs_vfsutils.c:2108`). |
| **IOKit** | `IOBSDGetPlatformSerialNumber`, `IOBSDIsMediaEjectable`, `IOBSDIterateMediaWithContent` | from 2050's `iokit/bsddev/IOKitBSDInit.cpp`. |
| **renames** | `vnode_name`→`vnode_getname`, `is_suser()`→`vfs_context_issuser()`, `proc_tbe`, `vfs_markdependency`, `ubc_create_upl`→`ubc_create_upl_kernel` | one-line shims, not ports. `ubc_create_upl` has a 4570 sibling `ubc_create_upl_kernel` (`ubc.h:154`, one extra `vm_tag_t`); `vfs_markdependency` 4570 does not declare *or* define at all (only a comment in `proc.h`), so that one is a genuine (small) addition. |

**So "port HFS+" is not 68,285 lines — and it is not even "eight drifts."** It is: eight genuine small
drifts, **two options** (`HFS`, `HFS_COMPRESSION`), one placeholder typedef, and **25 link symbols in four
families** of which five are renames. The much larger part — the 30-odd source files — compiles against
4570's headers unmodified once the options are set.

## 3. What it still is not

- **A mount.** The compile+link is a *floor*. The port also needs a `vfstbllist[]` row **before** mockfs,
  an `FT_HFS`, and the `HFS` option in the build — none of which this probe touches.
- **A driver.** Even a linked HFS+ needs the medium: the device `strategy` that moves a byte off the card
  (experiment 867). The two are separable; neither is done.
- **On the critical path of the current boot.** The OS enters on the mockfs RAM disk. Nothing here
  changes that, and the storage frontier is still **one press** (rungs 57–60, parked and unpressed).

## 4. What changed, in the tool

`tools/hfs_port_probe.sh`:

- **Shim 3** (a forced-header block) sets `HFS`, `HFS_COMPRESSION`, `CONFIG_HFS_STD` — 2050's own
  options — and adds the one placeholder `cp_wrap_func_t`. Each is named with its `MASTER` line.
- **A `LINK_GAP` stage** now runs after the compile and prints the 25-symbol external debt and its
  families, so the number 865 lacked is a measurement rather than a reading.
- The "WHAT THE TWO NUMBERS MEAN" text is corrected: `CONFIG_PROTECT=0` is **36/36**, and the two
  865 called failures (VTOCMP, and one cprotect file) are re-explained.

## 5. What this changes, and what it does not

- **The park backlog is not touched.** Rungs 57/58/59/60 stay **parked and unpressed**; this document
  adds no arm and spends none.
- **865's HFS number is corrected**, not overturned: the port is still cheap, and now it is cheap *and*
  link-measured. Experiment 529 §8's alternatives ("a smaller purpose-built read-only filesystem") are
  not needed on cost grounds.
- **THE GOAL IS NOT MET.** The OS enters on a synthetic root; no filesystem is mounted; the medium has
  never been read by the mounting OS; the eMMC driver clause is still open.

## 6. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press.** The probe is host-only and
writes nothing in the repository outside `tools/` and this document; `make check` is exit 0.