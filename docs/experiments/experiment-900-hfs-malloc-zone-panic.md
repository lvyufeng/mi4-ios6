# Experiment 900 — the HFS+ mount does not spin: it PANICS on its first cnode allocation

**Host-side root cause, from the machine and the linked image. No device action yet; the arm built
with the fix is pressed separately (901).**

## The finding

899 read the mount's live markers and saw `xnu_live_hfs_stage = 2` — the volume-header `bcopy` (1) and
the VCB fill (2) — with no `= 3`, and no `thread_block`, no data abort, no stub after either. It
concluded the mount **spins** in the extents `hfs_getnewvnode` (`hfs_chash_getcnode`'s prologue or
its body). It does not spin. **It panics**, on 4570's own assertion, at the first cnode allocation,
inside `hfs_chash_getcnode`, *before* the step-6 marker. A panic explains every 899 observation at
once: the mount thread, after the panic, never makes another call (no `thread_block`), never reads
again (one strategy read at offset `0x400`), and never faults (all `sleh_*` and `abort_entries` zero).

## The mechanism (from the linked image, not from reasoning)

`hfs_chash_getcnode` (staged `bsd/hfs/hfs_chash.c:406`) allocates a cnode with:

```c
MALLOC_ZONE(ncp, struct cnode *, sizeof(struct cnode), M_HFSNODE, M_WAITOK);
```

`MALLOC_ZONE` expands to `__MALLOC_ZONE(size, type, flags, &site)`, and 4570's `__MALLOC_ZONE`
(`bsd/kern/kern_malloc.c:692`) begins:

```c
kmz = &kmzones[type];
if (kmz->kz_zalloczone == KMZ_MALLOC)
        panic("_malloc_zone ZONE: type = %d", type);
```

In the linked entry image this is three instructions — `ldr r0,[kmzones+4+12*type]; cmp r0,#0;
bl panic` (`0x8019f360`–`0x8019f378`) — and the image's `kmzones[76]` (M_HFSNODE,
`0x806151a8`) is `{0x00000000, 0x00000000, 0x00000000}`, i.e. `kz_zalloczone = KMZ_MALLOC`. The
call site in `hfs_chash_getcnode` passes `r1 = 76`. So the first cnode allocation branches straight
into `panic("_malloc_zone ZONE: type = 76")`.

## Why 4570's rows are KMZ_MALLOC — and 2050's are not

`kmzones[]` is a `static` table in `bsd/kern/kern_malloc.c`; the zone initializer sizes each row with
`SOS(struct)` / `SOX(struct)`, so a row can only name a struct the file can see. **2050's
`kern_malloc.c` includes the HFS structs** (`#include <hfs/hfs_cnode.h>`, 2050 line 99) and its rows
are zones:

| row | 2050 (`kern_malloc.c:421-423`) | 4570 (literal `{0, KMZ_MALLOC, FALSE}`) |
|---|---|---|
| 75 M_HFSMNT | `{SOX(hfsmount), KMZ_LOOKUPZONE, FALSE}` | `{0, KMZ_MALLOC, FALSE}` |
| 76 M_HFSNODE | `{SOS(cnode), KMZ_CREATEZONE, TRUE}` | `{0, KMZ_MALLOC, FALSE}` |
| 77 M_HFSFORK | `{SOS(filefork), KMZ_CREATEZONE, TRUE}` | `{0, KMZ_MALLOC, FALSE}` |

4570 carries no HFS, so its `kern_malloc.c` cannot name `struct cnode`/`struct filefork` and its rows
75/76/77 (and 91/92/95, the journal types) are the `#else` arm — literal zeros. 2050's `#if HFS`
branch is simply absent from the file this project compiles.

**The port's forced header cannot reach that table.** `tools/build_xnu_arm_kernel.sh:1039` force-includes
`hfs_port_force.h` only for `${src} == bsd/hfs/* || bsd/vfs/vfs_journal.c`; `kern_malloc.c` is neither,
and `struct kmzones` is file-local to `kern_malloc.c` (the only other references are `struct kmzones *`
forward pointers, does not need the body — they do not need the type). So the port's `#define
M_HFSNODE 76` tells the HFS files which type number to use, and `kmzones[76]` is left with no zone.

This is the project's own **`mi4-one-value-two-definitions`** defect in a new place: "the HFS malloc
types are zones" is stated by the HFS files' `#define`s and delivered by a table in a different
translation unit that never sees them.

## The fix

`MALLOC_ZONE`'s ZONE path is the one that panics on a `KMZ_MALLOC` row; the MALLOC path is not.
`__MALLOC` (the same file, `kern_malloc.c:571`) does the same `type >= M_LAST` check and then
`kalloc_canblock` — it **never reads `kmzones[]`** — so it serves any type. `_FREE` (`:621`) is the
matching free. 2050's own `__MALLOC_ZONE` handles a non-zone row by falling through to `kalloc_zone`,
so using the malloc path for these types is 2050's behaviour, not a placeholder: an HFS cnode/filefork
is kalloc'd and kfree'd rather than drawn from a dedicated zone, which is the only correct answer when
`kmzones[76/77/91/92/95]` are zero rows this build cannot turn into zones.

The edit is a §3 in `src/shims/hfs/hfs_port_force.h`:

```c
#include <sys/malloc.h>
#undef  MALLOC_ZONE
#undef  FREE_ZONE
#define MALLOC_ZONE(space, cast, size, type, flags)  ((space) = (cast)__MALLOC((size), (type), (flags), NULL))
#define FREE_ZONE(addr, size, type)                  _FREE((void *)(addr), (type))
```

It covers **every** type an HFS `MALLOC_ZONE`/`FREE_ZONE` names — the staging grep is
`M_HFSNODE`, `M_HFSFORK`, `M_HFSDIRHINT`, `M_JNL_JNL`, `M_JNL_TR` (the journal's two), and
`M_DECMPFS_CNODE` (which 4570 *does* make a zone, so the malloc path is only redundant there) — and
no HFS file calls `_MALLOC_ZONE`/`_FREE_ZONE` directly, so nothing escapes the macro.

## Evidence the fix lands, before any build

- The port's own probe (`tools/hfs_port_probe.sh`) is **38/38** with the §3 in place; the macro
  preprocesses to `__MALLOC(...)` / `_FREE(...)`.
- The probe's `hfs_chash.o` now has **undefined `_FREE` + `__MALLOC`** (was `__MALLOC_ZONE` +
  `_FREE_ZONE`); `arm-none-eabi-nm` on the probe's ELF object shows exactly those two.
- `make check` exits 0, including `check_hfs_staged` (the force-header additions are re-derived).

## Where this leaves 899

899's markers are still true (the mount reaches step 2 and stops) and its "no `thread_block`" reading
still holds — but **the stop is a panic, not a spin**, and the site is `hfs_chash_getcnode`'s
`MALLOC_ZONE`, not a loop in `hfs_getnewvnode`'s prologue. The finer markers 6/7/8 (899's second arm)
are consistent: 6 sits *after* `hfs_chash_getcnode` returns, so a panic inside it can never reach 6.

## Goal status

**GOAL NOT MET.** The root still does not mount. This closes the *cause* of 899's stop with a fix that
is verified host-side; whether the mount then reaches step 3 (and the extents `BTOpenPath`, and
`/sbin/launchd` from the volume) is the next press's question.