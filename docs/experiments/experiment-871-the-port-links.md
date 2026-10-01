# 871 — THE PORT LINKS: THE GAP IS ZERO

**A host-side measurement: no device, no boot, no arm, no park, no switch.** Nothing outside `tools/` and
this document is written. The rung-57/58/59/60 arms stay parked and unpressed. **THE PRESS IS THE
OPERATOR'S; the goal is NOT met.**

In one paragraph: 868 measured the HFS+ port's link gap at 25 symbols; 869 bought the journal and cut it to
ten. This document **writes the ten bodies and measures the result: `CONFIG_PROTECT=0` is 38/38 files
compile and the link gap is ZERO** — every symbol the port leaves undefined is supplied, by 4570's kernel or
by the port itself. The ten were four empty bodies, three renames and three IOKit fall-backs, and reading
each one is what forced the shape: **four of the ten are empty because 4570 dropped the state they would
write** (`P_TBE` became `P_RESV6`; `vfs_markdependency`'s `mnt_dependent_process`/`_pid` are gone;
`hw_disk` is gone; the fslog API is gone). The link half of the filesystem clause is closed.

## 1. The ten, and what each one actually is

Written into the probe as `tree/port_shims.c` (one file, not ten scattered edits), and compiled by the same
loop as the port:

| symbol | 2050 source | 4570 | shim |
| --- | --- | --- | --- |
| `vnode_name` | `kpi_vfs.c:2060` | `vnode_getname` (`vnode.h:2030`) | rename |
| `is_suser()` | `kern_prot.c:1849` | `vfs_context_issuser` (`vnode.h:2222`) | rename |
| `ubc_create_upl` | `ubc.h:105` | `ubc_create_upl_kernel` (`ubc.h:154`) | rename + a `VM_KERN_MEMORY_FILE` tag |
| `proc_tbe` | `kern_proc.c:733`, `return p->p_flag & P_TBE` | 4570's `P_TBE` is `P_RESV6` *"used to be P_TBE"* (`proc.h:192`) — **no flag** | **empty** (`return 0`) |
| `vfs_markdependency` | `kpi_vfs.c:770`, writes `mnt_dependent_process`/`_pid` | 4570 has neither the function **nor the fields** in `mount_internal.h` | **empty** |
| `fslog_fs_corrupt` | `fslog.c:343`, one `fslog_err(...)` | 4570 dropped the whole fslog API (`fslog.h` has no `fslog_err`, no `FSLOG_KEY_*`) | **empty** |
| `proc_apply_thread_selfdiskacc` | `task_policy.c:1287`, writes `thread->appliedstate.hw_disk` | 4570 has no `hw_disk` field anywhere | **empty** |
| `IOBSDGetPlatformSerialNumber` | `hfs_vfsutils.c:2056` declares it | from 2050's `IOKitBSDInit.cpp` | `KERN_FAILURE` |
| `IOBSDIsMediaEjectable` | `hfs_vfsops.c:1147` declares it | same | `0` |
| `IOBSDIterateMediaWithContent` | `hfs_vfsutils.c:2055` declares it | same | empty (no iteration) |

**The pattern is the point:** a shim is empty *when the state it would write does not exist in 4570*, and it
is a rename when the function does. That distinction is the whole difference between "port" and "shim" — and
it is why 869's guess that `vfs_markdependency` was "a genuine (small) addition" was wrong: it is **empty**,
because the mount fields it writes are gone too. (869 §2 corrected.)

## 2. The measurement

```
CONFIG_PROTECT=0    ok=38  fail=0  of 38      (36 HFS + the journal + port_shims.c)
CONFIG_PROTECT=1    ok=34  fail=4  of 38      (the four `cp_*` files - unchanged)

THE LINK GAP (CONFIG_PROTECT=0): 0 symbols
  -- ZERO.  The port links.
```

## 3. What "zero" does and does not prove — the honest bound

**It proves a `link`, and a link is not a `mount`.** A static kernel link does not require every VNOP a
filesystem's op tables name to be *defined* by that filesystem, because 4570's VNOP tables for any optional
fs are built from descriptors whose unimplemented entries are stubs. So a missing operation would still link
and then `panic` at run time. The four `cp_*` files failing at `CONFIG_PROTECT=1` are **not** a link failure
either — they are a `CONFIG_PROTECT=0`-only closure, and the real port has to carry cprotect.

**And the probe still measures nothing about `hfs_init`, `hfs_mount`, a volume, or the root.** Those are the
device half (experiment 867) and the wiring (experiment 870). What changed here is only that the port's
*source* is now a closed set: 38 files compile, and it owes 4570 nothing external.

## 4. What changed, in the tool

`tools/hfs_port_probe.sh`:

- A heredoc writes **`tree/port_shims.c`** — the ten bodies, one file, each commented with why it is a shim
  or a rename.
- The `LINK_GAP` stage now branches: at zero it prints what the ten are and that the port links; otherwise it
  prints the families as before.
- The "WHAT THE TWO NUMBERS MEAN" text is corrected to 38/38 and zero.

## 5. What this changes, and what it does not

- **The park backlog is not touched.** Rungs 57/58/59/60 stay parked and unpressed.
- **The filesystem clause's source half is CLOSED.** 868/869/870/871 together: it compiles (868), its journal
  is bought (869), the ten it owes are written (871), and the wiring is mapped (870). What is left for a mount
  is a build (the `hfs` option and the row) and a **medium**.
- **THE GOAL IS NOT MET.** The OS enters on a synthetic root; no HFS+ volume is mounted; the medium has never
  been read by the mounting OS; the eMMC driver clause is still open.

## 6. Safety

Unchanged and untouched: **no device, no `fastboot`, no switch, no press, no build of the kernel.** The probe
is host-only and writes nothing in the repository outside `tools/` and this document; `make check` is exit 0.