#!/usr/bin/env python3
"""Assert BY VALUE that the linked image skips `vm_map_pmap_enter` on submap enters (1001).

Reads the section table (`arm-none-eabi-objdump -h`) and the disassembly (`-d`) of a linked image
(default `out/stage90/xnu_arm_entry.elf`) and checks the skip the 1001 patch plants in
`vm_map_enter` - from the bytes and the instructions, not from any source comment
(`mi4-a-claim-in-a-comment-is-not-a-check`).

The fault it fixes, by value (1000's press, `records/1000-press-read.txt`): XNU time 10.31 s,
`hw_lock_lock(): PANIC: Lock 0xc0005b50 = 0xffb5be0b` - the "object" 0xc0005b48 is a FREED
vm_map-zone element (w04 = 0xbaddecaf) that `vm_map_enter` handed to `vm_map_pmap_enter` because
on a SUBMAP enter the `object` parameter IS the submap (`vm_map.c:~2063`). 999's VMHINT991 records
name the same two elements; 990 died at +0x18 of one. The patch: inside the tail block,
`if (!is_submap) { ...pmap_enter... } else { bounded kprintf("SUBMAPSKIP: ...") }` - pmap_empty
stays FALSE.

Checks (the check_vmhint_guard.py shape, one hop allowed for the cold report block):
  1. the byte string `SUBMAPSKIP:` exists in an allocatable PROGBITS section.  objdump -h has no
     type column, but CONTENTS + ALLOC is that set - .bss/NOBITS carries no initialised bytes and
     the debug sections are not allocatable - and the string's VA is the section's VMA + offset.
  2. that VA is materialized in `vm_map_enter`'s body: a `movw rD, #lo` + `movt rD, #hi` pair on
     the same register, or an `ldr rD, [pc, #N]` whose pool word at insn_VA + 8 + N is that VA
     (ARM's PC bias is 8).  If clang put the cold report block in another symbol, exactly ONE hop
     through a branch in `vm_map_enter`'s own body is allowed, and that branch must be there.
  3. the body that carries the string reference also calls kprintf (a verdict that is computed but
     never reported is silent - `mi4-silence-is-a-reading-only-if-success-is-silent`).
  4. `vm_map_enter`'s OWN body still contains a `bl <vm_map_pmap_enter>` - the non-submap path is
     intact; the fix SKIPS the call for submaps, it does not delete the call (a switch that removed
     it would silently change every ordinary enter).

Rehearsal: against the 1000 image (`armed-d13-bbdf41bc`, and the frozen copy under
out/stage90/frozen/) it must FAIL check 1 - there is no `SUBMAPSKIP:` string anywhere, because the
switch did not exist when it was built.  Exit 0 iff all pass.
"""
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TARGET = "vm_map_enter"
STRING = b"SUBMAPSKIP:"
CALLEE = "vm_map_pmap_enter"  # the call the skip keeps for the non-submap path

# objdump pads the address column to the widest address it prints (8 hex digits for the kernel
# image, fewer - space-padded - for a small linked object): the leading spaces are not ours to
# assume away.
_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t([a-z0-9.]+)\s*(.*)$")
_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")


def objdump(args: list[str]) -> str:
    return subprocess.run([OBJDUMP] + args, capture_output=True, text=True, check=True).stdout


def sections(path: str) -> list[tuple[str, int, int, int]]:
    """[(name, vma, size, file_off)] for every allocatable, byte-carrying (PROGBITS) section.

    objdump -h prints no type word; CONTENTS + ALLOC is exactly the set a byte string can live
    in.  The flags sit on the line(s) AFTER the header row, so they are gathered until the next
    row or a blank."""
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


def find_string(path: str, secs: list[tuple[str, int, int, int]]):
    """(va, section_name) of the first `SUBMAPSKIP:` byte string in the image, or None."""
    with open(path, "rb") as fh:
        blob = fh.read()
    for name, vma, size, off in secs:
        hit = blob[off:off + size].find(STRING)
        if hit >= 0:
            return vma + hit, name
    return None


def make_u32_reader(path: str, secs: list[tuple[str, int, int, int]]):
    """read_u32(va) -> the little-endian word at that VA, via the section table; None if unmapped."""
    with open(path, "rb") as fh:
        blob = fh.read()

    def read_u32(va: int):
        for name, vma, size, off in secs:
            if vma <= va and va + 4 <= vma + size:
                at = off + (va - vma)
                return int.from_bytes(blob[at:at + 4], "little")
        return None

    return read_u32


def bodies(path: str) -> dict[str, list[tuple[int, str, str]]]:
    """{symbol: [(va, mnemonic, operands)]} for the whole disassembly, in link order.

    `.LBB...`-style local labels sit INSIDE a function: they must not start a new body (a reset at
    the wrong label is a false pass).  The `; (8002... <...>)` comment objdump appends to literal
    loads is stripped, so the operands are only what the instruction encodes."""
    funcs: dict[str, list[tuple[int, str, str]]] = {}
    cur = None
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
            cur = None  # disassembly header / section banner
    return funcs


def parse_imm(ops: str):
    """The immediate in `#59864` / `#0xe9d8` (objdump prints decimal, hex in a comment)."""
    m = re.search(r"#(0x[0-9a-fA-F]+|\d+)", ops)
    if not m:
        return None
    return int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))


_MOVW = re.compile(r"^movw\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
_MOVT = re.compile(r"^movt\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
# objdump omits the immediate entirely for an offset of 0: `ldr r0, [pc]` means +0
_LDR_PC = re.compile(r"^ldr\s+(r\d+),\s*\[pc(?:,\s*#(-?(?:0x[0-9a-fA-F]+|\d+)))?\]$")
# anything below writes its first register, so a pending movw for that register is dead
_WRITES = re.compile(r"^(?:mov|movw|movt|mvn|adr|ldr|ldrb|ldrh|ldrsb|ldrsh|ldrd|add|adds|adc|sub|subs"
                     r"|sbc|rsb|and|ands|orr|orrs|eor|eors|bic|bics|mul|mla|mls|lsl|lsls|lsr|lsrs"
                     r"|asr|asrs|ror|rors|rev|clz|sxtb|sxth|sxtb16|uxtb|uxth|uxtb16)\s+(r\d+)\b")


def materializes_string(body: list[tuple[int, str, str]], string_va: int, read_u32):
    """How `body` puts `string_va` into a register, or None.

    Accepted, per the 1001 contract: a `movw rD, #lo` + `movt rD, #hi` pair on the same register
    (the pair may be split by unrelated instructions; `pending` follows the register, and any
    instruction that writes it drops the half), or a literal-pool load `ldr rD, [pc, #N]` whose
    word is the VA (ARM's PC reads as insn_VA + 8)."""
    pending: dict[str, int] = {}
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
            reg = m.group(1)
            imm = parse_imm(ops)
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


_BRANCH = re.compile(r"^([0-9a-f]+) <([^>]+)>$")
_CONDS = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al")
_BRANCH_MN = {"b", "bl", "bx", "blx"} | {f"b{c}" for c in _CONDS}


def branch_targets(body: list[tuple[int, str, str]]) -> list[tuple[str, int]]:
    """[(symbol, branch_va)] for branches out of this body to ANOTHER symbol, in order.

    A target inside this function (`<vm_map_enter+0x38>`) is not a hop - that code is already
    part of the body being scanned - and neither is a `.L...` local label."""
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


def _branches_to(body: list[tuple[int, str, str]], name: str) -> bool:
    """The one-hop rule's second half: `vm_map_enter`'s own body must branch to the carrier."""
    return any(n == name for n, _ in branch_targets(body))


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "out/stage90/xnu_arm_entry.elf"
    print(f"check_submap_pmap_skip: {path}")
    if not os.path.isfile(path):
        print("  FAIL no such file - nothing to check")
        print("check_submap_pmap_skip: FAIL")
        return 2
    try:
        secs = sections(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read it: {exc}")
        print("check_submap_pmap_skip: FAIL")
        return 2
    read_u32 = make_u32_reader(path, secs)
    enter = funcs.get(TARGET)
    ok = True

    # ---- 1. the report string, by value
    found = find_string(path, secs)
    if found is None:
        print("  FAIL the SUBMAPSKIP report string is not in the image at all (the switch never "
              "reached the pool, or this is a pre-1001 image - the 1000 elf must fail here)")
        ok = False
    else:
        sva, sname = found
        print(f"  ok   SUBMAPSKIP: `{STRING.decode()}` at {sva:#010x} in {sname}")

    # ---- 2. the report block: its address is materialized, and kprintf runs
    if found is None:
        print("  ---- checks 2-3 not reached: there is no SUBMAPSKIP address to look for")
    elif enter is None:
        print(f"  FAIL checks 2-3: no {TARGET} body to look in")
        ok = False
    else:
        sva = found[0]
        mat = materializes_string(enter, sva, read_u32)
        carrier, where = enter, "own body"
        if mat is None:  # one hop: clang may have outlined the cold kprintf block
            for name, brva in branch_targets(enter):
                other = funcs.get(name)
                if other is None:
                    continue
                hop = materializes_string(other, sva, read_u32)
                if hop is not None and _branches_to(enter, name):
                    mat, carrier = hop, other
                    where = f"outlined body <{name}>, one hop: {brva:#010x} branches to it"
                    break
        if mat is None:
            print(f"  FAIL {TARGET}: the SUBMAPSKIP address {sva:#010x} is materialized nowhere - "
                  f"neither in vm_map_enter itself nor in any single symbol its body branches to")
            ok = False
        else:
            print(f"  ok   {TARGET}: SUBMAPSKIP address materialized by {mat} ({where})")
        if carrier is None:
            print("  ---- check 3 not reached: no body carries the string reference")
        else:
            calls = [f"{va:#010x}" for va, mn, ops in carrier
                     if mn in ("bl", "blx") and "kprintf" in ops.split("<", 1)[-1]]
            if calls:
                print(f"  ok   the skipped branch reports via kprintf at {calls[0]}")
            else:
                print("  FAIL the skip branch never reports - no bl/blx to kprintf in the body "
                      "that carries the SUBMAPSKIP string")
                ok = False

    # ---- 4. the non-submap path is intact: vm_map_enter still calls vm_map_pmap_enter
    if enter is None:
        print(f"  FAIL {TARGET} is not in the image - wrong image, or the symbol was optimised away")
        ok = False
    else:
        bls = [f"{va:#010x}" for va, mn, ops in enter
               if mn in ("bl", "blx") and CALLEE in ops.split("<", 1)[-1]]
        if bls:
            print(f"  ok   {TARGET} still calls <{CALLEE}> at {bls[0]} - the fix SKIPS the call "
                  f"for submaps, it does not delete the non-submap path")
        else:
            print(f"  FAIL {TARGET}: no bl/blx to <{CALLEE}> in its own body - the skip switch "
                  f"removed the ordinary enter's pmap path (or the call moved; check by value)")
            ok = False

    print(f"check_submap_pmap_skip: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())