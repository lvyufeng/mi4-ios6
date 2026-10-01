# 869 — THE JOURNAL IS BOUGHT, AND WHAT THE PORT STILL OWES IS TEN SHIMS

**A host-side measurement: no device, no boot, no arm, no park, no switch.** Nothing outside `tools/`
and this document is written. The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE
OPERATOR'S; the goal is NOT met.**

In one paragraph: 868 measured the HFS+ port's link gap at **25 symbols in four families** and named the
journal — 2050's own `bsd/vfs/vfs_journal.c`, ~4,000 lines guarded `#if JOURNALING` — as the largest single
item. This document **buys it**. The file compiles clean against 4570's headers once its own configuration
is supplied, taking `CONFIG_PROTECT=0` to **37/37** (the 36 of HFS proper plus the journal), and the link
gap drops **25 → 10**. And the point that makes this surprising is what the 4,000-line file *costs*: the
three things it needed were three one-line shims, and **everything else it calls — the whole `IF_*` HFS
interface, `buf_*`, `vfs_*`, `kauth_*`, the `hfs_*` caches — it already finds**. What remains owed is
**ten symbols, every one a one-line shim or an empty body.**

## 1. What the journal needed, and what it did not

868 folded `vfs_journal.c` into the probe and found the trim entry points (`journal_trim_set_callback`,
`journal_trim_add_extent`, `journal_trim_remove_extent`) missing. The cause was **not** their absence but
`JOURNALING` — 2050's `bsd/conf/MASTER:192` option — being **unset**, so the file compiled its `#else`
stub arm (`vfs_journal.c:124`) and none of the `#if` body's symbols existed. Enabling it made the real
body compile, and it failed on exactly **three** things:

| what | where | why | resolution |
| --- | --- | --- | --- |
| `M_JNL_JNL` (91), `M_JNL_TR` (92) | 2050 `bsd/sys/malloc.h:184-185` | the journal's own two malloc types; 4570 declares neither | both **free in 4570** (its highest is `M_* 127`, `M_KAUTH 100`, `M_UDF*` 84/85; its `M_JNL*` are 91/92 and are **declared nowhere**), so 2050's own values are taken |
| `kmem_alloc_kobject(map, addr, size)` | 2050 `osfmk/vm/vm_kern.h` | 4570 gave it the owning tag: `kmem_alloc_kobject(vm_map_t, vm_offset_t *, vm_size_t, vm_tag_t)` (`vm_kern.h:160` vs `:240`) | one-line macro, **exactly the existing `kmem_alloc` shim's shape** (`VM_KERN_MEMORY_FILE`) |
| `B_NORELSE` (`0x10000000`) | 2050 `bsd/sys/buf_internal.h:216` | a **private journal-layer buf flag** 4570 dropped; 2050 marks it in `modify_block_start` | 4570 still has `B_ZALLOC` (`0x08000000`) and `B_COMMIT_UPL` (`0x40000000`) on either side, so the bit is **free**; taken at 2050's value |

**Those three are the whole cost of the file.** The journal's symbol table is otherwise full of names it
*looks* like it would owe — `IF_FIND_CNODE`, `IF_LOCK`, `IF_VALIDATE_CNODE`, `IF_BADOP`, the `hfs_*` calls,
`buf_bread`/`buf_bwrite`/`buf_brelse`, `vfs_*`, `kauth_cred_getuid`, `proc_*` — and **every one of them is
already supplied** by the port (`hfs_cnode.c` et al.) or by 4570's own kernel. The link gap is where that
claim becomes a measurement rather than an assurance, and it did not move for any of them.

## 2. The measurement

```
CONFIG_PROTECT=0    ok=37  fail=0  of 37      (was 36/37 before this change, 34/36 before 868's options)
CONFIG_PROTECT=1    ok=33  fail=4  of 37      (the four `cp_*` files; unchanged in kind from 868)

THE LINK GAP (CONFIG_PROTECT=0): 10 symbols
  fslog_fs_corrupt            IOBSDGetPlatformSerialNumber    is_suser
  IOBSDIsMediaEjectable       IOBSDIterateMediaWithContent    proc_apply_thread_selfdiskacc
  proc_tbe                    ubc_create_upl                  vfs_markdependency    vnode_name
```

**The journal is gone from the gap** — all 18 `journal_*` symbols plus the file that carried them. That is
the 25 → 10. The remaining ten, each read:

| symbol | what it is | cost |
| --- | --- | --- |
| `fslog_fs_corrupt` | `hfs_vfsops.c:7702`, one call on a corrupt volume. 2050 defines it in `bsd/vfs/vfs_fslog.c:343`, but its **whole body is one `fslog_err(...)` call**, and 4570 dropped the fslog API (`fslog.h` has no `fslog_err` and no `FSLOG_KEY_*`) | **empty shim** (or drop the one call) |
| `IOBSDGetPlatformSerialNumber`, `IOBSDIsMediaEjectable`, `IOBSDIterateMediaWithContent` | 2050's `iokit/bsddev/IOKitBSDInit.cpp`; the BSD-side IOKit shims HFS uses for the media's identity/ejectability | three small functions or three stubs (a root fs needs none of them) |
| `vnode_name` → `vnode_getname`, `is_suser()` → `vfs_context_issuser()`, `ubc_create_upl` → `ubc_create_upl_kernel` | **renames** — 4570 has the same function under a new name (the `ubc` sibling differs by one `vm_tag_t`) | one-line each |
| `proc_tbe` | a thread-quantum helper; 2050 `kern_resource.c:1224` | small shim or stub |
| `vfs_markdependency` | 4570 has **neither a declaration nor a definition** (only a comment in `proc.h`) — a genuine small addition | one small function |
| `proc_apply_thread_selfdiskacc` | 4570 dropped `thread->appliedstate.hw_disk` **entirely** (`grep hw_disk` over 4570's `osfmk/` is empty), so 2050's body (`task_policy.c:1287`, two field writes) has no fields to write | **no-op shim** |

**So the port's external debt is ten one-line shims, one empty body, and (at most) three small IOKit
functions a root filesystem does not need.** The ~4,000-line journal — the item 868 named as the biggest —
is bought for three lines.

## 3. What it still is not

- **A mount.** Compile and link are a *floor*. The port still needs a `vfstbllist[]` row **before** mockfs,
  an `FT_HFS`, and the `HFS` option in the build — none of which this probe touches.
- **A driver.** Even a linked, mountable HFS+ needs the medium: the device `strategy` that moves a byte off
  the card (experiment 867). The two clauses are separable; neither is done.
- **On the critical path of the current boot.** The OS enters on the mockfs RAM disk. Nothing here changes
  that, and the storage frontier is still **one press** (rungs 57–60, parked and unpressed).

## 4. What changed, in the tool

`tools/hfs_port_probe.sh`:

- **Shim 2** gains `M_JNL_JNL`/`M_JNL_TR` (beside the five `M_HFS*`), the `kmem_alloc_kobject` macro (beside
  `kmem_alloc`), and `B_NORELSE` (its own block, with the free-bit argument in the comment). Each names the
  2050 file:line it comes from.
- **Shim 3** already set `JOURNALING` (added with a now-fixed regression — see below); this run makes that
  arm compile.
- The printed **link-gap families** and the **"WHAT THE TWO NUMBERS MEAN"** text are corrected to 37/37 and
  10 symbols.

**A regression caught in the making.** The `JOURNALING=1` define was added just before 868's write-up and
**regressed the measurement** (`CONFIG_PROTECT=0` fell to 36/37 and the gap *rose* back to 25) because the
real journal body did not yet have its three shims — so `vfs_journal.c` failed and **the journal symbols
vanished from the link gap entirely**, which reads as *more* debt rather than a broken build. That is
[[mi4-silence-is-a-reading-only-if-success-is-silent]]'s shape: a file that fails to compile contributes no
definitions and no *undefined* symbols, so its absence **shrinks what it defines and hides what it consumes.**
The three shims above are the fix, and the count is now 37/37 with the file present.

## 5. What this changes, and what it does not

- **The park backlog is not touched.** Rungs 57/58/59/60 stay parked and unpressed; this document adds no
  arm and spends none.
- **868's number is carried forward, not overturned**: the port is cheap, and now the *largest single item it
  named* is measured as bought.
- **THE GOAL IS NOT MET.** The OS enters on a synthetic root; no filesystem is mounted; the medium has never
  been read by the mounting OS; the eMMC driver clause is still open.

## 6. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press.** The probe is host-only and writes
nothing in the repository outside `tools/` and this document; `make check` is exit 0.