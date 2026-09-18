#!/usr/bin/env bash
#
# Generate the Mach interface headers XNU's kernel sources include, using the MIG built by
# tools/build_mig.sh.
#
#   ./tools/gen_mach_headers.sh              # generate every kernel .defs in the tree
#   ./tools/gen_mach_headers.sh mach_host mach_port    # only these (by basename)
#   ENTRY_PATH_ONLY=1 ./tools/gen_mach_headers.sh      # only the set the ARM entry path reaches
#
# Why this exists. `osfmk/vm/vm_object.h` includes `<mach_pagemap.h>`, `osfmk/mach_debug/
# mach_debug.h` includes `<mach/mach_host.h>` and `<mach/mach_port.h>`, and none of those exist in
# the xnu-4570.1.46 tarball — 40 `.defs` files ship under `osfmk/mach/` and their headers do not,
# because Apple's build generates them. That was recorded as the sharpest component of Phase 4's
# wall. It is not a wall any more; it is this script.
#
# How MIG is invoked, which is not obvious. MIG does not take a filename — it reads the `.defs`
# from **stdin**, preprocessed. Apple's own `mig.sh` does:
#
#     (echo '#line 1 "'"${file}"'"; cat "${file}") > "${temp}.c"
#     "$C" -E -arch ${arch} ... "${temp}.c" | "$M" "${migflags[@]}"
#
# so the pipeline below is that, with cc -E in place of the SDK's clang and no -arch, because the
# .defs language is architecture-independent — the generated header is what carries type widths.
#
# What to check on the output: the header's guard, its includes, and that `__MigTypeCheck` blocks
# are present. A MIG that produced an empty or trivial header would exit 0 and look like success.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
MIG=${MIG:-$REPO_ROOT/out/mig/build/mig}
OUT=${MIG_HEADERS_OUT:-$REPO_ROOT/out/mach_headers}
# The build-dir variant (MIGKSFLAGS) goes beside it, not inside it: `rm -rf $OUT` clears only OUT,
# and the two are different files under the same name.
KSERVER=${MIG_KSERVER_OUT:-$REPO_ROOT/out/mach_headers/kserver}

[[ -x $MIG ]] || { echo "no MIG at $MIG - run tools/build_mig.sh first" >&2; exit 2; }
[[ -d $XNU/osfmk/mach ]] || { echo "no .defs at $XNU/osfmk/mach" >&2; exit 2; }

# Apple's `$(DEFINES)` — the first thing in `MIGFLAGS` (`makedefs/MakeInc.def:470`), so every `.defs`
# Apple preprocesses for MIG is preprocessed with these in force:
#
#     DEFINES = -DAPPLE -DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE \
#               -DPRIVATE -D__MACHO__=1 -Dvolatile=__volatile $(CONFIG_DEFINES) $(SEED_DEFINES)
#                                                                       (:78-80)
#
# **`-DKERNEL` was missing, and it is load-bearing.** `osfmk/mach/vm_map.defs:116,132,153` wraps
# `vm_allocate`, `vm_deallocate` and `vm_protect` in
#
#     #if !KERNEL && !LIBSYSCALL_INTERFACE
#     skip;
#     #else
#     routine PREFIX(vm_deallocate)(...)
#     #endif
#
# — the `skip;` is the *userspace* side, where libsystem provides them. Without `-DKERNEL` the
# kernel's own `<mach/vm_map.h>` did not declare the three functions `osfmk/vm/vm_user.c:336`
# defines, and `grep -c deallocate out/mach_headers/mach/vm_map.h` was **0**. Measured cost:
# `iokit/Kernel/IOUserClient.cpp` (2 sites) and `IOMaterial`-sized `IOMemoryDescriptor.cpp` (2) failed
# on "use of undeclared identifier: vm_deallocate / mach_vm_deallocate", and 10 of the boot path's 46
# stubs. See experiment-156.
#
# Kept as one list rather than four, because it was four hand-copied copies of one Apple value in this
# file before — this project's most-repeated defect — and the copies are exactly how the omission
# survived: three of them agreed with each other and none with Apple.
#
# `-DAPPLE` is left out because it appears in no `.defs` outside license comments; `-D__MACHO__=1` and
# `-Dvolatile=__volatile` are referenced by no `.defs` in the tree; `$(CONFIG_DEFINES)` and
# `$(SEED_DEFINES)` are per-configuration facts this build carries in its own `-D` table and in the
# generated `OPTIONS/` headers. `-DPRIVATE` *is* used (`mach_host.defs:252-261` puts
# `mach_zone_force_gc` behind `#ifdef PRIVATE`), so it is here.
DEFS_DEFINES=(-DKERNEL=1 -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DPRIVATE=1)

# `-DMACH_KERNEL_PRIVATE` is NOT in `$(DEFINES)`: it comes from MIGKUFLAGS/MIGKSFLAGS per rule
# (`osfmk/mach/Makefile:247-248`). It is passed on every run here, including the export variant, and
# that is a deliberate deviation recorded in experiment-144 — `mach_types.defs:606-615` needs it
# *together with* `KERNEL_SERVER` for the eight `simport` lines, so it changes nothing for the export
# variant and is harmless on the user one.
MACH_KERNEL_PRIVATE_DEFINE=(-DMACH_KERNEL_PRIVATE=1)

# `--all` used to mean "every .defs in osfmk/mach"; that is now the default, so it is accepted and
# ignored rather than removed, in case a command line out there still spells it.
[[ ${1:-} == --all ]] && shift

# The set the ARM entry path reaches, by following its includes.
ENTRY_PATH_DEFS=(
    mach_host
    mach_port
    mach_vm
    task
    thread_act
    vm_map
    host_priv
    host_security
    lock_set
    clock
    clock_priv
    exc
    mach_exc
    mach_notify
    processor
    processor_set
    semaphore
    memory_object_control
    memory_object_default
    upl
    notify
    mach_voucher_attr_control
    audit_triggers
    coalition_notification
    ktrace_background
)

# What is generated, and this changed on 2026-09-17. The first version generated an explicit list of
# 25 `.defs`, all from `osfmk/mach`, "so that a failure here is a short list rather than forty". Two
# of them failed and the failures were informative, so the list was extended - and extending it to
# the whole tree is what the kernel build actually needs: `.defs` also live in `osfmk/device`,
# `osfmk/atm`, `osfmk/UserNotification`, `osfmk/lockd`, `osfmk/gssd` and `osfmk/kextd`, and
# `osfmk/device/iokit_rpc.c` includes `<device/device_server.h>`.
#
# The generated header goes to `$OUT/<dir of the .defs relative to osfmk>`, which is the spelling the
# sources use: `osfmk/mach/task.defs` -> `mach/task.h` for `#include <mach/task.h>`, and
# `osfmk/device/device.defs` -> `device/device_server.h` for `<device/device_server.h>`.
#
# `libsyscall/mach` holds 18 `.defs` that are the *user-side* copies of the same interfaces; a kernel
# does not build them, so they are excluded rather than generated and discarded.
# WHICH outputs to generate comes from Apple's own Makefiles, via xnu_config/mig_outputs.py, and
# this is the correction that made the generated root usable ahead of the source tree. Running MIG
# over *every* `.defs` over-generates, and the extra headers shadow real ones: Apple lists
# `notify_server.h` but NOT `notify.h`, because `osfmk/mach/notify.h` is hand-written and carries
# `MACH_NOTIFY_NO_SENDERS`; a generated `notify.h` is a user-side stub with none of it. Likewise
# `memory_object.h` IS listed, so Apple's kernel sees the generated one and never hits the
# hand-written `osfmk/mach/memory_object.h` colliding with `memory_object_types.h` over
# `memory_object_t`. Both directions are measured; see experiment-124.
SPEC=${MIG_OUTPUTS_SPEC:-$REPO_ROOT/out/mig_outputs.txt}
[[ -f $SPEC ]] || "$TOOLS_DIR/xnu_config/mig_outputs.py" --write "$SPEC" >/dev/null

DEFS_FILES=()
declare -A WANT_KINDS=()
declare -A WANT_DIR=()
while IFS=$'\t' read -r base dir kinds; do
    [[ -z $base || $base == \#* ]] && continue
    DEFS_FILES+=("$dir/$base.defs")
    WANT_KINDS[$base]=$kinds
    WANT_DIR[$base]=$dir
done < "$SPEC"

if [[ $# -gt 0 ]]; then
    filtered=()
    for f in "${DEFS_FILES[@]}"; do
        base=${f##*/}; base=${base%.defs}
        for w in "$@"; do
            [[ $base == "$w" ]] && { filtered+=("$f"); break; }
        done
    done
    DEFS_FILES=("${filtered[@]}")
fi

# The whole output directory is generated, so it is cleared rather than added to: a header left
# behind by a previous run resolves an `#include` this run no longer produces, and the build then
# cannot tell which set it was compiled against. Same class as the stale per-file logs in
# build_xnu_arm_kernel.sh, and the count-of-two-runs bug before that.
rm -rf "$OUT"
mkdir -p "$OUT" "$KSERVER"

ok=0
fail=0
typesonly=0

for rel in "${DEFS_FILES[@]}"; do
    defs="$XNU/$rel"
    base=${rel##*/}; base=${base%.defs}
    # osfmk/mach/task.defs -> mach ; osfmk/device/device.defs -> device
    outdir=${rel#osfmk/}; outdir=${outdir%/*}
    mkdir -p "$OUT/$outdir"
    tag="$outdir/$base"

    # A types-only `.defs` - std_types, mach_types, clock_types, mach_debug_types, machine_types,
    # atm_types, UNDTypes - carries no messages; it exists to be `#include`d by an interface. There
    # is nothing for MIG to generate, and running it anyway produces "no SubSystem declaration"
    # (or, for machine_types, "type 'int16_t' not defined" because std_types is what defines them).
    #
    # The test is on the PREPROCESSED text, not the file, and that distinction is load-bearing:
    # `osfmk/mach/mach_notify.defs` is 38 lines of comment and a single `#include <mach/notify.defs>`,
    # so it has no `subsystem` line of its own - but `osfmk/ipc/ipc_notify.c:68` includes
    # `<mach/mach_notify.h>`, and Apple's `osfmk/mach/Makefile` lists it among the MIG outputs. It is
    # a rename of notify.defs, and testing the file rather than its expansion skipped it.
    #
    # `-DKERNEL_SERVER` matters even for this test: `osfmk/mach/mach_types.defs:606-615` puts eight
    # `simport` lines behind `#if KERNEL_SERVER` / `#ifdef MACH_KERNEL_PRIVATE`, and they are what
    # makes the generated server headers `#include <kern/ipc_kobject.h>` - which is where IKOT_* and
    # ipc_kobject_type_t come from for vm_user.c, memory_object.c, vm_map.c, mach_port.c and
    # mk_timer.c. Without it those files fail on 34 occurrences of names that are in the tree.
    cc -E -x c "${DEFS_DEFINES[@]}" "${MACH_KERNEL_PRIVATE_DEFINE[@]}" -DKERNEL_SERVER=1 \
       -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" \
       "$defs" >"$OUT/$base.pp" 2>"$OUT/$base.cpp.log"
    if ! grep -qE '^[[:space:]]*subsystem' "$OUT/$base.pp"; then
        printf '  %-40s types only, nothing to generate\n' "$tag"
        typesonly=$((typesonly + 1))
        rm -f "$OUT/$base.pp"
        continue
    fi

    # The server-header spelling is per directory, read off each one's Makefile:
    #
    #   osfmk/mach/Makefile:236-238            -sheader $@          -> <base>_server.h
    #   osfmk/device/Makefile:52-53            -sheader device_server.h
    #   osfmk/atm/Makefile:70-71               -sheader $@          -> <base>_server.h
    #   osfmk/UserNotification/Makefile:80-81  -sheader $*Server.h   -> <base>Server.h
    #
    # and that is the only directory that differs. `osfmk/ipc/ipc_kobject.c:107` includes
    # `<UserNotification/UNDReplyServer.h>` and `<UserNotification/UNDReply_server.h>` does not exist,
    # so getting this wrong is a "file not found" that looks like a missing MIG run.
    case "$outdir" in
        UserNotification) server_suffix="Server" ;;
        *)                server_suffix="_server" ;;
    esac

    # TWO runs per `.defs`, because Apple's Makefile has two rules and they do not use the same
    # defines. `osfmk/mach/Makefile:361-382`:
    #
    #   %_user.c   : %.defs   $(MIG) $(MIGFLAGS) $(MIGKUFLAGS) -user $*_user.c -header $*.h ...
    #   %_server.c : %.defs   $(MIG) $(MIGFLAGS) $(MIGKSFLAGS) -server $*_server.c -sheader $*_server.h ...
    #
    # with `MIGKUFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_USER=1 -maxonstack 1024` and
    # `MIGKSFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_SERVER=1` (osfmk/mach/Makefile:247-248). One run
    # asking for both outputs is not the same thing: the `.defs` language is full of
    # `#if KERNEL_SERVER` / `#if KERNEL_USER` blocks, so the server header was being generated with
    # KERNEL_SERVER undefined and the user header with KERNEL_USER undefined.
    #
    # That is where `IKOT_*` and `ipc_kobject_type_t` went. `osfmk/mach/mach_types.defs:606-615` puts
    # eight `simport` lines behind `#if KERNEL_SERVER` + `#ifdef MACH_KERNEL_PRIVATE`:
    #
    #     simport <kern/ipc_kobject.h>;	/* for null conversion */
    #     simport <kern/ipc_tt.h>;	/* for task/thread conversion */
    #     ...
    #
    # and a `simport` becomes an `#include` in the generated server header. Preprocessed away, it
    # never reached MIG, and vm_user.c, memory_object.c, vm_map.c, mach_port.c and mk_timer.c failed
    # on 34 occurrences of names that have been in the tree all along.
    #
    # The preprocessed text is reused rather than the source, which is what makes mach_notify work
    # and costs two temp files. -E keeps the line directives, so a MIG diagnostic points at the
    # .defs line rather than at the flattened text.
    #
    # `-novouchers` is in `MIGFLAGS` (MakeInc.def:470) and is NOT passed here: it suppresses the
    # voucher conversion routines, and the kernel's own sources reference them. Left out on purpose,
    # and recorded rather than silently matched.
    # ONLY the outputs Apple's Makefile asks for. `kinds` comes from xnu_config/mig_outputs.py,
    # which reads the MIG_*HDRS / MIG_*SRC lists. This is the part that matters:
    #
    #   notify            sheader        -> notify_server.h, and NOT notify.h
    #   memory_object     header,user    -> memory_object.h, and no server side
    #
    # Note the ",$kinds," delimiters: a plain `*header*` match is true for "sheader", which is how
    # a first version of this generated notify.h anyway - the exact bug it exists to prevent.
    #
    # Generating more than that is not free. `mach/notify.h` in the tree is hand-written and carries
    # MACH_NOTIFY_NO_SENDERS and the notification structs; a generated one is a user-side stub with
    # none of it, and it shadows the real header for osfmk/ipc/ipc_voucher.c and
    # osfmk/kern/ipc_kobject.c the moment the generated root comes first in the include path - which
    # is where Apple puts its own (`INCFLAGS = -I. $(INCFLAGS_GEN) ...`, MakeInc.def:469).
    kinds=${WANT_KINDS[$base]:-}

    server_suffix_for_run="$server_suffix"
    # `-1` means "not asked for", `0` means "ran and succeeded", `1` means "ran and failed". A pass
    # that was never wanted must not make the base look like a failure.
    server_ok=-1
    user_ok=-1
    wrote=""

    rm -f "$OUT/$outdir/$base.h" "$OUT/$outdir/${base}${server_suffix_for_run}.h" \
          "$OUT/$outdir/${base}${server_suffix_for_run}.c" "$OUT/$outdir/${base}_user.c"

    # --- the server side, TWO variants, because Apple builds two ----------------------------
    #
    # `osfmk/mach/Makefile` has two rules for the same `%_server.h` name:
    #
    #   :231-238   MIG_USHDRS  ->  $(MIG) $(MIGFLAGS)                -sheader $@
    #   :372-380   MIG_KSHDRS  ->  $(MIG) $(MIGFLAGS) $(MIGKSFLAGS)  -sheader $*_server.h
    #
    # and `MIGKSFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_SERVER=1` (:247). The `simport` lines in
    # `mach_types.defs:606-615` are behind `#if KERNEL_SERVER`, so **the two rules produce different
    # files under the same name** - the export one without those includes, the build-dir one with
    # them. `EXPORT_MI_GEN_LIST = ${MIGINCLUDES}` exports UUHDRS + USHDRS and NOT KSHDRS, so the
    # variant a component sees depends on whether it reads the build dir (osfmk: `INCFLAGS_LOCAL`,
    # `INCFLAGS_GEN`) or only the export roots (`INCFLAGS_IMPORT`).
    #
    # That is what experiment-144's four `sync_qos_count_t` files need: a BSD file that reaches
    # `exc_server.h` through `mach_interface.h` must get the file WITHOUT
    # `simport <kern/ipc_kobject.h>`, or it is dragged into `osfmk/ipc/ipc_kmsg.h` and its
    # MACH_KERNEL_PRIVATE-only types. The export variant is written to $OUT (as before); the
    # build-dir variant goes to $OUT/kserver, which the build places first for osfmk files only.
    if [[ ",$kinds," == *,sheader,* || ",$kinds," == *,server,* ]]; then
        # (1) the export variant: MIGFLAGS only, exactly as the MIG_USHDRS rule.
        cc -E -x c "${DEFS_DEFINES[@]}" "${MACH_KERNEL_PRIVATE_DEFINE[@]}" \
           -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" "$defs" >"$OUT/$base.srv.pp" 2>>"$OUT/$base.cpp.log"
        # Header only. Apple's MIG_USHDRS rule asks for `-sheader $@` and nothing else, which is why
        # the export root carries no `_server.c`.
        sheader_out=/dev/null
        [[ ",$kinds," == *,sheader,* ]] && sheader_out="$OUT/$outdir/${base}${server_suffix_for_run}.h"
        if "$MIG" -header /dev/null -user /dev/null \
                  -server /dev/null -sheader "$sheader_out" \
                  <"$OUT/$base.srv.pp" >"$OUT/$base.mig.log" 2>&1; then
            server_ok=0
        fi

        # (2) the build-dir variant: + MIGKSFLAGS, exactly as the MIG_KSHDRS rule — and it produces
        # the `_server.c` too, because the two belong in one directory: the generated `.c` includes
        # its own header with QUOTES (`#include "mach_vm_server.h"`), which resolves next to the
        # file, so a `.c` in the export root would pick up the export header and lose the types the
        # KERNEL_SERVER run defines. Apple has both in the build dir; so does this.
        if [[ ",$kinds," == *,sheader,* || ",$kinds," == *,server,* ]]; then
            mkdir -p "$KSERVER/$outdir"
            cc -E -x c "${DEFS_DEFINES[@]}" "${MACH_KERNEL_PRIVATE_DEFINE[@]}" -DKERNEL_SERVER=1 \
               -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" "$defs" >"$OUT/$base.ksrv.pp" 2>>"$OUT/$base.cpp.log"
            server_out=/dev/null; ksrv_sheader=/dev/null
            [[ ",$kinds," == *,server,*  ]] && server_out="$KSERVER/$outdir/${base}${server_suffix_for_run}.c"
            [[ ",$kinds," == *,sheader,* ]] && ksrv_sheader="$KSERVER/$outdir/${base}${server_suffix_for_run}.h"
            "$MIG" -header /dev/null -user /dev/null \
                   -server "$server_out" -sheader "$ksrv_sheader" \
                   <"$OUT/$base.ksrv.pp" >>"$OUT/$base.mig.log" 2>&1 || true
            rm -f "$OUT/$base.ksrv.pp"
        fi
    fi

    # --- the user side: -DKERNEL_USER=1 ----------------------------------------------------
    if [[ ",$kinds," == *,header,* || ",$kinds," == *,user,* ]]; then
        cc -E -x c "${DEFS_DEFINES[@]}" "${MACH_KERNEL_PRIVATE_DEFINE[@]}" -DKERNEL_USER=1 \
           -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" "$defs" >"$OUT/$base.usr.pp" 2>>"$OUT/$base.cpp.log"
        header_out=/dev/null; user_out=/dev/null
        [[ ",$kinds," == *,header,* ]] && header_out="$OUT/$outdir/$base.h"
        [[ ",$kinds," == *,user,*   ]] && user_out="$OUT/$outdir/${base}_user.c"
        if "$MIG" -header "$header_out" -user "$user_out" \
                  -server /dev/null -sheader /dev/null \
                  <"$OUT/$base.usr.pp" >>"$OUT/$base.mig.log" 2>&1; then
            user_ok=0
        fi
    fi

    rm -f "$OUT/$base.srv.pp" "$OUT/$base.usr.pp"

    # Name what was actually written, so the run's output is a list of files rather than a number.
    for f in "$OUT/$outdir/$base.h" "$OUT/$outdir/${base}_user.c" \
             "$OUT/$outdir/${base}${server_suffix_for_run}.h" \
             "$OUT/$outdir/${base}${server_suffix_for_run}.c"; do
        [[ -s $f ]] && wrote+="$(basename "$f") "
    done

    if [[ $server_ok -le 0 && $user_ok -le 0 && -n $wrote ]]; then
        printf '  %-40s %s\n' "$tag" "$wrote"
        ok=$((ok + 1))
    else
        printf '  %-40s FAILED: server=%s user=%s (0=ok 1=failed -1=not asked) %s\n' "$tag" "$server_ok" "$user_ok" \
               "$(grep -m1 -E 'error|fatal' "$OUT/$base.mig.log" | cut -c1-90)"
        fail=$((fail + 1))
    fi
done

echo
echo "generated $ok, types-only $typesonly, failed $fail"
echo "headers in $OUT"
