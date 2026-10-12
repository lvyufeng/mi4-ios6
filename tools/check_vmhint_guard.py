#!/usr/bin/env python3
"""Assert BY VALUE that the linked image neutralizes an implausible `vm_object.memq_hint` (991).

Reads the section table (`arm-none-eabi-objdump -h`) and the disassembly (`-d`) of a linked image
(default `out/stage90/xnu_arm_entry.elf`) and checks the guard the 991 patch plants in
`vm_page_lookup` - from the bytes and the instructions, not from any source comment
(`mi4-a-claim-in-a-comment-is-not-a-check`).

The fault it guards, by value (990's press): pc = 0x80020838, `ldrd r0, [r5, #24]`, with
r5 = object->memq_hint = 0x1 (object = 0xc0005b48, lr = vm_map_pmap_enter).  The hint is loaded a
few instructions earlier as `ldr rN, [rM, #32]` (struct offset 0x20); when the field holds a word
that is not a kernel page pointer, the next access walks it as a `vm_page_t` and faults.  The
guard sits behind exactly that load: `if (mem != VM_PAGE_NULL && ((vm_offset_t)mem < 0x80000000u
|| ((vm_offset_t)mem & 3u)))` reports `VMHINT991: ` via kprintf (obj/mem/lr as %p, five words as
%x) and then sets `mem = VM_PAGE_NULL` - a LOCAL SHADOW: the rebind replaces the local used by
the walk, the object field itself is never written back, so a lookup never mutates the object.

Checks:
  1. the byte string `VMHINT991:` exists in an allocatable PROGBITS section.  objdump -h has no
     type column, but CONTENTS + ALLOC is that set - .bss/NOBITS carries no initialised bytes and
     the debug sections are not allocatable - and the string's VA is the section's VMA + offset.
  2. that VA is materialized in `vm_page_lookup`'s body: a `movw rD, #lo` + `movt rD, #hi` pair
     on the same register (the tree's codegen for a kprintf string address; the pair may straddle
     unrelated instructions), or an `ldr rD, [pc, #N]` whose pool word at insn_VA + 8 + N is that
     VA (ARM's PC bias is 8).  If clang outlined the cold report block into another symbol,
     exactly ONE hop through a branch in `vm_page_lookup`'s own body is allowed, and that branch
     must be there.
  3. the body that carries the string reference also computes the `& 3u` residue test - one of
     `tst rN, #3`, `and(s) rD, rN, #3`, `lsl(s) rD, rN, #30`, `mov(s) rD, rN, lsl #30`.
  4. that same body calls kprintf (a verdict that is computed but never reported is silent -
     `mi4-silence-is-a-reading-only-if-success-is-silent`).
  5. `vm_page_lookup`'s OWN body still contains `ldr rN, [rM, #32]` - the loop's memq_hint load
     at offset 0x20.  With 2-4 that is the whole chain: the guard is compiled, it is reached
     from the load 990 died on, and the struct offset has not moved.

Rehearsal: against the current 990 image (pre-fix) it must FAIL check 1 - there is no
`VMHINT991:` string anywhere, because the switch never reached the pool.  Exit 0 iff all pass.
"""
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TARGET = "vm_page_lookup"
STRING = b"VMHINT991:"
HINT_OFF = 32  # offsetof(struct vm_object, memq_hint) == 0x20 - the `ldr rN, [rM, #32]` load

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
    """(va, section_name) of the first `VMHINT991:` byte string in the image, or None."""
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

    Accepted, per the 991 contract: a `movw rD, #lo` + `movt rD, #hi` pair on the same register
    (the pair may be split by unrelated instructions - the tree's own kprintf call sites put an
    `add r1, sp, #N` between them; `pending` follows the register, and any instruction that
    writes it drops the half), or a literal-pool load `ldr rD, [pc, #N]` whose word is the VA
    (ARM's PC reads as insn_VA + 8)."""
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


_MASKS = (
    (re.compile(r"^tst\s+r\d+,\s*#3$"), "tst rN, #3"),
    (re.compile(r"^and\w*\s+r\d+,\s*r\d+,\s*#3$"), "and(s) rD, rN, #3"),
    (re.compile(r"^lsl\w*\s+r\d+,\s*r\d+,\s*#30$"), "lsl(s) rD, rN, #30"),
    (re.compile(r"^mov\w*\s+r\d+,\s*r\d+,\s*lsl\s+#30$"), "mov(s) rD, rN, lsl #30"),
)

# the load the guard sits behind: memq_hint is read at HINT_OFF, however objdump spells it
_LDR_HINT = re.compile(r"^ldr\s+r\d+,\s*\[(?:r\d+|ip|fp|sl|sb),\s*#(?:%d|%#x)\]!?$" % (HINT_OFF, HINT_OFF))

_BRANCH = re.compile(r"^([0-9a-f]+) <([^>]+)>$")
_CONDS = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al")
_BRANCH_MN = {"b", "bl", "bx", "blx"} | {f"b{c}" for c in _CONDS}


def branch_targets(body: list[tuple[int, str, str]]) -> list[tuple[str, int]]:
    """[(symbol, branch_va)] for branches out of this body to ANOTHER symbol, in order.

    A target inside this function (`<vm_page_lookup+0x38>`) is not a hop - that code is already
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
    """The one-hop rule's second half: `vm_page_lookup`'s own body must branch to the carrier."""
    return any(n == name for n, _ in branch_targets(body))


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "out/stage90/xnu_arm_entry.elf"
    print(f"check_vmhint_guard: {path}")
    if not os.path.isfile(path):
        print("  FAIL no such file - nothing to check")
        print("check_vmhint_guard: FAIL")
        return 2
    try:
        secs = sections(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read it: {exc}")
        print("check_vmhint_guard: FAIL")
        return 2
    read_u32 = make_u32_reader(path, secs)
    lookup = funcs.get(TARGET)
    ok = True

    # ---- 1. the report string, by value
    found = find_string(path, secs)
    if found is None:
        print("  FAIL the VMHINT991 report string is not in the image at all "
              "(the switch never reached the pool)")
        ok = False
    else:
        sva, sname = found
        print(f"  ok   VMHINT991: `{STRING.decode()}` at {sva:#010x} in {sname}")

    # ---- 2-4. the report block: its address is materialized, the & 3u test is there, kprintf runs
    if found is None:
        print("  ---- checks 2-4 not reached: there is no VMHINT991 address to look for")
    elif lookup is None:
        print(f"  FAIL checks 2-4: no {TARGET} body to look in")
        ok = False
    else:
        sva = found[0]
        mat = materializes_string(lookup, sva, read_u32)
        carrier, where = lookup, "own body"
        if mat is None:  # one hop: clang may have outlined the cold kprintf block
            for name, brva in branch_targets(lookup):
                other = funcs.get(name)
                if other is None:
                    continue
                hop = materializes_string(other, sva, read_u32)
                # the branch is required to be in vm_page_lookup's own body; it is, by
                # construction (the name came from that body), and it is printed as the evidence
                if hop is not None and _branches_to(lookup, name):
                    mat, carrier = hop, other
                    where = f"outlined body <{name}>, one hop: {brva:#010x} branches to it"
                    break
        if mat is None:
            print(f"  FAIL {TARGET}: the VMHINT991 address {sva:#010x} is materialized nowhere - "
                  f"neither in the lookup itself nor in any single symbol its body branches to")
            ok = False
        else:
            print(f"  ok   {TARGET}: VMHINT991 address materialized by {mat} ({where})")
        if carrier is None:
            print("  ---- checks 3-4 not reached: no body carries the string reference")
        else:
            hits = [f"{va:#010x}" for va, mn, ops in carrier
                    if any(r.match(f"{mn} {ops}".strip()) for r, _ in _MASKS)]
            if hits:
                print(f"  ok   the `& 3u` residue test is compiled in at {', '.join(hits[:3])}")
            else:
                print("  FAIL the guard body has no `& 3u` residue test (`tst rN, #3` / "
                      "`and(s) rD, rN, #3` / `lsl(s) rD, rN, #30` / `mov(s) rD, rN, lsl #30`) - "
                      "the plausibility test is not in the image")
                ok = False
            calls = [f"{va:#010x}" for va, mn, ops in carrier
                     if mn in ("bl", "blx") and "kprintf" in ops.split("<", 1)[-1]]
            if calls:
                print(f"  ok   the verdict reaches kprintf at {calls[0]}")
            else:
                print("  FAIL the guard computes a verdict but never reports it - no bl/blx to "
                      "kprintf in the body that carries the VMHINT991 string")
                ok = False

    # ---- 5. the memq_hint load the guard sits behind is in `vm_page_lookup` itself
    if lookup is None:
        print(f"  FAIL {TARGET} is not in the image - wrong image, or the symbol was optimised away")
        ok = False
    else:
        lds = [f"{va:#010x}" for va, mn, ops in lookup
               if mn == "ldr" and _LDR_HINT.match(f"{mn} {ops}".strip())]
        if lds:
            print(f"  ok   {TARGET}: memq_hint load `ldr rN, [rM, #32]` at {lds[0]} - the guard "
                  f"sits behind the load 990 died on (memq_hint still at offset {HINT_OFF:#x})")
        else:
            print(f"  FAIL {TARGET}: no `ldr rN, [rM, #32]` - the guard is not inside the lookup, "
                  f"or the struct offset moved")
            ok = False

    print(f"check_vmhint_guard: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())