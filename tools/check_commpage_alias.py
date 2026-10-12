#!/usr/bin/env python3
"""Assert BY VALUE that the linked image carries the iOS 7 commpage alias at 0xFFFF4000 (992+993).

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
  6. (993) `arm_vm_init` carries the zeroing of the stolen high-vector L2 table: its body loads
     `IOS7LAB vecl2:`'s report string AND calls bzero/memset with L2_SIZE (0x400) in r1 OR r2 (the
     size is the 2nd argument of bzero, the 3rd of memset - the check accepts either spelling).
     WHY this is a check and not a comment: 992's press died exactly because that table was raw DRAM
     and its slot 0xf4 (VA 0xFFFF4000) was the first such slot ever entered - pmap_enter_options read
     a leftover 0xFFD5A0xx as an "old mapping" and indexed pv_head_table 523,610 entries deep (it
     holds 123,904), a kernel data abort at VA 0xc1233b14, pc 0x80028b90.  The bzero is the fix.
  7. (993) `pmap_create_sharedpage` carries the slot probe: its body loads `IOS7LAB commpage alias:
     L2 slot pre` AND `... stale slot cleared` (the fail-soft clear that turns a missed zero into a
     reading instead of a second panic), and the `pre` report is materialized BEFORE the body's last
     pmap_enter call (the alias enter).  A missing probe must stop the build, not the press.  The
     `bl pmap_pte` count is NOT a signal: measured against the 992 image, the body carries zero of
     them (the `lab_pte` read-back is dead once the asserts compile out, and the inlined
     `pmap_extract` walks the pte inline), so it is printed as info, never checked.

Rehearsal: against the 991 image (pre-fix) it must FAIL check 1 - there is no `IOS7LAB` string
anywhere, because all six board gates are closed; against an image whose gates were opened but whose
marker line still reads kprintf it must FAIL check 1 alone; against the 992 image (alias staged, no
zeroing, no probe) it must FAIL checks 1 (the vecl2 + slot-pre strings are absent), 6 and 7.  Exit 0
iff all pass.
"""
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
TARGET = "pmap_create_sharedpage"
VECL2_STRING = b"IOS7LAB vecl2: %u nonzero slot(s) in the stolen high-vector L2 table before zeroing"
SLOT_PRE_STRING = b"IOS7LAB commpage alias: L2 slot pre = 0x%08x"
SLOT_CLEARED_STRING = b"IOS7LAB commpage alias: stale slot cleared (was 0x%08x)"
STRINGS = (b"IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN",
           b"IOS7LAB commpage cpus:",
           VECL2_STRING,
           SLOT_PRE_STRING)
ALIAS_VA = 0xFFFF4000
COMMPAGE_VA = 0x40000000
L2_SIZE = 0x400          # pmap.h:196 - one 256-entry L2 table page
ZERO_CALLEES = ("bzero", "memset")

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


def value_live_in_reg_at_call(body, value, regs, callees):
    """VA of the first call to one of `callees` that RECEIVES `value` in one of `regs`, or None.

    The generalized register walk behind checks 6-7 (the same discipline as
    `aliased_into_second_arg`, which stays because check 4's wording is bound to r1/pmap_enter):
    arm when the value COMPLETES into one of `regs` (a movw/movt pair on the same register, a single
    `mov`/`movw`), disarm that register on any later instruction that writes it, and disarm all of
    them on any call other than the accepted callees (AAPCS lets a call clobber r0-r3).  If a call to
    an accepted callee happens while a register is armed, the value is still its argument there.
    `callees` are matched as prefixes, so ("bzero", "memset") also accepts `_bzero` / `memset_s`.
    """
    state = {r: (False, None) for r in regs}
    for va, mn, ops in body:
        text = f"{mn} {ops}".strip()
        if mn in ("bl", "blx"):
            tail = ops.split("<", 1)[-1]
            # `_bzero` in C is emitted as `<bzero>` in this image (measured), but accept the
            # underscored spelling too - a name variant is not a reason to miss the call
            names = tail if tail.startswith(tuple(callees)) else tail.lstrip("_")
            if any(state[r][0] for r in regs) and names.startswith(tuple(callees)):
                return va
            for r in regs:
                state[r] = (False, None)
            continue
        m = _MOVT.match(text)
        if m:
            r = m.group(1)
            if r in state:
                _armed, lo = state[r]
                hi = parse_imm(ops)
                state[r] = (lo is not None and hi is not None and (hi << 16) | lo == value, None)
            continue
        m = _MOVW.match(text)
        if m:
            r = m.group(1)
            if r in state:
                lo = parse_imm(ops)
                # a lone movw COMPLETES any value <= 0xFFFF (it zero-extends); for a wider value it
                # is only the low half, pending a movt (`movw r1, #4096` is the measured shape of
                # pmap_grab_page's bzero size in this image)
                state[r] = (lo == value, lo if value > 0xFFFF else None)
            continue
        m = _MOV.match(text)
        if m:
            r = m.group(1)
            if r in state:
                state[r] = (parse_imm(ops) == value, None)
            continue
        m = _WRITES.match(text)
        if m and m.group(1) in state:
            state[m.group(1)] = (False, None)
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

    # ---- 6. arm_vm_init zeroes the stolen high-vector L2 table (993's root fix)
    vm_body = funcs.get("arm_vm_init")
    vecl2 = find_string(path, secs, VECL2_STRING)
    if vm_body is None:
        print("  FAIL arm_vm_init is not in the image - wrong image, or the symbol disappeared")
        ok = False
    elif vecl2 is None:
        print(f"  FAIL `{VECL2_STRING.decode()[:48]}`... is not in the image - the 993 zeroing of "
              f"the stolen high-vector L2 table is not compiled in (992's press died exactly there: "
              f"slot 0xf4 held raw DRAM, pmap_enter_options walked pv_head_table 523,610 entries "
              f"deep and aborted at VA 0xc1233b14)")
        ok = False
    else:
        m_str = materializes_value(vm_body, vecl2[0], read_u32)
        zero = value_live_in_reg_at_call(vm_body, L2_SIZE, ("r1", "r2"), ZERO_CALLEES)
        if m_str is None:
            print(f"  FAIL arm_vm_init never loads the vecl2 report string at {vecl2[0]:#010x} - the "
                  f"string exists but the block that prints it is not in this body")
            ok = False
        else:
            print(f"  ok   arm_vm_init loads the vecl2 report string ({m_str[1]})")
        if zero is None:
            print(f"  FAIL arm_vm_init calls no bzero/memset with L2_SIZE (0x{L2_SIZE:x}) in r1/r2 - "
                  f"the stolen table is left raw DRAM and the first pmap_enter into any of its other "
                  f"255 slots repeats 992's abort")
            ok = False
        else:
            print(f"  ok   arm_vm_init zeroes the stolen table: bzero/memset(..., 0x{L2_SIZE:x}) "
                  f"at {zero:#010x}")

    # ---- 7. the alias block probes (and fail-soft clears) slot 0xf4 first (993)
    if body:
        pre = find_string(path, secs, SLOT_PRE_STRING)
        cleared = find_string(path, secs, SLOT_CLEARED_STRING)
        if pre is None or cleared is None:
            print(f"  FAIL the 993 slot probe's report strings are missing from the image "
                  f"({'pre ' if pre is None else ''}{'cleared ' if cleared is None else ''}absent) - "
                  f"a 992-shaped alias block: it enters slot 0xf4 blind")
            ok = False
        else:
            m_pre = materializes_value(body, pre[0], read_u32)
            m_cleared = materializes_value(body, cleared[0], read_u32)
            ptes = calls_to(body, "pmap_pte")
            if m_pre is None or m_cleared is None:
                print(f"  FAIL {TARGET} never loads the probe's report string(s) - the strings exist "
                      f"but the probe is not in this body")
                ok = False
            else:
                print(f"  ok   {TARGET} loads the probe reports ({m_pre[1]}; {m_cleared[1]})")
                enters = calls_to(body, "pmap_enter")
                if not enters or m_pre[0] >= enters[-1]:
                    print(f"  FAIL {TARGET}: the probe's report is not materialized before the last "
                          f"pmap_enter call - the slot must be measured BEFORE the alias enter")
                    ok = False
                else:
                    print(f"  ok   {TARGET}: the probe's report loads at {m_pre[0]:#010x}, before "
                          f"the last pmap_enter call at {enters[-1]:#010x} - the slot is measured "
                          f"first")
                print(f"       (info: {len(ptes)} `bl pmap_pte` in the body - the compiler inlines "
                      f"pmap_pte at these callsites (the 992 body has 0 while its inlined "
                      f"pmap_extract walks the pte inline), so the string binding, not this count, "
                      f"is the check)")

    print(f"check_commpage_alias: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())