#!/usr/bin/env bash
#
# check_kernel_source_record_wired.sh - the kernel-source content record (980) is written by the
# kernel build and READ by the press gate, and the two name the SAME path.
#
# A content record is only worth the reader that re-hashes it. This check exists because the failure
# mode of a record-with-no-consumer is silent: the build writes `out/xnu_kernel_src_d13.sha256`, the
# gate never looks at it, and both pass - the record is a note, not a check ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
# And the converse - a consumer naming a path the producer never writes - is the same silent pass from
# the other side. So this asserts BOTH halves AND that they agree on the path by value.
#
# It is the wiring half; `tools/gen_kernel_source_manifest.py --selftest` (also in `make check`) is the
# mechanism half (content drift and a missing source refuse; an mtime-only change does not).
#
# Source-only: no compiler, no build, no device. Runs in `make check`.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

BUILD=$REPO_ROOT/tools/build_xnu_arm_kernel.sh
GATE=$REPO_ROOT/scripts/preflight_boot_check.sh
TOOL=$REPO_ROOT/tools/gen_kernel_source_manifest.py

fail=0
say() { printf '%s\n' "$*"; }

for f in "$BUILD" "$GATE" "$TOOL"; do
    [[ -f $f ]] || { say "FAIL: missing $f"; exit 1; }
done

# --- 1. the producer writes the record -------------------------------------------------------------
# The kernel build must invoke the tool with --write and a $XNU_OBJ_SUFFIX-suffixed out path.
if grep -qF 'gen_kernel_source_manifest.py' "$BUILD" \
   && grep -qF -- '--write "$KERNEL_SRC_RECORD"' "$BUILD"; then
    say "ok: build_xnu_arm_kernel.sh writes the record (--write \"\$KERNEL_SRC_RECORD\")"
else
    say "FAIL: build_xnu_arm_kernel.sh does not write the kernel-source record - a record no producer"
    say "      makes is a record the gate can never find, so the hole it closes reopens in silence"
    fail=1
fi

# --- 2. the consumer reads the record --------------------------------------------------------------
if grep -qF 'gen_kernel_source_manifest.py' "$GATE" \
   && grep -qF -- '--verify "$KERNEL_SRC_RECORD"' "$GATE"; then
    say "ok: preflight_boot_check.sh verifies the record (--verify \"\$KERNEL_SRC_RECORD\")"
else
    say "FAIL: preflight_boot_check.sh does not verify the kernel-source record - the gate goes back"
    say "      to proving the image matches a stale kernel it never looked at"
    fail=1
fi

# --- 3. the two name the SAME path, and it carries the tree suffix ---------------------------------
# `KERNEL_SRC_RECORD` must name the same FILE in both, so a rename in one cannot leave the reader
# looking where the writer never wrote. The two resolve the tree suffix by their OWN mechanism - the
# build sources `tools/xnu_tree_roots.sh` (`$XNU_OBJ_SUFFIX`), the gate reads the tree's discriminator
# itself (`_ksr_suffix`) and deliberately does not source a build helper - so the comparison is on the
# NORMALIZED path (every `$VAR`/`${VAR}` a single `<SUF>` placeholder), not the raw text. The suffix is
# what keeps a D13 record from being read for a 4570 pool (the 933 class: an asset pinned to the wrong
# tree), so both normalized paths must end in the suffix placeholder before `.sha256`.
build_rec=$(grep -nE '^KERNEL_SRC_RECORD=' "$BUILD" | head -1 | sed 's/^[0-9]*://')
gate_rec=$(grep -nE '^\s*KERNEL_SRC_RECORD=' "$GATE" | head -1 | sed 's/^[0-9]*:\s*//')
_norm() { printf '%s' "$1" | sed -E 's/\$\{[A-Za-z_][A-Za-z0-9_]*\}/\$SUF/g; s/\$[A-Za-z_][A-Za-z0-9_]*/\$SUF/g'; }
build_norm=$(_norm "$build_rec")
gate_norm=$(_norm "$gate_rec")
say "producer: $build_rec"
say "consumer: $gate_rec"
say "  (normalized: $build_norm)"
if [[ -z $build_rec || -z $gate_rec ]]; then
    say "FAIL: KERNEL_SRC_RECORD is not defined in both files"
    fail=1
elif [[ $build_norm != "$gate_norm" ]]; then
    say "FAIL: the producer and the consumer name DIFFERENT record paths - the gate would hunt a file"
    say "      the build never wrote, and the missing-record path would report a 'predates the record'"
    say "      NOTE for every arm, silently"
    fail=1
elif [[ $build_rec != *'xnu_kernel_src'* || $build_rec != *'XNU_OBJ_SUFFIX'* ]]; then
    say "FAIL: the record path does not carry the tree suffix - a D13 record could be read for a 4570 pool"
    fail=1
else
    say "ok: both name the same path, and it carries \$XNU_OBJ_SUFFIX (per-tree)"
fi

if [[ $fail -ne 0 ]]; then
    say ""
    say "check_kernel_source_record_wired: REFUSED"
    exit 1
fi

say ""
say "check_kernel_source_record_wired: OK - the record is written, read, and named the same by both."