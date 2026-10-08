#!/bin/bash
# The entry link's object pool must follow the tree the build selected (experiment 926).
#
# `src/entry/build_entry.sh` used to read one hard-pinned tree (`external/xnu-4570.1.46`) and one
# hard-pinned object pool (`$REPO_ROOT/out/xnu_{kernel,asm,platform}_obj`), 289 times. That is the
# recurring defect class: a path pinned to the 4570 line that has to follow the selected tree. The
# fix derives `XNU`, the four pool directories and the `<string.h>`-style paths from `XNU_TREE` +
# `XNU_OBJ_SUFFIX`, so `XNU_TREE=<d13>` selects the `_d13` pools the D13 kernel build writes.
#
# Source-half: no compiler, no device. It reads `build_entry.sh` and refuses DRIFT:
#   (a) the pool directories are derived from the tree (a `_d13` suffix keyed on the D13 header);
#   (b) no LIVE object-path literal remains (a re-pin would silently read the 4570 pool on D13);
#   (c) `XNU` is `$XNU_TREE`, not a hard pin;
#   (d) the DEFAULT tree is 4570, matching `tools/build_xnu_arm_kernel.sh`'s default, so a plain
#       `./build_entry.sh` is unchanged.
# The `--selftest` feeds the pre-926 text (the literal pool) and asserts it is refused.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/.." && pwd)
BE=$REPO_ROOT/src/entry/build_entry.sh

note() { printf '  %s\n' "$*"; }

# The lines that are NOT comments: a `#` only counts at the start of a line or after whitespace.
live() { grep -vE '^[[:space:]]*#' "$1"; }

check_one() {
    local f="$1"
    # Read the live lines once. (`live | grep -q` would be wrong under `set -o pipefail`: grep -q
    # exits at the first match, the producer takes SIGPIPE, and the pipeline's status becomes 141 -
    # a passing check reads as a failure. This is the defect class check_mem_size_max.py names.)
    local L; L=$(live "$f")
    # (a) the suffix is derived from the tree's own D13 discriminator.
    if ! grep -q 'XNU_OBJ_SUFFIX=_d13' <<<"$L"; then
        note "(a) does not select the _d13 pool"; return 1
    fi
    if ! grep -q 'osfmk/sys/types.h' <<<"$L"; then
        note "(a) does not key the pool on the D13 header osfmk/sys/types.h"; return 1
    fi
    # (b) no live object-path literal (the pool refs must go through the derived variable).
    if grep -qE '\$REPO_ROOT/out/xnu_(kernel|asm|platform)_obj/' <<<"$L"; then
        note "(b) a live object path is still pinned to a literal pool directory"
        grep -nE '\$REPO_ROOT/out/xnu_(kernel|asm|platform)_obj/' <<<"$L" | head -3
        return 1
    fi
    # (c) `XNU` is the selected tree.
    if ! grep -q '^XNU=\$XNU_TREE$' <<<"$L"; then
        note "(c) XNU is not \$XNU_TREE"; return 1
    fi
    # (d) the default tree is 4570 (matches build_xnu_arm_kernel.sh's default).
    if ! grep -q 'XNU_TREE=\${XNU_TREE:-\$REPO_ROOT/external/xnu-4570.1.46}' <<<"$L"; then
        note "(d) the default XNU_TREE is not the 4570 tree"; return 1
    fi
    echo "check_entry_tree_pools: ok - the entry link's pool follows the selected tree"
    return 0
}

if [[ ${1:-} == --selftest ]]; then
    # The pre-926 text: a live `XNU=$REPO_ROOT/external/xnu-4570.1.46` and a literal pool path. It must
    # be refused, or the check proves nothing.
    tmp=$(mktemp)
    cat >"$tmp" <<'EOF'
XNU_TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
XNU=$REPO_ROOT/external/xnu-4570.1.46
ARM_INIT_OBJ=${STAGE90_ENTRY_ARM_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_init.o}
EOF
    if check_one "$tmp" >/dev/null 2>&1; then
        echo "check_entry_tree_pools: SELFTEST FAIL - the pre-926 text was accepted" >&2
        rm -f "$tmp"; exit 1
    fi
    rm -f "$tmp"
    echo "check_entry_tree_pools: selftest ok (the pre-926 pinned pool is refused)"
    exit 0
fi

check_one "$BE" || exit 1