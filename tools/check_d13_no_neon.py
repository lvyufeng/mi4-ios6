#!/usr/bin/env python3
"""Assert BY VALUE that a code object emits NO compiler-generated FP/NEON instruction (rung 1021c,
switch `STAGE90_XNU_GENERAL_REGS`, the `-msoft-float` spelling).

The rung's substance is a COMPILER FLAG, so the only proof that the flag reached the compile is a
property of the emitted CODE: does the thing we are about to run contain an instruction that allocates
a VFP/NEON register?  A `#if`-guard cannot answer it, a comment cannot answer it, and a source grep
cannot answer it - the instructions are emitted by the vectorizer, not written by hand
(`[[mi4-a-claim-in-a-comment-is-not-a-check]]`).  So this tool DECODES THE INSTRUCTIONS with
`arm-none-eabi-objdump -d` and counts them.  It never reads source, never greps a string, never trusts
a claim.

WHY THE COUNT MATTERS (docs/1021c-general-regs-design.md §1.1, §3.1).  clang is built hard-float with
a soft calling convention (`-mfpu=neon-vfpv4 -mfloat-abi=softfp`), so the vectorizer may allocate
`d`-registers.  Every such instruction obeys the `FPEXC.EN` gate; XNU clears `FPEXC.EN` for kernel
execution (`osfmk/arm/pcb.c` save_vfp_context), so a kernel-mode NEON/VFP instruction is UNDEFINED and
faults (1021 root-cause, `0x8047b0ac`).  Scalar VFP (`vldr`/`vstr`/`vpush`/`vpop`, a 64-bit move or a
spill) reads the SAME `d`-registers and faults IDENTICALLY - so the count must include the scalar-VFP
family, not just NEON.  A flag that removed only NEON would leave the scalar residue faulting.

WHAT IS DECIDED, AND WHAT IS REFUSED.
  * SYMBOL MODE (`<file> --symbol <name>`) counts only within THAT SYMBOL'S OWN EXTENT, taken from
    `arm-none-eabi-nm -S` (`st_value`, `st_size`).  The window is the symbol's extent, never the whole
    file and never a fixed byte window, so a neighbouring symbol cannot satisfy the check on the
    asked-for symbol's behalf.  A symbol whose size is not resolvable is a LOUD refusal, never a
    whole-file fallback.
  * POOL MODE (`--pool <dir>`) counts in every `*.o`'s `.text` under `<dir>` and prints the ABSOLUTE
    path it walked plus the per-object breakdown - because a NEON-free pool that is NEON-free only
    because the wrong (empty/stale) directory was walked is this project's most-repeated defect class
    (`m1015b`, `records/1020-build-defect.txt`).  `--expect none` therefore ALSO requires at least
    `--min-objects` (default 100) objects to have been examined: an empty or stale directory FAILS
    rather than passing vacuously (docs/1021c §5.3).

  The FP/NEON accept-list is a SINGLE SOURCE OF TRUTH (one frozenset) used by both modes.  An
  instruction whose base mnemonic begins `v`/`f` but is NOT in the list is a LOUD REFUSAL, not a
  silent pass: a mnemonic this tool did not anticipate must not be countable as "not FP" by default.

Exit 0 iff the requested expectation is satisfied; nonzero on violation OR on refusal.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"

# The pool under test (`out/xnu_kernel_obj_d13`) held 705 `*.o` at rung 1021c.  `--expect none` must
# refuse if far fewer objects than that were EXAMINED, so an ON arm that rebuilt only a handful of
# objects (leaving the rest stale or absent) FAILS instead of passing vacuously (docs/1021c §5.3,
# m1015b, records/1020-build-defect.txt).  Overridable with --min-objects.
DEFAULT_MIN_OBJECTS = 700

# ---------------------------------------------------------------------------------------------------
# THE ACCEPT-LIST - one source of truth, used by symbol mode and pool mode alike.
#
# An ARMv7 instruction is FP/NEON iff it reads or writes the FP register file (`s`/`d`/`q` registers),
# which is exactly the state `FPEXC.EN` gates.  Every such mnemonic in ARMv7 begins `v` or `f`:
# the Advanced-SIMD/NEON family and the VFP family both do, and ARMv7 has NO non-FP data-processing
# mnemonic beginning `v` or `f` (the `v`/`f` space is entirely the coprocessor 10/11 register file).
# We therefore ENUMERATE the base mnemonics (a mnemonic's `.size`/`.type` suffix and any lane/`[..]`
# operand are stripped before the lookup) rather than trusting a loose `^v` prefix, and we treat an
# unrecognised `^[vf]` base mnemonic as a REFUSAL so a future/missed spelling cannot slip through as
# "not FP".
# ---------------------------------------------------------------------------------------------------

# Advanced SIMD / NEON (element and whole-vector forms; incl. loads/stores, DUP/VMOV family, VLDn/VSTn).
NEON_MNEMONICS = frozenset((
    "vaba", "vabal", "vabd", "vabdl", "vabs",
    "vacge", "vacgt", "vacle", "vaclt",
    "vadd", "vaddhn", "vaddl", "vaddw",
    "vand", "vbic", "vbif", "vbit", "vbsl",
    "vceq", "vcge", "vcgt", "vcle", "vclt", "vcls", "vclz", "vcnt", "vcvt",
    "vdup", "veor", "vext",
    "vfma", "vfms", "vfnma", "vfnms",
    "vhadd", "vhsub",
    "vld1", "vld2", "vld3", "vld4",
    "vmax", "vmin",
    "vmla", "vmlal", "vmls", "vmlsl", "vmov", "vmovl", "vmovn", "vmul", "vmull", "vmvn",
    "vneg", "vnmla", "vnmls", "vnmul", "vorn", "vorr",
    "vpadal", "vpadd", "vpaddl", "vpmax", "vpmin",
    "vqabs", "vqadd", "vqdmlal", "vqdmlsl", "vqdmulh", "vqdmull",
    "vqmovn", "vqmovun", "vqneg", "vqrdmulh", "vqrshl", "vqrshrn", "vqrshrun",
    "vqshl", "vqshlu", "vqshrn", "vqshrun", "vqsub",
    "vraddhn", "vrecpe", "vrecps",
    "vrev16", "vrev32", "vrev64", "vrhadd", "vrsqrte", "vrsqrts",
    "vrshl", "vrshrn", "vrsubhn",
    "vshl", "vshll", "vshr", "vshrn",
    "vsli", "vsra", "vsri",
    "vst1", "vst2", "vst3", "vst4",
    "vsub", "vsubhn", "vsubl", "vsubw", "vswp",
    "vtbl", "vtbx", "vtrn", "vtst", "vuzp", "vzip",
))

# VFP (scalar and short-vector floating point, and the FP register-file moves, loads/stores and
# multi-register save/restore that the C compiler emits for 64-bit moves and spills).
VFP_MNEMONICS = frozenset((
    "vabs", "vadd", "vcmp", "vcmpe", "vcvt", "vcvtr", "vdiv",
    "vfma", "vfms", "vfnma", "vfnms",
    "vldm", "vldmia", "vldmdb", "vldr",
    "vmla", "vmls", "vmov", "vmovx", "vmrs", "vmsr", "vmul",
    "vneg", "vnmla", "vnmls", "vnmul",
    "vpop", "vpush",
    "vrinta", "vrintm", "vrintn", "vrintp", "vrintr", "vrintx", "vrintz",
    "vsqrt", "vsub",
    "vstm", "vstmia", "vstmdb", "vstr",
    # the deprecated ARM (pre-UAL) scalar-VFP spellings objdump still prints for ARM mode
    "fabsd", "fabss", "faddd", "fadds", "fcmpd", "fcmped", "fcmpes", "fcmps",
    "fconstd", "fconsts", "fcpyd", "fcpyde", "fcpye", "fcpys",
    "fdivd", "fdivs", "fldd", "fldmdbd", "fldmdbs", "fldmiad", "fldmias",
    "fldmdbs", "flds", "fmrrd", "fmrrs", "fmrs", "fmscd", "fmscs", "fmsr",
    "fmstat", "fmuld", "fmuls", "fmxr", "fnmacd", "fnmacs", "fnmuld", "fnmuls",
    "fnmscd", "fnmscs", "fnmscs", "fscaled", "fscales", "fsitod", "fsitos",
    "fsqrtd", "fsqrts", "fstmdbd", "fstmdbs", "fstmiad", "fstmias",
    "fstd", "fsts", "fsubd", "fsubs", "ftosid", "ftosis", "ftosizd", "ftosizs",
    "ftouid", "ftouis", "ftouizd", "ftouizs", "fuitod", "fuitos",
))

FP_MNEMONICS = NEON_MNEMONICS | VFP_MNEMONICS

# A mnemonic whose base begins with one of these but is absent from FP_MNEMONICS is a REFUSAL.
FP_PREFIXES = ("v", "f")

# objdump lines we care about: "     b8:\tf4600a8d \tvld1.32\t{d16-d17}, [r0]!" (tabs) and the
# space-separated variant.  Anchored on the address and the raw-encoding field so the mnemonic is
# captured from a fixed position - we NEVER match on the raw bytes.
_INSN = re.compile(r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{4,8})\s+(\S+)")


class Refuse(Exception):
    """A loud refusal: the property could not be soundly decided (never a silent weaker proxy)."""


def _run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True).stdout
    except OSError as exc:
        raise Refuse(f"could not run {cmd[0]}: {exc}")


def base_mnemonic(mn):
    """The bare mnemonic: drop a `.size`/`.type` suffix and any trailing decoration."""
    return mn.split(".", 1)[0].split("(", 1)[0].lower()


def decode(text):
    """[(addr, base_mnemonic, raw_line), ...] for every decoded instruction line in `text`."""
    out = []
    for line in text.splitlines():
        m = _INSN.match(line)
        if m:
            out.append((int(m.group(1), 16), base_mnemonic(m.group(3)), line.strip()))
    return out


def classify(base):
    """True iff `base` allocates the FP register file.  Refuses on an unrecognised `v`/`f` mnemonic."""
    if base in FP_MNEMONICS:
        return True
    if base.startswith(FP_PREFIXES):
        raise Refuse(f"unrecognised `{base}` mnemonic begins with a VFP/NEON letter but is not in the "
                     f"accept-list - refusing rather than counting it as 'not FP'; add it to "
                     f"FP_MNEMONICS with a citation if it is genuinely general-purpose")
    return False


def fp_in(text, lo=None, hi=None):
    """[(addr, base, line), ...] for FP instructions in `text`, restricted to [lo, hi) when given.

    The Python-side range filter makes the symbol window exact regardless of objdump's
    --start-address/--stop-address bound semantics.
    """
    out = []
    for addr, base, line in decode(text):
        if classify(base):
            if lo is not None and addr < lo:
                continue
            if hi is not None and addr >= hi:
                continue
            out.append((addr, base, line))
    return out


def objdump_text(path, lo=None, hi=None, section=".text"):
    cmd = [OBJDUMP, "-d"]
    if section:
        cmd.append(f"--section={section}")
    if lo is not None and hi is not None:
        cmd += [f"--start-address=0x{lo:x}", f"--stop-address=0x{hi:x}"]
    cmd.append(path)
    return _run(cmd)


# ---------------------------------------------------------------------------------------------------
# symbol resolution
# ---------------------------------------------------------------------------------------------------

def nm_symbols(path):
    """{name: (value, size_or_None, type_char)} from `nm -S`.  Handles all three nm line shapes:
       `value size type name`, `value type name`, `type name` (undefined)."""
    if not os.path.isfile(path):
        raise Refuse(f"no such file: {path}")
    text = _run([NM, "-S", path])
    syms = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        toks = line.split()
        # find the single-character type field
        ti = None
        for i, t in enumerate(toks[:3]):
            if len(t) == 1 and t.isalpha():
                ti = i
                break
        if ti is None:
            continue
        name = " ".join(toks[ti + 1:])
        if not name:
            continue
        if ti >= 2:                                   # value, size, type, name
            try:
                value, size = int(toks[0], 16), int(toks[1], 16)
            except ValueError:
                continue
            syms.setdefault(name, (value, size, toks[ti]))
        elif ti == 1:                                 # value, type, name  (size unknown)
            try:
                value = int(toks[0], 16)
            except ValueError:
                continue
            syms.setdefault(name, (value, None, toks[ti]))
        # ti == 0: undefined/error symbol, no address - not a resolvable extent
    return syms


# nm type letters that denote a symbol IN A CODE (executable) section: T/t text, W/w weak text,
# and the GNU ifunc `i`.  A data/bss/rodata symbol (D/d/B/b/R/r/...) has no instructions.
_CODE_TYPES = frozenset(("T", "t", "W", "w", "i"))


def resolve_symbol(path, name):
    """(value, size, type_char) for `name`, or a LOUD refusal explaining why no window could be built.

    The TYPE CHAR is load-bearing: a RELOCATABLE object places every section's VMA at 0, so a `.data`
    symbol and a `.text` symbol share an address.  An address-only window would then "check" a data
    symbol against unrelated code at VMA 0 and report a vacuous pass.
    """
    syms = nm_symbols(path)
    hit = syms.get(name)
    if hit is None:
        raise Refuse(f"symbol `{name}` is not in {os.path.abspath(path)}'s symbol table "
                     f"({len(syms)} symbols read) - nothing to check (a missing symbol must FAIL, "
                     f"not pass)")
    value, size, typ = hit
    if not size:
        raise Refuse(f"symbol `{name}` in {os.path.abspath(path)} has no resolvable size "
                     f"(nm -S printed {size!r}) - its extent cannot be bounded; refusing a "
                     f"whole-file fallback")
    if typ not in _CODE_TYPES:
        raise Refuse(f"symbol `{name}` in {os.path.abspath(path)} has nm type `{typ}` (not a code "
                     f"section symbol) - it has no instructions to count; refusing rather than "
                     f"scanning the code that happens to share its (relocatable, VMA-0) address")
    return value, size, typ


def symbol_count(path, name):
    """(fp_count, [fp instructions], value, size, decoded_count) counting only within `name`'s own
    extent.  `decoded_count` is the number of instructions actually disassembled in that window: a
    symbol that resolved but whose range disassembles to NOTHING must FAIL, never read as "0 FP,
    therefore clean" (docs/1021c §5.3 - a check that did not read anything is not a check)."""
    value, size, _typ = resolve_symbol(path, name)
    text = objdump_text(path, value, value + size)
    decoded = [(a, b, l) for a, b, l in decode(text) if value <= a < value + size]
    fps = [(a, b, l) for a, b, l in decoded if classify(b)]
    return len(fps), fps, value, size, len(decoded)


# ---------------------------------------------------------------------------------------------------
# pool mode
# ---------------------------------------------------------------------------------------------------

def pool_objects(dirpath):
    """Sorted list of *absolute* `*.o` paths directly under `dirpath`."""
    if not os.path.isdir(dirpath):
        raise Refuse(f"--pool path is not a directory: {os.path.abspath(dirpath)}")
    ap = os.path.abspath(dirpath)
    return sorted(os.path.join(ap, n) for n in os.listdir(ap) if n.endswith(".o"))


def pool_count(dirpath):
    """(objects_examined, per_object {path: [(addr,base,line),...]}, total)."""
    objs = pool_objects(dirpath)
    per = {}
    total = 0
    for obj in objs:
        try:
            fps = fp_in(objdump_text(obj))
        except subprocess.CalledProcessError as exc:
            raise Refuse(f"{OBJDUMP} failed on {obj}: {exc}")
        if fps:
            per[obj] = fps
            total += len(fps)
    return len(objs), per, total


# ---------------------------------------------------------------------------------------------------
# reporting helpers
# ---------------------------------------------------------------------------------------------------

def _err(msg):
    print(msg, file=sys.stderr)


def _dump_breakdown(count, fps, context, limit=40):
    _err(f"    {context}: {count} FP instruction(s)")
    for addr, base, line in fps[:limit]:
        _err(f"      {addr:#010x}  {base:<8} {line}")
    if count > limit:
        _err(f"      ... and {count - limit} more")


# ---------------------------------------------------------------------------------------------------
# modes
# ---------------------------------------------------------------------------------------------------

def do_symbol(path, name, expect):
    print(f"check_d13_no_neon: symbol mode - {name} in {os.path.abspath(path)} (expect {expect})")
    try:
        count, fps, value, size, decoded = symbol_count(path, name)
    except Refuse as exc:
        _err(f"check_d13_no_neon: REFUSED - {exc}")
        return 1
    print(f"  window: {name} st_value={value:#010x} st_size={size:#x} -> [{value:#010x}, "
          f"{value + size:#010x})  (the SYMBOL'S OWN EXTENT, not the whole file)")
    print(f"  instructions decoded in window: {decoded}")
    print(f"  counted: {count} FP/NEON instruction(s)")
    if decoded == 0:
        _err(f"check_d13_no_neon: REFUSED - {name} resolved to [{value:#010x}, {value + size:#010x}) "
             f"but NOTHING disassembled there (0 instructions) in {os.path.abspath(path)}; reporting "
             f"'0 FP, therefore clean' on a range that was never read is a vacuous pass "
             f"(docs/1021c §5.3) - refusing")
        return 1
    if expect == "none":
        if count == 0:
            print(f"  ok   {name} contains no FP/NEON instruction - soft-float reached the compile")
            print("check_d13_no_neon: PASS")
            return 0
        _err(f"check_d13_no_neon: FAIL - expected NO FP/NEON in {name} ({os.path.abspath(path)}), "
             f"counted {count}:")
        _dump_breakdown(count, fps, f"{name} [{value:#x},{value + size:#x})")
        _err("  => the FP flag did not reach this compile, or this object came from another builder")
        return 1
    if count > 0:
        print(f"  ok   {name} contains FP/NEON ({count}) - the OFF arm still emits vector code")
        print("check_d13_no_neon: PASS")
        return 0
    _err(f"check_d13_no_neon: FAIL - expected at least one FP/NEON in {name} "
         f"({os.path.abspath(path)}), counted 0 (window [{value:#x},{value + size:#x}))")
    _err("  => the OFF arm is NOT byte-identical to the pre-rung pool "
         "([[mi4-off-option-two-spellings]])")
    return 1


def do_pool(dirpath, expect, min_objects):
    ap = os.path.abspath(dirpath)
    print(f"check_d13_no_neon: pool mode - {ap} (expect {expect}, min-objects {min_objects})")
    try:
        n_obj, per, total = pool_count(dirpath)
    except Refuse as exc:
        _err(f"check_d13_no_neon: REFUSED - {exc}")
        return 1
    n_with = len(per)
    print(f"  directory walked (ABSOLUTE): {ap}")
    print(f"  objects examined: {n_obj}")
    print(f"  objects with >=1 FP/NEON instruction: {n_with}")
    print(f"  total FP/NEON instructions: {total}")
    if per:
        print("  per-object breakdown:")
        for obj in sorted(per):
            print(f"    {len(per[obj]):6d}  {obj}")
    if expect == "none":
        if n_obj < min_objects:
            _err(f"check_d13_no_neon: FAIL - only {n_obj} object(s) examined under {ap}, below the "
                 f"anti-vacuous-pass floor of {min_objects} (--min-objects).  An empty or stale "
                 f"directory must FAIL, not pass vacuously (docs/1021c §5.3, m1015b).")
            return 1
        if total == 0:
            print(f"  ok   {total} FP/NEON instructions across {n_obj} object(s) - the pool emits no "
                  f"FP register allocation")
            print("check_d13_no_neon: PASS")
            return 0
        _err(f"check_d13_no_neon: FAIL - expected an FP/NEON-free pool, counted {total} FP/NEON "
             f"instruction(s) in {n_with}/{n_obj} object(s) under {ap}:")
        for obj in sorted(per):
            _dump_breakdown(len(per[obj]), per[obj], obj)
        _err("  => the soft-float flag did not reach every object of this pool")
        return 1
    # expect == "any"
    if total > 0:
        print(f"  ok   {total} FP/NEON instruction(s) across {n_with}/{n_obj} object(s) - the pool "
              f"still emits vector code (OFF arm)")
        print("check_d13_no_neon: PASS")
        return 0
    _err(f"check_d13_no_neon: FAIL - expected a non-empty FP/NEON census, counted 0 in {n_obj} "
         f"object(s) under {ap}")
    return 1


# ---------------------------------------------------------------------------------------------------
# self-test: build real fixtures with the host toolchain and assert the tool's VERDICTS (exit codes).
# ---------------------------------------------------------------------------------------------------

_FIXTURE = r"""
typedef struct { int a,b,c,d,e,f,g,h; } s32;
/* a 32-bit integer loop: auto-vectorizes to NEON under -mfloat-abi=softfp, and does not under
   -msoft-float.  Same source, same symbol name, two objects. */
void neon_sym(int *restrict d, const int *restrict a, const int *restrict b, int n)
{
    for (int i = 0; i < n; i++) d[i] = a[i] + b[i];
}
/* a second symbol that must stay FP-free in BOTH objects: proves the window is the SYMBOL's extent,
   not the whole file (a whole-file count would let neon_sym satisfy a check on fp_free_sym).
   Deliberately loop-free AND call-free: a plain scalar loop CAN auto-vectorize (measured - the first
   draft of this fixture was an `d[i]=i` loop and clang emitted 8 NEON instructions for it), so a
   counting loop is NOT a safe "FP-free" control.  Load-then-subtract cannot vectorize. */
int fp_free_sym(int *d, int n)
{
    return d[n] - d[0];
}
"""

_TARGET = "--target=armv7-unknown-netbsd-eabi"
_COMMON = ["-mcpu=cortex-a15", "-marm", "-O2", "-c"]
_HARD = ["-mfpu=neon-vfpv4", "-mfloat-abi=softfp"]
_SOFT = ["-msoft-float"]


def _capture(argv):
    """Run main(argv) capturing stdout/stderr; return (exit_code, stdout, stderr)."""
    import contextlib
    import io
    so, se = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(so), contextlib.redirect_stderr(se):
        rc = main(argv)
    return rc, so.getvalue(), se.getvalue()


def _selftest():
    clang = shutil.which("clang")
    if not clang:
        print("  FAIL selftest: no `clang` on PATH - cannot build fixtures")
        print("check_d13_no_neon: FAIL (self-test)")
        return 1
    fails = []
    tmp = tempfile.mkdtemp(prefix="check_no_neon_selftest.")
    try:
        def _cc(src_text, out, flags, name):
            """Compile `src_text` into `out`; return True on success, else record a failure."""
            sp = os.path.join(tmp, name + ".c")
            with open(sp, "w") as fh:
                fh.write(src_text)
            cmd = [clang, _TARGET] + _COMMON + list(flags) + ["-o", out, sp]
            p = subprocess.run(cmd, capture_output=True, text=True)
            if p.returncode != 0:
                fails.append(f"fixture compile of {name} failed ({' '.join(flags)}): "
                             f"{p.stderr.strip()}")
                return False
            return True

        src = os.path.join(tmp, "fx.c")
        with open(src, "w") as fh:
            fh.write(_FIXTURE)
        hard = os.path.join(tmp, "fx_hard.o")
        soft = os.path.join(tmp, "fx_soft.o")
        for out, flags in ((hard, _HARD), (soft, _SOFT)):
            cmd = [clang, _TARGET] + _COMMON + flags + ["-o", out, src]
            p = subprocess.run(cmd, capture_output=True, text=True)
            if p.returncode != 0:
                fails.append(f"fixture compile failed ({' '.join(flags)}): {p.stderr.strip()}")

        # A LINKED object: a vectorizing TU immediately followed by an FP-free TU.  This is the
        # NEIGHBOUR test - symbol mode must count ONLY the asked-for symbol's extent, so an
        # `fp_free_sym` check must not be satisfied by the vector code in its adjacent neighbour
        # (the `mach_msg_send`/`mach_msg_trap` hazard the design names in §5.1).
        neon_tu = ("void neon_sym(int *restrict d, const int *restrict a, const int *restrict b, "
                   "int n){for(int i=0;i<n;i++)d[i]=a[i]+b[i];}\n")
        fpfree_tu = "int fp_free_sym(int *d, int n){return d[n]-d[0];}\n"
        linked = os.path.join(tmp, "linked.o")
        ok_lnk = _cc(neon_tu, os.path.join(tmp, "tup_a.o"), _HARD, "tup_a")
        ok_lnk = _cc(fpfree_tu, os.path.join(tmp, "tup_b.o"), _HARD, "tup_b") and ok_lnk
        if ok_lnk:
            p = subprocess.run(["arm-none-eabi-ld", "-r", os.path.join(tmp, "tup_a.o"),
                                os.path.join(tmp, "tup_b.o"), "-o", linked],
                               capture_output=True, text=True)
            if p.returncode != 0:
                fails.append(f"`arm-none-eabi-ld -r` failed building the neighbour fixture: "
                             f"{p.stderr.strip()}")
                ok_lnk = False

        if fails:
            for f in fails:
                print("  FAIL selftest: " + f)
            print("check_d13_no_neon: FAIL (self-test)")
            return 1
        print(f"  ok   fixtures built: {hard} (hard-float), {soft} (soft-float), "
              f"{linked} (linked neighbour)")

        # EMPTY-WINDOW fixture: a resolved, non-zero-size symbol whose address range lies outside
        # `.text`, so nothing disassembles there.  Symbol mode must REFUSE, not report "0 FP, clean"
        # (docs/1021c §5.3 - a check that read nothing is not a check).
        data_sym = os.path.join(tmp, "data_sym.o")
        _cc("int fpfree_data = 7;\nint fpfree_fn(int *d, int n){return d[n]-d[0]+fpfree_data;}\n",
            data_sym, _HARD, "data_sym")

        # pool dirs: one with the vectorizing object, one with the FP-free object.
        pool_hard = os.path.join(tmp, "pool_hard")
        pool_soft = os.path.join(tmp, "pool_soft")
        pool_empty = os.path.join(tmp, "pool_empty")
        for d in (pool_hard, pool_soft, pool_empty):
            os.makedirs(d)
        shutil.copy(hard, os.path.join(pool_hard, "a.o"))
        shutil.copy(soft, os.path.join(pool_soft, "a.o"))

        # (argv-tail, expected_exit, description)
        cases = [
            ([hard, "--symbol", "neon_sym", "--expect", "any"], 0,
             "symbol/any on the vectorizing object PASSES"),
            ([hard, "--symbol", "neon_sym", "--expect", "none"], 1,
             "symbol/none on the vectorizing object FAILS"),
            ([soft, "--symbol", "neon_sym", "--expect", "none"], 0,
             "symbol/none on the soft-float object PASSES"),
            ([soft, "--symbol", "neon_sym", "--expect", "any"], 1,
             "symbol/any on the soft-float object FAILS"),
            ([hard, "--symbol", "fp_free_sym", "--expect", "none"], 0,
             "symbol/none on an FP-free symbol in the SAME object PASSES "
             "(the window is the symbol's extent, not the file)"),
            ([hard, "--symbol", "no_such_symbol", "--expect", "none"], 1,
             "a bogus symbol FAILS (must refuse, not pass vacuously)"),
            ([hard, "--symbol", "no_such_symbol", "--expect", "any"], 1,
             "a bogus symbol FAILS in BOTH directions"),
            ([linked, "--symbol", "fp_free_sym", "--expect", "none"], 0,
             "SYMBOL WINDOW: fp_free_sym in a LINKED object with a vectorizing neighbour PASSES "
             "(the neighbour's vector code did not leak into the window)"),
            ([linked, "--symbol", "neon_sym", "--expect", "any"], 0,
             "SYMBOL WINDOW: neon_sym in the same linked object still counts its OWN vector code"),
            ([linked, "--pool", os.path.join(tmp, "does_not_exist"), "--expect", "none"], 1,
             "REFUSAL: an unresolvable --pool path FAILS"),
            ([data_sym, "--symbol", "fpfree_data", "--expect", "none"], 1,
             "REFUSAL: a resolved symbol whose range disassembles to NOTHING FAILS "
             "(no '0 FP, therefore clean' on a range never read)"),
            ([data_sym, "--symbol", "fpfree_fn", "--expect", "none"], 0,
             "the real function in that same object still checks normally"),
            (["--pool", pool_hard, "--expect", "any"], 0,
             "pool/any over the vectorizing object PASSES"),
            (["--pool", pool_hard, "--expect", "none", "--min-objects", "1"], 1,
             "pool/none over the vectorizing object FAILS"),
            (["--pool", pool_soft, "--expect", "none", "--min-objects", "1"], 0,
             "pool/none over the soft-float object PASSES"),
            (["--pool", pool_empty, "--expect", "none"], 1,
             "an EMPTY pool directory FAILS (anti-vacuous-pass)"),
            (["--pool", pool_empty, "--expect", "any"], 1,
             "an EMPTY pool directory FAILS in the any direction too"),
            (["--pool", os.path.join(tmp, "does_not_exist"), "--expect", "none"], 1,
             "a non-existent pool directory FAILS"),
            (["--pool", pool_soft, "--expect", "none"], 1,
             "pool/none with the DEFAULT --min-objects floor FAILS on a 1-object dir "
             "(the stale-directory guard bites)"),
        ]
        for tail, want, desc in cases:
            rc, out, err = _capture(tail)
            if rc != want:
                fails.append(f"{desc}: exit {rc}, expected {want}\n    stdout: {out.strip()}"
                             f"\n    stderr: {err.strip()}")
            else:
                print(f"  ok   {desc} (exit {rc})")

        # the accept-list must be able to FAIL: a §-mnemonic that is neither v nor f is not FP, and an
        # unknown `v`/`f` mnemonic must REFUSE rather than pass.
        try:
            classify("vhypothetical_new_neon_op")
            fails.append("an unrecognised `v*` mnemonic did NOT refuse (it passed as 'not FP')")
        except Refuse:
            print("  ok   an unrecognised `v*` mnemonic REFUSES (cannot pass as 'not FP')")
        try:
            if classify("mov") or classify("ldr") or classify("b"):
                fails.append("a general-purpose mnemonic was classified as FP")
            else:
                print("  ok   general-purpose mnemonics (mov/ldr/b) classify as not-FP")
        except Refuse as exc:
            fails.append(f"a general-purpose mnemonic refused: {exc}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if fails:
        for f in fails:
            print("  FAIL selftest: " + f)
        print("check_d13_no_neon: FAIL (self-test)")
        return 1
    print("check_d13_no_neon: PASS (self-test)")
    return 0


# ---------------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------------

_USAGE = ("usage: check_d13_no_neon.py <elf-or-obj> --symbol <name> --expect none|any\n"
          "       check_d13_no_neon.py --pool <object-dir> --expect none|any [--min-objects N]\n"
          "       check_d13_no_neon.py --selftest")


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    path = None
    symbol = None
    pool = None
    expect = None
    min_objects = DEFAULT_MIN_OBJECTS
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--symbol":
            i += 1
            if i >= len(argv):
                _err(_USAGE)
                return 2
            symbol = argv[i]
        elif a == "--pool":
            i += 1
            if i >= len(argv):
                _err(_USAGE)
                return 2
            pool = argv[i]
        elif a == "--expect":
            i += 1
            if i >= len(argv):
                _err(_USAGE)
                return 2
            expect = argv[i]
        elif a == "--min-objects":
            i += 1
            if i >= len(argv):
                _err(_USAGE)
                return 2
            try:
                min_objects = int(argv[i])
            except ValueError:
                _err(f"REFUSING: --min-objects '{argv[i]}' is not an integer")
                return 2
        elif a == "--selftest":
            return _selftest()
        elif a.startswith("--"):
            _err(f"REFUSING: unknown option '{a}'")
            _err(_USAGE)
            return 2
        else:
            path = a
        i += 1

    if expect not in ("none", "any"):
        _err(f"REFUSING: --expect must be 'none' or 'any' (got {expect!r})")
        _err(_USAGE)
        return 2
    if pool and symbol:
        _err("REFUSING: --pool and --symbol are mutually exclusive")
        _err(_USAGE)
        return 2
    if pool:
        return do_pool(pool, expect, min_objects)
    if symbol:
        if path is None:
            _err("REFUSING: --symbol needs a file operand")
            _err(_USAGE)
            return 2
        return do_symbol(path, symbol, expect)
    _err(_USAGE)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())