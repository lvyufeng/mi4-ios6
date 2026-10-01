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
 * 4570's highest M_* is 128 and 75/76/77/95/96 are all free, so no other type moves. */
#define M_HFSMNT      75   /* 2050 bsd/sys/malloc.h:168 */
#define M_HFSNODE     76   /* 2050 bsd/sys/malloc.h:169 */
#define M_HFSFORK     77   /* 2050 bsd/sys/malloc.h:170 */
#define M_HFSDIRHINT  95   /* 2050 bsd/sys/malloc.h:188 */
#define M_HFSBITMAP   96   /* 2050 bsd/sys/malloc.h:189 */

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
 * cprotect API).  At CONFIG_PROTECT=0 hfs_cprotect.c's body is a stub that IGNORES its argument, so
 * this placeholder is honest rather than a semantic claim; at CONFIG_PROTECT=1 the real port must
 * decide whether to call 4570's `cp_*` API or drop the call - which is the extra cost 865 named. */
#ifndef cp_wrap_func_t
typedef void *cp_wrap_func_t;
#endif

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

#endif /* _HFS_PORT_FORCE_H_ */