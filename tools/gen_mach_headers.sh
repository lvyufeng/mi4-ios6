#!/usr/bin/env bash
#
# Generate the Mach interface headers XNU's kernel sources include, using the MIG built by
# tools/build_mig.sh.
#
#   ./tools/gen_mach_headers.sh              # generate the set the ARM entry path needs
#   ./tools/gen_mach_headers.sh --all        # generate from every .defs in osfmk/mach
#   ./tools/gen_mach_headers.sh mach_host mach_port
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

ALL=0
[[ ${1:-} == --all ]] && { ALL=1; shift; }

# The set the ARM entry path reaches, by following its includes. Kept explicit rather than globbing
# so that a failure here is a short list rather than forty.
DEFAULT_DEFS=(
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

if [[ $ALL -eq 1 ]]; then
    mapfile -t DEFS < <(cd "$XNU/osfmk/mach" && ls *.defs | sed 's/\.defs$//')
elif [[ $# -gt 0 ]]; then
    DEFS=("$@")
else
    DEFS=("${DEFAULT_DEFS[@]}")
fi

mkdir -p "$OUT"
# The generated headers include each other by <mach/foo.h>, so they are laid out the way the
# include path expects rather than flat.
mkdir -p "$OUT/mach"

ok=0
fail=0
missing=0

for base in "${DEFS[@]}"; do
    defs="$XNU/osfmk/mach/$base.defs"
    if [[ ! -f $defs ]]; then
        printf '  %-30s no .defs\n' "$base"
        missing=$((missing + 1))
        continue
    fi

    # -Eh: preprocess but keep the line directives, so a MIG diagnostic points at the .defs line.
    # -P would strip them and make MIG's errors unattributable.
    # Three outputs per .defs, because the kernel's own sources include all three:
    #   X.h          the message/subroutine declarations
    #   X_server.h   the server-side prototypes - what osfmk/kern/*.c includes as
    #                <mach/mach_host_server.h> and friends, and which nothing else provides
    #   X_client.c   not built here; the user-side is not part of a kernel
    if cc -E -x c -I"$XNU/osfmk/mach" -I"$XNU/osfmk" -I"$XNU/bsd" \
          "$defs" 2>"$OUT/$base.cpp.log" \
       | "$MIG" -header "$OUT/mach/$base.h" \
                -sheader "$OUT/mach/${base}_server.h" \
                -server "$OUT/mach/${base}_server.c" \
                -user /dev/null >"$OUT/$base.mig.log" 2>&1; then
        # A MIG that exits 0 without writing a header would look like success; check the file.
        if [[ -s $OUT/mach/$base.h ]]; then
            printf '  %-30s %s bytes\n' "$base" "$(stat -c%s "$OUT/mach/$base.h")"
            ok=$((ok + 1))
        else
            printf '  %-30s MIG wrote nothing\n' "$base"
            fail=$((fail + 1))
        fi
    else
        printf '  %-30s FAILED: %s\n' "$base" "$(head -c 120 "$OUT/$base.mig.log" | tr '\n' ' ')"
        fail=$((fail + 1))
    fi
done

echo
echo "generated $ok, failed $fail, absent-from-tarball $missing"
echo "headers in $OUT/mach"
