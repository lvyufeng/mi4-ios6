#!/usr/bin/env bash
#
# Are `rtclock_bus_configure`'s two timebase guards still present, in place, and unweakened?
#
# WHY THIS EXISTS.  `rtclock_bus_configure` (osfmk/arm/rtclock.c) is the frontier the D13 boot is
# heading for.  Before it computes anything it runs TWO guards, and their exact shape and position
# are load-bearing:
#
#   - **guard-1 at rtclock.c:294** - `panic("rtclock_bus_configure")` fires when
#     `gPEClockFrequencyInfo.timebase_frequency_hz` is 0.  This is the guard the PE lane's 19.2 MHz
#     fix has to keep satisfied: if the PE stops publishing a timebase, the boot stops HERE.
#   - **guard-2 at rtclock.c:297** - `panic("rtclock_bus_configure 2")` fires when
#     `rtclock_sec_divisor` is 0, i.e. when `rtc_configure` was never called.  **This is the guard
#     that names the L2.1 defect directly** (see `tools/check_rtc_published.sh`): with no caller of
#     `rtc_configure`, `rtclock_sec_divisor` is 0 and this panic is what the boot would hit.
#
# The guard is `if(<expr>) panic(...)` with the panic on the FOLLOWING line.  That layout, and the
# `if` immediately before each panic, is what this check pins - a "steady-state" refactor that turns
# the guard into a no-op, an early return, or moves the panic off these lines is refused.  (The line
# numbers are cited in several memory notes as 294/297, so the check anchors on those exact lines.)
#
# What this checks, and what it does NOT:
#
#   - it reads `osfmk/arm/rtclock.c` BY VALUE and asserts rtclock.c:294 and :297 are the two panic
#     lines, that rtclock.c:293/:296 are their guards, and that each guard names the right variable
#     with the `!x` (zero -> panic) sense;
#   - it asserts the guard expression is `if(!gPEClockFrequencyInfo.timebase_frequency_hz)` and
#     `if(!rtclock_sec_divisor)` - a weakened `!=` / removed `!` / swapped variable is refused;
#   - it asserts the function is `rtclock_bus_configure`, so the guards cannot drift into a
#     different (e.g. i386) rtclock.c;
#   - it does NOT judge whether the guards are *satisfied* at runtime - that is the PE lane's
#     `check_rtc_published.sh` and the device. This is a source-shape assertion only.
#
# NOTE ON `pipefail`.  Matching uses here-strings (`<<<"$var"`), never `producer | grep -q`, so a
# first-match `grep -q` cannot SIGPIPE a producer and turn a true fact into a false negative.
#
# Usage: ./check_timebase_guard.sh [--tree <XNU_TREE>] [--selftest]
# Exit 0 when both guards hold their lines and sense; 1 when a guard moved or weakened; 2 usage.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
SELFTEST=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --tree) TREE=$2; shift 2 ;;
        --selftest) SELFTEST=1; shift ;;
        *) echo "usage: $0 [--tree <XNU_TREE>] [--selftest]" >&2; exit 2 ;;
    esac
done

RTCLOCK=$TREE/osfmk/arm/rtclock.c

# Guard-1 at :294 (panic) guarded by :293; guard-2 at :297 guarded by :296.
GUARD1_LINE=294
GUARD2_LINE=297

refuse() { printf 'check_timebase_guard: %s\n' "$1" >&2; exit 1; }

# assert_guard FILE  - the whole set of assertions, over a possibly-mutated file (so --selftest can
# feed a weakened copy and prove each assertion bites).
assert_guard() {
    local f=$1
    [[ -f $f ]] || refuse "no D13 rtclock.c at $f"

    # The two cited lines are the two panic calls.
    local p1 p2
    p1=$(sed -n "${GUARD1_LINE}p" "$f")
    p2=$(sed -n "${GUARD2_LINE}p" "$f")
    grep -qE 'panic[[:space:]]*\([[:space:]]*"rtclock_bus_configure"[[:space:]]*\)' <<<"$p1" \
        || refuse "rtclock.c:${GUARD1_LINE} is not the timebase panic - it reads: ${p1}"
    grep -qE 'panic[[:space:]]*\([[:space:]]*"rtclock_bus_configure 2"[[:space:]]*\)' <<<"$p2" \
        || refuse "rtclock.c:${GUARD2_LINE} is not the divisor panic - it reads: ${p2}"

    # Their guards sit on the immediately preceding lines, with the `!x` (zero -> panic) sense.
    local g1 g2
    g1=$(sed -n "$((GUARD1_LINE - 1))p" "$f")
    g2=$(sed -n "$((GUARD2_LINE - 1))p" "$f")
    grep -qE 'if[[:space:]]*\([[:space:]]*![[:space:]]*gPEClockFrequencyInfo\.timebase_frequency_hz[[:space:]]*\)' <<<"$g1" \
        || refuse "rtclock.c:$((GUARD1_LINE - 1)) is not 'if(!gPEClockFrequencyInfo.timebase_frequency_hz)' - the timebase guard moved or weakened; it reads: ${g1}"
    grep -qE 'if[[:space:]]*\([[:space:]]*![[:space:]]*rtclock_sec_divisor[[:space:]]*\)' <<<"$g2" \
        || refuse "rtclock.c:$((GUARD2_LINE - 1)) is not 'if(!rtclock_sec_divisor)' - the divisor guard moved or weakened; it reads: ${g2}"

    # Guard-1 must precede guard-2 (timebase checked before the divisor).
    [[ $((GUARD1_LINE - 1)) -lt $((GUARD2_LINE - 1)) ]] || refuse "the two guards are out of order"

    # Both panics must be inside rtclock_bus_configure.
    local fn
    fn=$(awk -v ln="$GUARD2_LINE" '
        /^[A-Za-z_].*rtclock_bus_configure[[:space:]]*\(/ { last=NR }
        NR==ln { print last; exit }
    ' "$f" || true)
    [[ -n $fn ]] || refuse "could not find the enclosing function of the guards"
    local sig
    sig=$(sed -n "${fn}p" "$f")
    grep -qE 'rtclock_bus_configure[[:space:]]*\(' <<<"$sig" \
        || refuse "the guards at rtclock.c:${GUARD1_LINE}/${GUARD2_LINE} are no longer inside rtclock_bus_configure (enclosing line: ${sig})"
}

check_real() {
    assert_guard "$RTCLOCK"
    printf 'check_timebase_guard: ok - guard-1 at rtclock.c:%d (!timebase_frequency_hz -> panic) and guard-2 at :%d (!rtclock_sec_divisor -> panic) both hold, inside rtclock_bus_configure\n' \
        "$GUARD1_LINE" "$GUARD2_LINE"
}

if [[ $SELFTEST -eq 1 ]]; then
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT

    # Mutation A: weaken guard-1's sense (`!` removed) -> must refuse.
    sed -E "$((GUARD1_LINE - 1))s/if\(![[:space:]]*gPEClockFrequencyInfo/if(gPEClockFrequencyInfo/" "$RTCLOCK" > "$tmp/weak1.c" || true
    # drive assert_guard directly on the mutated copy
    if ( assert_guard "$tmp/weak1.c" ) >/dev/null 2>&1; then
        refuse "selftest: a weakened guard-1 (no '!') was accepted - the sense test does not bite"
    fi

    # Mutation B: move guard-2's panic off its cited line -> must refuse.
    awk -v ln="$GUARD2_LINE" 'NR==ln{print "# moved away"; next} {print}' "$RTCLOCK" > "$tmp/moved2.c"
    if ( assert_guard "$tmp/moved2.c" ) >/dev/null 2>&1; then
        refuse "selftest: a guard-2 panic moved off line ${GUARD2_LINE} was accepted - the line pin does not bite"
    fi

    # Mutation C: swap the guarded variable (rtclock_sec_divisor -> something else) -> must refuse.
    sed -E "$((GUARD2_LINE - 1))s/rtclock_sec_divisor/rtclock_something_else/" "$RTCLOCK" > "$tmp/swap2.c" || true
    if ( assert_guard "$tmp/swap2.c" ) >/dev/null 2>&1; then
        refuse "selftest: a guard-2 watching the wrong variable was accepted - the variable test does not bite"
    fi

    # Sanity: the unmutated file must PASS.
    if ! ( assert_guard "$RTCLOCK" ) >/dev/null 2>&1; then
        refuse "selftest: the unmutated tree FAILED the guard check - the check is broken, not the tree"
    fi

    printf 'check_timebase_guard: selftest ok - weakened, moved, and swapped guards are all refused; the real file passes\n'
    exit 0
fi

check_real