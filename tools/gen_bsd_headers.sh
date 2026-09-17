#!/usr/bin/env bash
#
# Generate the BSD-side headers and sources XNU's build produces, which are not in the tarball.
#
#   ./tools/gen_bsd_headers.sh
#
# All of them come from one input, `bsd/kern/syscalls.master`, through one tool, `bsd/kern/makesyscalls.sh`,
# with a different output kind each time. `makesyscalls.sh:75` states the kinds:
#
#     usage: makesyscalls.sh input-file [<names|proto|header|table|audit|trace>]
#
# and Apple's own Makefiles give the mapping and the destination:
#
#   bsd/sys/Makefile:216            header  -> syscall.h        (the #define SYS_* numbers)
#   (the `proto` kind is the default) proto  -> sysproto.h      (the syscall argument structs)
#   bsd/conf/Makefile.template:291  table   -> init_sysent.c    (the sysent table)
#   bsd/conf/Makefile.template:295  names   -> syscalls.c       (the syscall name table)
#   bsd/conf/Makefile.template:299  audit   -> audit_kevents.c
#   bsd/conf/Makefile.template:303  systrace -> systrace_args.c
#
# The first version of this script ran only `proto`, because `sysproto.h` was the file the failure
# list named - 55 files include `<sys/sysproto.h>`. That is the same mistake as
# `gen_mach_headers.sh` generating one of MIG's two outputs: a tool with several output kinds has
# to be asked for all of them. `bsd/sys/syscall.h` is where `SYS_mmap`, `SYS_pread` and the rest
# live, and without it `bsd/kern/kern_mman.c:663`, `bsd/kern/sys_generic.c:263` and five others fail
# on the syscall number of a routine they define.
#
# `makesyscalls.sh` writes into the *current directory*, so each run happens in a scratch dir and
# the result is moved. The generator is Apple's, unmodified.

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
OUT=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}

MASTER=$XNU/bsd/kern/syscalls.master
MAKESYSCALLS=$XNU/bsd/kern/makesyscalls.sh

[[ -f $MASTER ]] || { echo "missing $MASTER" >&2; exit 2; }
[[ -f $MAKESYSCALLS ]] || { echo "missing $MAKESYSCALLS" >&2; exit 2; }

mkdir -p "$OUT/bsd/sys"

# kind <tab> destination-relative-to-$OUT
KINDS=(
    "proto	sys/sysproto.h"
    "header	sys/syscall.h"
    "table	init_sysent.c"
    "names	syscalls.c"
    "audit	audit_kevents.c"
    "systrace	systrace_args.c"
)

ok=0
fail=0
for entry in "${KINDS[@]}"; do
    kind=${entry%%$'\t'*}
    dest=${entry#*$'\t'}
    work=$(mktemp -d)
    produced=""

    if ( cd "$work" && bash "$MAKESYSCALLS" "$MASTER" "$kind" >"$work/makesyscalls.log" 2>&1 ); then
        # The name is the kind's own (`sysproto.h`, `syscall.h`, `init_sysent.c`, ...) and it is the
        # only file written, so take whatever appeared rather than hard-coding it a second time.
        produced=$(cd "$work" && ls -1 | grep -v '^makesyscalls\.log$' | head -1)
    fi

    if [[ -n $produced && -s $work/$produced ]]; then
        install -m 0644 "$work/$produced" "$OUT/bsd/$dest"
        printf '  %-10s -> %-22s %s bytes\n' "$kind" "bsd/$dest" "$(stat -c%s "$OUT/bsd/$dest")"
        ok=$((ok + 1))
    else
        printf '  %-10s -> %-22s FAILED: %s\n' "$kind" "$dest" \
               "$(tail -2 "$work/makesyscalls.log" 2>/dev/null | tr '\n' ' ' | cut -c1-110)"
        fail=$((fail + 1))
    fi
    rm -rf "$work"
done

echo
if [[ $fail -gt 0 ]]; then
    echo "$ok generated, $fail failed"
    exit 1
fi

# The one check that makes the header output worth anything: without the SYS_* numbers the file is
# a well-formed header that defines nothing, and it would look like a successful run.
if ! grep -q '^#define[[:space:]]\+SYS_' "$OUT/bsd/sys/syscall.h"; then
    echo "sys/syscall.h has no SYS_* definitions" >&2
    exit 1
fi
if ! grep -q '^struct[[:space:]]' "$OUT/bsd/sys/sysproto.h"; then
    echo "sys/sysproto.h has no argument structs" >&2
    exit 1
fi

echo "$ok generated from syscalls.master ($(grep -c '^#define[[:space:]]\+SYS_' "$OUT/bsd/sys/syscall.h") SYS_* numbers)"
