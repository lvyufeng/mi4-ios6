#!/usr/bin/env python3
"""Assert BY VALUE that the linked image carries (ON) or lacks (OFF) the 996 shared-region probe.

It reads the image's own section table, symbol table, bytes and disassembly - never a source comment
(`mi4-a-claim-in-a-comment-is-not-a-check`), and it reads the linked code BY VALUE rather than
trusting the source shape (`mi4-linked-code-order-is-not-source-order`).

WHAT IT CHECKS (mode-independent, both modes):
  1. the `sysent` table is in the image and its rows 294 / 438 still dispatch where they must:
     narg / arg_bytes / function pointer - the master's signature, re-run off the image
     (`bsd/kern/syscalls.master:452/:671` -> `out/xnu_generated_d13/bsd/init_sysent.c:440/:649`).
     The probe must never move the dispatch.
  2. the two syscall bodies and `_shared_region_map` have disassembly bodies.
  3. the wire is STILL 20 bytes wide: `shared_region_copyin_mappings` computes `count * 5 * 4`
     (`add rX, rY, rY, lsl #2` then `lsl rZ, rX, #2` - measured at 0x8041fdf4/0x8041fdf8 in the
     994/995 image).  The 996 probe MEASURES the misparse, it does not fix it: a 20 -> 32 change here
     at this rung would make the probe's own reading uninterpretable (`mi4-measurement-defects`).

SWITCH ON (`--expect on`, the default): each of the five literals (patch_d13_shreg_probe.LITERALS) is
  (a) present in the image, and
  (b) referenced INSIDE its own function's body - by a `movw rX, #lo` / `movt rX, #hi` pair composing
      to the literal's VA (the shape this tree's compiler emits for a string argument, measured in the
      994 image: `movw r0, #31177 ; 0x79c9` / `movt r0, #32853 ; 0x8055` / `bl printf` at
      0x80477c60-0x80477c6c), or by a pc-relative literal load whose word is the literal's VA - and
  (c) that body carries at least one `bl printf` after the reference.
  The counts of `bl printf` are printed as INFO only: clang MERGES identical print tails (measured:
  map_slide's three `ret=` sites compile to two `bl printf`, the third a tail-branch into the second),
  so a count is not a contract.
  (d) "inside the probed functions ONLY", by value: no OTHER body in the image references any probe
      literal.  This is the strongest form of the statement available at image level.
SWITCH OFF (`--expect off`): the mirror image, asserted POSITIVELY: zero occurrences of the
  `IOS7SHREG` byte string in every ALLOC+CONTENTS section, and no body references one.  The gate is
  `#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE`, so OFF must leave the image as if
  the probe did not exist.

THE 999 LADDER (`--ladder`, default off): the 999 slide half adds three literals - `_shared_region_slide`'s
  entry, its step past `slide_init`, its step past the copyin - held in their own table
  (LADDER_LITERALS, 3 print sites).  With `--ladder` they join the ON clauses above (and their host
  joins the allowed set), and `--tree` expects the fully staged file's 10 guard regions instead of 7.
  WITHOUT the flag the checker is the 996 checker, so the frozen 996/998 home images keep passing
  unchanged - a default that demanded literals the home image cannot carry would fail every build's
  own home-image discipline.  The 999 spelling is `--expect on --ladder --tree bsd/vm/vm_unix.c`
  (note: --tree takes the FILE, not the tree root).  On the 999 image that run is EXPECTED to fail
  exactly one clause - the frozen 20 B straddle (`996 measures the misparse, it does not fix it`; the
  wire is 32 B from 997 on) - and build_999.sh asserts exactly one `^  FAIL ` line, naming it.  A 999
  image run WITHOUT `--ladder` fails the outsiders clause (the slide half's host is not among the base
  hosts) and the message says to re-run with the flag.

`--tree PATH` is optional and adds the SOURCE-SIDE gate check the image cannot show: the guard line
`#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE` occurs 7 times in the staged
`bsd/vm/vm_unix.c` - 10 with `--ladder`, the fully staged 999 file - and every print site in scope
sits INSIDE a guard region (a line scan that tracks the guard depth).  That is the "the gate line"
assertion; without it the checker can only assert the gate's EFFECT (present ON / absent OFF).

`--control ELF` (repeatable) runs BOTH modes against an image that predates the change and demands it
discriminate: ON must FAIL and OFF must PASS.  The frozen 994 image
`out/stage90/captures/994-xnu_arm_entry.elf` is the control.

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

# (literal, function whose body must reference it, what it prints, print SITES in the staged source)
# The sites are 7 for 5 literals: 438 has THREE returns (an early `return kr`, the slide-failure one,
# and the shared `done:` exit) all printing the same text, and clang merges identical string constants
# into ONE .rodata copy (measured: the merged image carries 5 distinct IOS7SHREG strings).
PROBE_LITERALS = (
    (b"IOS7SHREG check_np pid=%d out=0x%08x\n", "shared_region_check_np", "294 entry", 1),
    (b"IOS7SHREG check_np ret=%d\n", "shared_region_check_np", "294 exit (the errno)", 1),
    (b"IOS7SHREG map_slide pid=%d fd=%d count=%u mappings=0x%08x slide=0x%x slide_start=0x%08x "
     b"size=0x%08x\n", "shared_region_map_and_slide_np", "438 entry (the whole wire)", 1),
    (b"IOS7SHREG map_slide ret=%d\n", "shared_region_map_and_slide_np", "438 exit (all three)", 3),
    (b"IOS7SHREG map_file kr=%d\n", "_shared_region_map", "the kr inside _shared_region_map", 1),
)
PROBE_SITES = sum(n for _l, _f, _w, n in PROBE_LITERALS)

# The 999 slide half's three literals.  They stay OUT of the table above and out of the default run:
# the frozen 996/998 home images cannot carry them, and a checker that demanded them by default would
# fail every build's own home-image discipline.  `--ladder` judges the 999 image: this table joins
# the ON clauses and check_tree() expects the fully staged file's 10 guard regions; its host then
# joins the allowed set, so `_shared_region_slide`'s references stop being "outside the probed
# functions" (see the module docstring's THE 999 LADDER).
LADDER_LITERALS = (
    (b"IOS7SHREG slide_enter slide=0x%x src=0x%08x n=0x%x\n", "_shared_region_slide", "999 entry "
     "(slide, slide_start, slide_size)", 1),
    (b"IOS7SHREG slide_init kr=%d entry=%p n=0x%x\n", "_shared_region_slide", "999 step past "
     "slide_init (error, entry still NULL, slide_size)", 1),
    (b"IOS7SHREG slide_copyin kr=%d\n", "_shared_region_slide", "999 step past the copyin "
     "(both outcomes)", 1),
)
LADDER_SITES = sum(n for _l, _f, _w, n in LADDER_LITERALS)
LADDER_FNS = {fn for _l, fn, _w, _n in LADDER_LITERALS}   # the hosts only --ladder allows

STRADDLE_FN = "shared_region_copyin_mappings"
PROBE_PREFIX = b"IOS7SHREG"
GUARD_OPEN = "#if defined(STAGE90_XNU_SHREG_PROBE) && STAGE90_XNU_SHREG_PROBE"
VM_UNIX = "bsd/vm/vm_unix.c"

_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t(\S+)\s*(.*)$")
_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")
_MOVW = re.compile(r"^(r\d+|ip|sl|fp|lr|sp),\s*#(-?(?:0x[0-9a-fA-F]+|\d+))")
_MOVT = re.compile(r"^(r\d+|ip|sl|fp|lr|sp),\s*#(-?(?:0x[0-9a-fA-F]+|\d+))")
_LDR_PC = re.compile(r"^(r\d+),\s*\[pc,\s*#(-?(?:0x[0-9a-fA-F]+|\d+))\]")
_ADD_LSL2 = re.compile(r"^(r\d+),\s*(r\d+),\s*\2,\s*lsl\s*#2$")
_LSL2 = re.compile(r"^(r\d+),\s*(r\d+),\s*#2$")


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
    """{name: (va, size)} for defined symbols; a global definition wins over a same-named local.

    Visibility markers are dropped first.  MEASURED in this image: `sysent`'s line is
    `804e5f84 g     O .text\\t00002940 .hidden sysent` - seven fields, with `.hidden` between the size
    and the name, so a plain `t[-3], t[-2], t[-1]` reads the SECTION as `.hidden` and the SIZE as the
    section name, and the symbol is silently dropped (this checker's first run reported "sysent is not
    a symbol in this image" for a symbol that was there all along).
    """
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
    """The image's bytes, read ONCE per path.

    A kernel image is ~25 MB and the check walks ~16 000 function bodies; re-opening the file per
    string read made the first full run take minutes.  Caching the bytes is not an optimisation of the
    MEASUREMENT - the bytes are the same bytes - it only stops the checker from paying for the same
    `read()` 100 000 times.
    """
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
    """({symbol: [(va, mnemonic, operands)]}, {va: objdump_line}) - ONE disassembly pass.

    The body list and the raw lines (which carry objdump's own immediate comments) come from the same
    `objdump -d` output; parsing it twice only doubled the cost of the slowest call.
    """
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


def bodies(path):
    return bodies_and_raw(path)[0]


def imm_value(ops, raw_line):
    """The immediate objdump printed, preferring its own `; 0x..` comment.

    objdump prints `movw r0, #31177\t; 0x79c9` - decimal plus a hex comment.  They are two encodings
    of one value; when both are present they must AGREE or this returns None (`mi4-measurement-defects`
    - a number that is an artifact of how it was read is worse than no number).
    """
    m = re.search(r";\s*0x([0-9a-fA-F]+)\s*$", raw_line)
    comment = int(m.group(1), 16) if m else None
    m = re.search(r"#(-?(?:0x[0-9a-fA-F]+|\d+))", ops)
    literal = None
    if m:
        literal = int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))
    if comment is not None and literal is not None and comment != literal:
        return None
    return comment if comment is not None else literal


def string_refs(path, secs, body, read_u32, raw):
    """{literal_va: (va_of_instruction, form)} - every literal-string reference in one body.

    Two forms are accepted, both measured: `movw rX,#lo` followed by `movt rX,#hi` (what this tree's
    compiler emits for a string argument - 994 image 0x80477c60: `movw r0, #31177 ; 0x79c9`, `movt r0,
    #32853 ; 0x8055`, `bl printf`) and a pc-relative literal load `ldr rX,[pc,#off]` whose word is the
    string's VA.  The filter is the string CONTENT read at the composed address, never an immediate
    alone: a pair composing to some other address cannot produce an `IOS7SHREG` string.

    The movw is remembered across up to 8 instructions (a compiler may interleave the argument setup)
    and expires after that, so a stale low half in another basic block is not composed with a later
    high half - false composition would have to land on a probe string to matter, but a check should
    not rely on that.
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
                    # 192, not the 160-byte default: the 438 entry literal is 101 bytes, and a read
                    # window shorter than the string makes read_cstr return None for a reference that
                    # is really there - the measurement would be the defect (mi4-measurement-defects).
                    s = read_cstr(path, secs, addr, 192)
                    if s and s.startswith(PROBE_PREFIX.decode()):
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
                        if s and s.startswith(PROBE_PREFIX.decode()):
                            refs.setdefault(word, []).append((va, "ldr pc-relative"))
        else:
            age += 1
            if age > 8:
                pend = None
        i += 1
    return refs


def reaches_print(body, at):
    """The VA of a printf the reference at `at` actually reaches, or None.

    Two shapes, both measured in the ON rehearsal: a `bl printf` later in the same body, and - when
    clang MERGES identical print tails - a short `b <tgt>` whose target is itself a `bl printf`
    (`done:`'s print compiles as `b` into the ret1/ret2 print block).  The merged site is not a
    defect: it still prints exactly one line per call.
    """
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


class Env:
    def __init__(self, path):
        self.path = path
        self.secs = sections(path)
        self.syms = symbols(path)
        self.funcs, self.raw = bodies_and_raw(path)
        self.read_u32 = make_u32_reader(path, self.secs)


def check_common(env):
    ok = True
    table_va = env.syms.get(SYSENT, (None, 0))[0]
    if table_va is None:
        print(f"  FAIL {SYSENT} is not a symbol in this image - wrong image?")
        return False, None
    for no, fn, narg, argbytes, why in SYSENT_ROWS:
        fn_va = env.syms.get(fn, (None, 0))[0]
        if fn_va is None:
            print(f"  FAIL syscall {no}: {fn} is not a defined symbol - the probed body is absent")
            ok = False
            continue
        base = table_va + no * SYSENT_ENTRY
        got = (env.read_u32(base), env.read_u32(base + 4), env.read_u32(base + 20))
        if got != (narg, fn_va, argbytes):
            print(f"  FAIL syscall {no} ({why}): {SYSENT} row is "
                  f"{{narg={got[0]}, call={got[1] and hex(got[1])}, arg_bytes={got[2]}}}, expected "
                  f"{{{narg}, {fn_va:#x}, {argbytes}}} - the probe must never move the dispatch")
            ok = False
        else:
            print(f"  ok   syscall {no} -> {fn} @{fn_va:#x} (narg={narg}, arg_bytes={argbytes}, "
                  f"{why})")
        if fn not in env.funcs:
            print(f"  FAIL syscall {no}: {fn} has no disassembly body")
            ok = False
    stride = straddle_terms(env.funcs.get(STRADDLE_FN, []))
    if stride:
        a, l = stride[0]
        print(f"  ok   the wire is still 20 B: {STRADDLE_FN} computes count*5 then <<2 "
              f"(add @{a:#x}, lsl @{l:#x}) - 996 measures the misparse, it does not fix it")
    else:
        print(f"  FAIL {STRADDLE_FN} does not compute `count * 5 << 2`: the 20 B wire is not what "
              f"this image parses - either the image is not this tree's or 996's premise is void")
        ok = False
    return ok, table_va


def check_on(env, ladder=False):
    """The image-side ON clauses.  `ladder` adds LADDER_LITERALS to the demanded set and their host
    to the allowed set (module docstring, THE 999 LADDER)."""
    ok, _ = check_common(env)
    table = PROBE_LITERALS + (LADDER_LITERALS if ladder else ())
    refs_by_fn = {}
    for fn in env.funcs:
        r = string_refs(env.path, env.secs, env.funcs[fn], env.read_u32, env.raw)
        if r:
            refs_by_fn[fn] = r
    for lit, fn, why, _sites in table:
        found = find_string(env.path, env.secs, lit)
        if found is None:
            print(f"  FAIL {why}: the literal {lit[:34]!r} is not in the image at all")
            ok = False
            continue
        va, sec = found
        body = env.funcs.get(fn)
        # The reference must be in the named function.  If the compiler INLINED that function into its
        # caller, the named body need not exist: the reference is then accepted in any of the probed
        # bodies (the caller), and the report says so - a name match is never trusted on its own.
        host = fn if va in refs_by_fn.get(fn, {}) else \
            next((f for f, r in refs_by_fn.items() if va in r), None)
        if host is None:
            print(f"  FAIL {why}: no body in this image references {lit[:34]!r} @{va:#x} ({sec}) - the "
                  f"print is not in the linked code at all")
            ok = False
            continue
        if host != fn:
            print(f"       (info: {fn} has no reference of its own - the print is in {host}, i.e. "
                  f"{fn} was INLINED; the body pair is judged as one)")
        prints = calls_to(env.funcs[host], "printf")
        wired = [(at, form, reaches_print(env.funcs[host], at))
                 for at, form in refs_by_fn[host][va]]
        good = [(at, form, p) for at, form, p in wired if p is not None]
        if not good:
            print(f"  FAIL {why}: {host} references {lit[:24]!r} at "
                  f"{', '.join(f'{a:#x} ({f})' for a, f, _p in wired)} but no reference reaches a "
                  f"printf (no later `bl printf`, no merged-tail `b` to one)")
            ok = False
            continue
        at, form, p = good[0]
        extra = f", +{len(good) - 1} more reference(s)" if len(good) > 1 else ""
        print(f"  ok   {why}: {host} loads @{va:#x} ({sec}) at {at:#x} via {form} -> printf "
              f"{p:#x} ({len(prints)} bl printf in the body{extra})")
    allowed = {fn for _l, fn, _w, _n in table}
    outsiders = {fn: sorted(r) for fn, r in refs_by_fn.items() if fn not in allowed}
    if outsiders:
        for fn, addr in outsiders.items():
            hint = " (this is the 999 slide half - re-run with --ladder)" if (
                not ladder and fn in LADDER_FNS) else ""
            print(f"  FAIL {fn} references a probe literal ({', '.join(hex(a) for a in addr)}) - the "
                  f"prints must live in the probed functions ONLY{hint}")
        ok = False
    else:
        print(f"  ok   no body outside {{{', '.join(sorted(allowed))}}} references an IOS7SHREG "
              f"literal (by value, over all {len(env.funcs)} bodies)")
    for fn in sorted(allowed):
        if fn in refs_by_fn:
            print(f"       (info: {fn} carries {len(calls_to(env.funcs[fn], 'printf'))} bl printf "
                  f"for {sum(n for _l, f, _w, n in table if f == fn)} print site(s) in "
                  f"source; "
                  f"clang merges identical tails, so fewer calls than sites is the compiler's "
                  f"choice, never a defect)")
    return ok


def check_off(env):
    ok, _ = check_common(env)
    total = count_bytes(env.path, env.secs, PROBE_PREFIX)
    if total:
        at = []
        for fn in env.funcs:
            r = string_refs(env.path, env.secs, env.funcs[fn], env.read_u32, env.raw)
            at += [f"{fn}@{k:#x}" for k in r]
        print(f"  FAIL the image carries {total} occurrence(s) of {PROBE_PREFIX!r} - the switch is "
              f"ON in this build (referenced from {', '.join(at) if at else 'a data section'})")
        ok = False
    else:
        print(f"  ok   zero occurrences of {PROBE_PREFIX!r} in every ALLOC+CONTENTS section - OFF "
              f"leaves the image as if the probe did not exist")
    return ok


def check_tree(path, ladder=False):
    """The source-side gate: the guard line and every literal inside a guard region.

    `ladder` selects the scope: 996's 5 literals / 7 guard regions by default, the 999 file's
    8 / 10 with the flag (see LADDER_LITERALS).
    """
    table = PROBE_LITERALS + (LADDER_LITERALS if ladder else ())
    want = PROBE_SITES + (LADDER_SITES if ladder else 0)
    ok = True
    if not os.path.isfile(path):
        print(f"  FAIL --tree {path}: no such file")
        return False
    text = open(path).read()
    opens = text.count(GUARD_OPEN)
    if opens != want:
        detail = "2 in check_np, 4 in map_slide, 1 in _shared_region_map"
        detail += ", 3 in _shared_region_slide" if ladder else " (+3 more with --ladder)"
        print(f"  FAIL --tree: {opens} guard line(s) `{GUARD_OPEN}`, expected {want} "
              f"(one per print site: {detail})")
        ok = False
    else:
        print(f"  ok   --tree: {opens} guard regions in {path}")
    depth, bad, seen = 0, [], {}
    for no, line in enumerate(text.splitlines(), 1):
        if GUARD_OPEN in line:
            depth += 1
        elif depth and line.strip() == "#endif":
            depth -= 1
        for lit, _fn, why, _n in table:
            needle = lit.decode().rstrip("\n")
            if needle in line:
                seen[lit] = seen.get(lit, 0) + 1
                if depth < 1:
                    bad.append(f"line {no} ({why}) is OUTSIDE every guard")
    for lit, _fn, why, sites in table:
        n = seen.get(lit, 0)
        if n != sites:
            print(f"  FAIL --tree: {why}: the literal {lit[:34]!r} occurs {n} time(s) in the "
                  f"source, expected {sites}")
            ok = False
    if bad:
        for b in bad:
            print(f"  FAIL --tree: {b}")
        ok = False
    elif not bad and all(seen.get(l, 0) == n for l, _f, _w, n in table):
        print(f"  ok   --tree: all {want} print sites of the {len(table)} literals are "
              f"INSIDE a guard region - the gate line is the only way they reach the image")
    return ok


def run(path, expect, tree=None, ladder=False):
    print(f"  -- {path} (expect switch {expect.upper()}{', ladder on' if ladder else ''})")
    if not os.path.isfile(path):
        print("  FAIL no such file - nothing to check")
        return False
    try:
        env = Env(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read it: {exc}")
        return False
    ok = (check_on(env, ladder) if expect == "on" else check_off(env))
    if tree:
        ok = check_tree(tree, ladder) and ok
    return ok


def main() -> int:
    argv = sys.argv[1:]
    expect, args, controls, tree, ladder, i = "on", [], [], None, False, 0
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
        elif a == "--ladder":
            ladder = True
        elif not a.startswith("--"):
            args.append(a)
        else:
            print(f"check_d13_shreg_probe: unknown option {a}", file=sys.stderr)
            return 2
        i += 1
    if expect not in ("on", "off"):
        print(f"check_d13_shreg_probe: --expect must be on or off, not [{expect}]", file=sys.stderr)
        return 2
    path = args[0] if args else "out/stage90/xnu_arm_entry.elf"

    print(f"check_d13_shreg_probe: {path} (expect switch {expect.upper()}"
          f"{', ladder on' if ladder else ''})")
    ok = run(path, expect, tree, ladder)
    print(f"check_d13_shreg_probe: {'PASS' if ok else 'FAIL'} ({expect})")

    for ctl in controls:
        print(f"check_d13_shreg_probe: control {ctl} (must FAIL on and PASS off)")
        on = run(ctl, "on", ladder=ladder)
        off = run(ctl, "off", ladder=ladder)
        if on or not off:
            print(f"  FAIL the control does not discriminate: on={on} off={off} - it is not an image "
                  f"this checker can tell apart")
            ok = False
        else:
            print("  ok   control discriminates (on FAILs, off PASSes)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())