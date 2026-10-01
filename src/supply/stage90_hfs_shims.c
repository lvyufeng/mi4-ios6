/*
 * The ten symbols 2050's HFS+ needs and 4570 does not define (experiments 869/871).
 *
 * 868 measured the port's link gap at 25 symbols in four families; 869 bought the journal and cut it
 * to ten; 871 wrote these ten bodies and measured the gap reaching ZERO.  This is that file: the
 * SHIMS half of the port, in the tree, compiled with the HFS sources by `tools/stage_hfs.sh`.
 *
 * Each is one line and each is honest about WHY it is a shim.  A symbol is a **RENAME** when the
 * function survives in 4570 under another name; it is an **EMPTY BODY** when 4570 dropped the STATE
 * the body would write, because then there is nothing to rename to and inventing one would be a
 * semantic claim the tree does not support.  That distinction is why four of the ten are empty:
 *
 *   - `fslog_fs_corrupt`             - 4570 has no fslog API (no fslog.h, no fslog_err).
 *   - `proc_tbe`                     - 4570's `P_TBE` is `P_RESV6` ("used to be P_TBE", proc.h:192).
 *   - `vfs_markdependency`           - 4570 has neither the function nor the `mnt_dependent_process`
 *                                      / `mnt_dependent_pid` fields it would have written.
 *   - `proc_apply_thread_selfdiskacc`- 4570 dropped `thread->appliedstate.hw_disk` entirely
 *                                      (`grep hw_disk` over its osfmk/ is empty).
 *
 * and three are renames (`vnode_name`->`vnode_getname`, `is_suser()`->`vfs_context_issuser()`,
 * `ubc_create_upl`->`ubc_create_upl_kernel` + a memory tag).  The last three are the BSD-side IOKit
 * shims HFS uses for the media's serial, ejectability and journal content; a ROOT filesystem needs
 * none of them, so each returns the failure that makes HFS fall back rather than claiming success.
 *
 * Nothing here is a stub that stops the run: these are real bodies for symbols 4570 genuinely lacks.
 */
#include <mach/kern_return.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <sys/vfs_context.h>
#include <sys/ubc.h>
#include <sys/ubc_internal.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/mount_internal.h>
#include <vm/vm_kern.h>

/* -------------------------------------------------------------------------------------------- renames */

const char *vnode_name(vnode_t vp) { return vnode_getname(vp); }
int is_suser(void) { return vfs_context_issuser(vfs_context_current()); }
int ubc_create_upl(vnode_t vp, off_t off, int size, upl_t *uplp, upl_page_info_t **plp, int flags)
{ return ubc_create_upl_kernel(vp, off, size, uplp, plp, flags, VM_KERN_MEMORY_FILE); }

/* ------------------------------------------------------------------- empty bodies - 4570 dropped the state */

int  proc_tbe(proc_t p) { (void)p; return 0; }
void vfs_markdependency(mount_t mp) { (void)mp; }
int  proc_apply_thread_selfdiskacc(int policy) { (void)policy; return 0; }
void fslog_fs_corrupt(mount_t mp) { (void)mp; }

/* ------------------------------------------------------- BSD-side IOKit shims - the failure that makes HFS fall back */

kern_return_t IOBSDGetPlatformSerialNumber(char *s, u_int32_t len) { (void)s; (void)len; return KERN_FAILURE; }
int IOBSDIsMediaEjectable(const char *cdev_name) { (void)cdev_name; return 0; }
void IOBSDIterateMediaWithContent(const char *uuid_cstring,
                                  int (*func)(const char *, const char *, void *), void *arg)
{ (void)uuid_cstring; (void)func; (void)arg; }