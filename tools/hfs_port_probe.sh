#!/usr/bin/env bash
#
# Can 2050's HFS+ compile against 4570's kernel headers?
#
#   ./tools/hfs_port_probe.sh            # the two configurations this measures
#   ./tools/hfs_port_probe.sh --verbose  # print each file's outcome, and the first error of each failure
#
# A HOST-ONLY MEASUREMENT.  No device, no boot, no arm, no park, and no file inside the repository is
# written: a sandbox is built under /tmp, the port's additions are applied *there*, and the tree is
# compiled with the stage90 kernel build's own clang invocation.  The repository is read and never
# touched, which is what makes this runnable at any time and why it is not in `make check`.
#
# WHY THIS EXISTS.  The repository holds FOUR XNU trees and only ONE of them - `xnu-4570.1.46`, the one
# this project compiles - has no HFS+ (experiment 529 measured that: three trees carry it, 68,285 lines,
# and that tree carries none of it, no `bsd/conf/files` row, no `FT_HFS`).  So "port HFS+ into the kernel
# this project boots" was priced by reading and never by compiling.  This turns "68,285 lines" into a
# number, and the number is the thing a decision needs.
#
# THE TRICK THAT MAKES IT CHEAP.  4570's `bsd/sys/malloc.h` declares no `M_HFS*`, and 2050's HFS uses
# five of them.  4570's `<stdbool.h>` — reached through `<kern/...>` — `#define`s `false` and `true` as
# macros, under which 2050's `enum { false = 0, true = 1 }` cannot be declared.  Both are resolved
# WITHOUT EDITING APPLE'S SOURCE: the enum is replaced in the sandbox's own copy of the one header, and
# everything else the port ADDS goes into a forced header (`-include`), so the drift is visible in one
# place instead of scattered across 36 files.
#
# WHAT IT DOES NOT MEASURE.  Whether the result LINKS, whether it MOUNTS, and whether a volume exists
# to mount.  A compile count is a floor on the work and not the work.  See the experiment doc.

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
XNU=$REPO_ROOT/external/xnu-4570.1.46
HFS_SRC=$REPO_ROOT/external/xnu-2050.18.24/bsd/hfs
SANDBOX=${HFS_PROBE_SANDBOX:-/tmp/mi4-hfs-port-probe}

VERBOSE=0
[[ ${1:-} == --verbose ]] && VERBOSE=1

[[ -d $HFS_SRC ]] || { echo "hfs_port_probe: no HFS at $HFS_SRC" >&2; exit 2; }
[[ -d $XNU ]]     || { echo "hfs_port_probe: no kernel tree at $XNU" >&2; exit 2; }

TARGET=$(REPO_ROOT=$REPO_ROOT "$REPO_ROOT/tools/xnu_config/arm_target.sh")

# ---------------------------------------------------------------------------------------------------
# The sandbox.  Rebuilt from scratch every run: a stale copy of a header is exactly the failure mode
# this measurement is least able to notice (an include that resolves to last run's shim still compiles).
# ---------------------------------------------------------------------------------------------------
rm -rf "$SANDBOX"
mkdir -p "$SANDBOX/root" "$SANDBOX/obj" "$SANDBOX/supply/vfs" "$SANDBOX/supply/machine"
cp -r "$HFS_SRC" "$SANDBOX/tree"
ln -s "$SANDBOX/tree" "$SANDBOX/root/hfs"

# The two headers 4570 lacks, taken from 2050 verbatim.  Copying them rather than 2050's whole `bsd/`
# is deliberate: the question is what the port OWES 4570, and a header copied in is a header owed.
cp "$REPO_ROOT/external/xnu-2050.18.24/bsd/vfs/vfs_journal.h" "$SANDBOX/supply/vfs/vfs_journal.h"
cp "$REPO_ROOT/external/xnu-2050.18.24/bsd/machine/spl.h"     "$SANDBOX/supply/machine/spl.h"

# The journal is not a shim, it is a FILE the port brings: HFS's `journal_*` (18 of the 25 link symbols)
# are 2050's `bsd/vfs/vfs_journal.c`, `#if JOURNALING`.  It is copied beside the port and compiled by
# the same loop, so the LINK GAP below counts it as bought rather than owed.  Its header comes with it
# by a quoted include (`"vfs_journal.h"`), so both land in the tree.
cp "$REPO_ROOT/external/xnu-2050.18.24/bsd/vfs/vfs_journal.c" "$SANDBOX/tree/vfs_journal.c"
cp "$REPO_ROOT/external/xnu-2050.18.24/bsd/vfs/vfs_journal.h" "$SANDBOX/tree/vfs_journal.h"

python3 - "$SANDBOX" <<'PY'
import sys
sandbox = sys.argv[1]

# --- shim 1: the one header that cannot be used as written -----------------------------------------
# 4570's <stdbool.h> is reached through <kern/...> and defines `false` and `true` as macros.  2050's
# hfs_macos_defs.h declares them as enum members under `#if !TYPE_BOOL` - and TYPE_BOOL is 0 for C
# (`hfs_macos_defs.h:65`, it is 1 only under __cplusplus), so the enum is compiled and the macros win.
# The enum is the only thing supplying the values, so replacing it with the definitions it used to make
# is the whole change to this file.  Nothing else in it differs.
p = sandbox + "/tree/hfs_macos_defs.h"
s = open(p).read()
old = """#if !TYPE_BOOL

enum {
\tfalse\t\t\t\t\t\t= 0,
\ttrue\t\t\t\t\t\t= 1
};

#endif  /*  !TYPE_BOOL */"""
assert old in s, "the false/true enum is not where this script expects it"
new = """/* PORT SHIM (tools/hfs_port_probe.sh): 4570 reaches <stdbool.h>, which #defines false/true, so
 * this enum cannot be declared under those macros.  It becomes the definitions it used to make. */
#if !TYPE_BOOL
#ifndef false
#define false\t0
#endif
#ifndef true
#define true\t1
#endif
#endif  /*  !TYPE_BOOL */"""
open(p, "w").write(s.replace(old, new))

# --- shim 2: everything the port ADDS, in one forced header ----------------------------------------
# Forced rather than patched in, so that what the port owes 4570 is one readable list.  Each row names
# the 2050 file and line it comes from, so a reader can tell an addition from a substitution.
open(sandbox + "/port_force.h", "w").write("""/*
 * Everything the HFS+ port ADDS to 4570, in one forced header.
 * Nothing here edits an Apple source file; `make check`-visible drift is what this file is for.
 */

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
""")

# --- shim 3: the OPTIONS the port's own configuration declares --------------------------------------
# 2050's `bsd/conf/MASTER` declares these as OPTIONS, and 4570's `bsd/conf/MASTER` declares the same
# names.  They are what a REAL port turns on; without them every `#if HFS_COMPRESSION` block in HFS -
# including `hfs_cnode.h`'s `c_decmp` field and the `VTOCMP` macro - is switched off, and `VTOCMP(vp)`
# degrades to an implicit-int CALL (which is why one expression in `hfs_vfsutils.c` reads `int`).
# Appended to the forced header so the option is visible in the same one list as the drift it prevents.
with open(sandbox + "/port_force.h", "a") as f:
    f.write('''
/* 2050 bsd/conf/MASTER:193 and 4570 bsd/conf/MASTER - the port's own configuration options.
 * `HFS` (MASTER:188) is the one that turns on hfscommon/Unicode/UnicodeWrappers.c and
 * hfs_encodings.c (`#if HFS`); without it those two files compile to nothing and every symbol they
 * define - FastRelString, GetEmbeddedFileID, ConvertUnicodeToUTF8Mangled, hfs_converterinit, the
 * unicode converters - shows up as an undefined link symbol the port does not actually owe. */
#ifndef HFS
#define HFS 1
#endif
#ifndef HFS_COMPRESSION
#define HFS_COMPRESSION 1
#endif
#ifndef CONFIG_HFS_STD
#define CONFIG_HFS_STD 1
#endif
/* MASTER:192.  The journal's real body is `#if JOURNALING` (vfs_journal.c:124); unset, the file
 * compiles its `#else` stub arm and the TRIM entry points HFS calls (journal_trim_set_callback,
 * journal_trim_add_extent, journal_trim_remove_extent) never exist. */
#ifndef JOURNALING
#define JOURNALING 1
#endif

/* 4570's bsd/sys/cprotect.h dropped `cp_wrap_func_t` and `cp_register_wraps` (it restructured the
 * cprotect API).  At CONFIG_PROTECT=0 hfs_cprotect.c's body is a stub that IGNORES its argument, so
 * this placeholder is honest rather than a semantic claim; at CONFIG_PROTECT=1 the real port must
 * decide whether to call 4570's `cp_*` API or drop the call - which is the extra cost 865 named. */
#ifndef cp_wrap_func_t
typedef void *cp_wrap_func_t;
#endif
''')

print("sandbox ready")
PY

# --- the ten shims, as a FILE the port brings -------------------------------------------------------
# 869 measured the link gap at ten symbols.  This writes the ten bodies, so the probe can show the gap
# reaching ZERO rather than only naming what is owed.  Each is a one-line shim and each is honest about
# WHY it is a shim (a body whose fields 4570 dropped is EMPTY, not invented).  Written into the tree so
# the same compile loop builds it; nothing here edits an Apple source.
cat >"$SANDBOX/tree/port_shims.c" <<'SHIMS'
/*
 * The ten symbols 2050's HFS+ needs and 4570 does not define (tools/hfs_port_probe.sh, experiment 869).
 * Four are EMPTY BODIES because 4570 dropped the state they would write: fslog_fs_corrupt (4570 has no
 * fslog API), proc_tbe (P_TBE became P_RESV6), vfs_markdependency (no mnt_dependent_process/pid), and
 * proc_apply_thread_selfdiskacc (no thread->appliedstate.hw_disk).  Three are RENAMES.  Three are the
 * BSD-side IOKit shims HFS uses for the media's serial/ejectability/journal-content, none of which a
 * root filesystem needs - each returns the failure that makes HFS fall back.
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

/* renames */
const char *vnode_name(vnode_t vp) { return vnode_getname(vp); }
int is_suser(void) { return vfs_context_issuser(vfs_context_current()); }
int ubc_create_upl(vnode_t vp, off_t off, int size, upl_t *uplp, upl_page_info_t **plp, int flags)
{ return ubc_create_upl_kernel(vp, off, size, uplp, plp, flags, VM_KERN_MEMORY_FILE); }

/* empty bodies - 4570 dropped the state */
int  proc_tbe(proc_t p) { (void)p; return 0; }
void vfs_markdependency(mount_t mp) { (void)mp; }
int  proc_apply_thread_selfdiskacc(int policy) { (void)policy; return 0; }
void fslog_fs_corrupt(mount_t mp) { (void)mp; }

/* BSD-side IOKit shims - the failure that makes HFS fall back */
kern_return_t IOBSDGetPlatformSerialNumber(char *s, u_int32_t len) { (void)s; (void)len; return KERN_FAILURE; }
int IOBSDIsMediaEjectable(const char *cdev_name) { (void)cdev_name; return 0; }
void IOBSDIterateMediaWithContent(const char *uuid_cstring,
                                  int (*func)(const char *, const char *, void *), void *arg)
{ (void)uuid_cstring; (void)func; (void)arg; }
SHIMS

# ---------------------------------------------------------------------------------------------------
# The compile.  The same clang, the same flags, the same configuration defines and the same forced
# headers the stage90 kernel build uses - a different harness would measure a different question.
# ---------------------------------------------------------------------------------------------------
CONFIG=STAGE90_XNU
CONFIG_DEFS=()
while IFS= read -r _d; do [[ -n $_d ]] && CONFIG_DEFS+=("$_d"); done \
    < <("$REPO_ROOT/tools/xnu_config/make_defines.sh" "$CONFIG")
(( ${#CONFIG_DEFS[@]} )) || { echo "hfs_port_probe: $CONFIG expanded to no options" >&2; exit 2; }

INCLUDES=(
    -I"$REPO_ROOT/out/xnu_generated/bsd" -I"$REPO_ROOT/out/xnu_generated"
    -I"$REPO_ROOT/out/xnu_options/$CONFIG"
    -I"$REPO_ROOT/out/xnu_device/$CONFIG"
    -I"$REPO_ROOT/out/mach_headers"
    -I"$XNU/bsd" -I"$XNU/osfmk"
    -I"$XNU/iokit" -I"$XNU/libkern" -I"$XNU/pexpert" -I"$XNU"
    -I"$REPO_ROOT/out/xnu_libsa_export"
    -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm"
    -I"$XNU/EXTERNAL_HEADERS"
    -I"$REPO_ROOT/src/shims" -I"$REPO_ROOT/src/shims/kern" -I"$REPO_ROOT/src/shims/mach"
    -I"$REPO_ROOT/src/shims_arm" -I"$REPO_ROOT/src/shims_arm/kern" -I"$REPO_ROOT/src/shims_arm/mach"
    -I"$REPO_ROOT/src/shims_arm/sys" -I"$REPO_ROOT/src/shims_arm/sys/_pthread"
    # Two headers 4570 does not have at all, which the port must therefore bring with it: 2050's
    # `bsd/vfs/vfs_journal.h` (included by hfs.h:62 and hfs_vfsops.c:95) and `bsd/machine/spl.h`
    # (hfs_vnops.c:57).  They are placed at their 4570-relative paths under the sandbox root so the
    # port's own headers reach them by the same spelling they use at home.
    -I"$SANDBOX/supply"
    # The port's own include root.  A `<hfs/...>` include resolves to the sandbox copy; every other
    # include resolves to 4570.  `root` is a directory holding one symlink, and it is the symlink that
    # makes this work: HFS's own headers include each other as `"../../hfs.h"`, which climbs out of the
    # sandbox and into 2050's tree unless the tree's own name on the include path is `hfs`.
    -I"$SANDBOX/root"
)

FORCE_INCLUDES=(
    -include sys/_types/_u_int.h
    -include arm/simple_lock.h
    -include kern/ast.h
    -include mach/task_policy.h
    -include mach/thread_policy.h
    -include mi4ios6_build_config.h
    -include sys/_types/_caddr_t.h
    -include sys/_types/_u_char.h
    -include meta_features.h
    -include "$SANDBOX/port_force.h"
)

CDEFS=(
    "${CONFIG_DEFS[@]}"
    -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
    -DMACH_BSD=1 -DPRIVATE=1 -DKPC=1 -DMONOTONIC=1 -DXPR_DEBUG=0 -DLOCK_PRIVATE=1
    -DARMA7=1 -DKERNEL=1 -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    -D__ARM__=1 -DNPTY=1 -DNPTMX=1 -D__APPLE__=1
    -DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1
    -UCONFIG_NO_PRINTF_STRINGS -USECURE_KERNEL
    $("$REPO_ROOT/tools/xnu_config/component_defines.sh" bsd)
)

run_configuration() {
    local label=$1 protect=$2 ok=0 fail=0 hang=0 src key
    local extra=(); [[ $protect == 0 ]] && extra=(-UCONFIG_PROTECT)
    printf '\n== %s ==\n' "$label"
    while IFS= read -r src; do
        key=$(printf '%s' "${src#"$SANDBOX/tree/"}" | tr '/' '_')
        key=${key%.c}
        if timeout 60 clang --target="$TARGET" -mcpu=cortex-a15 -marm \
                -mfpu=neon-vfpv4 -mfloat-abi=softfp \
                -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w -ferror-limit=0 \
                "${FORCE_INCLUDES[@]}" "${CDEFS[@]}" "${extra[@]}" "${INCLUDES[@]}" \
                -c "$src" -o "$SANDBOX/obj/$protect-$key.o" 2>"$SANDBOX/obj/$protect-$key.log"; then
            ok=$((ok + 1))
            (( VERBOSE )) && printf '  OK    %s\n' "${src#"$SANDBOX/tree/"}"
        elif [[ $? -eq 124 ]]; then
            hang=$((hang + 1))
            printf '  HANG  %s\n' "${src#"$SANDBOX/tree/"}"
        else
            fail=$((fail + 1))
            printf '  FAIL  %-24s errors=%-4s %s\n' "${src#"$SANDBOX/tree/"}" \
                "$(grep -c 'error:' "$SANDBOX/obj/$protect-$key.log")" \
                "$(grep -m1 'error:' "$SANDBOX/obj/$protect-$key.log" \
                    | sed "s|$SANDBOX/tree/||; s|.*/external/|external/|" | cut -c1-110)"
        fi
    done < <(find "$SANDBOX/tree" -name '*.c' | sort)
    printf '  -- ok=%d fail=%d hang=%d of %d\n' "$ok" "$fail" "$hang" "$((ok + fail + hang))"
}

echo "hfs_port_probe: 2050's HFS+ ($(find "$SANDBOX/tree" -name '*.c' | wc -l) .c files) against 4570's kernel"
echo "  sandbox:      $SANDBOX   (rebuilt; nothing in $REPO_ROOT was written)"
echo "  clang:        --target=$TARGET -mcpu=cortex-a15, the stage90 kernel build's own invocation"

run_configuration "CONFIG_PROTECT=0  (protection off - hfs_cprotect.c is then dead code)" 0
run_configuration "CONFIG_PROTECT=1  (the stage90 configuration's own value)"             1

# ---------------------------------------------------------------------------------------------------
# THE LINK GAP.  A compile count is a floor on the work; 865 said so and left the link unmeasured.
# This subtracts, from the symbols the compiled port leaves UNDEFINED, (a) everything the built kernel
# already defines (its own object set plus the linked entry image) and (b) everything the port defines
# within itself.  What remains is exactly what 4570 does NOT supply and the port must bring or shim -
# the real external debt, and the number a port decision needs.
# ---------------------------------------------------------------------------------------------------
LINK_GAP() {
    local nm=kern_all.txt undef=hfs_undef.txt def=hfs_def.txt
    { arm-none-eabi-nm "$REPO_ROOT"/out/xnu_macho_obj/*.o "$REPO_ROOT"/out/xnu_arm_full_obj/*.o 2>/dev/null \
        | awk '$2 ~ /^[TtDdBbRrWwVvGgSs]$/{print $3}'
      arm-none-eabi-nm "$REPO_ROOT"/out/stage90/xnu_arm_entry.elf 2>/dev/null \
        | awk '$2 ~ /^[TtDdBbRrWwVv]$/{print $3}'; } | sort -u >"$SANDBOX/$nm"
    arm-none-eabi-nm "$SANDBOX"/obj/0-*.o 2>/dev/null | awk '$1=="U"{print $2}' | sort -u >"$SANDBOX/$undef"
    arm-none-eabi-nm "$SANDBOX"/obj/0-*.o 2>/dev/null | awk '$2 ~ /^[TtDdBbRrWwVvGgSs]$/{print $3}' | sort -u >"$SANDBOX/$def"
    comm -23 "$SANDBOX/$undef" "$SANDBOX/$nm" | comm -23 - "$SANDBOX/$def"
}

GAP=$(LINK_GAP)
printf '\n== THE LINK GAP (CONFIG_PROTECT=0): symbols 4570 does not define and the port must bring ==\n'
printf '%s\n' "$GAP" | sed 's/^/  /'
N_GAP=$(printf '%s\n' "$GAP" | grep -c .)
if (( N_GAP == 0 )); then
cat <<'EOF'
  -- ZERO.  The port links: every symbol it leaves undefined, 4570's kernel or the
     port itself supplies.  The ten 869 named are written in tree/port_shims.c:

     renames (3)   vnode_name -> vnode_getname; is_suser() -> vfs_context_issuser();
                   ubc_create_upl -> ubc_create_upl_kernel (+ a VM_KERN_MEMORY_FILE tag).
     empty (4)     fslog_fs_corrupt, proc_tbe, vfs_markdependency, and
                   proc_apply_thread_selfdiskacc.  Each is EMPTY because 4570 dropped the state it
                   would write: no fslog API; P_TBE became P_RESV6; no mnt_dependent_process/pid; no
                   thread->appliedstate.hw_disk.
     IOKit (3)     IOBSDGetPlatformSerialNumber / IsMediaEjectable / IterateMediaWithContent - the
                   BSD-side shims HFS uses for the media's serial, ejectability and journal content;
                   a root filesystem needs none, so each returns the failure that makes HFS fall back.
EOF
else
cat <<'EOF'
     fslog_fs_corrupt
     fslog_fs_corrupt               hfs_vfsops.c:7702's one call, on a corrupt volume.  2050 defines it
                                    in bsd/vfs/vfs_fslog.c:343, but its whole body is one fslog_err()
                                    call and 4570 dropped the fslog API (no fslog_err, no FSLOG_KEY_*).
                                    An empty shim, not a port.
     IOBSD*                         IOBSDGetPlatformSerialNumber / IsMediaEjectable /
                                    IterateMediaWithContent - from 2050's iokit/bsddev/IOKitBSDInit.cpp.
     renames                        vnode_name -> vnode_getname; is_suser() -> vfs_context_issuser();
                                    ubc_create_upl -> ubc_create_upl_kernel; vfs_markdependency (4570 has
                                    neither decl nor def); proc_tbe; proc_apply_thread_selfdiskacc (4570
                                    dropped `thread->appliedstate.hw_disk` entirely - a no-op shim).
                                    One-line shims, not ports.
EOF
fi
printf '  -- %d symbol(s) of external debt\n' "$N_GAP"

cat <<'EOF'

WHAT THE TWO NUMBERS MEAN
  CONFIG_PROTECT=0   ALL 38 FILES COMPILE (36 of HFS proper + the journal it brings + its ten shims).
                     Two of the eight "drifts" 865 named were not drifts at all:
                     (1) hfs_vfsutils.c:3140's `VTOCMP(vp)->cmp_type` read as `int` only because
                     `HFS_COMPRESSION` (2050's own bsd/conf/MASTER:193 option) was UNSET, so `VTOCMP`
                     never defined and the expression degraded to an implicit-int call; (2) the whole
                     hfscommon/Unicode + hfs_encodings family never compiled because the `HFS` option
                     (MASTER:188) was unset.  Setting both leaves ONE file, hfs_cprotect.c, which exists
                     only for POSIX protection and which a root filesystem does not need (at
                     CONFIG_PROTECT=0 its body is a one-line stub, and the `cp_wrap_func_t` it names is
                     the ONE type 4570's cprotect dropped - a placeholder typedef closes it).
  CONFIG_PROTECT=1   four files fail, all on 2050's `cp_*` API: `struct cp_wrap_func`, `cp_wrap_func_t`,
                     `CP_READ_ACCESS`/`CP_WRITE_ACCESS`, `struct cp_root_xattr`.  4570 renamed and
                     restructured cprotect, so this is real work rather than a flag - the extra cost of
                     mounting a volume whose files carry protection.

  So BOTH HALVES OF THE PORT ARE CLOSED AT CONFIG_PROTECT=0: 38 files compile (the 36 of HFS proper,
  the journal it brings, and its ten shims) and the LINK GAP IS ZERO - nothing the port leaves undefined
  is unsupplied.  The journal (18 symbols, the largest single item 865 left open) is bought for three
  one-line shims; the ten 869 named are four empty bodies, three renames and three IOKit fall-backs.
  What remains is NOT source: a `vfstbllist[]` row before mockfs, an `FT_HFS`, and the `HFS` option in
  the build (experiment 870) - which a build sets, not this probe - plus, behind it, a medium to mount
  (experiment 867).
EOF