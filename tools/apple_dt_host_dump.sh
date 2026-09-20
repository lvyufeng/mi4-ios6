#!/bin/bash
#
# Build the payload's real device-tree builder for the HOST and dump the blob it produces.
#
#     tools/apple_dt_host_dump.sh [outdir]        # default out/apple_dt_host
#
# Why this exists, and why it is a *build* rather than a re-implementation. Experiment 440's run
# ended in `pexpert/gen/device_tree.c:56`'s overflow panic, from `next_prop()` - which means some
# property in the tree has a `length` that cannot be a length. The tree is built on the device by
# `build_stage90_apple_dt()` in `stages/stage90/stage90_main.c`, and every node's property count is
# written **by hand** at `apple_dt_node_begin(b, N, C)` while the N properties below it are separate
# statements. A count that has drifted from the statements it counts is invisible to a reader and
# fatal to an exhaustive walk, and XNU's `DTIterateProperties` (reached only from IOKit's
# `MakeReferenceTable`) is the first *exhaustive* property walk this tree has ever had.
#
# The alternative - re-implementing the builder in Python - would measure the re-implementation. So
# the builder's **own source text** is sliced out of `stage90_main.c` by line range and compiled
# together with the real `apple_dt.c`, and the blob this writes is the blob the device builds. The
# only thing that differs is `stage90_xnu_consistent_debug_region_init()`'s return value, which is a
# pointer into the payload's own memory: the dump says so in its own header, and `--debug-root`
# substitutes the device's value into the one property that carries it so the blob is byte-exact.
#
# The slice is checked rather than trusted: the first and last line of each range are asserted to be
# the braces and declarations the range is supposed to start and end with, so a `stage90_main.c` edit
# that moves the function fails this script instead of silently dumping a different tree.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO_ROOT/stages/stage90/stage90_main.c"
OUT="${1:-$REPO_ROOT/out/apple_dt_host}"
DEBUG_ROOT="${STAGE90_DEBUG_ROOT_VAL:-0}"
PYTHON=${PYTHON:-python3}

mkdir -p "$OUT"

# The builder's extent comes from `tools/apple_dt_extract.py`, which finds each piece **by its
# declaration** and closes it by matching braces. That is the fix for this script's own worst defect,
# which 459's edit to `stage90_main.c` exposed: this script used to carry a *second* extractor, and
# that one sliced the function out by hard-coded line numbers with each end asserted. The assertions
# caught the move and this script exited 1 - and left `apple_dt.bin` on disk from the previous run.
# A reader of that file (tools/xnu_dt_walk.py, and the step-0 plan that reads it) then gets a
# confident, self-consistent tree **from before the edit**: `/chosen` with no `memory-map` child,
# which is exactly the shape of the defect 459's run was hunting. One extractor, one definition of the
# extent, and its report is written into the dump's own header so the range is readable - not
# asserted twice by two tools that can disagree.
"$PYTHON" "$REPO_ROOT/tools/apple_dt_extract.py" \
    --src "$SRC" \
    --out "$OUT/builder.inc" \
    --facts "$OUT/builder.facts" \
    --banner-note "Sliced from stages/stage90/stage90_main.c by tools/apple_dt_extract.py - do not edit."

# `align4` is the builder's only other dependency, and it is one line in stages/stage90/runtime.c.
# The body is copied rather than re-invented, and compared against the real source so a change there
# fails this script rather than making the dump a different tree.
if ! grep -q 'return (v + 3u) & ~3u;' "$REPO_ROOT/stages/stage90/runtime.c"; then
    echo "FAIL: runtime.c's align4 is not the one-line definition this harness copies." >&2
    grep -n -A3 '^uint32_t align4' "$REPO_ROOT/stages/stage90/runtime.c" >&2
    exit 1
fi

# The generated header `stage90_main.c` includes (line 5): 459's `/chosen/memory-map` node carries
# `STAGE90_XNU_RAMDISK_VA`/`_SIZE` from it, and those two words are addresses in the *entry image's*
# link, written by `xnu_arm_boot/build_entry.sh`. The harness compiles against the same header the
# payload does, rather than restating the numbers - so the dumped blob carries the device's values and
# is byte-exact for those eight bytes. Missing header is a loud stop naming the prerequisite, because
# a dump that silently skipped the node would be the stale-blob defect again.
ENTRY_HEADER="$REPO_ROOT/out/stage90/xnu_arm_entry.h"
if [[ ! -f $ENTRY_HEADER ]]; then
    echo "FAIL: no $ENTRY_HEADER. stage90_main.c includes it for the RAM disk's two words, so run" >&2
    echo "      ./stages/stage90/xnu_arm_boot/build_entry.sh first - it links the entry image and" >&2
    echo "      writes that header." >&2
    exit 1
fi

cat > "$OUT/harness.c" <<HARNESS
/*
 * Host harness for the payload's device-tree builder. See tools/apple_dt_host_dump.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "stage90.h"
#include "xnu_arm_entry.h"

/* The builder's only two external dependencies that are not apple_dt_*. Both are logging in the
 * payload; on the host they are silent, and neither changes a byte of the tree. */
void log_puts(const char *s) { (void)s; }
void log_kv32(const char *key, uint32_t value) { (void)key; (void)value; }

/* stages/stage90/runtime.c:58-61, asserted above. */
uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }

/*
 * The one value that is the payload's own memory rather than a constant: the property
 * \`consistent-debug-root\` carries this address. \`--debug-root\` substitutes the device's value so the
 * dumped blob is byte-exact; with the default 0 the blob differs from the device's in those four
 * bytes and nowhere else, and a walk cannot see a value.
 */
uint32_t stage90_xnu_consistent_debug_region_init(void) { return ${DEBUG_ROOT}u; }

#include "builder.inc"

#include "$REPO_ROOT/stages/stage90/apple_dt.c"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "apple_dt.bin";
    static struct apple_dt_builder b;
    uint32_t len;

    build_stage90_apple_dt(&b);
    len = apple_dt_finish(&b);
    if (len == 0u) {
        fprintf(stderr, "apple_dt_finish reported a builder error\\n");
        return 1;
    }
    FILE *fh = fopen(path, "wb");
    if (!fh) { perror(path); return 1; }
    if (fwrite(g_apple_dt, 1, len, fh) != len) { perror("fwrite"); return 1; }
    fclose(fh);
    printf("%u\\n", len);
    return 0;
}
HARNESS

gcc -std=gnu11 -O1 -Wall \
    -I "$REPO_ROOT/stages/stage90" \
    -I "$REPO_ROOT/out/stage90" \
    -o "$OUT/apple_dt_dump" "$OUT/harness.c"

"$OUT/apple_dt_dump" "$OUT/apple_dt.bin" > "$OUT/len.txt"
echo "blob: $OUT/apple_dt.bin ($(cat "$OUT/len.txt") bytes)"
