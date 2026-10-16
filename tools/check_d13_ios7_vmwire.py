#!/usr/bin/env python3
"""Assert BY VALUE that the linked image carries the iOS 7 mach-trap wire fix for traps -10/-12/-14.

Reads the section table (`arm-none-eabi-objdump -h`), the symbol table (`-t`) and the disassembly
(`-d`) of a linked image (default `out/stage90/xnu_arm_entry.elf`) - the code, by value, not any
source comment (`mi4-a-claim-in-a-comment-is-not-a-check`); and it reads the linked disassembly BY
VALUE rather than trusting a source shape (`mi4-linked-code-order-is-not-source-order`).

The defect it answers.  A mach trap's arguments are the SAVED-REGISTER FRAME.  iOS 7 ARMv7 dyld
passes every 64-bit address/size as a PAIR of 32-bit words, and its own MIG stubs say so by value:
`__kernelrpc_mach_vm_allocate_trap` (0x2be1b410) is `mov r12,sp; push {r4,r5}; ldr r4,[r12]` - it
lifts the 5th word into r4, so the frame is 5 words {target, addr, size_lo, size_hi, flags};
`__kernelrpc_mach_vm_deallocate_trap` (0x2be1b42c) lifts the 5th word the same way;
`__kernelrpc_mach_vm_protect_trap` (0x2be1b448) is `push {r4,r5,r6,r8}; ldm r12,{r4,r5,r6}` - 7
words.  This kernel's native structs are one word SHORT each (mach_vm_address_t/mach_vm_size_t are
uint32_t on !__LP64__, osfmk/mach/arm/vm_types.h:107-109), so the native bodies read the wire one
field early: trap -10's flags is really size_hi, trap -12's size is really addr_hi, trap -14's
set_maximum/new_protection are really size_lo/size_hi.  The pre-fix image proves it: trap -10 loads
flags from `[args,#12]` while the wire's flags sits at #16.

Checks (default = expect the switch-ON shape):
  1. `mach_trap_table` is in the image and entries 10/12/14 still carry {5,5,7} words and point at
     the REAL trap functions, whose name word still names them - the fix must not move the
     dispatch, only the bodies (`mi4-a-claim-in-a-comment-is-not-a-check`: the table is the claim).
  2. each trap routes through its 5/7-word adapter: a `bl`/tail-`b` to an adapter FUNCTION symbol,
     or - when the compiler inlined the static adapter (one call site each on CANCRO) - the trap
     body itself carries the wire read.  "The adapter" is judged by a disassembly body, never by a
     name match alone: the statics' `static unsigned int logged` counters link as `....logged` DATA
     symbols in .bss and must not be mistaken for the adapter (that false positive failed the real
     994 image once).
  3. the adapter reads the wire's EXTRA word through the args base register (r0 at entry or a copy
     of it), i.e. an offset the native struct cannot have: #16 for trap -10 (flags, the 5th word),
     #16 for trap -12 (size_high, the 5th word), #24 for trap -14 (new_protection, the 7th word).
     This is the by-value proof the 5/7-word frame is in play: a native body's largest read is
     #12 / #8 / #16 respectively.
  4. the three report strings exist in the image (`IOS7LAB vm_allocate64 `, `IOS7LAB vm_deallocate64 `,
     `IOS7LAB vm_protect64 `) - the press's only reading of which wire was parsed.
  5. (info) whether the native `bl mach_vm_*` call survives in each body; a folded-away native path
     is a stronger image, not a weaker one, so this is printed and never checked.
  6. the DEFERRAL, by value: traps 15/24/25/41/42 (`ios7lab_mach_vm_map_trap` and the port
     construct/destruct/guard/unguard labs) are NOT opened by this rung.  Their `mach_trap_table`
     entries must still say `kern_invalid`, keyed on entry 0's own {fn, name} (both rows are
     `MACH_TRAP(kern_invalid, 0, NULL, NULL)`) - no symbol name is trusted.  Runs in BOTH modes:
     the widening is one file-local line in mach_kernelrpc.c and syscall_sw.c is untouched, so the
     five rows stay the `#else` branch of `#if IOS7_COMPAT_PLATFORM` (0 on CANCRO) in both states.

`--off` is the rehearsal against an image built WITHOUT the switch: it asserts the mirror image -
no adapter FUNCTION body, none of the three strings, and the DEFECT offsets by value (trap -10 reads #12
and never #16; trap -12 reads #8 and never #16; trap -14 reads #16 and never #24).  The two modes
assert disjoint offsets, so a build left OFF fails the ON mode and a build left ON fails OFF.  The
five-traps-still-kern_invalid check (6) and the table check (1) are mode-independent.
Exit 0 iff every check in the selected mode passes.
"""
import os
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"

# (trap number, trap fn symbol, arg count, adapter symbol, adapter report string, ON extra-word
#  offset, OFF largest-word offset, native callee)
TRAPS = (
    dict(no=10, fn="_kernelrpc_mach_vm_allocate_trap", argc=5, adapter="ios7lab_vm_allocate",
         marker=b"IOS7LAB vm_allocate64 ", on_off=16, off_off=12, native="mach_vm_allocate",
         on_word="flags (the 5th wire word)", off_word="size_high - read AS flags"),
    dict(no=12, fn="_kernelrpc_mach_vm_deallocate_trap", argc=5, adapter="ios7lab_vm_deallocate",
         marker=b"IOS7LAB vm_deallocate64 ", on_off=16, off_off=8, native="mach_vm_deallocate",
         on_word="size_high (the 5th wire word)", off_word="address_high - read AS size"),
    dict(no=14, fn="_kernelrpc_mach_vm_protect_trap", argc=7, adapter="ios7lab_vm_protect",
         marker=b"IOS7LAB vm_protect64 ", on_off=24, off_off=16, native="mach_vm_protect",
         on_word="new_protection (the 7th wire word)",
         off_word="size_high - read AS new_protection"),
)

TRAP_TABLE = "mach_trap_table"
ENTRY_BYTES = 12          # {int arg_count; fn; const char *name} on this 32-bit build

# The traps this rung deliberately does NOT open (rule: traps 10/12/14 only; no evidence on the
# measured crash path touches them and their bodies are LEO lab reimplementations with deliberate
# isolation/QoS bypass).  They are `#if IOS7_COMPAT_PLATFORM / #else kern_invalid` rows in
# osfmk/kern/syscall_sw.c:126-170; the macro is 0 on CANCRO and this fix never touches it, so
# they stay kern_invalid in BOTH switch states.  Keyed on table entry 0 (also kern_invalid), not on
# a symbol name.
# **Trap 15 was OPENED BY RUNG 1022 and trap 24 BY RUNG 1023** (docs/1022-mach-trap-15-design.md,
# docs/1023-port-construct-trap24-design.md): row 15 now has a switch-ON arm
# `MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL)` (`syscall_sw.c:126`) and row 24 a switch-ON arm
# `MACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL)` (`:139`), so check 6 below must NOT
# require either to stay kern_invalid.  The other three (25/41/42) keep their bare `#if` (row 25's is a
# SEPARATE gate at `:144`, rows 41/42 share a third at `:164`) and this check still holds for them.
# Reading a row in BOTH directions now lives in the rung's own tool:
# `tools/check_1022_trap15.py` / `tools/check_1023_trap24.py` (`--expect on|off`).
DEFERRED_TRAPS = (25, 41, 42)

_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t([a-z0-9.]+)\s*(.*)$")
_SECTION = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)"
                      r"\s+2\*\*\d+\s*$")
_MOV_REG = re.compile(r"^movs?\s+(r\d+),\s*(r\d+)$")
_ADD_IMM = re.compile(r"^adds?\s+(r\d+),\s*(r\d+),\s*#(-?(?:0x[0-9a-fA-F]+|\d+))$")
_LDR_OFF = re.compile(r"^ldr\s+(r\d+),\s*\[(r\d+|sp)(?:,\s*#(-?(?:0x[0-9a-fA-F]+|\d+)))?\]$")
_STR_SP = re.compile(r"^str\s+(r\d+),\s*\[sp(?:,\s*#(-?(?:0x[0-9a-fA-F]+|\d+)))?\]$")
_LDR_SP = re.compile(r"^ldr\s+(r\d+),\s*\[sp(?:,\s*#(-?(?:0x[0-9a-fA-F]+|\d+)))?\]$")
_LDM = re.compile(r"^ldm(?:ia|ib|da|db|fd|ed|fa|ea)?\s+(r\d+)!?,\s*\{([^}]*)\}$")
_CALL = re.compile(r"^(bl|blx|b|bx)\b")
_TRAPFN = re.compile(r"^_(kernelrpc_\w+_trap|kernelrpc_\w+)$")


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
    """{name: (va, size)} for defined symbols (skips $mapping symbols and undefined ones).

    `objdump -t` pads the flag column, so field position is not fixed:
    `80477b64 g     F .text\t00000098 _kernelrpc_mach_vm_allocate_trap` is 6 fields, `8001b3dc l
    .text\t00000000 L_mach_trap_table` is 5.  The tail is stable - section, size, name - and
    undefined symbols carry no address.
    """
    out, globals_ = {}, {}
    for line in objdump(["-t", path]).splitlines():
        t = line.split()
        if len(t) < 4 or not re.fullmatch(r"[0-9a-f]{8,16}", t[0]):
            continue
        section, size, name = t[-3], t[-2], t[-1]
        if section == "*UND*" or name.startswith("$") or not re.fullmatch(r"[0-9a-f]{8,16}", size):
            continue
        entry = (int(t[0], 16), int(size, 16))
        (globals_ if "g" in " ".join(t[1:-3]) else out).setdefault(name, entry)
    out.update(globals_)        # a global definition wins over a same-named local stub
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


def read_cstr(path, secs, va, limit=96):
    """The NUL-terminated string at `va`, or None."""
    with open(path, "rb") as fh:
        blob = fh.read()
    for name, vma, size, off in secs:
        if vma <= va < vma + size:
            at = off + (va - vma)
            end = blob.find(b"\0", at, at + limit)
            if end < 0:
                return None
            return blob[at:end].decode("latin-1")
    return None


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
    m = re.search(r"#(-?(?:0x[0-9a-fA-F]+|\d+))", ops)
    if not m:
        return None
    return int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))


def off_of(text):
    v = parse_imm(text) if text else 0
    return 0 if v is None else v


def adapter_function(funcs, syms, name):
    """The adapter's FUNCTION symbol, or None if the adapter has no body in this image.

    A `name.suffix` match can be a DATA symbol: each adapter's `static unsigned int logged` counter
    links as `ios7lab_vm_allocate.logged` in .bss (`.bss` at 0x8085da08 in the 994 image), and on
    CANCRO each adapter has one call site so GCC inlines all three.  Presence in `funcs` - a label
    with a disassembly body in an executable section - is the operable "this is code" test
    (`mi4-a-stand-in-can-be-the-right-size-and-the-wrong-value`: a symbol name is not a function).
    """
    for n in syms:
        if (n == name or n.startswith(name + ".")) and n in funcs:
            return n
    return None


def calls_to(body, name, tail=False):
    """[va] of every bl/blx in `body` whose target symbol is `name` (or a `.suffix` of it).

    `tail=True` also accepts `b`/`bx`: a tail branch to the adapter routes through it just as a call
    does (and a compiler is free to emit one, since the adapter's value is returned unchanged).
    """
    out = []
    for va, mn, ops in body:
        if mn not in (("bl", "blx", "b", "bx") if tail else ("bl", "blx")):
            continue
        tail = ops.split("<", 1)[-1].rstrip(">")
        if tail == name or tail.startswith(name + "."):
            out.append(va)
    return out


def frame_word_reads(body):
    """{offset: [(va, base)]} - every word of the saved-register frame read through the args base.

    The args pointer arrives in r0 (traps_lo.s passes the saved-state pointer there and the trap
    table's mungers are ignored on __arm__).  This walk tracks which registers hold that pointer:
    `mov rD, rS` copies the property, a reload from a spill slot that holds it re-acquires it, and a
    `bl`/`blx` DROPS it from r0-r3 (AAPCS clobbers them on every path).  `ldr`/`ldmXX` reads are
    recorded when their base carries the property, with the derived `add rD, rBase, #n` form folded
    in.

    Deliberately NOT a full kill-on-write dataflow: an immediate write or a `pop` in the body is not
    a kill (the tree's own trap -12 does `mov r5, #0x10000003` and `pop {r4,r5,fp,pc}` on the error
    path, BEFORE the `ldmib r5, {r1, r2}` the good path reaches - a linear kill would erase exactly
    the read this checker looks for).  The over-approximation can only ADD bases, and the one false
    positive it could create here is excluded by the reg-to-reg rule: in trap -14 `mov r4, r0` runs
    AFTER `bl port_name_to_task`, where r0 no longer carries the property, so the later
    `ldr r0, [r4, #24]` (r4 = task->map, by value at 80477c90) is not a frame read.
    """
    regs, derived, spilled, hits = {"r0"}, {}, {}, {}

    def drop(*rs):
        for r in rs:
            regs.discard(r)
            derived.pop(r, None)

    for va, mn, ops in body:
        text = f"{mn} {ops}".strip()
        if _CALL.match(text):
            drop("r0", "r1", "r2", "r3")
            continue
        m = _MOV_REG.match(text)
        if m:
            drop(m.group(1))
            if m.group(2) in regs:
                regs.add(m.group(1))
            continue
        m = _ADD_IMM.match(text)
        if m:
            dst, src, imm = m.group(1), m.group(2), off_of(",#" + m.group(3))
            if src in regs:
                derived[dst] = imm
            elif src in derived:
                derived[dst] = derived[src] + imm
            else:
                drop(dst)
            continue
        m = _LDM.match(text)
        if m:
            base = m.group(1)
            lst = [r.strip() for r in m.group(2).split(",")]
            if base in regs or base in derived:
                start = 4 if "ib" in mn else 0
                for i, _r in enumerate(lst):
                    hits.setdefault(derived.get(base, 0) + start + 4 * i, []).append((va, base))
            drop(*lst)              # the ldm writes every listed register, base included
            continue
        m = _LDR_OFF.match(text)
        if m:
            dst, base = m.group(1), m.group(2)
            off = off_of((",#" + m.group(3)) if m.group(3) else "")
            if base in regs:
                hits.setdefault(off, []).append((va, base))
            elif base in derived:
                hits.setdefault(derived[base] + off, []).append((va, f"{base}+{derived[base]}"))
            drop(dst)
            continue
        m = _LDR_SP.match(text)
        if m:
            slot = off_of((",#" + m.group(2)) if m.group(2) else "")
            drop(m.group(1))
            if spilled.get(slot):
                regs.add(m.group(1))
            continue
        m = _STR_SP.match(text)
        if m:
            slot = off_of((",#" + m.group(2)) if m.group(2) else "")
            if m.group(1) in regs or m.group(1) in derived:
                spilled[slot] = True
            else:
                spilled.pop(slot, None)
            continue
    return hits


def main() -> int:
    argv = sys.argv[1:]
    expect_on = "--off" not in argv
    argv = [a for a in argv if a != "--off"]
    path = argv[0] if argv else "out/stage90/xnu_arm_entry.elf"
    print(f"check_d13_ios7_vmwire: {path} (expecting switch {'ON' if expect_on else 'OFF'})")
    if not os.path.isfile(path):
        print("  FAIL no such file - nothing to check")
        print("check_d13_ios7_vmwire: FAIL")
        return 2
    try:
        secs = sections(path)
        syms = symbols(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read it: {exc}")
        print("check_d13_ios7_vmwire: FAIL")
        return 2
    read_u32 = make_u32_reader(path, secs)
    ok = True

    # ---- 1. the dispatch table still points at the real trap functions
    table_va = syms.get(TRAP_TABLE, (None, 0))[0]
    if table_va is None:
        print(f"  FAIL {TRAP_TABLE} is not a symbol in this image - wrong image?")
        ok = False
    for spec in TRAPS:
        fn_va = syms.get(spec["fn"], (None, 0))[0]
        if fn_va is None:
            print(f"  FAIL trap {spec['no']}: {spec['fn']} is not a defined symbol - the trap body "
                  f"this fix patches is not in this image")
            ok = False
            continue
        if table_va is None:
            continue
        base = table_va + spec["no"] * ENTRY_BYTES
        argc, fn, name_ptr = read_u32(base), read_u32(base + 4), read_u32(base + 8)
        name = read_cstr(path, secs, name_ptr) if name_ptr else None
        if argc != spec["argc"] or fn != fn_va or name != spec["fn"]:
            print(f"  FAIL trap {spec['no']}: {TRAP_TABLE} entry is "
                  f"{{arg_count={argc}, fn={fn and hex(fn)}, name={name!r}}}, expected "
                  f"{{{spec['argc']}, {fn_va:#x}, {spec['fn']!r}}} - the fix must move the body, "
                  f"never the dispatch")
            ok = False
        else:
            print(f"  ok   trap {spec['no']} -> {spec['fn']} (arg_count={argc}, name word intact)")

    # ---- 6. the deferral, by value: the five traps this rung does not open stay kern_invalid
    if table_va is not None:
        fn0, name0 = read_u32(table_va + 4), read_u32(table_va + 8)
        name0s = read_cstr(path, secs, name0) if name0 else None
        if name0s != "kern_invalid":
            print(f"  FAIL {TRAP_TABLE} entry 0 is {name0s!r}, expected 'kern_invalid' - cannot "
                  f"key the deferral check")
            ok = False
        else:
            opened = []
            for no in DEFERRED_TRAPS:
                b = table_va + no * ENTRY_BYTES
                fn, np = read_u32(b + 4), read_u32(b + 8)
                ns = read_cstr(path, secs, np) if np else None
                if (fn, ns) != (fn0, name0s):
                    opened.append(f"trap {no} -> {{fn={fn and hex(fn)}, name={ns!r}}}")
            if opened:
                print(f"  FAIL traps {'/'.join(str(n) for n in DEFERRED_TRAPS)} must stay "
                      f"kern_invalid (this rung opens 10/12/14 only; syscall_sw.c rows 15/24/25/"
                      f"41/42 are the `#else` of #if IOS7_COMPAT_PLATFORM): " + "; ".join(opened))
                ok = False
            else:
                print(f"  ok   traps {'/'.join(str(n) for n in DEFERRED_TRAPS)} are kern_invalid in "
                      f"{TRAP_TABLE} - the rung's deferral holds, by value")

    # ---- 2/3/5. each trap's body routes through the adapter and reads the wire at its real width
    for spec in TRAPS:
        body = funcs.get(spec["fn"])
        if body is None:
            print(f"  FAIL trap {spec['no']}: no disassembly for {spec['fn']} - the symbol exists "
                  f"but the function has no body")
            ok = False
            continue
        adapter_body = None
        if expect_on:
            sym = adapter_function(funcs, syms, spec["adapter"])
            if sym is not None:
                adapter_body = funcs.get(sym)
                if not calls_to(body, spec["adapter"], tail=True):
                    print(f"  FAIL trap {spec['no']}: {spec['fn']} never calls {spec['adapter']} "
                          f"(function symbol {sym} exists at {syms[sym][0]:#x}) - the trap is not "
                          f"routed through the 5/7-word adapter")
                    ok = False
                else:
                    print(f"  ok   trap {spec['no']}: {spec['fn']} routes to {spec['adapter']} at "
                          f"{', '.join(f'{v:#x}' for v in calls_to(body, spec['adapter'], tail=True))}")
            else:
                read = frame_word_reads(body)
                if spec["on_off"] not in read:
                    print(f"  FAIL trap {spec['no']}: no {spec['adapter']} function body AND no read "
                          f"of wire word #{spec['on_off']} in the trap body - the adapter was neither "
                          f"called nor inlined")
                    ok = False
                else:
                    print(f"  ok   trap {spec['no']}: {spec['adapter']} is INLINED (no function "
                          f"symbol; its .logged data symbol is not a body) - the trap body itself "
                          f"reads the wire at #{spec['on_off']}")
        # 3. the wire read, by value
        if expect_on:
            read = {}
            for cand, who in ((body, spec["fn"]), (adapter_body, spec["adapter"])):
                if cand is None:
                    continue
                for off, hits in frame_word_reads(cand).items():
                    for va, base in hits:
                        read.setdefault(off, []).append((va, base, who))
            if spec["on_off"] in read:
                va, base, who = read[spec["on_off"]][0]
                print(f"  ok   trap {spec['no']}: the wire is read at #{spec['on_off']} = "
                      f"{spec['on_word']} through the args base ({base}) in <{who}> at {va:#x}")
            else:
                print(f"  FAIL trap {spec['no']}: no read of wire word #{spec['on_off']} "
                      f"({spec['on_word']}) through the args base in {spec['fn']}"
                      f"{'/' + spec['adapter'] if adapter_body is not None else ''} - the pre-fix "
                      f"shape (largest read is #{spec['off_off']}) means the native struct is still "
                      f"what parses the wire")
                ok = False
        # 5. info only: does the native call survive?
        natives = calls_to(body, spec["native"]) + (calls_to(adapter_body, spec["native"]) if adapter_body else [])
        print(f"       (info: {len(natives)} `bl {spec['native']}` in "
              f"{spec['fn']}{'/' + spec['adapter'] if adapter_body is not None else ''}"
              f"{' - native path folded away' if not natives else ''})")

    # ---- 4. the report strings, and the OFF mirror
    for spec in TRAPS:
        found = find_string(path, secs, spec["marker"])
        if expect_on and found is None:
            print(f"  FAIL `{spec['marker'].decode()}` is not in the image - the switch did not reach "
                  f"mach_kernelrpc.c (built without -DSTAGE90_XNU_IOS7_TRAPS=1, or the trap is not "
                  f"routed) ; the press has no reading of which wire was parsed")
            ok = False
        elif expect_on:
            print(f"  ok   `{spec['marker'].decode()}` at {found[0]:#010x} in {found[1]}")
        elif found is not None:
            print(f"  FAIL `{spec['marker'].decode()}` IS in the image while expecting the switch "
                  f"OFF - the OFF image must carry the tree's original bytes")
            ok = False
    if not expect_on:
        for spec in TRAPS:
            sym = adapter_function(funcs, syms, spec["adapter"])
            if sym is not None:
                print(f"  FAIL {spec['adapter']} has a FUNCTION body in this image while expecting "
                      f"the switch OFF (symbol at {syms[sym][0]:#x}) - the fix is compiled in")
                ok = False
            body = funcs.get(spec["fn"]) or []
            read = frame_word_reads(body)
            if spec["off_off"] in read:
                va = read[spec["off_off"]][0][0]
                print(f"  ok   trap {spec['no']}: the native body reads wire word #{spec['off_off']} "
                      f"({spec['off_word']}) at {va:#x} - the pre-fix shape, by value")
            else:
                print(f"  FAIL trap {spec['no']}: no read of wire word #{spec['off_off']} through the "
                      f"args base - this is not the pre-fix image and not the fixed one either")
                ok = False
            if spec["on_off"] in read:
                print(f"  FAIL trap {spec['no']}: the body reads wire word #{spec['on_off']} "
                      f"(the fixed wire) while expecting the switch OFF")
                ok = False
            else:
                print(f"  ok   trap {spec['no']}: nothing reads #{spec['on_off']} - the fixed wire "
                      f"is not in this image")

    print(f"check_d13_ios7_vmwire: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())