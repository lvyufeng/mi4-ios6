#!/usr/bin/env bash
#
# Build `libkern/version.h` the way Apple's Makefile does, from the two files it ships.
#
#   ./tools/gen_libkern_version.sh
#
# `libkern/libkern/version.h` does not exist in the tarball, and three files include it:
# `osfmk/kern/startup.c:115`, and the two that reach it through `osfmk/kern/kernel_mach_header.c`
# and `libkern/libkern/kernel_mach_header.c`. It is not a MIG output and not an `OPTIONS/` header -
# it is the third kind of generated file, and its generator is in the tarball too:
#
#   libkern/libkern/Makefile:79-87
#     EXPORT_MI_GEN_LIST = version.h
#     version.h: version.h.template $(SRCROOT)/config/MasterVersion
#             install -c -S -m 0644 $< $@
#             $(NEWVERS) $@ > /dev/null
#
# with `NEWVERS = $(SRCROOT)/config/newvers.pl` (makedefs/MakeInc.cmd:146). So: copy the template
# and stamp it. `newvers.pl` reads `config/MasterVersion` (present, and its first line is `17.0.0`)
# and replaces the `###KERNEL_VERSION_*###` placeholders in place - 7 of them.
#
# `newvers.pl` is a perl script and needs only `SRCROOT` and `OBJROOT` in the environment; it was
# run directly on this host before this script existed, and it works. It also substitutes
# `###KERNEL_BUILDER###` and `###KERNEL_BUILD_DATE###`, so the output is **not reproducible** - the
# build date and the user name land in the header. That is Apple's behaviour, not a defect
# introduced here, and it is worth knowing before anyone tries to hash the output.
#
# The header is placed at `<out>/libkern/version.h`, because every include site says
# `#include <libkern/version.h>` and the include root is the top of the output tree.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
OUT=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}

TEMPLATE=$XNU/libkern/libkern/version.h.template
MASTERVERSION=$XNU/config/MasterVersion
NEWVERS=$XNU/config/newvers.pl

for f in "$TEMPLATE" "$MASTERVERSION" "$NEWVERS"; do
    [[ -f $f ]] || { echo "missing $f" >&2; exit 2; }
done

mkdir -p "$OUT/libkern"
# The template has no include guard around its placeholders and is not meant to be read directly;
# copy first, stamp second, exactly as the rule does.
install -m 0644 "$TEMPLATE" "$OUT/libkern/version.h"

SRCROOT=$XNU OBJROOT=$OUT TARGET=$OUT perl "$NEWVERS" "$OUT/libkern/version.h" || {
    echo "newvers.pl failed" >&2; exit 2; }

# A stamping that silently matched nothing would leave the placeholders in place and look like a
# successful run - and `###KERNEL_VERSION_MAJOR###` is not a number, so the first file to include it
# would fail with a confusing error rather than an obvious one.
if grep -q '###KERNEL_VERSION' "$OUT/libkern/version.h"; then
    echo "newvers.pl left placeholders unsubstituted:" >&2
    grep -n '###KERNEL_VERSION' "$OUT/libkern/version.h" >&2
    exit 1
fi

VERSION=$(head -1 "$MASTERVERSION" | tr -d '[:space:]')
echo "libkern/version.h generated from MasterVersion $VERSION"
echo "  $(grep -c . "$OUT/libkern/version.h") lines in $OUT/libkern/version.h"
