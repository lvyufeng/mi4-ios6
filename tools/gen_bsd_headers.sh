#!/usr/bin/env bash
#
# Generate the BSD-side headers XNU's build produces, which are not in the tarball.
#
#   ./tools/gen_bsd_headers.sh
#
# `bsd/sys/sysproto.h` is generated from `bsd/kern/syscalls.master` by `bsd/kern/makesyscalls.sh`,
# and `bsd/conf/Makefile.template:289` shows how: the script takes the master and an output kind,
# and writes into the *current directory*. 55 of the kernel's files include `<sys/sysproto.h>`, so
# its absence is the single largest blocker in the manifest build.
#
# The generator is Apple's, unmodified. This script only runs it and puts the result where an
# include path can find it.

set -euo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
OUT=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}

[[ -f $XNU/bsd/kern/makesyscalls.sh ]] || {
    echo "no makesyscalls.sh in $XNU/bsd/kern" >&2; exit 2; }

mkdir -p "$OUT/bsd/sys"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# It writes relative to the cwd, so run it there and move the result.
( cd "$work" && bash "$XNU/bsd/kern/makesyscalls.sh" "$XNU/bsd/kern/syscalls.master" proto >/dev/null )

if [[ ! -s $work/sysproto.h ]]; then
    echo "makesyscalls.sh produced no sysproto.h" >&2
    exit 1
fi

cp "$work/sysproto.h" "$OUT/bsd/sys/sysproto.h"
echo "wrote $OUT/bsd/sys/sysproto.h ($(stat -c%s "$OUT/bsd/sys/sysproto.h") bytes)"
