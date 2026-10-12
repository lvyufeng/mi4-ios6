#!/usr/bin/env python3
"""Assert BY VALUE that the linked image carries (ON) or lacks (OFF) the iOS 7 32-byte shared-region
wire - 997's G3, the fix 996's measurement points at (`docs/reference/d13-shreg-frontier.md` 2).

SIBLING, not an edit: `check_d13_shreg_probe.py` (sha256 e68a7657...84c, frozen for 996 by the lead's
ruling) asserts the PROBE's presence and, in BOTH modes, that the wire is still 20 bytes wide ("996
measures the misparse, it does not fix it").  997 fixes it, so this checker's OFF mode keeps 996's
clause verbatim and its ON mode asserts the exact inversion.  The utility layer below is copied from
the frozen checker byte-for-byte in meaning (objdump/sections/symbols/image_blob/find_string/
count_bytes/make_u32_reader/read_cstr/bodies_and_raw/imm_value/string_refs/reaches_print/calls_to/
straddle_terms/Env) so the two can be read against each other; the frozen file is never imported from,
so a change to it cannot change this one silently.

WHAT IT CHECKS (both modes):
  * the `sysent` rows 294 / 438 still dispatch where they must (narg / arg_bytes / function pointer),
    re-run off the image - the wire must never move the dispatch;
  * the two syscall bodies have disassembly bodies.

SWITCH ON (`--expect on`, the default) - three clauses, each read from the image's own bytes:
  1. the literal `IOS7LAB cache mapping %u ...` (70 B, `vm_unix.c:1214`) is present and referenced
     INSIDE `shared_region_copyin_mappings` by a reference that reaches a printf.  Same host rule as
     996: if the compiler inlined the function, the reference is accepted in the body that carries it
     and the report says so.
  2. the wire is 32 bytes: `shared_region_copyin_mappings` computes `count * 32` (`lsl rX, rY, #5` -
     measured `lsl r2, r9, #5` at the object's +0x30 and `add r0, r1, r2, lsl #5` in the bounds
     check), the loop advances the wire pointer by 32 (`add rX, rX, #32`), and the 20 B pair
     (`add rX,rY,rY,lsl #2` + `lsl rX,#2` = `count*5<<2`) is ABSENT.
  3. the 294 half: `shared_region_check_np` copyout's r2 is 8 (`mov r2, #8` before `bl copyout` -
     measured), not 4: dyld allocates 8 bytes and reads the low word, so the ON image must write the
     full `uint64_t` (the OFF image writes 4 into a buffer dyld zeroed).
  Plus: no body OUTSIDE `shared_region_copyin_mappings` references the literal.

SWITCH OFF (`--expect off`): the mirror, asserted POSITIVELY: zero occurrences of the prefix
  `IOS7LAB cache mapping` in every ALLOC+CONTENTS section (the 994 image's `IOS7LAB vm_allocate64`
  literals are a DIFFERENT prefix and do not count), the 20 B pair is present, and `check_np`'s
  copyout's r2 is 4 with no 8.

`--tree PATH` is the SOURCE-SIDE gate check the image cannot show.  `bsd/vm/vm_unix.c` must be in
exactly one of the two states - unstaged (3 bare `#if IOS7_COMPAT_PLATFORM`, 0 gates, the helper's
original return) or staged (3 gated `#if` lines, 1 helper body gate), a partial/foreign edit FAILS -
and the mapping printf's line must lie INSIDE a gate region when staged, outside every gate when
unstaged.  It takes the FILE, not the tree root.

`--control ELF` (repeatable) runs BOTH modes against an image that predates the change and demands it
discriminate: ON must FAIL and OFF must PASS.  The frozen 994 image
`out/stage90/captures/994-xnu_arm_entry.elf` is the control (no wire literal, 20 B pair present,
copyout r2=4); so is the 996 image.

Exit 0 iff every check of the asserted mode passes (and, with controls, each control discriminates).
"""
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"

SYSENT = "sysent"
SYSENT_ENTRY = 24                    # struct sysent on this 32-bit build (bsd/sys/sysent.h:42-53)
SYSENT_ROWS = (
    # (no, fn symbol, narg, arg_bytes, what dyld calls it for)
    (294, "shared_region_check_np", 1, 4, "dyld's mapSharedCache preamble"),
    (438, "shared_region_map_and_slide_np", 6, 24, "dyld's cache mapping call"),
)

# The literal is 70 bytes; the check uses the 23-byte prefix so a printf-format edit is still
# FOUND (and then has to survive clause 1's printf reachability), rather than silently missing.
WIRE_PREFIX = b"IOS7LAB cache mapping"
WIRE_LITERAL_SRC = 'IOS7LAB cache mapping %u va=%08x size=%08x offset=%08x max=%x init=%x'
WIRE_FN = "shared_region_copyin_mappings"
CHECK_NP = "shared_region_check_np"

VM_UNIX = "bsd/vm/vm_unix.c"
GUARD_IF = ("#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_SHREG_WIRE) "
            "&& STAGE90_XNU_SHREG_WIRE)")
BARE_IF = "#if IOS7_COMPAT_PLATFORM"
BODY_OPEN = "#if defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE"
HELPER_RET = "    return ios7_compat_vmabi_enabled();"
SITES_A = 3
SITES_B = 1

_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(\S+)\s*(.*)$")
_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")
_MOVW = re.compile(r"^(r\d+|ip|sl|fp|lr|sp),\s*#(-?(?:0x[0-9a-fA-F]+|\d+))")
_MOVT = re.compile(r"^(r\d+|ip|sl|fp|lr|sp),\s*#(-?(?:0x[0-9a-fA-F]+|\d+))")
_LDR_PC = re.compile(r"^(r\d+),\s*\[pc,\s*#(-?(?:0x[0-9a-fA-F]+|\d+))\]")
_ADD_LSL2 = re.compile(r"^(r\d+),\s*(r\d+),\s*\2,\s*lsl\s*#2$")
_LSL2 = re.compile(r"^(r\d+),\s*(r\d+),\s*#2$")
# 997's shapes.  Measured in the ON rehearsal object (sha256 3fa5c230...): `lsl r2, r9, #5` is the
# copyin size (a shift, so the immediate objdump prints is 5, NOT the 32 it multiplies by - the first
# run of this checker asserted `#32` here and failed on the real shape), `add r0, r1, r2, lsl #5` the
# bounds check, `add r6, r6, #32` the wire-pointer stride (this one IS the literal 32).
_LSL5 = re.compile(r"^(r\d+),\s*(r\d+),\s*#5$")
_ADD_LSL5 = re.compile(r"^(r\d+),\s*(r\d+),\s*(r\d+),\s*lsl\s*#5$")
_ADD32 = re.compile(r"^(r\d+),\s*(r\d+),\s*#(?:32|0x20)$")
_R2_IMM = re.compile(r"^r2,\s*#(-?(?:0x[0-9a-fA-F]+|\d+))$")


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


def symbols(path):
    """{name: (va, size)} for defined symbols; a global definition wins over a same-named local."""
    out, globals_ = {}, {}
    for line in objdump(["-t", path]).splitlines():
        t = [f for f in line.split() if f not in (".hidden", ".protected", ".internal")]
        if len(t) < 4 or not re.fullmatch(r"[0-9a-f]{8,16}", t[0]):
            continue
        section, size, name = t[-3], t[-2], t[-1]
        if section == "*UND*" or name.startswith("$") or not re.fullmatch(r"[0-9a-f]{8,16}", size):
            continue
        entry = (int(t[0], 16), int(size, 16))
        (globals_ if "g" in " ".join(t[1:-3]) else out).setdefault(name, entry)
    out.update(globals_)
    return out


_BLOBS = {}


def image_blob(path):
    if path not in _BLOBS:
        with open(path, "rb") as fh:
            _BLOBS[path] = fh.read()
    return _BLOBS[path]


def find_string(path, secs, needle):
    """(va, section_name) of the first `needle` byte string in the image, or None."""
    blob = image_blob(path)
    for name, vma, size, off in secs:
        hit = blob[off:off + size].find(needle)
        if hit >= 0:
            return vma + hit, name
    return None


def count_bytes(path, secs, needle):
    blob = image_blob(path)
    return sum(blob[off:off + size].count(needle) for _n, _v, size, off in secs)


def make_u32_reader(path, secs):
    blob = image_blob(path)

    def read_u32(va):
        for name, vma, size, off in secs:
            if vma <= va and va + 4 <= vma + size:
                at = off + (va - vma)
                return int.from_bytes(blob[at:at + 4], "little")
        return None

    return read_u32


def read_cstr(path, secs, va, limit=160):
    blob = image_blob(path)
    for name, vma, size, off in secs:
        if vma <= va < vma + size:
            at = off + (va - vma)
            end = blob.find(b"\0", at, at + limit)
            if end < 0:
                return None
            return blob[at:end].decode("latin-1")
    return None


def bodies_and_raw(path):
    """({symbol: [(va, mnemonic, operands)]}, {va: objdump_line}) - ONE disassembly pass."""
    funcs, raw, cur = {}, {}, None
    for line in objdump(["-d", path]).splitlines():
        m = _LABEL.match(line)
        if m:
            if not m.group(2).startswith("."):
                cur = m.group(2)
                funcs.setdefault(cur, [])
            continue
        m = _INSN.match(line)
        if m:
            va = int(m.group(1), 16)
            raw[va] = line
            if cur is not None:
                funcs[cur].append((va, m.group(2), m.group(3).split(";")[0].strip()))
        elif line and not line.startswith((" ", "\t")) and ":" not in line:
            cur = None
    return funcs, raw


def imm_value(ops, raw_line):
    """The immediate objdump printed, preferring its own `; 0x..` comment (they must agree)."""
    m = re.search(r";\s*0x([0-9a-fA-F]+)\s*$", raw_line)
    comment = int(m.group(1), 16) if m else None
    m = re.search(r"#(-?(?:0x[0-9a-fA-F]+|\d+))", ops)
    literal = None
    if m:
        literal = int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))
    if comment is not None and literal is not None and comment != literal:
        return None
    return comment if comment is not None else literal


def string_refs(path, secs, body, read_u32, raw, prefix=WIRE_PREFIX):
    """{literal_va: (va_of_instruction, form)} - every `prefix` string reference in one body.

    Two forms, both measured: `movw rX,#lo` + `movt rX,#hi` composing to the literal's VA, and a
    pc-relative `ldr rX,[pc,#off]` whose word is the VA.  The filter is the string CONTENT read at
    the composed address.  (Copied from the frozen 996 checker, whose module-level prefix is
    `IOS7SHREG`; here it is a parameter so the same body scan answers for either prefix.)
    """
    refs, i, pend, age = {}, 0, None, 0
    while i < len(body):
        va, mn, ops = body[i]
        if mn == "movw" and _MOVW.match(ops):
            m = _MOVW.match(ops)
            pend, age = (m.group(1), imm_value(ops, raw.get(va, "")), va), 0
        elif mn in ("movt",) and pend is not None:
            m = _MOVT.match(ops)
            if m and m.group(1) == pend[0]:
                hi = imm_value(ops, raw.get(va, ""))
                if pend[1] is not None and hi is not None:
                    addr = ((hi & 0xffff) << 16) | (pend[1] & 0xffff)
                    s = read_cstr(path, secs, addr, 192)
                    if s and s.startswith(prefix.decode()):
                        refs.setdefault(addr, []).append((pend[2], "movw/movt"))
            pend = None
        elif mn.endswith("ldr") and "pc" in ops:
            m = _LDR_PC.match(ops)
            if m:
                off = imm_value(ops, raw.get(va, ""))
                if off is not None:
                    word = read_u32(va + 8 + off)
                    if word is not None:
                        s = read_cstr(path, secs, word, 192)
                        if s and s.startswith(prefix.decode()):
                            refs.setdefault(word, []).append((va, "ldr pc-relative"))
        else:
            age += 1
            if age > 8:
                pend = None
        i += 1
    return refs


def reaches_print(body, at):
    """The VA of a printf the reference at `at` actually reaches, or None (bl, or merged tail `b`)."""
    printf_vas = calls_to(body, "printf")
    later = [v for v in printf_vas if v > at]
    if later:
        return later[0]
    for va, mn, ops in body:
        if not (at < va <= at + 16):
            continue
        m = re.match(r"^([0-9a-f]+)\s", ops)
        if mn in ("b", "bx") and m and int(m.group(1), 16) in printf_vas:
            return int(m.group(1), 16)
    return None


def calls_to(body, name):
    out = []
    for va, mn, ops in body:
        if mn not in ("bl", "blx"):
            continue
        tail = ops.split("<", 1)[-1].rstrip(">")
        if tail == name or tail.startswith(name + "."):
            out.append(va)
    return out


def straddle_terms(body):
    """[(add_va, lsl_va)] - `count * 5 * 4`: the 20-byte stride, read from the linked code."""
    out = []
    for i, (va, mn, ops) in enumerate(body):
        if mn not in ("add", "adds"):
            continue
        m = _ADD_LSL2.match(ops)
        if not m:
            continue
        dst = m.group(1)
        for va2, mn2, ops2 in body[i + 1:i + 4]:
            if mn2 not in ("lsl", "lsls"):
                continue
            m2 = _LSL2.match(ops2)
            if m2 and m2.group(2) == dst:
                out.append((va, va2))
    return out


def stride32_terms(body):
    """[(va, form)] - the three measured shapes of `count * 32` / a 32-byte pointer stride."""
    out = []
    for va, mn, ops in body:
        if mn in ("lsl", "lsls") and _LSL5.match(ops):
            out.append((va, "lsl #5 (the copyin size, count * 32)"))
        elif mn in ("add", "adds") and _ADD_LSL5.match(ops):
            out.append((va, "add ..., lsl #5 (the bounds check, user + count * 32)"))
        elif mn in ("add", "adds") and _ADD32.match(ops):
            out.append((va, "add rX, rX, #32 (the wire-pointer stride)"))
    return out


def copyout_sizes(body):
    """[(call_va, size)] - the r2 immediate in force at each `bl copyout`, scanning back to a call."""
    out = []
    for i, (va, mn, ops) in enumerate(body):
        if mn not in ("bl", "blx"):
            continue
        if ops.split("<", 1)[-1].rstrip(">") != "copyout":
            continue
        size = None
        for va2, mn2, ops2 in reversed(body[max(0, i - 6):i]):
            if mn2 in ("bl", "blx"):
                break
            if mn2 in ("mov", "movs", "movw"):
                m = _R2_IMM.match(ops2)
                if m:
                    size = int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))
                    break
        out.append((va, size))
    return out


class Env:
    def __init__(self, path):
        self.path = path
        self.secs = sections(path)
        self.syms = symbols(path)
        self.funcs, self.raw = bodies_and_raw(path)
        self.read_u32 = make_u32_reader(path, self.secs)


def check_dispatch(env):
    """The sysent rows - the one thing the wire must never move.  Mode-independent."""
    ok = True
    table_va = env.syms.get(SYSENT, (None, 0))[0]
    if table_va is None:
        print(f"  FAIL {SYSENT} is not a symbol in this image - wrong image?")
        return False
    for no, fn, narg, argbytes, why in SYSENT_ROWS:
        fn_va = env.syms.get(fn, (None, 0))[0]
        if fn_va is None:
            print(f"  FAIL syscall {no}: {fn} is not a defined symbol - the wired body is absent")
            ok = False
            continue
        base = table_va + no * SYSENT_ENTRY
        got = (env.read_u32(base), env.read_u32(base + 4), env.read_u32(base + 20))
        if got != (narg, fn_va, argbytes):
            print(f"  FAIL syscall {no} ({why}): {SYSENT} row is "
                  f"{{narg={got[0]}, call={got[1] and hex(got[1])}, arg_bytes={got[2]}}}, expected "
                  f"{{{narg}, {fn_va:#x}, {argbytes}}} - the wire must never move the dispatch")
            ok = False
        else:
            print(f"  ok   syscall {no} -> {fn} @{fn_va:#x} (narg={narg}, arg_bytes={argbytes}, "
                  f"{why})")
        if fn not in env.funcs:
            print(f"  FAIL syscall {no}: {fn} has no disassembly body")
            ok = False
    return ok


def check_on(env):
    ok = check_dispatch(env)

    # --- clause 1: the literal is in the image, referenced inside the wire function, reaching printf.
    found = find_string(env.path, env.secs, WIRE_PREFIX)
    if found is None:
        print(f"  FAIL the literal {WIRE_PREFIX!r} is not in the image at all - the 32 B branch is "
              f"not compiled in (switch OFF? or the branch was compiled out)")
        ok = False
    else:
        va, sec = found
        refs_by_fn = {}
        for fn in env.funcs:
            r = string_refs(env.path, env.secs, env.funcs[fn], env.read_u32, env.raw)
            if r:
                refs_by_fn[fn] = r
        host = WIRE_FN if va in refs_by_fn.get(WIRE_FN, {}) else \
            next((f for f, r in refs_by_fn.items() if va in r), None)
        if host is None:
            print(f"  FAIL no body in this image references {WIRE_PREFIX!r} @{va:#x} ({sec}) - the "
                  f"mapping print is not in the linked code at all")
            ok = False
        else:
            if host != WIRE_FN:
                print(f"       (info: {WIRE_FN} has no reference of its own - the print is in "
                      f"{host}, i.e. {WIRE_FN} was INLINED; the body pair is judged as one)")
            prints = calls_to(env.funcs[host], "printf")
            wired = [(at, form, reaches_print(env.funcs[host], at))
                     for at, form in refs_by_fn[host][va]]
            good = [(at, form, p) for at, form, p in wired if p is not None]
            if not good:
                print(f"  FAIL {host} references {WIRE_PREFIX!r} at "
                      f"{', '.join(f'{a:#x} ({f})' for a, f, _p in wired)} but no reference reaches a "
                      f"printf (no later `bl printf`, no merged-tail `b` to one)")
                ok = False
            else:
                at, form, p = good[0]
                print(f"  ok   the mapping literal @{va:#x} ({sec}) referenced in {host} at {at:#x} "
                      f"via {form} -> printf {p:#x} ({len(prints)} bl printf in the body)")
        outsiders = {fn: sorted(r) for fn, r in refs_by_fn.items() if fn != WIRE_FN}
        if outsiders:
            for fn, addr in outsiders.items():
                print(f"  FAIL {fn} references the mapping literal "
                      f"({', '.join(hex(a) for a in addr)}) - the print must live in {WIRE_FN} ONLY")
            ok = False
        elif found is not None:
            print(f"  ok   no body outside {{ {WIRE_FN} }} references a {WIRE_PREFIX.decode()!r} literal "
                  f"(by value, over all {len(env.funcs)} bodies)")

    # --- clause 2: the wire is 32 bytes, and the 20 B pair is gone.
    body = env.funcs.get(WIRE_FN, [])
    terms = stride32_terms(body)
    if terms:
        print(f"  ok   the wire is 32 B: {WIRE_FN} carries {len(terms)} 32-byte stride shape(s) - "
              + ", ".join(f"{f} @{v:#x}" for v, f in terms))
    else:
        print(f"  FAIL {WIRE_FN} carries no `count * 32` shape (no `lsl #5`, no `add ..., lsl #5`, no "
              f"`add rX, rX, #32`) - the 32 B wire is not what this image parses")
        ok = False
    twenty = straddle_terms(body)
    if twenty:
        a, l = twenty[0]
        print(f"  FAIL {WIRE_FN} still computes `count * 5 << 2` (add @{a:#x}, lsl @{l:#x}) - the 20 B "
              f"path is still linked in; the switch did not reach this object")
        ok = False
    else:
        print(f"  ok   the 20 B pair (`count * 5 << 2`) is ABSENT from {WIRE_FN} - the dead "
              f"else-path was deleted by the constant-folded selector, as the ON rehearsal measured")

    # --- clause 3: 294's copyout is the full 8 bytes.
    sizes = copyout_sizes(env.funcs.get(CHECK_NP, []))
    if not sizes:
        print(f"  FAIL {CHECK_NP} has no `bl copyout` at all")
        ok = False
    elif all(s == 8 for _v, s in sizes):
        print(f"  ok   294 half: {CHECK_NP}'s copyout writes 8 B "
              f"({', '.join(f'{v:#x} r2={s}' for v, s in sizes)}) - the uint64_t wire")
    else:
        print(f"  FAIL {CHECK_NP}'s copyout size(s) are "
              f"{', '.join(f'{v:#x} r2={s}' for v, s in sizes)}, expected 8 everywhere - the 294 half "
              f"did not open (dyld reads only the low word, so 4 is silently wrong, not loud)")
        ok = False
    return ok


def check_off(env):
    ok = check_dispatch(env)

    total = count_bytes(env.path, env.secs, WIRE_PREFIX)
    if total:
        at = []
        for fn in env.funcs:
            r = string_refs(env.path, env.secs, env.funcs[fn], env.read_u32, env.raw)
            at += [f"{fn}@{k:#x}" for k in r]
        print(f"  FAIL the image carries {total} occurrence(s) of {WIRE_PREFIX!r} - the switch is ON "
              f"in this build (referenced from {', '.join(at) if at else 'a data section'})")
        ok = False
    else:
        print(f"  ok   zero occurrences of {WIRE_PREFIX!r} in every ALLOC+CONTENTS section - OFF "
              f"leaves the image as if the 32 B branch did not exist")

    body = env.funcs.get(WIRE_FN, [])
    twenty = straddle_terms(body)
    if twenty:
        a, l = twenty[0]
        print(f"  ok   the wire is still 20 B: {WIRE_FN} computes count*5 then <<2 "
              f"(add @{a:#x}, lsl @{l:#x}) - the 996 pre-image's shape")
    else:
        print(f"  FAIL {WIRE_FN} does not compute `count * 5 << 2`: the 20 B wire is not what this "
              f"image parses - either the image is not this tree's or the OFF state is wrong")
        ok = False
    terms = stride32_terms(body)
    if terms:
        print(f"  FAIL {WIRE_FN} carries a 32-byte stride shape while the switch is OFF: "
              + ", ".join(f"{f} @{v:#x}" for v, f in terms))
        ok = False
    else:
        print(f"  ok   no 32-byte stride shape in {WIRE_FN}")

    sizes = copyout_sizes(env.funcs.get(CHECK_NP, []))
    if not sizes:
        print(f"  FAIL {CHECK_NP} has no `bl copyout` at all")
        ok = False
    elif all(s == 4 for _v, s in sizes):
        print(f"  ok   294 half: {CHECK_NP}'s copyout writes 4 B "
              f"({', '.join(f'{v:#x} r2={s}' for v, s in sizes)}) - the tree's own mach_vm_offset_t")
    else:
        print(f"  FAIL {CHECK_NP}'s copyout size(s) are "
              f"{', '.join(f'{v:#x} r2={s}' for v, s in sizes)}, expected 4 everywhere - the 8 B wire "
              f"is present while the switch is OFF")
        ok = False
    return ok


def check_tree(path):
    """The source-side state: the gate lines and the literal's position relative to them."""
    ok = True
    if not os.path.isfile(path):
        print(f"  FAIL --tree {path}: no such file")
        return False
    text = open(path).read()
    # EXACT LINES, not substrings: `#if IOS7_COMPAT_PLATFORM` is a PREFIX of the gated line, so a
    # `text.count(BARE_IF)` counts every gated line as bare as well (found by this checker's first
    # run, which read a fully staged file as "3 gated, 3 bare" - neither state).
    lines = text.splitlines()
    gates = sum(1 for line in lines if line.strip() == GUARD_IF)
    bare = sum(1 for line in lines if line.strip() == BARE_IF)
    bodies = text.count(BODY_OPEN)
    if (gates, bare, bodies) == (SITES_A, 0, SITES_B):
        state = "staged"
        print(f"  ok   --tree: staged - {gates} gated `#if` line(s) and {bodies} helper body gate")
    elif (gates, bare, bodies) == (0, SITES_A, 0):
        state = "unstaged"
        print(f"  ok   --tree: unstaged - {bare} bare `#if IOS7_COMPAT_PLATFORM` line(s), 0 gates "
              f"(the 996 pre-image: the 32 B branch is in the file but unreachable)")
    else:
        print(f"  FAIL --tree: neither state: {gates} gated (want 0 or {SITES_A}), {bare} bare "
              f"(want 0 or {SITES_A}), {bodies} body gate(s) (want 0 or {SITES_B}) - a partial or "
              f"foreign edit")
        return False

    # Depth counts BOTH wire constructs as openers (the helper region nests its body gate), and the
    # pairing is exact because these two are the only preprocessor conditionals inside the three
    # regions (measured: the probe's own guards sit outside all three - before/after R2 in check_np,
    # and R3 carries none).
    depth, lit = 0, []
    for no, line in enumerate(text.splitlines(), 1):
        if GUARD_IF in line or BODY_OPEN in line:
            depth += 1
        elif depth and line.strip() == "#endif":
            depth -= 1
        if WIRE_LITERAL_SRC in line:
            lit.append((no, depth))
    if len(lit) != 1:
        print(f"  FAIL --tree: the mapping printf literal occurs on {len(lit)} line(s), expected 1")
        ok = False
    elif state == "staged" and lit[0][1] < 1:
        print(f"  FAIL --tree: the mapping printf (line {lit[0][0]}) is OUTSIDE every gate region")
        ok = False
    elif state == "unstaged" and lit[0][1] > 0:
        print(f"  FAIL --tree: the mapping printf (line {lit[0][0]}) is inside a gate region, but the "
              f"file is unstaged")
        ok = False
    else:
        print(f"  ok   --tree: the mapping printf (line {lit[0][0]}) is "
              f"{'INSIDE' if state == 'staged' else 'OUTSIDE every'} gate region, as {state} requires")

    # The helper's body gate: `return 1;` must sit in the #if half and the original return in #else.
    lines = text.splitlines()
    i = next((n for n, line in enumerate(lines) if BODY_OPEN in line), None)
    if state == "staged":
        if i is None:
            print("  FAIL --tree: no helper body gate")
            return False
        els = next((n for n in range(i + 1, len(lines)) if lines[n].strip() == "#else"), None)
        end = next((n for n in range(i + 1, len(lines)) if lines[n].strip() == "#endif"), None)
        if els is None or end is None or els > end:
            print(f"  FAIL --tree: the helper body gate has no well-formed #else/#endif pair")
            ok = False
        elif not any("return 1;" in lines[n] for n in range(i, els)):
            print(f"  FAIL --tree: the body gate's #if half does not return 1")
            ok = False
        elif not any(HELPER_RET in lines[n] for n in range(els, end)):
            print(f"  FAIL --tree: the helper's original return is not in the #else half")
            ok = False
        else:
            print(f"  ok   --tree: helper body gated - `return 1;` in the #if half (line {i + 1}), "
                  f"the original return in #else (line {els + 1})")
    else:
        n = text.count(HELPER_RET)
        if n != SITES_B:
            print(f"  FAIL --tree: {n} occurrence(s) of the helper's original return, expected "
                  f"{SITES_B}")
            ok = False
        else:
            print(f"  ok   --tree: the helper's original return is present and ungated")
    return ok


def run(path, expect, tree=None):
    print(f"  -- {path} (expect switch {expect.upper()})")
    if not os.path.isfile(path):
        print("  FAIL no such file - nothing to check")
        return False
    try:
        env = Env(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read it: {exc}")
        return False
    ok = (check_on if expect == "on" else check_off)(env)
    if tree:
        ok = check_tree(tree) and ok
    return ok


def main() -> int:
    argv = sys.argv[1:]
    expect, args, controls, tree, i = "on", [], [], None, 0
    while i < len(argv):
        a = argv[i]
        if a == "--expect" and i + 1 < len(argv):
            expect, i = argv[i + 1], i + 1
        elif a.startswith("--expect="):
            expect = a.split("=", 1)[1]
        elif a == "--control" and i + 1 < len(argv):
            controls.append(argv[i + 1])
            i += 1
        elif a == "--tree" and i + 1 < len(argv):
            tree, i = argv[i + 1], i + 1
        elif a == "--off":
            expect = "off"
        elif a == "--on":
            expect = "on"
        elif not a.startswith("--"):
            args.append(a)
        else:
            print(f"check_d13_shreg_wire: unknown option {a}", file=sys.stderr)
            return 2
        i += 1
    if expect not in ("on", "off"):
        print(f"check_d13_shreg_wire: --expect must be on or off, not [{expect}]", file=sys.stderr)
        return 2
    path = args[0] if args else "out/stage90/xnu_arm_entry.elf"

    print(f"check_d13_shreg_wire: {path} (expect switch {expect.upper()})")
    ok = run(path, expect, tree)
    print(f"check_d13_shreg_wire: {'PASS' if ok else 'FAIL'} ({expect})")

    for ctl in controls:
        print(f"check_d13_shreg_wire: control {ctl} (must FAIL on and PASS off)")
        on = run(ctl, "on")
        off = run(ctl, "off")
        if on or not off:
            print(f"  FAIL the control does not discriminate: on={on} off={off} - it is not an image "
                  f"this checker can tell apart")
            ok = False
        else:
            print("  ok   control discriminates (on FAILs, off PASSes)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())