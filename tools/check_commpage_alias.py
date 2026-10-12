#!/usr/bin/env python3
"""Assert BY VALUE that the linked image carries the iOS 7 commpage alias at 0xFFFF4000 (992).

Reads the section table (`arm-none-eabi-objdump -h`) and the disassembly (`-d`) of a linked image
(default `out/stage90/xnu_arm_entry.elf`) and checks the code the 992 switch compiles in - from the
bytes and the instructions, not from any source comment (`mi4-a-claim-in-a-comment-is-not-a-check`).

The fault it answers, by value (991's press): launchd aborted 231x with `usermode data abort,
EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0) (Translation fault on page)`.  iOS 7
userspace reads the commpage at 0xffff4000; this tree's control ABI sits at 0x40000000
(`osfmk/arm/cpu_capabilities.h:13` `_COMM_PAGE32_BASE_ADDRESS`), so without the alias the high page
is unmapped.  With `STAGE90_XNU_COMMPAGE_ALIAS=1`, `pmap_create_sharedpage` maps the commpage's own
physical page a second time at `IOS7LAB_COMMPAGE_ALIAS` = 0xffff4000, and the pte builders add
L2_ACCESS_USER|L2_ACCESS_APX|L2_NX_BIT for that (map, va) pair.  Two of the block's own runtime cues
are DEAD in this build, by value: its read-back `assert`s compile out (MACH_ASSERT is off, so
`assert(ex)` is `((void)0)` - `osfmk/kern/assert.h:97`), and its `kprintf` marker is a no-op macro
(`osfmk/arm/pmap.c:409` `#define kprintf(args...)`), which is why the patcher stages that one line as
a LIVE `printf`.  This checker therefore verifies the IMAGE statically, and binds by register rather
than by string.

Checks:
  1. both report strings exist in allocatable PROGBITS sections:
     `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN` (pmap.c, staged as printf
     because kprintf is a no-op there) and `IOS7LAB commpage cpus:` (commpage.c) - the two anchors
     that the switch reached both pool files.
  2. in `pmap_create_sharedpage`'s body, the constant 0xFFFF4000 is materialized (a `movw`+`movt`
     pair on the same register, a matching `mov rD, #imm`, or an `ldr rD, [pc, #N]` whose pool word
     is the constant) - the alias's own virtual address is compiled into the function that maps it.
  3. the same body still materializes 0x40000000 - the control ABI mapping is RETAINED, not replaced
     (the pair of constants in one body is the whole "same page, two addresses" claim).
  4. the same body calls pmap_enter at least TWICE (commpage + alias; a pre-992 image calls it once),
     and the alias va stays LIVE in r1 up to one pmap_enter call - a movw/movt pair completing into
     r1 with 0xFFFF4000, then no write to r1 and no other call (which would clobber r0-r3) before
     `bl pmap_enter`: at that call r1 still holds the alias va, i.e. it IS pmap_enter's 2nd argument.
     This is the static stand-in for the block's read-back asserts, which cannot exist in any image
     this configuration produces.
  5. image-wide, at least TWO bodies materialize 0xFFFF4000 - `pmap_create_sharedpage` and the pte
     attribute path (`leo_pte_build` / the template sites), so the constant is not only the map
     request but also the permission grant.

Rehearsal: against the 991 image (pre-fix) it must FAIL check 1 - there is no `IOS7LAB` string
anywhere, because all six board gates are closed; against an image whose gates were opened but whose
marker line still reads kprintf it must FAIL check 1 alone.  Exit 0 iff all pass.
"""
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TARGET = "pmap_create_sharedpage"
STRINGS = (b"IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN",
           b"IOS7LAB commpage cpus:")
ALIAS_VA = 0xFFFF4000
COMMPAGE_VA = 0x40000000

_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t([a-z0-9.]+)\s*(.*)$")
_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")
_MOVW = re.compile(r"^movw\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
_MOVT = re.compile(r"^movt\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
_MOV = re.compile(r"^mov\w*\s+(r\d+),\s*#(?:0x[0-9a-fA-F]+|\d+)$")
# objdump omits the immediate entirely for an offset of 0: `ldr r0, [pc]` means +0
_LDR_PC = re.compile(r"^ldr\s+(r\d+),\s*\[pc(?:,\s*#(-?(?:0x[0-9a-fA-F]+|\d+)))?\]$")
# anything below with a first destination register kills a pending movw for that register
_WRITES = re.compile(r"^(?:mov|movw|movt|mvn|adr|ldr|ldrb|ldrh|ldrsb|ldrsh|ldrd|add|adds|adc|sub|subs"
                     r"|sbc|rsb|and|ands|orr|orrs|eor|eors|bic|bics|mul|mla|mls|lsl|lsls|lsr|lsrs"
                     r"|asr|asrs|ror|rors|rev|clz|sxtb|sxth|sxtb16|uxtb|uxth|uxtb16)\s+(r\d+)\b")


def objdump(args):
    return subprocess.run([OBJDUMP] + args, capture_output=True, text=True, check=True).stdout


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


def find_string(path, secs, needle):
    """(va, section_name) of the first `needle` byte string in the image, or None."""
    with open(path, "rb") as fh:
        blob = fh.read()
    for name, vma, size, off in secs:
        hit = blob[off:off + size].find(needle)
        if hit >= 0:
            return vma + hit, name
    return None


def make_u32_reader(path, secs):
    """read_u32(va) -> the little-endian word at that VA, via the section table; None if unmapped."""
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
    """{symbol: [(va, mnemonic, operands)]} for the whole disassembly, in link order."""
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


def materializes_value(body, value, read_u32):
    """How `body` puts `value` into a register, or None.  Returns (va, description); va is the
    address of the COMPLETING instruction (the movt / the mov / the ldr).

    Accepted: a `movw rD, #lo` + `movt rD, #hi` pair on the same register (the pair may be split by
    unrelated instructions; any instruction that writes the register drops the half), a single
    `mov rD, #imm` when the value is rotated-immediate encodable (0x40000000 is: 0x40 ror 8), or a
    literal-pool load `ldr rD, [pc, #N]` whose word is the value (ARM's PC reads as insn_VA + 8)."""
    pending = {}
    for va, mn, ops in body:
        text = f"{mn} {ops}".strip()
        m = _MOVT.match(text)
        if m:
            reg = m.group(1)
            hi, lo = parse_imm(ops), pending.pop(reg, None)
            if lo is not None and hi is not None and (hi << 16) | lo == value:
                return va, f"movw/movt {reg} ({value:#010x}) at {va:#010x}"
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
        m = _MOV.match(text)
        if m:
            if parse_imm(ops) == value:
                return va, f"mov {m.group(1)}, #{value:#010x} at {va:#010x}"
            pending.pop(m.group(1), None)
            continue
        m = _LDR_PC.match(text)
        if m:
            off = parse_imm(ops) if m.group(2) is not None else 0
            if off is not None and read_u32(va + 8 + off) == value:
                return va, f"ldr {m.group(1)}, [pc, #{off}] (pool word at {va + 8 + off:#010x})"
            continue
        m = _WRITES.match(text)
        if m:
            pending.pop(m.group(1), None)
    return None


def calls_to(body, name):
    """[va] of every bl/blx in `body` whose target symbol is `name` (objdump prints `<name>`)."""
    out = []
    for va, mn, ops in body:
        if mn not in ("bl", "blx"):
            continue
        tail = ops.split("<", 1)[-1]
        if tail.startswith(name):
            out.append(va)
    return out


def aliased_into_second_arg(body, value):
    """VA of the first pmap_enter call that RECEIVES `value` in r1 (its 2nd argument), or None.

    The alias block's own read-back asserts cannot exist in an image this configuration produces
    (MACH_ASSERT is off: `assert(ex)` is `((void)0)`, osfmk/kern/assert.h:97), so this register walk
    is the static read-back.  Arm on the movw/movt pair that COMPLETES `value` into r1; disarm on any
    later instruction that writes r1, and on any other call (AAPCS lets a call clobber r0-r3); if a
    `bl pmap_enter` happens while armed, r1 still holds the value at that call.
    """
    armed = False
    pending_lo = None
    for va, mn, ops in body:
        text = f"{mn} {ops}".strip()
        if mn in ("bl", "blx"):
            if armed and ops.split("<", 1)[-1].startswith("pmap_enter"):
                return va
            armed = False
            pending_lo = None
            continue
        m = _MOVW.match(text)
        if m:
            pending_lo = parse_imm(ops) if m.group(1) == "r1" else None
            continue
        m = _MOVT.match(text)
        if m:
            if m.group(1) == "r1":
                hi = parse_imm(ops)
                armed = (pending_lo is not None and hi is not None
                         and (hi << 16) | pending_lo == value)
            pending_lo = None
            continue
        m = _MOV.match(text)
        if m:
            if m.group(1) == "r1":
                armed = parse_imm(ops) == value
            continue
        m = _WRITES.match(text)
        if m and m.group(1) == "r1":
            armed = False
    return None


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "out/stage90/xnu_arm_entry.elf"
    print(f"check_commpage_alias: {path}")
    if not os.path.isfile(path):
        print("  FAIL no such file - nothing to check")
        print("check_commpage_alias: FAIL")
        return 2
    try:
        secs = sections(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read it: {exc}")
        print("check_commpage_alias: FAIL")
        return 2
    read_u32 = make_u32_reader(path, secs)
    ok = True

    # ---- 1. the two report strings, by value
    found_any = 0
    for needle in STRINGS:
        found = find_string(path, secs, needle)
        if found is None:
            print(f"  FAIL `{needle.decode()}` is not in the image at all (the switch never reached "
                  f"its pool file - or, for the pmap.c marker, the patcher's printf staging was not "
                  f"applied: pmap.c:409 `#define kprintf(args...)` makes kprintf a no-op there)")
            ok = False
        else:
            found_any += 1
            print(f"  ok   `{needle.decode()[:40]}`... at {found[0]:#010x} in {found[1]}")

    # ---- 2-4. the alias block inside pmap_create_sharedpage
    body = funcs.get(TARGET)
    if body is None:
        print(f"  FAIL {TARGET} is not in the image - wrong image, or the symbol disappeared")
        ok = False
        body = []
    else:
        m_alias = materializes_value(body, ALIAS_VA, read_u32)
        if m_alias is None:
            print(f"  FAIL {TARGET}: 0x{ALIAS_VA:x} is never materialized in its body - the alias "
                  f"mapping is not in the image (the pool was built without "
                  f"-DSTAGE90_XNU_COMMPAGE_ALIAS=1, or the gate was not opened)")
            ok = False
        else:
            print(f"  ok   {TARGET}: the alias va is materialized by {m_alias[1]}")
        m_base = materializes_value(body, COMMPAGE_VA, read_u32)
        if m_base is None:
            print(f"  FAIL {TARGET}: 0x{COMMPAGE_VA:x} is not materialized in its body - the control "
                  f"ABI mapping is gone; the alias must be an ADDITION, never a replacement")
            ok = False
        else:
            print(f"  ok   {TARGET}: the control-ABI va 0x{COMMPAGE_VA:x} is still materialized "
                  f"({m_base[1]}) - the alias is an addition")
        enters = calls_to(body, "pmap_enter")
        if len(enters) < 2:
            print(f"  FAIL {TARGET}: only {len(enters)} call(s) to pmap_enter - the alias block calls "
                  f"it a second time (commpage + alias); a pre-992 image calls it once")
            ok = False
        else:
            print(f"  ok   {TARGET}: {len(enters)} calls to pmap_enter at "
                  f"{', '.join(f'{v:#010x}' for v in enters[:3])} (commpage + alias)")
        bound = aliased_into_second_arg(body, ALIAS_VA)
        if bound is None:
            print(f"  FAIL {TARGET}: 0x{ALIAS_VA:x} never stays live in r1 up to a pmap_enter call - "
                  f"the alias va is materialized but not passed as pmap_enter's 2nd argument (the "
                  f"block's read-back asserts compile out under MACH_ASSERT=0, "
                  f"osfmk/kern/assert.h:97, so this binding IS the static read-back)")
            ok = False
        else:
            print(f"  ok   {TARGET}: the alias va stays live in r1 at the pmap_enter call at "
                  f"{bound:#010x} - 2nd argument, user read-only (asserts compile out; this binds)")

    # ---- 5. the constant reaches the pte attribute path too
    sites = []
    for name, b in funcs.items():
        m = materializes_value(b, ALIAS_VA, read_u32)
        if m is not None:
            sites.append((name, m[0], m[1]))
    if len(sites) < 2:
        print(f"  FAIL only {len(sites)} body/bodies materialize 0x{ALIAS_VA:x} - expected at least "
              f"{TARGET} plus the pte attribute path (leo_pte_build / the template sites): the map "
              f"request without the permission grant maps nothing user-readable")
        ok = False
    else:
        for name, _va, desc in sites[:4]:
            print(f"  ok   0x{ALIAS_VA:x} in <{name}>: {desc}")
        if len(sites) > 4:
            print(f"       ... and {len(sites) - 4} more")

    print(f"check_commpage_alias: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())