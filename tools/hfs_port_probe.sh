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

/* Two obsolete namei flags 4570 dropped.  Both are read only in the absurd branch (declare a wap for
 * a name that does not exist), so the VALUE does not matter and the port should not invent one that
 * looks meaningful.  Kept at 2050's numbers so a future reader can find them. */
#define DOWHITEOUT   0x00040000  /* 2050 bsd/sys/vnode.h:209, OBSOLETE */
#define ISWHITEOUT   0x00000080  /* 2050 bsd/sys/vnode.h, OBSOLETE */

/* 4570's vfc_vfsflags has no DIRLINKS bit and nothing reads one.  **This value is a PLACEHOLDER and
 * the port must NOT take it as-is**: 0x020 is VFC_VFSCANMOUNTROOT in 4570's table, so the bit has to
 * be chosen free and its meaning registered with whatever reads it - which 4570's vfs_syscalls.c
 * dropped along with the bit. */
#define VFC_VFSDIRLINKS 0x800

/* 4570's kmem_alloc takes the owning memory tag; 2050's took three arguments.  One line here instead
 * of seven call sites.  VM_KERN_MEMORY_FILE is 4570's own answer for a filesystem's buffer. */
#define kmem_alloc(map, addr, size) kmem_alloc((map), (addr), (size), VM_KERN_MEMORY_FILE)
""")
print("sandbox ready")
PY

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

cat <<'EOF'

WHAT THE TWO NUMBERS MEAN
  CONFIG_PROTECT=0   all but two files compile, and one of the two is hfs_cprotect.c - a file that
                     exists only for POSIX file protection and that a root filesystem does not need.
                     The other is ONE EXPRESSION: hfs_vfsutils.c:3140's `VTOCMP(vp)->cmp_type`, where
                     4570 keeps `decmpfs_cnode.c_decmp` as an `int` slot on the cnode and 2050 keeps a
                     pointer.  So "protection off" is one file dropped and one line changed.
  CONFIG_PROTECT=1   four more files fail, all of them on 2050's `cp_*` API: `struct cp_wrap_func` and
                     `cp_wrap_func_t`, `CP_READ_ACCESS`/`CP_WRITE_ACCESS`, `struct cp_root_xattr`.  4570
                     renamed and restructured cprotect, so this is real work rather than a flag - it is
                     the extra cost of mounting a volume whose files carry protection.
EOF