#!/usr/bin/env python3
"""Assert BY VALUE that the linked image's `mach_trap_table[15]` row IS (or is NOT) the trap-15
handler `ios7lab_mach_vm_map_trap` -- rung 1022, switch `STAGE90_XNU_IOS7_TRAPS`.

WHY A SEPARATE TOOL.  `tools/check_d13_ios7_vmwire.py` check 6 asserts that traps 15/24/25/41/42 all
read `kern_invalid`; that check is what the 994 rung's deferral looked like, and 1022 OPENS row 15
(`records/1022-trap15-plumbing.txt` section 2).  Deleting 15 from that tuple would also delete the
check that the OTHER FOUR rows are still closed, so this tool reads row 15's own two words in BOTH
directions instead (the checker edits `DEFERRED_TRAPS` to `(24, 25, 41, 42)`, keeping the other four).

WHAT IT READS, AND WHY IT IS THE RIGHT READING.  A mach trap's dispatch word is the SAVED-REGISTER
FRAME the shim writes, and `traps_lo.s`'s `swi_mach` addresses the table ARITHMETICALLY -
`add r11, r5, r5, lsl#1` (x3), `lsl#2` (x12), `ldr r1, [r1, #4]` - so entry i's `fn` word is at
`mach_trap_table + 12*i + 4` (`syscall_sw.h:72-77`, `mach_trap_t` = `{int arg_count; fn; name}`;
measured stride 0xC on the OFF image).  This tool therefore reads the WORDS at those offsets out of
the section table (`arm-none-eabi-objdump -h`), never a source shape, and never trusts a symbol name
for the identity of a row.

  `--expect on`:  `mach_trap_table` AND `ios7lab_mach_vm_map_trap` must BOTH be defined symbols
                  (a MISSING symbol is a FAILURE, never a pass), and row 15 must read
                  `{arg_count == 8, fn == &ios7lab_mach_vm_map_trap, name == "ios7lab_mach_vm_map_trap"}`.
  `--expect off`: the mirror - row 15's `{fn, name}` must equal `mach_trap_table[0]`'s OWN `{fn, name}`
                  (also `kern_invalid`), keyed on entry 0's words and NOT on the name literal, so a
                  shifted or re-shaped table cannot pass by coincidence.  Only `mach_trap_table` is a
                  required symbol here: the handler is absent BY CONSTRUCTION in an unopened image
                  (the current `out/stage90/xnu_arm_entry.elf` is exactly that negative control).

  The two modes assert disjoint values, so an ON image fails `--expect off` and vice versa.

  `--selftest`: build real ELF fixtures with the host `arm-none-eabi` toolchain in a fresh mkdtemp and
                assert this tool's VERDICTS (exit codes) over a fixture matrix: a table whose row 15 is
                `kern_invalid` must FAIL `--expect on` and PASS `--expect off`; a table whose row 15 is
                the handler must do the reverse; a fixture with NO `mach_trap_table` must FAIL both.

Exit 0 iff the requested expectation holds; nonzero on violation OR on refusal (a missing symbol, an
unreadable image, a name word that does not decode).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"
AS = "arm-none-eabi-as"
LD = "arm-none-eabi-ld"

TRAP_TABLE = "mach_trap_table"
HANDLER = "ios7lab_mach_vm_map_trap"
TRAP_NO = 15
ENTRY_BYTES = 12          # {int arg_count; fn; const char *name} on this 32-bit build
ARG_COUNT = 8             # the row's declared arity (syscall_sw.c:126) -- documentation on __arm__

_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")


class Refuse(Exception):
    """A loud refusal: the property could not be soundly decided (never a silent weaker proxy)."""


def _run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p


def sections(path):
    """[(name, vma, size, file_off)] for every allocatable, byte-carrying (PROGBITS) section."""
    out = _run([OBJDUMP, "-h", path])
    if out.returncode != 0:
        raise Refuse(f"{OBJDUMP} -h failed on {os.path.abspath(path)}: {out.stderr.strip()[:200]}")
    lines = out.stdout.splitlines()
    res, i = [], 0
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
            res.append((name, vma, size, off))
    return res


def nm_symbols(path):
    """{name: value} for DEFINED symbols (a line's first field is the address, so undefined ones skip)."""
    out = _run([NM, path])
    if out.returncode != 0:
        raise Refuse(f"{NM} failed on {os.path.abspath(path)}: {out.stderr.strip()[:200]}")
    syms = {}
    for line in out.stdout.splitlines():
        t = line.split()
        if len(t) != 3 or not re.fullmatch(r"[0-9a-fA-F]{8,16}", t[0]):
            continue                      # 'U name' (undefined) has 2 fields and no address
        syms.setdefault(t[2], int(t[0], 16))
    return syms


def make_readers(path, secs):
    with open(path, "rb") as fh:
        blob = fh.read()

    def read_u32(va):
        for name, vma, size, off in secs:
            if vma <= va and va + 4 <= vma + size:
                at = off + (va - vma)
                return int.from_bytes(blob[at:at + 4], "little")
        return None

    def read_cstr(va, limit=128):
        for name, vma, size, off in secs:
            if vma <= va < vma + size:
                at = off + (va - vma)
                end = blob.find(b"\0", at, at + limit)
                if end < 0:
                    raise Refuse(f"the string at {va:#x} is not NUL-terminated within {limit} bytes")
                return blob[at:end].decode("latin-1")
        return None

    return read_u32, read_cstr


def check(path, expect):
    """(ok, lines) -- the whole reading, printed by the caller."""
    lines = []
    if not os.path.isfile(path):
        raise Refuse(f"no such file: {os.path.abspath(path)}")
    secs = sections(path)
    if not secs:
        raise Refuse(f"{os.path.abspath(path)} has no allocatable PROGBITS section - not a linked image")
    syms = nm_symbols(path)
    read_u32, read_cstr = make_readers(path, secs)

    if TRAP_TABLE not in syms:
        raise Refuse(f"`{TRAP_TABLE}` is ABSENT from {os.path.abspath(path)}'s symbol table - the "
                     f"image has no trap table to read (a missing symbol must FAIL, not pass)")
    table = syms[TRAP_TABLE]
    base = table + TRAP_NO * ENTRY_BYTES
    argc, fn, nameptr = read_u32(base), read_u32(base + 4), read_u32(base + 8)
    for what, v in (("arg_count", argc), ("fn", fn), ("name pointer", nameptr)):
        if v is None:
            raise Refuse(f"row {TRAP_NO}'s {what} word at {base:#x} is outside every section - the "
                         f"table is not fully mapped")
    name = read_cstr(nameptr)
    lines.append(f"  {TRAP_TABLE} = {table:#010x}; row {TRAP_NO} at {base:#010x} = "
                 f"{{arg_count={argc}, fn={fn:#010x}, name={name!r}}}")

    # entry 0's own words -- the OFF identity key (never the name literal).
    argc0, fn0, np0 = read_u32(table), read_u32(table + 4), read_u32(table + 8)
    name0 = read_cstr(np0) if np0 else None
    lines.append(f"  entry 0 = {{arg_count={argc0}, fn={fn0:#010x}, name={name0!r}}}")

    if expect == "on":
        if HANDLER not in syms:
            raise Refuse(f"`{HANDLER}` is ABSENT from {os.path.abspath(path)}'s symbol table while "
                         f"--expect on: the row's handler is not linked, so the row cannot be installed "
                         f"(a missing symbol must FAIL, not pass)")
        want = syms[HANDLER]
        ok = True
        if argc != ARG_COUNT:
            lines.append(f"  FAIL row {TRAP_NO} arg_count is {argc}, expected {ARG_COUNT}")
            ok = False
        else:
            lines.append(f"  ok   row {TRAP_NO} arg_count == {ARG_COUNT}")
        if fn != want:
            lines.append(f"  FAIL row {TRAP_NO} fn is {fn:#010x}, expected &{HANDLER} == {want:#010x} "
                         f"({'== entry 0, i.e. kern_invalid - the row is NOT installed' if fn == fn0 else 'a different function'})")
            ok = False
        else:
            lines.append(f"  ok   row {TRAP_NO} fn == &{HANDLER} == {want:#010x}")
        if name != HANDLER:
            lines.append(f"  FAIL row {TRAP_NO} name word reads {name!r}, expected {HANDLER!r}")
            ok = False
        else:
            lines.append(f"  ok   row {TRAP_NO} name word reads {HANDLER!r}")
        return ok, lines

    # expect == "off": the mirror, keyed on entry 0's own words.
    ok = True
    if name0 != "kern_invalid":
        lines.append(f"  FAIL entry 0's name word reads {name0!r}, expected 'kern_invalid' - cannot "
                     f"key the OFF identity on it")
        ok = False
    else:
        lines.append("  ok   entry 0 reads 'kern_invalid' - the OFF key is present")
    if (fn, name) != (fn0, name0):
        lines.append(f"  FAIL row {TRAP_NO} reads {{fn={fn:#010x}, name={name!r}}}, expected entry 0's "
                     f"own {{fn={fn0:#010x}, name={name0!r}}} - the row is OPENED in an --expect off image")
        ok = False
    else:
        lines.append(f"  ok   row {TRAP_NO} == entry 0 by value {{fn={fn0:#010x}, name={name0!r}}} - "
                     f"the row is uninstalled (kern_invalid)")
    if HANDLER in syms:
        lines.append(f"       (info: `{HANDLER}` IS defined at {syms[HANDLER]:#010x} -- harmless if "
                     f"the row does not reference it)")
    return ok, lines


# ---------------------------------------------------------------------------------------------------
# self-test: build real ELF fixtures and assert the tool's VERDICTS (exit codes)
# ---------------------------------------------------------------------------------------------------

_FIXTURE_PRE = """\
\t.syntax unified
\t.arm
\t.text
\t.align 2
\t.globl kern_invalid
\t.type kern_invalid, %function
kern_invalid:
\tbx lr
\t.size kern_invalid, .-kern_invalid
"""
_FIXTURE_HANDLER = """\
\t.globl %(h)s
\t.type %(h)s, %%function
%(h)s:
\tbx lr
\t.size %(h)s, .-%(h)s
""" % {"h": HANDLER}
_FIXTURE_TABLE_HEAD = """\
\t.globl %(t)s
\t.type %(t)s, %%object
%(t)s:
""" % {"t": TRAP_TABLE}


def _table_asm(row15, define_handler):
    """Assembly for a 17-entry table {arg_count, fn, name}, row 15 = `row15`, plus helper symbols."""
    parts = [_FIXTURE_PRE]
    if define_handler:
        parts.append(_FIXTURE_HANDLER)
    parts.append(_FIXTURE_TABLE_HEAD)
    parts.append("\t.word 0, kern_invalid, .Lkn\n")
    for _ in range(1, TRAP_NO):
        parts.append("\t.word 0, kern_invalid, .Lkn\n")
    if row15 == "handler":
        parts.append("\t.word %d, %s, .Lhm\n" % (ARG_COUNT, HANDLER))
    else:
        parts.append("\t.word 0, kern_invalid, .Lkn\n")
    parts.append("\t.word 0, kern_invalid, .Lkn\n")   # entry 16
    parts.append("\t.size %s, .-%s\n" % (TRAP_TABLE, TRAP_TABLE))
    parts.append('\t.section .rodata\n.Lkn:\n\t.asciz "kern_invalid"\n')
    if define_handler:
        parts.append('\t.asciz ""\n.Lhm:\n\t.asciz "%s"\n' % HANDLER)
    return "".join(parts)


def _build_elf(tmp, name, src, ld_extra=()):
    sp = os.path.join(tmp, name + ".s")
    op = os.path.join(tmp, name + ".o")
    ep = os.path.join(tmp, name + ".elf")
    with open(sp, "w") as fh:
        fh.write(src)
    for cmd, out in (([AS, "-o", op, sp], op), ([LD] + list(ld_extra) + ["-o", ep, op], ep)):
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            raise Refuse(f"fixture {name}: {' '.join(cmd)} failed: {p.stderr.strip()[:300]}")
    return ep


def _capture(argv):
    import contextlib
    import io
    so, se = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(so), contextlib.redirect_stderr(se):
        rc = main(argv)
    return rc, so.getvalue(), se.getvalue()


def _selftest():
    for tool in (AS, LD, NM, OBJDUMP):
        if not shutil.which(tool):
            print(f"  FAIL selftest: no `{tool}` on PATH - cannot build/read fixtures")
            print("check_1022_trap15: FAIL (self-test)")
            return 1
    fails = []
    tmp = tempfile.mkdtemp(prefix="check_trap15_selftest.")
    try:
        def build(name, row15, define_handler, ld_extra=()):
            return _build_elf(tmp, name, _table_asm(row15, define_handler), ld_extra)

        off = build("fx_off", "invalid", True)
        on = build("fx_on", "handler", True)
        # Missing mach_trap_table: the object defines the helper but NO table symbol.
        missing = _build_elf(tmp, "fx_missing",
                             _FIXTURE_PRE + _FIXTURE_HANDLER +
                             '\t.section .rodata\n.Lkn:\n\t.asciz "kern_invalid"\n')
        # Missing handler while the table IS present and row 15 is kern_invalid.
        missing_handler = build("fx_missing_handler", "invalid", False)

        cases = [
            ([on, "--expect", "on"], 0, "ON fixture PASSES --expect on"),
            ([on, "--expect", "off"], 1, "ON fixture FAILS --expect off"),
            ([off, "--expect", "off"], 0, "OFF fixture PASSES --expect off"),
            ([off, "--expect", "on"], 1, "OFF fixture FAILS --expect on (the row is not installed)"),
            ([missing, "--expect", "on"], 1, "a MISSING mach_trap_table FAILS --expect on"),
            ([missing, "--expect", "off"], 1, "a MISSING mach_trap_table FAILS --expect off too"),
            ([missing_handler, "--expect", "on"], 1,
             "a MISSING handler symbol FAILS --expect on (never a pass)"),
            ([missing_handler, "--expect", "off"], 0,
             "a MISSING handler with an unopened table still PASSES --expect off "
             "(the handler is absent by construction in an unopened image)"),
            ([os.path.join(tmp, "no_such_file"), "--expect", "on"], 1,
             "a non-existent image FAILS"),
            ([on], 2, "no --expect FAILS (usage)"),
            ([on, "--expect", "maybe"], 2, "a bad --expect value FAILS (usage)"),
        ]
        for tail, want, desc in cases:
            rc, out, err = _capture(tail)
            if rc != want:
                fails.append(f"{desc}: exit {rc}, expected {want}\n    stdout: {out.strip()}"
                             f"\n    stderr: {err.strip()}")
            else:
                print(f"  ok   {desc} (exit {rc})")

        # The OFF identity must be keyed on entry 0's OWN word, not a name literal: build a table whose
        # entry 0 and row 15 are BOTH the handler (so entry0.name != 'kern_invalid').  --expect off must
        # REFUSE (it cannot key), and --expect on must also fail (arg_count 0 != 8).
        both = build("fx_both", "handler", True)
        src = open(os.path.join(tmp, "fx_both.s")).read()
        src = src.replace("\t.word 0, kern_invalid, .Lkn\n", "\t.word %d, %s, .Lhm\n" % (ARG_COUNT, HANDLER), 1)
        both2 = _build_elf(tmp, "fx_both2", src)
        for tail, want, desc in (([both2, "--expect", "off"], 1,
                                  "a table whose entry 0 is NOT kern_invalid makes --expect off REFUSE"),
                                 ([both, "--expect", "on"], 0,
                                  "sanity: the unmodified ON fixture still passes")):
            rc, out, err = _capture(tail)
            if rc != want:
                fails.append(f"{desc}: exit {rc}, expected {want}\n    stdout: {out.strip()}"
                             f"\n    stderr: {err.strip()}")
            else:
                print(f"  ok   {desc} (exit {rc})")
    except Refuse as exc:
        fails.append(f"fixture build/read refused: {exc}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if fails:
        for f in fails:
            print("  FAIL selftest: " + f)
        print("check_1022_trap15: FAIL (self-test)")
        return 1
    print("check_1022_trap15: PASS (self-test)")
    return 0


# ---------------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------------

_USAGE = ("usage: check_1022_trap15.py <elf> --expect on|off\n"
          "       check_1022_trap15.py --selftest")


def _err(msg):
    print(msg, file=sys.stderr)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--selftest" in argv:
        return _selftest()
    path = None
    expect = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--expect":
            i += 1
            if i >= len(argv):
                _err(_USAGE)
                return 2
            expect = argv[i]
        elif a.startswith("--"):
            _err(f"REFUSING: unknown option '{a}'")
            _err(_USAGE)
            return 2
        else:
            path = a
        i += 1
    if expect not in ("on", "off"):
        _err(f"REFUSING: --expect must be 'on' or 'off' (got {expect!r})")
        _err(_USAGE)
        return 2
    if path is None:
        _err(_USAGE)
        return 2
    print(f"check_1022_trap15: {os.path.abspath(path)} (expect row {TRAP_NO} "
          f"{'== ' + HANDLER if expect == 'on' else '== kern_invalid'})")
    try:
        ok, lines = check(path, expect)
    except Refuse as exc:
        _err(f"check_1022_trap15: REFUSED - {exc}")
        _err("check_1022_trap15: FAIL")
        return 1
    for line in lines:
        print(line)
    print(f"check_1022_trap15: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())