#!/usr/bin/env bash
#
# A filtered export of `osfmk/libsa`'s types headers, for the include path.
#
#   ./tools/gen_libsa_export.sh          -> out/xnu_libsa_export/
#
# Why a filtered copy rather than the directory. `osfmk/libsa/stdlib.h:63` is `#include <types.h>`,
# and it means `osfmk/libsa/types.h` - the kernel's own `u_char`, `u_short`, `u_int`, `u_long`,
# `caddr_t`, `daddr_t`, and a `size_t`. Apple's build reaches it because `libsa`'s Makefile sets
# `INCFLAGS_MAKEFILE = -I..` and the component exports into `EXPORT_HDRS/libsa`.
#
# Putting `-I$XNU/osfmk/libsa` on this build's path works for `<types.h>` and breaks something else
# every time, because the same directory holds `string.h`, whose `strncat`/`strcpy` are
# `__builtin___*_chk` macros for a kernel build: it shadowed the real `<string.h>` and took
# `iokit/Kernel/IOStringFuncs.c` from passing to failing. Exposing a whole source directory is what
# experiment-117 measured as costing four files, and this is the same lesson from the third side.
#
# So: copy the three headers `<types.h>` needs, and nothing else. It is the EXPORT_HDRS principle -
# a *selected list* per component - applied to the one component that needed it.
#
# Measured: `RELEASE` 564 → 565 with no regressions. The whole directory gives 567 and breaks
# IOStringFuncs.c; a filtered root at the end of the include path changes nothing at all, because
# `<types.h>` then loses to `bsd/arm/types.h` (Darwin's machine types, no `u_char`).

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
OUT=${XNU_LIBSA_EXPORT:-$REPO_ROOT/out/xnu_libsa_export}

# `<types.h>` includes `libsa/machine/types.h`, which includes `libsa/arm/types.h` for `__arm__`.
FILES=(
    "types.h"
    "machine/types.h"
    "arm/types.h"
)

rm -rf "$OUT"
for rel in "${FILES[@]}"; do
    src=$XNU/osfmk/libsa/$rel
    [[ -f $src ]] || { echo "missing $src" >&2; exit 2; }
    mkdir -p "$OUT/$(dirname "$rel")"
    install -m 0644 "$src" "$OUT/$rel"
done

# The one property that makes the copy worth anything: without `u_char` the root is a no-op and
# would look like a successful run.
grep -q 'typedef.*u_char' "$OUT/types.h" || {
    echo "types.h has no u_char - the export is not doing anything" >&2; exit 1; }

echo "exported ${#FILES[@]} headers from osfmk/libsa to $OUT"
