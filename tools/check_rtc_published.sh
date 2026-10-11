#!/usr/bin/env bash
#
# Does the linked entry image actually PUBLISH the 19.2 MHz timebase by calling `rtc_configure`?
#
# WHY THIS EXISTS.  `osfmk/arm/rtclock.c:281` defines `rtc_configure(uint64_t hz)`, which stores its
# argument into `rtclock_sec_divisor` - the divisor `mach_absolute_time` and `absolutetime_to_*`
# divide by.  Nothing in the tree calls it, and nothing in the linked image calls it either: the
# only reference `nm` finds is the definition itself.  So `rtclock_sec_divisor` stays 0 and the
# 0x124f800 (19 200 000 Hz) the SoC's own CNTFRQ reports never reaches the clock subsystem by this
# path - it only lives in the PE shim's `msm8974_cntfrq` (src/platform/darwin13/pe_msm8974.c:68),
# which is a *different* variable with a *different* reader.  Making the boot call `rtc_configure`
# is the L2.1 fix.
#
# **THIS CHECK IS EXPECTED TO FAIL UNTIL L2.1 LANDS.**  The failure message says so explicitly: it
# is the DEFECT, not a broken check.  It is deliberately NOT a build refusal yet - folding a refusal
# into `build_entry.sh`'s link path would force a rebuild of the arm, and the backlog keeps this
# standalone and byte-neutral to the arm until the fix is in.
#
# What this checks, and what it does NOT:
#
#   - it disassembles `out/stage90/xnu_arm_entry.elf` and looks for a STATIC caller of
#     `rtc_configure` - either `bl <rtc_configure>` (a normal call) or `b <rtc_configure>` (a tail
#     call).  This is the TAB-SAFE form: `\s+` between the mnemonic and the target, anchored on the
#     `<rtc_configure>` symbol name and end-of-line;
#   - it requires the caller to BUILD the 0x124f800 argument in a register (`movw rN,#0xf800` paired
#     with `movt rN,#0x124`), so a caller that passes a wrong or zero hz is refused;
#   - and it independently asserts the value is embedded in the image at all, so a deleted
#     `#define MSM8974_CNTFRQ_HZ 19200000ULL` cannot leave the argument silently absent.
#   - it does NOT run anything on the device, and it does NOT prove the stored divisor is *used*
#     correctly downstream - only that it is published.
#
# NOTE ON `pipefail`.  All matching reads happen through here-strings (`<<<"$var"`), never a
# `producer | grep -q` pipeline: under `set -o pipefail` a `grep -q` on first match SIGPIPEs the
# producer and a true fact reads as a moved one.
#
# Usage: ./check_rtc_published.sh [--elf <path>] [--selftest]
# Exit 0 when a caller publishes 0x124f800; 1 while the L2.1 defect stands or a fact moved; 2 usage.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

ELF=${ENTRY_ELF:-$REPO_ROOT/out/stage90/xnu_arm_entry.elf}
SELFTEST=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --elf) ELF=$2; shift 2 ;;
        --selftest) SELFTEST=1; shift ;;
        *) echo "usage: $0 [--elf <path>] [--selftest]" >&2; exit 2 ;;
    esac
done

HZ=0x124f800
HZ_LO=$(printf '#%d' $((HZ & 0xffff)))   # #63488 -> objdump prints `; 0xf800`
HZ_HI=$(printf '#%d' $((HZ >> 16)))      # #292   -> objdump prints `; 0x124`

refuse() { printf 'check_rtc_published: %s\n' "$1" >&2; exit 1; }
has() { grep -qE "$2" <<<"$1"; }

# caller_lines DUMP  - the `bl/b <rtc_configure>` call sites, if any.
caller_lines() { grep -E '(^|[[:space:]])(bl|b)[[:space:]]+[0-9a-f]+ <rtc_configure>$' <<<"$1" || true; }

# builds_hz BLOCK  - true if BLOCK materialises 0x124f800: a `movw rN, #0xf800`/`movt rN, #0x124`
# pair for the same register N. objdump renders the immediate both in decimal and as `; 0x....`, so
# match the hex suffix.
builds_hz() {
    local lo hi
    lo=$(grep -oE 'movw[[:space:]]+r[0-9]+,[[:space:]]+#[0-9]+[[:space:]]*;[[:space:]]*0xf800' <<<"$1" | grep -oE 'r[0-9]+' | head -1 || true)
    [[ -n $lo ]] || return 1
    hi=$(grep -oE 'movt[[:space:]]+'"$lo"'[[:space:]]*,[[:space:]]*#[0-9]+[[:space:]]*;[[:space:]]*0x124' <<<"$1" | head -1 || true)
    [[ -n $hi ]]
}

# block_of DUMP ADDR  - the disassembly block (function body) that contains the line whose address
# is ADDR: split the dump on `^[0-9a-f]+ <sym>:$` headers and print the block whose text contains a
# line beginning `<ADDR>:`.
block_of() {
    awk -v a="$2" '
        /^[0-9a-f]+ <[^>]+>:$/ { if (buf != "" && hit) { print buf; exit }
                                  buf=$0 ORS; hit=0; next }
        buf != "" { buf=buf $0 ORS; if (substr($0,1,length(a)+1)==(a ":")) hit=1 }
        END { if (buf != "" && hit) print buf }
    ' <<<"$1" || true
}

# ---- selftest: drive the matchers over synthetic disassembly -------------------------------------
if [[ $SELFTEST -eq 1 ]]; then
    good='80090000 <caller>:
80090000:	e30c0f80 	movw	r0, #63488	; 0xf800
80090004:	e3410124 	movt	r0, #292	; 0x124
80090008:	ebffff00 	bl	8001b5e0 <rtc_configure>
'
    bad_no_caller='80090000 <caller>:
80090000:	e12fff1e 	bx	lr
'
    bad_wrong_hz='80090000 <caller>:
80090000:	e3a00000 	mov	r0, #0
80090004:	ebffff00 	bl	8001b5e0 <rtc_configure>
'
    # good: a caller is found AND it builds 0x124f800
    cl=$(caller_lines "$good")
    [[ -n $cl ]] || refuse "selftest: the matcher missed a real 'bl <rtc_configure>' - the check would never pass"
    builds_hz "$good" || refuse "selftest: the matcher rejected a caller that builds 0x124f800"
    # bad: no caller
    [[ -z $(caller_lines "$bad_no_caller") ]] || refuse "selftest: a dump with no caller was read as having one"
    # bad: caller present, wrong hz
    cl=$(caller_lines "$bad_wrong_hz")
    [[ -n $cl ]] || refuse "selftest: the wrong-hz dump was read as having no caller"
    if builds_hz "$bad_wrong_hz"; then
        refuse "selftest: a caller that passes #0 was accepted as publishing 0x124f800"
    fi
    # block_of must isolate the caller's OWN function, not a neighbouring one.
    two='80080000 <other>:
80080000:	e30c0f80 	movw	r0, #63488	; 0xf800
80080004:	e3410124 	movt	r0, #292	; 0x124
80090000 <caller>:
80090000:	e3a00000 	mov	r0, #0
80090004:	ebffff00 	bl	8001b5e0 <rtc_configure>
'
    blk=$(block_of "$two" 80090004)
    has "$blk" '^80090000 <caller>:$' || refuse "selftest: block_of did not isolate the caller's function"
    has "$blk" '^80090004:' || refuse "selftest: block_of did not include the caller line"
    if builds_hz "$blk"; then
        refuse "selftest: block_of leaked a neighbouring function's 0x124f800 into the caller's block"
    fi
    printf 'check_rtc_published: selftest ok - finds a real caller, requires 0x124f800, refuses a caller without it, isolates the caller block\n'
    exit 0
fi

# ---- the real check ------------------------------------------------------------------------------
[[ -f $ELF ]] || refuse "no entry image at $ELF"
command -v arm-none-eabi-objdump >/dev/null 2>&1 || refuse "arm-none-eabi-objdump not on PATH"

dump=$(arm-none-eabi-objdump -d "$ELF" 2>/dev/null || true)
[[ -n $dump ]] || refuse "objdump produced no disassembly for $ELF"

# fact 1: the 19.2 MHz value is embedded in the image at all.
python3 -I - "$ELF" "$HZ" <<'PY' || refuse "the 19.2 MHz value 0x124f800 is not embedded in $ELF - MSM8974_CNTFRQ_HZ moved or was deleted"
import sys
elf, hz = sys.argv[1], int(sys.argv[2], 16)
with open(elf, "rb") as f:
    data = f.read()
sys.exit(0 if data.count(hz.to_bytes(4, "little")) else 1)
PY

# fact 2: a static caller of rtc_configure exists.
cl=$(caller_lines "$dump")
if [[ -z $cl ]]; then
    refuse "rtc_configure has 0 STATIC CALLERS in $ELF - it is defined (nm shows exactly one 'rtc_configure' symbol, the definition) but nothing reaches it, so rtclock_sec_divisor is never published. THIS IS THE L2.1 DEFECT, not a broken check; it will pass once the boot calls rtc_configure."
fi

# fact 3: each caller builds 0x124f800 in a register.
addr=$(grep -oE '^[[:space:]]*[0-9a-f]+' <<<"$cl" | head -1 | tr -d ' ' || true)
[[ -n $addr ]] || refuse "found a caller line but could not read its address: $cl"
blk=$(block_of "$dump" "$addr")
[[ -n $blk ]] || refuse "could not isolate the function block containing the caller at $addr"
builds_hz "$blk" \
    || refuse "a caller of rtc_configure at $addr does not build 0x124f800 (19 200 000 Hz) in a register - it publishes the wrong divisor"

printf 'check_rtc_published: ok - the image calls rtc_configure and publishes 0x124f800 (19.2 MHz)\n'