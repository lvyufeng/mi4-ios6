/*
 * Everything the HFS+ port ADDS to 4570, in one forced header (experiments 865/869/871/873).
 *
 * This header is the SINGLE DEFINITION of what the port owes 4570.  Two stages use it:
 *
 *   - the host-only probe, `tools/hfs_port_probe.sh`, force-includes it to measure the port in a
 *     /tmp sandbox (nothing in the tree is touched); and
 *   - the real build, `tools/stage_hfs.sh`, which stages 2050's HFS+ sources into 4570's tree and
 *     adds an `-include` of this file to the one component (bsd) that compiles them.
 *
 * A second copy of these values would be the project's "one value, two definitions" defect, so
 * there is one copy: this file.  `tools/check_hfs_staged.sh` (in `make check`) re-derives the tree's
 * staged state from it and refuses drift.
 *
 * Nothing here edits an Apple source file.  Every row names the 2050 file and line it comes from, so
 * a reader can tell an addition from a substitution.
 */
#ifndef _HFS_PORT_FORCE_H_
#define _HFS_PORT_FORCE_H_

/* ------------------------------------------------------------------------------------------------
 * 1. Additions - macros and constants 4570 does not declare.
 * --------------------------------------------------------------------------------------------- */

/* The five malloc types 2050's bsd/sys/malloc.h declares and 4570's does not.  At 2050's own numbers:
 * is not merely unused: 4570's kmzones[75/76/77/95] are KMZ_MALLOC rows, and NO visible row is 96 at
 * all (see the note below it). */
#define M_HFSMNT      75   /* 2050 bsd/sys/malloc.h:168 */
#define M_HFSNODE     76   /* 2050 bsd/sys/malloc.h:169 */
#define M_HFSFORK     77   /* 2050 bsd/sys/malloc.h:170 */
#define M_HFSDIRHINT  95   /* 2050 bsd/sys/malloc.h:188 */
/* M_HFSBITMAP was declared here as 96 and is REMOVED (experiment 898).  4570's kmzones[96] is
 * `SOS(cl_readahead)` + KMZ_CREATEZONE - i.e. 4570 defines M_CLRDAHEAD = 96 (bsd/sys/malloc.h:189,
 * verified), so 2050's :189 citation is false and 96 is NOT free.  Nothing in the staged HFS tree
 * reads M_HFSBITMAP (grep: zero call sites; 2050's malloc.h never declares it either), so the define
 * allocated nothing and its removal is behaviour-preserving - it only stops a future MALLOC from
 * landing in the wrong kmzone.  This is the port's own "one value, two definitions" rule applied to
 * its own file. */

/* Two more of the same kind, owed by the JOURNAL (2050 bsd/vfs/vfs_journal.c, `#if JOURNALING`):
 * the port brings that file, so its malloc types come with it.  Also free in 4570. */
#define M_JNL_JNL     91   /* 2050 bsd/sys/malloc.h:184 */
#define M_JNL_TR      92   /* 2050 bsd/sys/malloc.h:185 */

/* Two obsolete namei flags 4570 dropped.  Both are read only in the absurd branch (declare a wap for
 * a name that does not exist), so the VALUE does not matter and the port should not invent one that
 * looks meaningful.  Kept at 2050's numbers so a future reader can find them. */
#define DOWHITEOUT   0x00040000  /* 2050 bsd/sys/vnode.h:209, OBSOLETE */
#define ISWHITEOUT   0x00000080  /* 2050 bsd/sys/vnode.h, OBSOLETE */

/* 2050's journal layer uses a PRIVATE buf flag that 4570's buf_internal.h dropped.  4570 still has
 * B_ZALLOC (0x08000000) and B_COMMIT_UPL (0x40000000) around it, so 0x10000000 is free here and the
 * port can take 2050's own value.  Set exactly where 2050 set it, in `modify_block_start`. */
#define B_NORELSE   0x10000000  /* 2050 bsd/sys/buf_internal.h:216 - don't brelse() in bwrite() */

/* 4570's vfc_vfsflags has no DIRLINKS bit and nothing reads one.  **This value is a PLACEHOLDER and
 * the port must NOT take it as-is**: 0x020 is VFC_VFSCANMOUNTROOT in 4570's table, so the bit has to
 * be chosen free and its meaning registered with whatever reads it - which 4570's vfs_syscalls.c
 * dropped along with the bit. */
#define VFC_VFSDIRLINKS 0x800

/* 4570's kmem_alloc takes the owning memory tag; 2050's took three arguments.  One line here instead
 * of seven call sites.  VM_KERN_MEMORY_FILE is 4570's own answer for a filesystem's buffer. */
#define kmem_alloc(map, addr, size) kmem_alloc((map), (addr), (size), VM_KERN_MEMORY_FILE)

/* Its kobject sibling, dropped in the same 4570 restructuring (vm_kern.h:160 vs :240): the JOURNAL
 * calls it four times (vfs_journal.c:1120, 1708, 1878, 2061).  Same one-line shape. */
#define kmem_alloc_kobject(map, addr, size) kmem_alloc_kobject((map), (addr), (size), VM_KERN_MEMORY_FILE)

/* 4570's bsd/sys/cprotect.h dropped `cp_wrap_func_t` and `cp_register_wraps` (it restructured the
 * cprotect API).  This was a `void *` placeholder while the port compiled only at CONFIG_PROTECT=0;
 * now that the HFS-facing cprotect layer is ported (`hfs_cprotect_port.h`, experiment 877),
 * `cp_register_wraps` at `hfs_cprotect.c:97` dereferences it as `key_store_func->wrapper`, so the
 * TYPE must be the port's `struct cp_wrap_func` and not `void *`.  The forward declaration is enough
 * here; the definition follows in `hfs_cprotect_port.h`, which is force-included after this one. */
#ifndef cp_wrap_func_t
typedef struct cp_wrap_func *cp_wrap_func_t;
#endif

/* ------------------------------------------------------------------------------------------------
 * 1b. Scoped renames - 4570 MOVED these symbols into the common VFS after 2050, so 2050's HFS copies
 *     collide at link.
 *
 * Reached only by the FIRST real kernel link of the port (895's step): experiments 869/871/873 proved
 * 38/38 COMPILE and a zero link gap against the *probe's* stand-ins, and 885 linked the port only
 * into the *platform* objects. Linking the whole 37-file port together with 4570's own `vfs_subr.o`
 * and `vfs_cprotect.o` is what exposes these - measured, not predicted: `ld` reports
 * `multiple definition of` exactly these three, and no others (a `comm` of every HFS object's defined
 * symbols against the rest of the pool gives five mangled names collapsing to these three token
 * roots). Each is 2050's private copy of something 4570 now owns in the shared layer:
 *
 *   - `flush_cache_on_write` - 2050 defines it in `bsd/hfs/hfs_readwrite.c:94` (the HFS write path's
 *     own knob). 4570 has NO such symbol in its `vfs_subr.c` (verified), so it is a genuine HFS
 *     private global, not a superseded copy - but 4570's `vfs_subr.o` defines `flush_cache_on_write`
 *     (a `static`, but the `SYSCTL_INT(_kern, OID_AUTO, flush_cache_on_write, ...)` at
 *     `vfs_subr.c:9837` emits the non-static `sysctl__kern_flush_cache_on_write` + its
 *     `__set_...` linker-set entry, which DO collide with HFS's identically-named sysctl node).
 *     Renaming the token in HFS files renames HFS's sysctl node (the `##name` concatenation) and its
 *     pointer in the same stroke; 4570's own node is untouched.
 *   - `root_unmounted_cleanly` - same shape: 2050's `hfs_vfsops.c:1271` registers
 *     `SYSCTL_INT(_vfs_generic, OID_AUTO, root_unmounted_cleanly, ...)`; 4570's `vfs_subr.c:3919`
 *     registers the identical node name. Two NODES of one name is the collision.
 *   - `cp_key_store_action` - 2050's `hfs_cprotect.c` defines it (twice: the real body at `:79` and
 *     the `#else` stub at `:1745`, one of which compiles). 4570 MOVED this function into
 *     `bsd/vfs/vfs_cprotect.c:301`, and `bsd/sys/cprotect.h:182` still declares it - so 4570's
 *     `vfs_cprotect.o` owns the name. HFS's copy is dead (nothing in the HFS subset calls it), so the
 *     rename is a pure rename with no HFS caller to follow.
 *
 * These are **renames, not stand-ins**: the symbol survives in the tree, and the port keeps its own
 * copy private rather than pretending 4570's answer is its own (`mi4-one-value-two-definitions` - the
 * same defect that made `cp_is_valid_class` a rename in `hfs_cprotect_port.h`). A token rename is
 * safe here because each token is read in exactly ONE staged file (grep over `bsd/hfs/` +
 * `vfs_journal.c`: `flush_cache_on_write` -> hfs_readwrite.c only, `root_unmounted_cleanly` ->
 * hfs_vfsops.c only, `cp_key_store_action` -> hfs_cprotect.c only), and the forced header reaches no
 * other translation unit. The value of a sysctl node is its NAME, so HFS's knob moves to
 * `kern.stage90_hfs.*` rather than shadowing 4570's.
 * --------------------------------------------------------------------------------------------- */
#define flush_cache_on_write      stage90_hfs_flush_cache_on_write
#define root_unmounted_cleanly    stage90_hfs_root_unmounted_cleanly
#define cp_key_store_action       stage90_hfs_cp_key_store_action

/* ------------------------------------------------------------------------------------------------
 * 2. The port's own configuration options (experiments 865/869).
 *
 * 2050's `bsd/conf/MASTER` declares these as OPTIONS and 4570's does not carry HFS at all.  They are
 * what a real port turns on; without them every `#if HFS_COMPRESSION` block in HFS - including
 * `hfs_cnode.h`'s `c_decmp` field and the `VTOCMP` macro - is switched off, and `VTOCMP(vp)`
 * degrades to an implicit-int CALL (which is why one expression in `hfs_vfsutils.c` reads `int`).
 * These reach the cpp through the same forced header, so no `OPTIONS/hfs` row is owed in a
 * `conf/files` - the port's additions live in one tracked file rather than in rows of an untracked
 * one.  (`JTYPE_*`, `IOCTL_*` and `DEF_*` in 2050's HFS also read `HFS_COMPRESSION`/`HFS` and are
 * satisfied by these two.)
 * --------------------------------------------------------------------------------------------- */
#ifndef HFS
#define HFS 1                 /* 2050 bsd/conf/MASTER:188 - HFS/HFS+ support */
#endif
#ifndef HFS_COMPRESSION
#define HFS_COMPRESSION 1     /* 2050 bsd/conf/MASTER:193 - hfs compression */
#endif
#ifndef CONFIG_HFS_STD
#define CONFIG_HFS_STD 1      /* 2050 bsd/conf/MASTER:194 - hfs standard support */
#endif
/* MASTER:192.  The journal's real body is `#if JOURNALING` (vfs_journal.c:124); unset, the file
 * compiles its `#else` stub arm and the TRIM entry points HFS calls (journal_trim_set_callback,
 * journal_trim_add_extent, journal_trim_remove_extent) never exist. */
#ifndef JOURNALING
#define JOURNALING 1
#endif

/* ------------------------------------------------------------------------------------------------
 * 3. The HFS malloc TYPES must be served by the malloc path, not the zone path.
 *
 * THE DEFECT THIS FIXES (900).  `MALLOC_ZONE(space, cast, size, type, flags)` expands to
 * `__MALLOC_ZONE(size, type, flags, ...)`, and 4570's `__MALLOC_ZONE` (bsd/kern/kern_malloc.c:692)
 * **panics** when `kmzones[type].kz_zalloczone == KMZ_MALLOC`:
 *
 *     kmz = &kmzones[type];
 *     if (kmz->kz_zalloczone == KMZ_MALLOC) panic("_malloc_zone ZONE: type = %d", type);
 *
 * 2050 makes the five HFS rows ZONES by giving its `kern_malloc.c` the HFS structs
 * (`#include <hfs/hfs_cnode.h>`, 2050 kern_malloc.c:99) and rows
 * `{SOS(cnode), KMZ_CREATEZONE, TRUE}` / `{SOS(filefork), KMZ_CREATEZONE, TRUE}` for M_HFSNODE(76) /
 * M_HFSFORK(77).  This port's force header reaches ONLY the HFS translation units
 * (`build_xnu_arm_kernel.sh` force-includes it for `bsd/hfs/*` + `vfs_journal.c`), never
 * `kern_malloc.c`, so 4570's rows 75/76/77/91/92/95 stay the literal `{0, KMZ_MALLOC, FALSE}`
 * initializer (no `struct cnode`, no conditional).  The first `hfs_chash_getcnode` cnode allocation
 * therefore reaches `__MALLOC_ZONE(type = M_HFSNODE)` and **panics at `kern_malloc.c:706`** - inside
 * `hfs_chash_getcnode`, AFTER the mount's VCB fill (marker 2) and BEFORE its `return cp` (step 6),
 * which is exactly where 899's mount stops (`xnu_live_hfs_stage = 2`, one strategy read, no
 * `thread_block`, no data abort, no stub).
 *
 * THE FIX.  Redirect the HFS `MALLOC_ZONE`/`FREE_ZONE` onto the MALLOC path.  `__MALLOC` (the same
 * file, :571) does the same `type >= M_LAST` check and then `kalloc_canblock` - it never reads
 * `kmzones[]` - so it serves **any** type, including a `KMZ_MALLOC` row, without allocating an
 * entry size from a zone that does not exist.  `_FREE` :621 is the matching free.  The behaviour is
 * 2050's (2050's own `__MALLOC_ZONE` handles a `KMZ_LOOKUPZONE` row for M_HFSMNT by falling through
 * to `kalloc_zone`), so this is not a placeholder: an HFS cnode/filefork is kalloc'd and kfree'd
 * rather than drawn from a dedicated zone, which is the only correct answer when the zone table this
 * build links cannot be taught the HFS struct types.
 *
 * The redefine is safe where malloc.h has ALREADY been processed: those TUs expand the real macro at
 * their `MALLOC_ZONE(...)` site before this header's tail runs, so this only reaches TUs that first
 * pull malloc.h after the forced header (e.g. hfs_cnode.c, which includes it itself).  A TU that
 * expanded the zone path before this edit existed is recompiled by the same build. */

#include <sys/malloc.h>
#undef  MALLOC_ZONE
#undef  FREE_ZONE
#define MALLOC_ZONE(space, cast, size, type, flags)  ((space) = (cast)__MALLOC((size), (type), (flags), NULL))
#define FREE_ZONE(addr, size, type)                  _FREE((void *)(addr), (type))

#endif /* _HFS_PORT_FORCE_H_ */