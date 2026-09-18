#!/usr/bin/env bash
#
# Run XNU's own device-tree reader over the tree our builder produces.
#
# This assembles a host test from the *shipping* sources rather than from a copy:
#
#   - build_stage90_apple_dt is extracted verbatim from stages/stage90/stage90_main.c
#   - the constants it references are extracted from stages/stage90/stage90.h's own
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
STAGE_DIR=$REPO_ROOT/stages/stage90
WORK=$REPO_ROOT/out/host-dt

CC=${CC:-cc}
ARM_CC=${ARM_CC:-arm-none-eabi-gcc}
PYTHON=${PYTHON:-python3}

mkdir -p "$WORK"

# --- 1. extract build_stage90_apple_dt verbatim -----------------------------------
"$PYTHON" - "$STAGE_DIR/stage90_main.c" "$WORK/stage90_dt_extract.c" <<'PY'
import re
import sys

src_path, out_path = sys.argv[1], sys.argv[2]
src = open(src_path).read()

m = re.search(r'\nstatic void build_stage90_apple_dt\(struct apple_dt_builder \*b\)\n\{', src)
if not m:
    sys.exit("host_dt_check: cannot find build_stage90_apple_dt in %s - "
             "was it renamed or made non-static? The harness must test the shipping "
             "function, so fix the extraction rather than the harness." % src_path)

i = m.end() - 1
depth = 0
j = i
while j < len(src):
    if src[j] == '{':
        depth += 1
    elif src[j] == '}':
        depth -= 1
        if depth == 0:
            break
    j += 1
if depth != 0:
    sys.exit("host_dt_check: unbalanced braces while extracting build_stage90_apple_dt")

body = src[m.start():j + 1]
# The function is static in the payload; the harness lives in another unit.
body = body.replace("static void build_stage90_apple_dt", "void build_stage90_apple_dt", 1)

# --- and the /chosen random-seed rule it calls --------------------------------------
#
# Experiment 211 added a `/chosen` `random-seed` property, which `build_stage90_apple_dt`
# produces through `build_chosen_random_seed`. That is a second shipping function the
# extracted builder depends on, so it is extracted the same way and with the same
# loud-failure rule: if it is renamed or made non-static, this aborts rather than compiling a
# shim that quietly produces a different tree from the payload's.
m = re.search(r'\nstatic void build_chosen_random_seed\(uint8_t \*out, uint32_t len\)\n\{', src)
if not m:
    sys.exit("host_dt_check: cannot find build_chosen_random_seed in %s - the extracted "
             "builder calls it, so the harness would not reproduce the payload's tree. "
             "Fix the extraction rather than the harness." % src_path)
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
if depth != 0:
    sys.exit("host_dt_check: unbalanced braces while extracting build_chosen_random_seed")

seed_fn = src[m.start():j + 1]
seed_fn = seed_fn.replace("static void build_chosen_random_seed",
                          "void build_chosen_random_seed", 1)

m = re.search(r'\n#define STAGE90_CHOSEN_RANDOM_SEED_BYTES \d+u\n', src)
if not m:
    sys.exit("host_dt_check: cannot find STAGE90_CHOSEN_RANDOM_SEED_BYTES in %s" % src_path)
seed_define = m.group(0).strip()

# The rule string is the seed's whole content, so it comes across verbatim rather than being
# restated here - a shim that spelled its own rule would test itself.
m = re.search(r'\nstatic const char stage90_chosen_random_seed_rule\[\] = "[^"]*";\n', src)
if not m:
    sys.exit("host_dt_check: cannot find stage90_chosen_random_seed_rule in %s" % src_path)
seed_rule = m.group(0).strip()

body = seed_define + "\n" + seed_rule + "\n" + seed_fn + "\n" + body
print("extracted build_chosen_random_seed: %d lines" % seed_fn.count("\n"))

# And it is compiled outside the payload, so it needs the shim header itself.
body = '#include "stage90_dt_shim.h"\n' + body
open(out_path, "w").write(body + "\n")
print("extracted build_stage90_apple_dt: %d lines" % body.count("\n"))
PY

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
  echo "/* The payload keeps its device-tree buffer in stage90_main.c's .bss; the harness"
  echo "   owns it instead, with the same size and alignment so \`sizeof\` in the extracted"
  echo "   builder means the same thing in both. */"
  echo "extern uint8_t g_apple_dt[32768];"
  echo "void log_puts(const char *s);"
  echo "void log_kv32(const char *key, uint32_t value);"
  echo
  echo "/* The builder's constants and the addresses they name, taken from stage90.h. Only"
  echo "   the payload's own namespaces, so no compiler builtins leak in. */"
  grep -E '^#define (STAGE90_|RAM_|BOOT_LINE_LENGTH|MACHINE_TYPE_)' "$WORK/stage90.defines" \
    | grep -vE '^#define STAGE90_[A-Z0-9_]*\(' || true
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
