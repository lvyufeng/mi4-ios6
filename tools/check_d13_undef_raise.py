#!/usr/bin/env python3
"""Assert BY VALUE that the linked image makes `sleh_undef`'s user-mode VFP-enable early-return
CONDITIONAL (rung 1020, switch `STAGE90_XNU_UNDEF_RAISE`), so that a `udf #254` whose VFP is already
enabled FALLS THROUGH to the printf/EXC_BAD_INSTRUCTION raise instead of returning to the faulting PC
and looping forever (1018/1019).

This is a LINKED-IMAGE STRUCTURAL checker, modelled on `tools/check_d13_abt_retry_bound.py`.  It decodes
the actual control flow of `sleh_undef`'s body in `out/stage90/xnu_arm_entry.elf` (an ARM basic-block
CFG built from `arm-none-eabi-objdump -d`) and decides the property from edges and reachability - never
from a source grep, never from a comment, never from a bare string presence
(`mi4-a-claim-in-a-comment-is-not-a-check`).

THE SOURCE SHAPE (osfmk/arm/trap.c, `sleh_undef`, cpsr == 0x10 arm).

    if (instruction != 0xfedeffe7) {        /* the NEON / non-magic instruction arm */
        thread->machine.vfp_dirty = 0;
        if (!thread->machine.vfp_enable) {
            vfp_enable_exception(TRUE);
            vfp_context_load(&thread->machine.vfp_regs);
            thread->machine.vfp_enable = TRUE;
            return;                         /* OFF: the `return` sits here, UNCONDITIONAL for this arm */
        }
        /* ON: the `return` moves INSIDE the `if`, so the fall-through reaches the raise below */
    }
    ... printf("usermode undefined instruction, EXC_BAD_INSTRUCTION ...") + exception_triage ...

  OFF (`--expect off`): the VFP-enable block's terminating return is unconditional for the non-magic
    path.  There is NO fall-through edge from the `vfp_enable` test to the EXC printf block; the printf
    block is entered ONLY from the magic compare's edge (the `instruction == 0xfedeffe7` fall-through).
  ON  (`--expect on`): the `vfp_enable` test is CONDITIONAL: its "already enabled" successor (the
    not-taken / fall-through edge of `if (!vfp_enable)`) reaches the EXC printf block WITHOUT passing
    through the VFP-enable block's terminating return.  So a repeat `udf` raises instead of looping.

WHAT IS DECIDED, AND WHAT IS REFUSED.  The distinguishing signal is exactly one reachability question on
the decoded CFG: "does the already-enabled successor of the `vfp_enable` test reach the EXC-printf
block, avoiding the VFP-enable block?"  That is decidable from edges alone.  If the reconstruction cannot
be done soundly - the `vfp_enable` test branch is not found, or the EXC-printf block is not found, or the
magic constant is not materialised - the checker FAILS LOUDLY and names what it could not build, rather
than blessing a weaker proxy (`mi4-a-claim-in-a-comment-is-not-a-check`).

Exit 0 iff every check for the requested orientation passes.
"""
import hashlib
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TARGET = "sleh_undef"
# The EXC literal that the raise path prints.  We locate the string's VA by VALUE (bytes) and require
# that VA to be materialised in the raise block, then check the block edges.
EXC_LITERAL = b"usermode undefined instruction, EXC_BAD_INSTRUCTION"
VFP_CONTEXT_LOAD = "vfp_context_load"
VFP_ENABLE = "vfp_enable_exception"
EXCEPTION_TRIAGE = "exception_triage"
# The magic NEON instruction the source compares against (`instruction == 0xfedeffe7`).  Materialised as
# `movw rX, #0xdefe ; movt rX, #0xe7ff`; the compare then swaps it (OSSwapInt32) so the immediate pair is
# the fingerprint we look for.
MAGIC_LO = 0xdefe
MAGIC_HI = 0xe7ff
# The frozen OFF arm for rung 1020.  PLACEHOLDER: fill with the sha256 of the frozen
# armed-d13-* / xnu_arm_entry.elf once 1020 freezes its OFF build.  While None the `--off-elf` identity
# assertion is SKIPPED (and reported as NOT PINNED), never invented.
FROZEN_ELF_SHA256 = None
LIVE_ELF = "out/stage90/xnu_arm_entry.elf"

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


def find_string(path, secs, needle=EXC_LITERAL):
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


def make_bytes_reader(path, secs):
    with open(path, "rb") as fh:
        blob = fh.read()

    def read(va, n):
        for name, vma, size, off in secs:
            if vma <= va and va + n <= vma + size:
                at = off + (va - vma)
                return blob[at:at + n]
        return None

    return read


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


def materialized_vas(body, read_u32):
    """[(va, desc)] for every constant address materialised in `body` by a movw/movt pair or an
    `ldr rD,[pc,#N]` pool word.  A movw/movt pair keyed by register, invalidated by any other write."""
    out, pending = [], {}
    for va, mn, ops in body:
        text = f"{mn} {ops}".strip()
        m = _MOVT.match(text)
        if m:
            reg = m.group(1)
            hi, lo = parse_imm(ops), pending.pop(reg, None)
            if lo is not None and hi is not None:
                out.append(((hi << 16) | lo, f"movw/movt {reg}"))
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
            if off is not None:
                w = read_u32(va + 8 + off)
                if w is not None:
                    out.append((w, f"ldr {m.group(1)}, [pc, #{off}]"))
            continue
        m = _WRITES.match(text)
        if m:
            pending.pop(m.group(1), None)
    return out


def materializes_string(body, string_va, read_u32):
    """Return a description if `string_va` is materialised in `body` by a movw/movt pair or an
    `ldr rD,[pc,#N]` whose pool word is that VA; else None."""
    for va, desc in materialized_vas(body, read_u32):
        if va == string_va:
            return f"{desc} (#{string_va:#010x})"
    return None


def calls_to(body, callee):
    return [f"{va:#010x}" for va, mn, ops in body
            if mn in ("bl", "blx") and callee in ops.split("<", 1)[-1]]


def branch_target(ops):
    """The numeric VA of a `b<cc> 8046dea4 <sym>` operand, or None."""
    m = _BRANCH.match(ops)
    if not m:
        return None
    return int(m.group(1), 16)


def is_return(mn, ops):
    """A function return within this body: `bx lr`, `pop {..., pc}`, `mov pc, lr`, `ldm ... pc`."""
    o = ops.replace(" ", "")
    if mn == "bx" and o == "lr":
        return True
    if mn == "pop" and "pc" in ops:
        return True
    if mn == "mov" and o in ("pc,lr", "pc,r14"):
        return True
    if mn in ("ldm", "ldmia", "ldmfd") and re.search(r"\bpc\b", ops):
        return True
    return False


def is_cond_branch(mn):
    return mn in _BRANCH_MN and mn not in ("b", "bl", "blx", "bx")


def blocks(body):
    """Split a decoded function body into basic blocks.
    Returns [(start, end, [(va,mn,ops), ...]), ...] ordered by start.
    A block starts at the function entry, at every intra-body branch target, and right after every
    branch or return instruction."""
    if not body:
        return []
    addrs = [va for va, _, _ in body]
    idx = set(addrs)
    starts = {addrs[0]}
    for i, (va, mn, ops) in enumerate(body):
        # bl/blx are calls: they fall through, so they do NOT end a basic block.
        if mn in _BRANCH_MN and mn not in ("bl", "blx"):
            t = branch_target(ops)
            if t is not None and t in idx:
                starts.add(t)
            if i + 1 < len(body):
                starts.add(body[i + 1][0])
        elif is_return(mn, ops):
            if i + 1 < len(body):
                starts.add(body[i + 1][0])
    s = sorted(starts)
    out = []
    for j, st in enumerate(s):
        end = s[j + 1] if j + 1 < len(s) else addrs[-1] + 4
        insns = [(va, mn, ops) for va, mn, ops in body if st <= va < end]
        out.append((st, end, insns))
    return out


def build_cfg(body):
    """Return (blks, entry_by_start, succ) where succ maps a block start to a list of
    (successor_start, kind) with kind in {"fall","taken"}."""
    blks = blocks(body)
    starts = {b[0] for b in blks}
    succ = {}
    for st, end, insns in blks:
        if not insns:
            succ[st] = []
            continue
        va, mn, ops = insns[-1]
        t = branch_target(ops)
        edges = []
        if is_return(mn, ops):
            edges = []
        elif mn == "b":
            if t is not None and t in starts:
                edges = [(t, "taken")]
            else:
                edges = []  # tail call out of the function
        elif mn == "bl" or mn == "blx":
            if end in starts:
                edges = [(end, "fall")]
        elif is_cond_branch(mn):
            if t is not None and t in starts:
                edges.append((t, "taken"))
            if end in starts:
                edges.append((end, "fall"))
        else:
            if end in starts:
                edges = [(end, "fall")]
        succ[st] = edges
    return blks, starts, succ


def reachable(start, succ, forbid=None):
    """Set of block starts reachable from `start` (inclusive) following succ edges, not entering
    any block in `forbid`."""
    forbid = forbid or set()
    seen, stack = set(), [start]
    while stack:
        cur = stack.pop()
        if cur in seen or cur in forbid:
            continue
        seen.add(cur)
        for nxt, _ in succ.get(cur, []):
            if nxt not in seen:
                stack.append(nxt)
    return seen


def preds_of(target_block, succ):
    out = []
    for st, edges in succ.items():
        for nxt, kind in edges:
            if nxt == target_block:
                out.append((st, kind))
    return out


def block_of(addr, blks):
    for st, end, insns in blks:
        if st <= addr < end:
            return st
    return None


def _fail(msg):
    print("  FAIL " + msg)
    return False


def _warn(msg):
    print("  WARN " + msg)


def analyze(path):
    """Decode `sleh_undef` and build the block CFG plus the located blocks.  Returns a dict, or a
    string naming what could not be built (a LOUD refusal - never a silent weaker proxy)."""
    if not os.path.isfile(path):
        return "no such file - nothing to check"
    try:
        secs = sections(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        return f"{OBJDUMP} could not read it: {exc}"
    read_u32 = make_u32_reader(path, secs)
    read_bytes = make_bytes_reader(path, secs)
    body = funcs.get(TARGET)
    if body is None:
        return f"no {TARGET} body in the image"
    blks, starts, succ = build_cfg(body)
    if not blks:
        return f"{TARGET} decoded to zero instructions"

    # the EXC printf block: the block that materialises a constant VA whose pointed-to bytes CONTAIN the
    # EXC literal (the source concatenates an ANSI colour prefix before the literal, so the pool word is
    # the START of the whole format string, and the literal is a SUBSTRING of it) AND calls printf.
    hit = find_string(path, secs)
    if hit is None:
        return ("the EXC literal is not in the image at all - the raise path was never linked "
                "(wrong symbol name, or the image is not an XNU entry elf)")
    needle_at = hit[0]
    raise_block = None
    raise_printf = None
    exc_va = None
    for st, end, insns in blks:
        pr = calls_to(insns, "printf")
        if not pr:
            continue
        for mva, desc in materialized_vas(insns, read_u32):
            b = read_bytes(mva, len(EXC_LITERAL) + 48)  # room for the ANSI-colour prefix
            if b is not None and EXC_LITERAL in b:
                raise_block, raise_printf, exc_va = st, pr[0], mva
                break
        if raise_block is not None:
            break
    if raise_block is None:
        return (f"no block inside {TARGET} materialises a VA whose pointed-to bytes contain the EXC "
                f"literal (needle at {needle_at:#010x}) while also calling printf - cannot locate the "
                f"raise block by value")

    # the VFP-enable block: the block that calls vfp_context_load (or vfp_enable_exception).
    vfp_block = None
    for st, end, insns in blks:
        if calls_to(insns, VFP_CONTEXT_LOAD) or calls_to(insns, VFP_ENABLE):
            vfp_block = st
            break
    if vfp_block is None:
        return (f"no block inside {TARGET} calls <{VFP_CONTEXT_LOAD}>/<{VFP_ENABLE}> - cannot locate "
                f"the VFP-enable block by value")

    # the magic-constant block: the block where 0xdefe/0xe7ff (0xfedeffe7) is materialised.
    magic_block = None
    pending = {}
    for st, end, insns in blks:
        pending = {}
        for va, mn, ops in insns:
            m = _MOVW.match(f"{mn} {ops}".strip())
            if m and parse_imm(ops) == MAGIC_LO:
                pending[m.group(1)] = MAGIC_LO
                continue
            m = _MOVT.match(f"{mn} {ops}".strip())
            if m:
                reg, hi = m.group(1), parse_imm(ops)
                if pending.get(reg) == MAGIC_LO and hi == MAGIC_HI:
                    magic_block = st
                pending.pop(reg, None)
        if magic_block is not None:
            break
    if magic_block is None:
        return ("0xfedeffe7 is not materialised in any block of " + TARGET +
                " - cannot locate the magic-compare block by value")

    # the vfp_enable test: a block ending in a CONDITIONAL branch with the VFP-enable block as ONE of
    # its two successors - either the branch TARGETS it (OFF layout: `beq vfp_block`) or the branch's
    # FALL-THROUGH enters it (ON layout: `bne raise ; <vfp block>`, the not-enabled path falls in).
    # Generalised because the fix may flip which edge enters the VFP block.
    test_candidates = []
    for st, end, insns in blks:
        if not insns:
            continue
        va, mn, ops = insns[-1]
        if not is_cond_branch(mn):
            continue
        tgt = branch_target(ops)
        if tgt == vfp_block or end == vfp_block:
            test_candidates.append((st, va, mn, end))
    if not test_candidates:
        return (f"no conditional branch inside {TARGET} has the VFP-enable block {vfp_block:#010x} as a "
                f"successor (by target or fall-through) - the `if (!vfp_enable)` test could not be "
                f"reconstructed (cannot soundly distinguish a conditional from an unconditional return; "
                f"refusing rather than blessing a proxy)")
    # if several, prefer one in the user arm (the reachable-from-magic region)
    magic_reach = reachable(magic_block, succ)
    test_block, test_va, test_mn, ft_addr = test_candidates[0]
    for cand in test_candidates:
        if cand[0] in magic_reach:
            test_block, test_va, test_mn, ft_addr = cand
            break
    return {
        "path": path, "secs": secs, "blks": blks, "starts": starts, "succ": succ,
        "exc_va": exc_va, "raise_block": raise_block, "raise_printf": raise_printf,
        "vfp_block": vfp_block, "magic_block": magic_block,
        "test_block": test_block, "test_va": test_va, "test_mn": test_mn,
        "fallthrough": ft_addr,
    }


def _report_edge(kind, dst):
    return f"{kind} -> {dst:#010x}"


def check_on(path):
    info = analyze(path)
    if isinstance(info, str):
        _fail(info)
        print("check_d13_undef_raise: FAIL")
        return 1
    print(f"  ({TARGET} at block base {info['blks'][0][0]:#010x}; EXC literal VA "
          f"{info['exc_va']:#010x})")
    ok = True
    blks, succ = info["blks"], info["succ"]
    raise_block, vfp_block, magic_block = info["raise_block"], info["vfp_block"], info["magic_block"]
    test_block, test_va = info["test_block"], info["test_va"]

    # successor bookkeeping for the vfp_enable test
    test_edges = succ.get(test_block, [])
    vfp_edges = [(dst, k) for dst, k in test_edges if dst == vfp_block]
    other_edges = [(dst, k) for dst, k in test_edges if dst != vfp_block]
    if not vfp_edges or not other_edges:
        _fail(f"the vfp_enable test block {test_block:#010x} does not have both a VFP-enable edge and "
              f"an 'already enabled' edge (edges: {[_report_edge(k, d) for d, k in test_edges]}) - "
              f"cannot soundly decide the edge; refusing")
        print("check_d13_undef_raise: FAIL")
        return 1
    enabled_dst, enabled_kind = other_edges[0]
    print(f"  ok   vfp_enable test: `{info['test_mn']}` at {test_va:#010x} in block "
          f"{test_block:#010x}; VFP-enable edge {[_report_edge(k, d) for d, k in vfp_edges]}, "
          f"'already enabled' edge {_report_edge(enabled_kind, enabled_dst)}")

    # 2. the escalation survives: sleh_undef still reaches exception_triage, and the raise prints EXC.
    if any(calls_to(insns, EXCEPTION_TRIAGE) for _, _, insns in blks):
        tri = next(va for _, _, insns in blks for va, mn, ops in insns
                   if mn in ("bl", "blx") and EXCEPTION_TRIAGE in ops.split("<", 1)[-1])
        print(f"  ok   {TARGET} still reaches <{EXCEPTION_TRIAGE}> (bl at {tri:#010x}) - escalation intact")
    else:
        _fail(f"{TARGET} no longer reaches <{EXCEPTION_TRIAGE}> - the escalation has nowhere to go")
        ok = False
    if info["raise_printf"]:
        print(f"  ok   raise block {raise_block:#010x} calls printf at {info['raise_printf']} with the "
              f"EXC literal ({info['exc_va']:#010x})")
    else:
        _fail("the raise block does not call printf with the EXC literal")
        ok = False

    # 2 (load-bearing). the already-enabled successor reaches the EXC-printf block WITHOUT going
    # through the VFP-enable block.
    reach = reachable(enabled_dst, succ, forbid={vfp_block})
    if raise_block in reach:
        print(f"  ok   CONDITIONAL return: the 'already enabled' edge {_report_edge(enabled_kind, enabled_dst)}"
              f" REACHES the raise block {raise_block:#010x} without entering the VFP-enable block "
              f"{vfp_block:#010x}")
        print(f"  ok   EVIDENCE: test branch {test_va:#010x} ({info['test_mn']} -> {vfp_block:#010x}) "
              f"vs fall-through target {enabled_dst:#010x} -> raise {raise_block:#010x}")
    else:
        _fail(f"the 'already enabled' successor {enabled_dst:#010x} does NOT reach the raise block "
              f"{raise_block:#010x} (avoiding the VFP-enable block) - the early return is still "
              f"UNCONDITIONAL for this arm; the ON fix is not present")
        ok = False

    # the early return must still exist inside the VFP-enable block (the fix is 'return only on the
    # first trip', not 'delete the return').
    vfp_ins = next(insns for st, _, insns in blks if st == vfp_block)
    if vfp_ins and is_return(*vfp_ins[-1][1:]) or any(is_return(mn, ops) for _, mn, ops in vfp_ins):
        print(f"  ok   the VFP-enable block {vfp_block:#010x} still ends in a function return "
              f"(the return survives, now conditional)")
    else:
        _fail(f"the VFP-enable block {vfp_block:#010x} no longer returns - the fix removed the early "
              f"return instead of making it conditional")
        ok = False

    print(f"check_d13_undef_raise: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def check_off(off_elf):
    ok = True
    path = off_elf or LIVE_ELF
    info = analyze(path)
    if isinstance(info, str):
        _fail(info)
        print("check_d13_undef_raise: FAIL")
        return 1
    print(f"  ({TARGET} at block base {info['blks'][0][0]:#010x}; EXC literal VA "
          f"{info['exc_va']:#010x}; OFF elf {path})")
    blks, succ = info["blks"], info["succ"]
    raise_block, vfp_block, magic_block = info["raise_block"], info["vfp_block"], info["magic_block"]
    test_block, test_va = info["test_block"], info["test_va"]

    # 4a. escalation + EXC printf survive exactly as in ON.
    if any(calls_to(insns, EXCEPTION_TRIAGE) for _, _, insns in blks):
        tri = next(va for _, _, insns in blks for va, mn, ops in insns
                   if mn in ("bl", "blx") and EXCEPTION_TRIAGE in ops.split("<", 1)[-1])
        print(f"  ok   {TARGET} reaches <{EXCEPTION_TRIAGE}> (bl at {tri:#010x})")
    else:
        _fail(f"{TARGET} no longer reaches <{EXCEPTION_TRIAGE}>")
        ok = False
    print(f"  ok   raise block {raise_block:#010x} calls printf at {info['raise_printf']} with the "
          f"EXC literal ({info['exc_va']:#010x})")

    # 4b. THE DECISIVE OFF PROPERTY: the printf/EXC block is NOT reachable from the vfp_enable test
    # region at all - so reaching the printf requires the magic branch, not the test's fall-through.
    region = reachable(test_block, succ)
    preds = preds_of(raise_block, succ)
    print(f"  ok   vfp_enable test: `{info['test_mn']}` at {test_va:#010x} in block {test_block:#010x} "
          f"-> VFP-enable block {vfp_block:#010x}")
    print(f"  ok   raise block {raise_block:#010x} predecessors: "
          f"{', '.join(f'{st:#010x}({kind})' for st, kind in preds) or '(none)'}; "
          f"magic-compare block {magic_block:#010x}")

    if raise_block in region:
        _fail(f"the raise block {raise_block:#010x} IS reachable from the vfp_enable test region "
              f"{test_block:#010x} - a fall-through path to the printf exists; this is the ON shape, "
              f"not OFF")
        ok = False
    else:
        print(f"  ok   NO fall-through path from the vfp_enable test region to the printf - the return "
              f"after the test is UNCONDITIONAL for the non-magic path (OFF shape)")
    bad_preds = [(st, k) for st, k in preds if st in region and st != raise_block]
    if bad_preds:
        _fail(f"the raise block has a predecessor inside the vfp_enable test region: "
              f"{', '.join(f'{st:#010x}({k})' for st, k in bad_preds)} - not OFF")
        ok = False
    else:
        print(f"  ok   the raise block's ONLY predecessor is the magic-compare edge (none from the "
              f"vfp_enable test region): "
              f"{', '.join(f'{st:#010x}({k})' for st, k in preds) or '(none)'}")
    if magic_block not in [st for st, _ in preds]:
        _fail(f"the raise block {raise_block:#010x} is not entered from the magic-compare block "
              f"{magic_block:#010x} - the ESC_BAD_INSTRUCTION path lost its only predecessor")
        ok = False

    # 4c. the vfp_enable test's 'already enabled' successor still returns (no fall-through to raise).
    other_edges = [(dst, k) for dst, k in succ.get(test_block, []) if dst != vfp_block]
    if other_edges:
        dst, kind = other_edges[0]
        if dst == raise_block or raise_block in reachable(dst, succ, forbid={vfp_block}):
            _fail(f"the 'already enabled' edge {_report_edge(kind, dst)} reaches the raise block on an "
                  f"OFF image")
            ok = False
        else:
            print(f"  ok   the 'already enabled' edge {_report_edge(kind, dst)} does NOT reach the raise "
                  f"block - the test's non-magic path returns (OFF shape)")
    else:
        _warn(f"the vfp_enable test block {test_block:#010x} has no non-vfp successor (unconditional); "
              f"cannot show the fall-through returns - refusing to bless")
        ok = False

    # 5. optional frozen-elf identity.
    if off_elf:
        if FROZEN_ELF_SHA256 is None:
            _warn(f"--off-elf given but FROZEN_ELF_SHA256 is not pinned (placeholder None) - the "
                  f"byte-identity check is SKIPPED, not claimed. sha256({off_elf}) = {sha256(off_elf)}")
        else:
            got = sha256(off_elf)
            if got == FROZEN_ELF_SHA256:
                print(f"  ok   OFF elf {off_elf} is BYTE-IDENTICAL to the frozen 1020 OFF arm")
            else:
                _fail(f"OFF elf sha256 {got} != frozen {FROZEN_ELF_SHA256} - the OFF build is not "
                      f"byte-identical (the switch leaked into OFF)")
                ok = False

    print(f"check_d13_undef_raise: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


# ---------------------------------------------------------------------------------------------------
# self-test: feed synthetic decoded bodies through the SAME block/CFG decider and assert it returns the
# right verdict for the OFF shape and the ON shape.  Validates the decider without needing a real ON elf.
def _selftest():
    """Build two tiny synthetic `sleh_undef` bodies (OFF-shape and ON-shape) and assert the CFG decider
    distinguishes them the way check_on/check_off require.  No files, no objdump."""
    # We reuse blocks()/build_cfg()/reachable() only - the parts that decide the structure.
    # A tiny synthetic function; addresses chosen to keep it readable.
    def insn(va, mn, ops):
        return (va, mn, ops)

    def shape(on):
        # 0x1000: cmp r0,#0 ; beq 0x1010 (vfp block)      <- the vfp_enable test
        # 0x1008: OFF: pop {pc} (return)  |  ON: b 0x1020 (fall-through to the raise block)
        # 0x1010: bl vfp_enable_exception ; bl vfp_context_load ; mov r0,#1 ; str ; pop {pc}
        # 0x1020: movw/movt EXC ; bl printf ; pop {pc}      <- the raise block
        body = [
            insn(0x1000, "cmp", "r0, #0"),
            insn(0x1004, "beq", "1010 <sleh_undef+0x10>"),
        ]
        if on:
            body.append(insn(0x1008, "b", "1020 <sleh_undef+0x20>"))
        else:
            body.append(insn(0x1008, "pop", "{r4, pc}"))
        body += [
            insn(0x1010, "bl", "8000 <vfp_enable_exception>"),
            insn(0x1014, "bl", "8010 <vfp_context_load>"),
            insn(0x1018, "mov", "r0, #1"),
            insn(0x101c, "pop", "{r4, pc}"),
            insn(0x1020, "movw", "r0, #0xfe9d"),
            insn(0x1024, "movt", "r0, #0x8054"),
            insn(0x1028, "bl", "8020 <printf>"),
            insn(0x102c, "pop", "{r4, pc}"),
        ]
        return body

    fails = []
    for on in (False, True):
        body = shape(on)
        blks, starts, succ = build_cfg(body)
        test_block = 0x1000
        vfp_block = next(st for st, _, ins in blks
                         if any("vfp_context_load" in o for _, _, o in ins))
        edges = succ[test_block]
        other = [(d, k) for d, k in edges if d != vfp_block]
        if not other:
            fails.append(f"on={on}: test block has no 'already enabled' successor")
            continue
        dst, _ = other[0]
        reaches = 0x1020 in reachable(dst, succ, forbid={vfp_block})
        if on and not reaches:
            fails.append("ON shape: decider says the 'already enabled' edge does NOT reach the raise block")
        if (not on) and reaches:
            fails.append("OFF shape: decider says the 'already enabled' edge DOES reach the raise block")
    if fails:
        for f in fails:
            print("  FAIL selftest: " + f)
        print("check_d13_undef_raise: FAIL (self-test)")
        return 1
    print("  ok   self-test: decider separates the OFF shape (no fall-through to raise) from the ON "
          "shape (fall-through to raise)")
    print("check_d13_undef_raise: PASS (self-test)")
    return 0


def main():
    argv = sys.argv[1:]
    expect = "on"
    elf = None
    off_elf = None
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
        elif a == "--selftest":
            return _selftest()
        else:
            elf = a
        i += 1
    if expect not in ("on", "off"):
        print("usage: check_d13_undef_raise.py [<elf>] [--expect on|off] [--off-elf <elf>] [--selftest]",
              file=sys.stderr)
        return 2
    if expect == "off":
        print("check_d13_undef_raise: OFF shape")
        return check_off(off_elf or elf)
    print("check_d13_undef_raise: ON shape")
    return check_on(elf or LIVE_ELF)


if __name__ == "__main__":
    raise SystemExit(main())