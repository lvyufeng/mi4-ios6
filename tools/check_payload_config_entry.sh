#!/usr/bin/env bash
#
# Does the payload in `out/` carry the XNU-entry switch the record's arms carry?
#
# WHY THIS EXISTS
# ---------------
# `./scripts/build.sh` takes its variant switches from `STAGE90_EXTRA_CFLAGS`, and **its default leaves
# `STAGE90_XNU_ENTRY` at `0u` where every arm in `records/revert-set.txt` carries `1`.** A payload built
# that way has the entry path switched off (`xnu_kernel.c:234`'s call site is behind
# `#if STAGE90_XNU_ENTRY`), so it never jumps into XNU - the boot it produces cannot reach the OS at
# all, which is the one clause of the standing goal this project is working on.
#
# **It has happened three times and nothing in the build refused any of them.** The record's own
# account, of 741's step: *"that payload's own record said `#define STAGE90_XNU_ENTRY 0u`, its embedded
# blob was the right entry image, and every hash it takes part in matched. It was caught by comparing
# `stage90-build-config.txt` against this record."* Every hash matching is the point: this is
# [[mi4-one-value-two-definitions]] in its purest form - one quantity (which switches the payload was
# built with) with two readings (the config file, and what the build was actually told), and **the
# config file is the only record of it anywhere.** No manifest, no SHA256SUMS and no symbol table can
# see the difference, because both builds' hashes are internally consistent.
#
# **What the record names as the net is the gate** (`preflight_boot_check.sh:1699-1719` refuses
# `--allow-xnu-entry` against an image built with the switch off), which is row 3 of
# `tools/verify_press_ready.sh`. That net works, and it is a READINESS net: it fires only when someone
# chooses to run the readiness tool. The record has asked for this check in `make check` for four steps
# and called it "one line of `cmp`".
#
# **A `cmp` is the wrong instrument, and this is worth saying because the record says `cmp`.** The
# mistake happens *before* the arm is recorded, so there is no record line to compare against yet - the
# four steps that asked for it would each have found the comparison silent at exactly the moment it was
# needed. What is compared here instead is a **property**: the live config's own value must be the value
# the record's arms carry. That fires on the wrong build itself, with no record line for the new arm
# required.
#
# WHAT IT CHECKS, AND WHY EACH ONE IS A REFUSAL RATHER THAN A SENTENCE
# -------------------------------------------------------------------
#   1. the live config exists and is a non-empty regular file     - otherwise exit 2: a check that
#      could not read its own subject has not passed
#   2. it names `STAGE90_XNU_ENTRY` EXACTLY ONCE                  - an absent key and a doubled key are
#      the two ways a value stops being readable (m720), and a `sed -n 's/^#define K //p'` over a file
#      that names it twice prints both lines into one variable, silently
#   3. its value equals the value EVERY parked arm carries        - **derived, not asserted**: a
#      constant `1` written into this file would be a value nothing in the record can contradict, which
#      is m719. The parks are the record's arms, frozen on disk; if they ever disagree among
#      themselves the baseline is ambiguous and this refuses rather than picking one
#   4. at least one parked arm's config could be read             - otherwise exit 2. Zero parks is
#      not unanimity and must never read as a pass
#
# A parked arm whose config does not name the key exactly once is SKIPPED AND COUNTED, and the count is
# printed in both the refusal and the pass line. That is the project's own treatment of a historical
# oddity (`tools/check_set_name_rule.sh` names `frozen-574` as a label and does not refuse it): a frozen
# park is not something to fix, and a silent skip is the failure this file exists to prevent - so it is
# named, and the derivation says how many arms it rests on.
#
# THE ARGUMENT
# ------------
# An optional first argument names the `out/` directory, so this can be falsified against a scratch tree
# without touching `out/stage90/`. A path the caller names is resolved against the caller, never against
# this script. `make check` invokes it with no argument.
#
# Exit status: 0 the live payload carries the record's value, 1 a refusal (the property is false, or the
# derivation is ambiguous), 2 the check could not run (a refusal, never a pass).

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

OUT=${1:-$REPO_ROOT/out/stage90}
CFG=$OUT/stage90-build-config.txt
KEY=STAGE90_XNU_ENTRY
PARKS=$OUT/frozen

if [[ ! -f $CFG || ! -s $CFG ]]; then
    echo "check_payload_config_entry: $CFG is absent, not a regular file, or empty - REFUSING." >&2
    echo "  The payload's switch record is the only place the build's switches are written down, so there" >&2
    echo "  is nothing here to compare and this is not a pass." >&2
    exit 2
fi

# --- the live reading, and the two ways it stops being readable -----------------------------------
n=$(grep -c "^#define $KEY " "$CFG")
if [[ "$n" != 1 ]]; then
    echo "check_payload_config_entry: REFUSED. $CFG names $KEY $n time(s); it must be named exactly once."
    echo "  An ABSENT line means the switch was never defined and the payload's behaviour is decided" >&2
    echo "  somewhere no record reaches; a DOUBLED line means whichever reader asks gets the last one and" >&2
    echo "  a reader that asks twice cannot tell. Neither is a value this check can compare." >&2
    exit 1
fi
live=$(sed -n "s/^#define $KEY //p" "$CFG")

# --- the value the record's arms carry, read off the parks rather than written down here ----------
declare -A tally=()
npark=0; nskip=0; skipped=''
if [[ -d $PARKS ]]; then
    shopt -s nullglob
    for f in "$PARKS"/*/stage90-build-config.txt; do
        m=$(grep -c "^#define $KEY " "$f")
        if [[ "$m" != 1 ]]; then
            nskip=$(( nskip + 1 ))
            skipped="$skipped $(basename "$(dirname "$f")")($m)"
            continue
        fi
        v=$(sed -n "s/^#define $KEY //p" "$f")
        tally[$v]=$(( ${tally[$v]:-0} + 1 ))
        npark=$(( npark + 1 ))
    done
    shopt -u nullglob
fi

if [[ $npark -eq 0 ]]; then
    echo "check_payload_config_entry: REFUSING. No parked arm under $PARKS has a readable $KEY line, so" >&2
    echo "  there is no baseline to compare the live value against. Zero parks is not unanimity, and a" >&2
    echo "  check with nothing to derive from has not passed." >&2
    exit 2
fi

if [[ ${#tally[@]} -ne 1 ]]; then
    echo "check_payload_config_entry: REFUSED. The parked arms disagree about $KEY:"
    for v in "${!tally[@]}"; do
        printf '    %-12s carried by %d arm(s)\n' "$v" "${tally[$v]}"
    done
    echo "  An ambiguous baseline is not a baseline, and picking one of these would be this check" >&2
    echo "  naming its own answer. The record has to be settled before a new arm can be measured" >&2
    echo "  against it." >&2
    exit 1
fi

want=''
for v in "${!tally[@]}"; do want=$v; done
[[ -z "$skipped" ]] || echo "check_payload_config_entry: skipped ${nskip} park(s) whose config does not name $KEY once:${skipped}"

if [[ "$live" != "$want" ]]; then
    echo "check_payload_config_entry: REFUSED. $CFG says $KEY is $live, and $npark parked arm(s) say $want."
    echo "  **THIS IS THE BUILD THAT NEVER REACHES XNU.** ./scripts/build.sh takes its variant switches" >&2
    echo "  from STAGE90_EXTRA_CFLAGS and its default leaves $KEY at 0u, so a plain build produces a" >&2
    echo "  payload whose entry path is switched off - it hands nothing to the image, and no hash, no" >&2
    echo "  manifest and no symbol table can see the difference, because both builds agree with" >&2
    echo "  themselves. Rebuild with, from the repo root:" >&2
    printf '    STAGE90_EXTRA_CFLAGS=%s ./scripts/build.sh\n' "'-DSTAGE90_XNU_ENTRY=1'"
    exit 1
fi

echo "check_payload_config_entry: ok - $CFG names $KEY once, value $live, and all $npark parked arm(s) with a"
echo "  readable line carry $live. This is the switch whose default the build leaves OFF, so a refusal here"
echo "  is a payload that could not reach the OS at all - and one that no hash in the tree can see."
