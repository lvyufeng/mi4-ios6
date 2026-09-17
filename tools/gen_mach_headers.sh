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

[[ -x $MIG ]] || { echo "no MIG at $MIG - run tools/build_mig.sh first" >&2; exit 2; }
[[ -d $XNU/osfmk/mach ]] || { echo "no .defs at $XNU/osfmk/mach" >&2; exit 2; }

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
mapfile -t DEFS_FILES < <(cd "$XNU" && find osfmk -name '*.defs' | sort)

if [[ $# -gt 0 ]]; then
    wanted=("$@")
    filtered=()
    for f in "${DEFS_FILES[@]}"; do
        base=${f##*/}; base=${base%.defs}
        for w in "${wanted[@]}"; do
            [[ $base == "$w" ]] && { filtered+=("$f"); break; }
        done
    done
    DEFS_FILES=("${filtered[@]}")
elif [[ ${ENTRY_PATH_ONLY:-0} -eq 1 ]]; then
    filtered=()
    for f in "${DEFS_FILES[@]}"; do
        base=${f##*/}; base=${base%.defs}
        for w in "${ENTRY_PATH_DEFS[@]}"; do
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
mkdir -p "$OUT"

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
    cc -E -x c -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DMACH_KERNEL_PRIVATE=1 -DKERNEL_SERVER=1 \
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
    server_suffix_for_run="$server_suffix"
    server_ok=0
    user_ok=0

    # --- the server side: -DKERNEL_SERVER=1 ------------------------------------------------
    cc -E -x c -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DMACH_KERNEL_PRIVATE=1 -DKERNEL_SERVER=1 \
       -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" "$defs" >"$OUT/$base.srv.pp" 2>>"$OUT/$base.cpp.log"
    if "$MIG" -header /dev/null \
              -user /dev/null \
              -server "$OUT/$outdir/${base}${server_suffix_for_run}.c" \
              -sheader "$OUT/$outdir/${base}${server_suffix_for_run}.h" \
              <"$OUT/$base.srv.pp" >"$OUT/$base.mig.log" 2>&1; then
        server_ok=1
    fi

    # --- the user side: -DKERNEL_USER=1 ----------------------------------------------------
    cc -E -x c -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=1 -DMACH_KERNEL_PRIVATE=1 -DKERNEL_USER=1 \
       -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" "$defs" >"$OUT/$base.usr.pp" 2>>"$OUT/$base.cpp.log"
    if "$MIG" -header "$OUT/$outdir/$base.h" \
              -user "$OUT/$outdir/${base}_user.c" \
              -server /dev/null \
              -sheader /dev/null \
              <"$OUT/$base.usr.pp" >>"$OUT/$base.mig.log" 2>&1; then
        user_ok=1
    fi

    rm -f "$OUT/$base.srv.pp" "$OUT/$base.usr.pp"

    if [[ $server_ok -eq 1 && $user_ok -eq 1 && -s $OUT/$outdir/$base.h ]]; then
        # A MIG that exits 0 without writing a header would look like success; check the file.
        printf '  %-40s %s bytes (server %s)\n' "$tag" "$(stat -c%s "$OUT/$outdir/$base.h")" \
               "$(stat -c%s "$OUT/$outdir/${base}${server_suffix_for_run}.h" 2>/dev/null || echo 0)"
        ok=$((ok + 1))
    elif grep -q "no SubSystem declaration" "$OUT/$base.mig.log"; then
        # Not reachable through the source check above; kept because a MIG that fails on a `.defs`
        # this script believed to be an interface must say so rather than be counted as one.
        printf '  %-40s no subsystem in the preprocessed text\n' "$tag"
        typesonly=$((typesonly + 1))
        rm -f "$OUT/$outdir/$base.h" "$OUT/$outdir/${base}${server_suffix_for_run}.h" \
              "$OUT/$outdir/${base}${server_suffix_for_run}.c" "$OUT/$outdir/${base}_user.c"
    else
        printf '  %-40s FAILED: server=%s user=%s %s\n' "$tag" "$server_ok" "$user_ok" \
               "$(grep -m1 -E 'error|fatal' "$OUT/$base.mig.log" | cut -c1-90)"
        fail=$((fail + 1))
    fi
done

echo
echo "generated $ok, types-only $typesonly, failed $fail"
echo "headers in $OUT"
