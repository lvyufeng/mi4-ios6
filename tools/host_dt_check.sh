#!/usr/bin/env bash
#
# Run XNU's own device-tree reader over the tree our builder produces.
#
# This assembles a host test from the *shipping* sources rather than from a copy:
#
#   - build_stage90_apple_dt is extracted verbatim from src/stage90_main.c
#   - the constants it references are extracted from src/stage90.h's own
#     preprocessor output, so they cannot drift from the payload's values
#   - apple_dt.c and XNU's pexpert/gen/device_tree.c are compiled as-is
#
# If the extraction ever stops finding the function, the script fails loudly rather than
# quietly testing nothing - which is the failure mode a copied-out harness would have.
#
# Usage: ./host_dt_check.sh
# Exit 0 if XNU's reader finds everything its ARM platform code asks for.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)
STAGE_DIR=$REPO_ROOT/src
WORK=$REPO_ROOT/out/host-dt

CC=${CC:-cc}
ARM_CC=${ARM_CC:-arm-none-eabi-gcc}
PYTHON=${PYTHON:-python3}

mkdir -p "$WORK"

# --- 1. extract build_stage90_apple_dt verbatim -----------------------------------
#
# The extent and the extraction both come from `tools/apple_dt_extract.py`, which finds each piece by
# its declaration and closes it by matching braces - because this script and the *other* consumer of
# this builder (`tools/apple_dt_host_dump.sh`) used to find it two different ways, and the line-number
# one stopped following `stage90_main.c` at 459 while leaving a stale blob on disk for its readers.
# One extractor, so the builder's extent has one definition. It de-`static`s the buffer and both
# functions (the harness is a different unit), and its `--facts` file carries the buffer's declared
# bound, which is where the shim's `extern` and this harness's `sizeof` now both come from.
"$PYTHON" "$TOOLS_DIR/apple_dt_extract.py" \
    --src "$STAGE_DIR/stage90_main.c" \
    --out "$WORK/stage90_dt_extract.c" \
    --include stage90_dt_shim.h \
    --facts "$WORK/apple_dt_extent.facts"

grep -q '^STAGE90_APPLE_DT_BYTES=' "$WORK/apple_dt_extent.facts" || {
  echo "host_dt_check: the extractor wrote no STAGE90_APPLE_DT_BYTES - the harness cannot size the" >&2
  echo "               tree buffer from a missing reading. Aborting rather than assuming a size." >&2
  exit 2
}

# --- 1b. extract align4 from runtime.c, for the same reason ------------------------
"$PYTHON" - "$STAGE_DIR/runtime.c" "$WORK/align4_extract.c" <<'PY'
import re
import sys

src = open(sys.argv[1]).read()
m = re.search(r'\nuint32_t align4\(uint32_t v\)\n\{', src)
if not m:
    sys.exit("host_dt_check: cannot find align4 in runtime.c - the extraction must test "
             "the shipping helper, not a reimplementation.")
i = m.end() - 1
depth, j = 0, i
while j < len(src):
    if src[j] == '{':
        depth += 1
    elif src[j] == '}':
        depth -= 1
        if depth == 0:
            break
    j += 1
open(sys.argv[2], "w").write('#include <stdint.h>\n' + src[m.start():j + 1] + "\n")
print("extracted align4 from runtime.c")
PY

# --- 2. extract the constants it uses, from the real header ------------------------
"$ARM_CC" -mcpu=cortex-a15 -marm -ffreestanding -I"$STAGE_DIR" \
  -E -dM -include stage90.h - </dev/null > "$WORK/stage90.defines"

if ! grep -q '^#define RAM_PHYS_BASE' "$WORK/stage90.defines"; then
  echo "host_dt_check: could not extract constants from stage90.h - the harness would" >&2
  echo "               test nothing. Aborting rather than reporting a false pass." >&2
  exit 2
fi

# --- 2b. the two words 459's memory-map node carries, from the generated entry header ------
#
# `build_stage90_apple_dt` uses `STAGE90_XNU_RAMDISK_VA` and `_SIZE` for the `/chosen/memory-map`
# `RAMDisk` property, and those two numbers live in `out/stage90/xnu_arm_entry.h` - written by
# `src/entry/build_entry.sh` after it links the entry image, because they are the address and
# size of `g_stage90_ramdisk` *in that link* and there is nowhere else they can come from. So this
# harness reads the same header the payload compiles against rather than inventing a value, and it
# stops loudly if the header is not there: the node it checks is the root device.
ENTRY_HEADER=$REPO_ROOT/out/stage90/xnu_arm_entry.h
if [[ ! -f $ENTRY_HEADER ]]; then
  echo "host_dt_check: no $ENTRY_HEADER. The payload's device tree carries the entry image's RAM" >&2
  echo "               disk address since 459, so run ./src/entry/build_entry.sh" >&2
  echo "               first - it links the image and writes that header." >&2
  exit 2
fi
RAMDISK_DEFINES=$(grep -E '^#define STAGE90_XNU_RAMDISK_(VA|SIZE) ' "$ENTRY_HEADER" || true)
for macro in STAGE90_XNU_RAMDISK_VA STAGE90_XNU_RAMDISK_SIZE; do
  if ! grep -qE "^#define $macro " <<<"$RAMDISK_DEFINES"; then
    echo "host_dt_check: $ENTRY_HEADER has no $macro - the header is from a build_entry.sh that" >&2
    echo "               predates 459, and the extracted builder would not compile against it." >&2
    exit 2
  fi
done
echo "RAM disk, from $ENTRY_HEADER: $(tr '\n' ' ' <<<"$RAMDISK_DEFINES")"

{
  echo "/* Generated by tools/host_dt_check.sh - do not edit. */"
  echo "#ifndef STAGE90_DT_SHIM_H"
  echo "#define STAGE90_DT_SHIM_H"
  echo "#include <stdint.h>"
  echo "#include <string.h>"
  echo
  echo "/* Types and prototypes the extracted builder and apple_dt.c need. */"
  echo "struct apple_dt_builder { uint8_t *base; uint32_t capacity; uint32_t pos; uint32_t error; };"
  echo "void apple_dt_begin(struct apple_dt_builder *b, void *buf, uint32_t cap);"
  echo "void apple_dt_node_begin(struct apple_dt_builder *b, uint32_t nprops, uint32_t nchildren);"
  echo "void apple_dt_prop(struct apple_dt_builder *b, const char *name, const void *value, uint32_t len);"
  echo "void apple_dt_prop_str(struct apple_dt_builder *b, const char *name, const char *value);"
  echo "void apple_dt_prop_u32(struct apple_dt_builder *b, const char *name, uint32_t value);"
  echo "void apple_dt_prop_u32_array(struct apple_dt_builder *b, const char *name, const uint32_t *values, uint32_t count);"
  echo "uint32_t apple_dt_finish(struct apple_dt_builder *b);"
  echo "void build_stage90_apple_dt(struct apple_dt_builder *b);"
  echo "/* The payload keeps its device-tree buffer in stage90_main.c's .bss. Since 460 the storage is"
  echo "   that declaration verbatim, de-\`static\`d by tools/apple_dt_extract.py into the extracted"
  echo "   unit, and the bound below is that declaration's own - one reading, so the harness's"
  echo "   \`sizeof\` and the payload's buffer cannot disagree. */"
  grep '^STAGE90_APPLE_DT_BYTES=' "$WORK/apple_dt_extent.facts" \
    | sed 's/^STAGE90_APPLE_DT_BYTES=/#define STAGE90_APPLE_DT_BYTES /; s/$/u/'
  echo "extern uint8_t g_apple_dt[STAGE90_APPLE_DT_BYTES];"
  echo "void log_puts(const char *s);"
  echo "void log_kv32(const char *key, uint32_t value);"
  echo
  echo "/* The builder's constants and the addresses they name, taken from stage90.h. Only"
  echo "   the payload's own namespaces, so no compiler builtins leak in. */"
  grep -E '^#define (STAGE90_|RAM_|BOOT_LINE_LENGTH|MACHINE_TYPE_)' "$WORK/stage90.defines" \
    | grep -vE '^#define STAGE90_[A-Z0-9_]*\(' || true
  echo
  echo "/* 459: the root device's two words, from the generated entry header rather than from"
  echo "   stage90.h - they are addresses in the entry image's own link. */"
  printf '%s\n' "$RAMDISK_DEFINES"
  echo
  echo "#ifndef ARRAY_SIZE"
  echo "#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))"
  echo "#endif"
  echo "#endif"
} > "$WORK/stage90_dt_shim.h"

echo "constants extracted: $(grep -c '^#define' "$WORK/stage90_dt_shim.h")"

# --- 3. build and run --------------------------------------------------------------
"$CC" -std=c11 -O1 -Wall -Wextra -Werror \
  -I "$TOOLS_DIR/host_dt_shim" \
  -I "$WORK" \
  -o "$WORK/host_dt_harness" \
  "$TOOLS_DIR/host_dt_harness.c" \
  "$WORK/stage90_dt_extract.c" \
  "$WORK/align4_extract.c" \
  "$STAGE_DIR/apple_dt.c" \
  "$REPO_ROOT/external/xnu-upstream/pexpert/gen/device_tree.c" \
  -Wno-unused-parameter

# --- 4. does the payload's own selftest catch a corrupted node header? --------------
# A load-bearing claim: the reason a device-tree edit is considered safe without a hardware
# run is that apple_dt_selftest_and_log catches a wrong count. That deserves testing against
# the real apple_dt.c rather than being asserted, and it can be, here.
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
  -I "$TOOLS_DIR/host_dt_shim" \
  -I "$WORK" \
  -o "$WORK/host_dt_selftest_probe" \
  "$TOOLS_DIR/host_dt_selftest_probe.c" \
  "$WORK/stage90_dt_extract.c" \
  "$WORK/align4_extract.c" \
  "$STAGE_DIR/apple_dt.c"

echo
"$WORK/host_dt_harness"

echo
"$WORK/host_dt_selftest_probe"
