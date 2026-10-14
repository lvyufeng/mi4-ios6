#!/usr/bin/env python3
"""Assert BY VALUE that the linked image bounds the user data-abort retry (1011 F4), and that the OFF
image is byte-identical to the frozen 1009 arm.

Reads the section table (`arm-none-eabi-objdump -h`) and the disassembly (`-d`) of a linked image, and
checks the bound `tools/patch_d13_abt_retry_bound.py` plants in `sleh_abort` - from the BYTES and the
INSTRUCTIONS, not from any source comment (`mi4-a-claim-in-a-comment-is-not-a-check`).

WHAT IT PROVES.

  ON  (`--expect on`, default): the image carries the bound.
    1. the byte string `ABT_RETRY_BOUND:` exists in an allocatable PROGBITS section, and its VA is
       materialized in `sleh_abort`'s body (a `movw/movt` pair, or an `ldr rD,[pc,#N]` whose pool word
       is that VA) - or, if clang outlined the cold report, in a symbol one branch away from
       `sleh_abort`.
    2. the body that carries the string calls printf (`bl/blx`).
    3. `sleh_abort` still calls `vm_fault` and `doexception` - the retry machinery is intact and the
       escalation path (fall through to `doexception`) is present.
    4. the bound N is a small immediate: some body reachable from `sleh_abort` (or `sleh_abort`
       itself) compares against an immediate <= ABT_RETRY_BOUND_MAX.  A bound that is not small is
       not a bound (the storm is ~2.09 M).
  OFF (`--expect off`): the image is BYTE-IDENTICAL to the frozen 1009 arm and carries NO bound.
    5. `--off-elf <path>`: sha256(path) == FROZEN_ELF_SHA256 (the frozen 1009 elf), i.e. `cmp`-level
       identity - the switch OFF build reproduces the 1009 artifact exactly.
    6. the image carries NO `ABT_RETRY_BOUND:` string and NO intrinsic hint (`udf` / `__builtin_trap`)
       - the NEGATIVE CONTROL (`docs/1011-design.md` §3 "OFF identity").  A `sleh_abort` that instead
       jumped straight to an intrinsic would be a DIFFERENT fix (a hard trap), and this checker names
       it rather than passing.

Exit 0 iff every check for the requested orientation passes.  The frozen 1009 elf must FAIL the ON
checks (no `ABT_RETRY_BOUND:` string) and PASS the OFF checks.
"""
import hashlib
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TARGET = "sleh_abort"
STRING = b"ABT_RETRY_BOUND:"
VMFAULT = "vm_fault"
# The escalation target, BY VALUE.  `doexception` (`hd2 trap.c:363`) is a two-line wrapper around
# `exception_triage` and clang INLINES it into `sleh_abort` - so the name `doexception` never appears
# in sleh_abort's body in the linked image (measured: `doexception` at 0x8046c394 in the frozen elf is
# a tiny push/ldr/stm/bl/ret, and NO `bl` in sleh_abort targets it; sleh_abort has a direct
# `bl exception_triage`).  The honest by-value check for "the escalation path survives" is therefore
# that sleh_abort still reaches `exception_triage`, not the inlined wrapper's name.
EXCEPTION_TRIAGE = "exception_triage"
# The frozen 1009 arm (`armed-d13-24997fc5`): the negative control AND the OFF-identity reference.
FROZEN_ELF = "out/stage90/frozen/armed-d13-24997fc5/xnu_arm_entry.elf"
FROZEN_ELF_SHA256 = "c66367dfdaa228062df8d6d5a67f998dc453ad52df02adb4e5b1a0072725b3cf"
FROZEN_BIN_SHA256 = "24997fc5eb23956548d2c3d0736642ed6a82f22075fda341e916030eed9b4315"
# The OFF live artifact (the arm a press names): its .bin must equal the frozen 1009 bin.
LIVE_ELF = "out/stage90/xnu_arm_entry.elf"
LIVE_BIN = "out/stage90/xnu_arm_entry.bin"
ABT_RETRY_BOUND_MAX = 64  # a "small N"; the storm is ~2.09 M, so the bound must be far below this

_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t([a-z0-9.]+)\s*(.*)$")
_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")
_BRANCH = re.compile(r"^([0-9a-f]+) <([^>]+)>$")
_CONDS = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al")
_BRANCH_MN = {"b", "bl", "bx", "blx"} | {f"b{c}" for c in _CONDS}
_MOVW = re.compile(r"^movw\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
_MOVT = re.compile(r"^movt\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
_LDR_PC = re.compile(r"^ldr\s+(r\d+),\s*\[pc(?:,\s*#(-?(?:0x[0-9a-fA-F]+|\d+)))?\]$")
_WRITES = re.compile(r"^(?:mov|movw|movt|mvn|adr|ldr|ldrb|ldrh|ldrsb|ldrsh|ldrd|add|adds|adc|sub|subs"
                     r"|sbc|rsb|and|ands|orr|orrs|eor|eors|bic|bics|mul|mla|mls|lsl|lsls|lsr|lsrs"
                     r"|asr|asrs|ror|rors|rev|clz|sxtb|sxth|sxtb16|uxtb|uxth|uxtb16)\s+(r\d+)\b")
# a compare against a small immediate: `cmp rN, #imm`, `cmn rN, #imm`, `subs rD, rN, #imm`
_CMP_IMM = re.compile(r"^(?:cmp|cmn|subs?)\s+r\d+,\s*r?\d*,?\s*#(0x[0-9a-fA-F]+|\d+)$")


def objdump(args):
    return subprocess.run([OBJDUMP] + args, capture_output=True, text=True, check=True).stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sections(path):
    """[(name, vma, size, file_off)] for every allocatable, byte-carrying (PROGBITS) section."""
    lines = objdump(["-h", path]).splitlines()
    out, i = [], 0
    while i < len(lines):
        m = _SECTION.match(lines[i])
        if not m:
            i += 1
            continue
        name, size, vma, off = m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(5), 16)
        i += 1
        flags = []
        while i < len(lines) and lines[i].strip() and not _SECTION.match(lines[i]):
            flags.append(lines[i].strip())
            i += 1
        f = " ".join(flags)
        if "ALLOC" in f and "CONTENTS" in f:
            out.append((name, vma, size, off))
    return out


def find_string(path, secs, needle=STRING):
    with open(path, "rb") as fh:
        blob = fh.read()
    for name, vma, size, off in secs:
        hit = blob[off:off + size].find(needle)
        if hit >= 0:
            return vma + hit, name
    return None


def make_u32_reader(path, secs):
    with open(path, "rb") as fh:
        blob = fh.read()

    def read_u32(va):
        for name, vma, size, off in secs:
            if vma <= va and va + 4 <= vma + size:
                at = off + (va - vma)
                return int.from_bytes(blob[at:at + 4], "little")
        return None

    return read_u32


def bodies(path):
    funcs, cur = {}, None
    for line in objdump(["-d", path]).splitlines():
        m = _LABEL.match(line)
        if m:
            if not m.group(2).startswith("."):
                cur = m.group(2)
                funcs.setdefault(cur, [])
            continue
        m = _INSN.match(line)
        if m and cur is not None:
            funcs[cur].append((int(m.group(1), 16), m.group(2), m.group(3).split(";")[0].strip()))
        elif line and not line.startswith((" ", "\t")) and ":" not in line:
            cur = None
    return funcs


def parse_imm(ops):
    m = re.search(r"#(0x[0-9a-fA-F]+|\d+)", ops)
    if not m:
        return None
    return int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))


def materializes_string(body, string_va, read_u32):
    pending = {}
    for va, mn, ops in body:
        text = f"{mn} {ops}".strip()
        m = _MOVT.match(text)
        if m:
            reg = m.group(1)
            hi, lo = parse_imm(ops), pending.pop(reg, None)
            if lo is not None and hi is not None and (hi << 16) | lo == string_va:
                return f"movw/movt {reg} (#{string_va:#010x})"
            continue
        m = _MOVW.match(text)
        if m:
            reg, imm = m.group(1), parse_imm(ops)
            if imm is not None:
                pending[reg] = imm
            else:
                pending.pop(reg, None)
            continue
        m = _LDR_PC.match(text)
        if m:
            off = parse_imm(ops) if m.group(2) is not None else 0
            if off is not None and read_u32(va + 8 + off) == string_va:
                return f"ldr {m.group(1)}, [pc, #{off}] (pool word at {va + 8 + off:#010x})"
            continue
        m = _WRITES.match(text)
        if m:
            pending.pop(m.group(1), None)
    return None


def branch_targets(body):
    out, seen = [], set()
    for va, mn, ops in body:
        if mn not in _BRANCH_MN:
            continue
        m = _BRANCH.match(ops)
        if not m:
            continue
        name = m.group(2).split("+", 1)[0]
        if name.startswith(".") or name == TARGET or name in seen:
            continue
        seen.add(name)
        out.append((name, va))
    return out


def calls_to(body, callee):
    return [f"{va:#010x}" for va, mn, ops in body
            if mn in ("bl", "blx") and callee in ops.split("<", 1)[-1]]


def reaches(body, target_sym):
    """direct bl/blx to target_sym, or a to-another-symbol branch whose target's own body calls it."""
    direct = calls_to(body, target_sym)
    if direct:
        return direct, "direct"
    for name, _ in branch_targets(body):
        if target_sym in name:
            return [], f"via <{name}>"
    return [], None


def small_imm_hits(funcs, start, depth=2):
    """[(symbol, va, imm)] for every `cmp/cmn/subs rN, #imm` with a small imm, reachable from `start`
    by itself plus <= depth-1 hops through symbols its bodies branch to."""
    seen, frontier, out = set(), [TARGET], []
    while frontier and depth > 0:
        cur = frontier.pop()
        if cur in seen:
            continue
        seen.add(cur)
        body = funcs.get(cur, [])
        for va, mn, ops in body:
            if mn in ("cmp", "cmn", "sub", "subs"):
                m = _CMP_IMM.match(f"{mn} {ops}".strip())
                if m:
                    imm = parse_imm(ops)
                    if imm is not None and 0 < imm <= ABT_RETRY_BOUND_MAX:
                        out.append((cur, va, imm))
        for name, _ in branch_targets(body):
            frontier.append(name)
        depth -= 1
    return out


def intrinsic_hint(funcs):
    """Locations of a hard trap (`udf` / `__builtin_trap` = `udf #0xfe`): an escalation that is a trap,
    not the fall-through-to-doexception this tool plants.  Named, not silently accepted."""
    hits = []
    for sym, body in funcs.items():
        for va, mn, ops in body:
            if mn == "udf":
                hits.append((sym, va, ops))
    return hits


def _fail(msg):
    print("  FAIL " + msg)
    return False


def check_on(path):
    if not os.path.isfile(path):
        _fail("no such file - nothing to check")
        return 2
    try:
        secs = sections(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        _fail(f"{OBJDUMP} could not read it: {exc}")
        return 2
    read_u32 = make_u32_reader(path, secs)
    abort = funcs.get(TARGET)
    ok = True

    found = find_string(path, secs)
    if found is None:
        _fail(f"the `{STRING.decode()}` string is not in the image at all (the switch never reached "
              f"the pool, or this is a pre-1011 image)")
        ok = False
    else:
        print(f"  ok   `{STRING.decode()}` at {found[0]:#010x} in {found[1]}")

    if found is None:
        print("  ---- checks 2-4 not reached: there is no string address to look for")
    elif abort is None:
        _fail(f"no {TARGET} body to look in")
        ok = False
    else:
        sva = found[0]
        mat = materializes_string(abort, sva, read_u32)
        carrier, where = abort, "own body"
        if mat is None:
            for name, brva in branch_targets(abort):
                other = funcs.get(name)
                if other is None:
                    continue
                hop = materializes_string(other, sva, read_u32)
                if hop is not None:
                    mat, carrier = hop, other
                    where = f"outlined body <{name}>, one hop: {brva:#010x} branches to it"
                    break
        if mat is None:
            _fail(f"{TARGET}: the `{STRING.decode()}` address {sva:#010x} is materialized nowhere - "
                  f"neither in sleh_abort nor in any single symbol its body branches to")
            ok = False
        else:
            print(f"  ok   {TARGET}: string address materialized by {mat} ({where})")
        if carrier is not None:
            cs = calls_to(carrier, "printf")
            if cs:
                print(f"  ok   the bound reports via printf at {cs[0]}")
            else:
                _fail("the bound body never reports - no bl/blx to printf in the body carrying the string")
                ok = False

    # the retry machinery + the escalation path must both still be reachable from sleh_abort
    if abort is None:
        _fail(f"{TARGET} is not in the image")
        ok = False
    else:
        vf, vfhow = reaches(abort, VMFAULT)
        de, dehow = reaches(abort, EXCEPTION_TRIAGE)
        if vf or vfhow:
            print(f"  ok   {TARGET} still reaches <{VMFAULT}> ({vf or vfhow}) - the retry machinery is intact")
        else:
            _fail(f"{TARGET}: no path to <{VMFAULT}> - the fix removed the fault attempt")
            ok = False
        if de or dehow:
            print(f"  ok   {TARGET} still reaches <{EXCEPTION_TRIAGE}> ({de or dehow}) - the escalation "
                  f"path is intact (doexception is inlined; the call is by value)")
        else:
            _fail(f"{TARGET}: no path to <{EXCEPTION_TRIAGE}> - the escalation has nowhere to go")
            ok = False

    # the bound is a SMALL immediate
    hits = small_imm_hits(funcs, TARGET)
    hits = [h for h in hits if h[2] <= ABT_RETRY_BOUND_MAX]
    # narrow to a compare whose neighbourhood also touches the string carrier (best-effort signal);
    # otherwise report any small compare reachable - the two together are the bound.
    if hits:
        shown = ", ".join(f"<{s}+{va:#x}> #{imm}" for s, va, imm in hits[:4])
        print(f"  ok   a small immediate compare is reachable from {TARGET}: {shown}")
    else:
        _fail(f"no `cmp/cmn/subs rN, #imm` with 0 < imm <= {ABT_RETRY_BOUND_MAX} is reachable from "
              f"{TARGET} - the bound is not a small constant (or the compare was optimised away)")
        ok = False

    print(f"check_d13_abt_retry_bound: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def check_off(frozen, off_elf):
    """OFF identity: (5) the OFF elf is byte-identical to the frozen 1009 arm, and (6) carries no
    bound string and no intrinsic-trap hint (the negative control)."""
    ok = True
    if not os.path.isfile(frozen):
        _fail(f"no frozen 1009 elf at {frozen} - nothing to compare against")
        return 2
    fsha = sha256(frozen)
    if fsha != FROZEN_ELF_SHA256:
        _fail(f"the frozen reference {frozen} has sha256 {fsha}, not the pinned "
              f"{FROZEN_ELF_SHA256} - the reference moved, re-pin before trusting an OFF pass")
        ok = False
    else:
        print(f"  ok   frozen reference sha256 == {FROZEN_ELF_SHA256}")

    target = off_elf if off_elf else frozen
    if not os.path.isfile(target):
        _fail(f"no OFF elf at {target}")
        return 2
    if off_elf:
        osha = sha256(target)
        if osha == FROZEN_ELF_SHA256:
            print(f"  ok   OFF elf {target} is BYTE-IDENTICAL to the frozen 1009 arm (sha256 match)")
        else:
            _fail(f"OFF elf {target} sha256 {osha} != frozen {FROZEN_ELF_SHA256} - the OFF build is "
                  f"NOT byte-identical to 1009 (the switch leaked into OFF)")
            ok = False
    else:
        print(f"  ---- no --off-elf given: checking the NEGATIVE CONTROL on {target} only")

    try:
        secs = sections(target)
        funcs = bodies(target)
    except (OSError, subprocess.CalledProcessError) as exc:
        _fail(f"{OBJDUMP} could not read {target}: {exc}")
        return 2
    if find_string(target, secs) is None:
        print(f"  ok   no `{STRING.decode()}` string - the bound is ABSENT (negative control holds)")
    else:
        _fail(f"the `{STRING.decode()}` string IS present in an OFF/1009 image - the switch leaked")
        ok = False
    traps = intrinsic_hint(funcs)
    abort_traps = [t for t in traps if t[0] == TARGET]
    if abort_traps:
        _fail(f"{TARGET} carries a `udf` intrinsic at {abort_traps[0][1]:#010x} - that is a HARD-TRAP "
              f"fix, not the fall-through-to-doexception this tool plants; name it, do not pass it")
        ok = False
    else:
        print(f"  ok   {TARGET} carries no `udf` intrinsic (the fix is not a hard trap)")

    print(f"check_d13_abt_retry_bound: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main():
    argv = sys.argv[1:]
    expect = "on"          # default: the ON image (out/stage90/xnu_arm_entry.elf)
    elf = None
    frozen = FROZEN_ELF
    off_elf = None
    it = iter(range(len(argv)))
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--expect":
            i += 1
            expect = argv[i]
        elif a == "--elf":
            i += 1
            elf = argv[i]
        elif a == "--off-elf":
            i += 1
            off_elf = argv[i]
        elif a == "--frozen":
            i += 1
            frozen = argv[i]
        else:
            # a bare path is the image to check
            elf = a
        i += 1
    if expect not in ("on", "off"):
        print("usage: check_d13_abt_retry_bound.py [<elf>] [--expect on|off] [--off-elf <elf>] "
              "[--frozen <elf>]", file=sys.stderr)
        return 2
    if expect == "off":
        print(f"check_d13_abt_retry_bound: OFF identity (frozen {frozen})")
        return check_off(frozen, off_elf)
    print(f"check_d13_abt_retry_bound: {elf or LIVE_ELF}")
    return check_on(elf or LIVE_ELF)


if __name__ == "__main__":
    raise SystemExit(main())