#!/usr/bin/env python3
"""
Every place a function loads a **storage stand-in**'s address, and what it does with it.

    ./tools/standin_value_loads.py <function> [--image <elf>] [--realstubs <o>] [--from <off>]

`tools/first_stub_call.py` answers "is this `bl` real". This answers the *other* question a stop
prediction needs, and it is the one that stopped experiment 340: **the symbol is in the image, and
what it holds is zero.** A generated storage stand-in is a 4-byte (or n-byte) `.bss` word whose
address is right and whose contents are nothing, so

  * a load *through* the stand-in's address reads zeros and usually "works", which is how the
    hazard stays invisible for so long;
  * a load *of* its contents followed by a use of the loaded register as an address dereferences
    zero and takes a `data abort` with `dfar = 0`. That is 340: `ldr r5, [r1]` where
    `r1 = kOSBooleanTrue`, then `ldr r2, [r5]`. It is also 332's prediction and 331's
    `sKextLock` (a `ldr r5, [r0, #12]` with `r0 = 0` one level down).

The set of stand-ins is the 108 `B` symbols of the generated `xnu_arm_entry_realstubs.o`: that
object defines one `.bss` word per missing *storage* symbol and one `.text` body per missing
*function*, so its `B` list **is** the stand-in list and its `T` list is the stub list. Addresses
come from the linked image's own symbol table, by name - a stand-in keeps its name in the image.

Reported per site: the four instructions that form the address, what uses the register, and a
verdict -

    worth-reading   the address is loaded into a register that is then used as the *address* of a
                    load: whatever the stand-in holds is dereferenced (340's class, guaranteed
                    fault when it is zero, silent skip when it is not - 211's class)
    read-through    the stand-in's own bytes are read (invisible; the value is whatever the boot
                    wrote, usually zero)
    write           the stand-in is stored into (the value arrives at run time - 320's class)
    address-only    the register is compared, moved or passed but not dereferenced here

and the tool prints the count of `movw` immediates it could not pair with a `movt`, so a site that
is skipped because the constant was formed across a branch is a number rather than a silence.
"""
import argparse
import re
import subprocess
import sys

OBJDUMP = 'arm-none-eabi-objdump'
NM = 'arm-none-eabi-nm'
DEFAULT_IMAGE = 'out/stage90/xnu_arm_entry.elf'
DEFAULT_REALSTUBS = 'out/stage90/xnu_arm_entry_realstubs.o'

LOAD_STORE_RE = re.compile(r'^(ldr|ldrb|ldrh|ldrsb|ldrsh|str|strb|strh)\s+(r\d+|sp|fp|ip|lr|pc)\s*,\s*(\[.*)$')
IMM_PAIR_RE = re.compile(r'^(movw|movt)\s+(r\d+)\s*,\s*#(\d+)$')
USE_AS_ADDR_RE = re.compile(r'\[(r\d+)(,\s*#[-0-9a-fx]+)?\]')


def command(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def standin_names(realstubs):
    """The 108 storage stand-in names: the `B` symbols of the generated stub object."""
    names = set()
    for line in command([NM, '--defined-only', realstubs]).splitlines():
        f = line.split()
        if len(f) == 3 and f[1] == 'B':
            names.add(f[2])
    return names


def standin_addrs(image, names):
    """{address: name} for those names, in the image the device will actually run."""
    found = {}
    for line in command([NM, '-n', image]).splitlines():
        f = line.split()
        if len(f) == 3 and f[1] == 'B' and f[2] in names:
            found[int(f[0], 16)] = f[2]
    return found


def function_body(image, name):
    """[(address, instruction)] for `name`, stopping at the next symbol."""
    lines = command([OBJDUMP, '-d', '--section=.text', image]).splitlines()
    start = None
    for i, l in enumerate(lines):
        if l.endswith('<%s>:' % name):
            start = i
            break
    if start is None:
        sys.exit('%s: no symbol %r' % (image, name))
    body = []
    for l in lines[start + 1:]:
        if re.match(r'^[0-9a-f]{8} <', l):
            break
        m = re.match(r'\s*([0-9a-f]+):\t[0-9a-f ]+\t(.*)$', l)
        if m:
            body.append((int(m.group(1), 16), m.group(2).strip()))
    return body


def writes_reg(clean, reg):
    """Does this instruction overwrite `reg`? (a store writes memory, not the register)"""
    if clean.startswith('pop'):
        return ('{%s' % reg) in clean.replace(', ', ', ') or ('%s,' % reg) in clean
    m = re.match(r'^(\w+)\s+(r\d+)\s*[,}]', clean)
    if not m:
        m = re.match(r'^(\w+)\s+(r\d+)$', clean)
        return bool(m) and m.group(2) == reg and m.group(1) in ('mov', 'movs')
    return m.group(2) == reg and m.group(1) not in ('cmp', 'tst', 'str', 'strb', 'strh')


def classify(body, i, addr_reg, from_off, base):
    """What happens to `addr_reg` after instruction `i`.

    The window is 20 instructions and it is only cut short by a write to the loaded register: the
    340 site dereferences the value **seven** instructions after loading it, across a `movw`/`movt`
    pair for a second address and a real `bl`, none of which touch the loaded register. The first
    version of this function stopped at the first `mov` or `bl` and reported `read-through` for the
    one site in the image it was written to find.
    """
    for k in range(i + 1, min(i + 9, len(body))):
        off, insn = body[k]
        clean = insn.split(';')[0].strip()
        m = LOAD_STORE_RE.match(clean)
        if m:
            op, rt, mem = m.group(1), m.group(2), m.group(3)
            inner = mem.split(']')[0].lstrip('[')
            regs = [r.split(',')[0].strip() for r in inner.split(',')]
            if regs and regs[0] == addr_reg:
                if op.startswith('str'):
                    return ('write', insn, off)
                # the stand-in's contents are now in `rt`; where does `rt` go?
                for j in range(k + 1, min(k + 20, len(body))):
                    off2, insn2 = body[j]
                    clean2 = insn2.split(';')[0].strip()
                    if re.search(r'\[%s(\]|,)' % rt, clean2):
                        return ('worth-reading', '%s   then  %s' % (insn, insn2), off2)
                    if writes_reg(clean2, rt):
                        return ('read-through', insn, off)
                return ('read-through', insn, off)
            if rt == addr_reg:
                return ('address-only', insn, off)
        if re.search(r'\[%s(\]|,)' % addr_reg, clean):
            return ('write' if clean.startswith('str') else 'read-through', insn, off)
        if writes_reg(clean, addr_reg):
            return ('address-only', insn, off)
    return ('address-only', '', 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('function')
    ap.add_argument('--image', default=DEFAULT_IMAGE)
    ap.add_argument('--realstubs', default=DEFAULT_REALSTUBS)
    ap.add_argument('--from', dest='from_off', type=lambda s: int(s, 0), default=0)
    args = ap.parse_args()

    standins = standin_addrs(args.image, standin_names(args.realstubs))
    body = function_body(args.image, args.function)
    if not body:
        sys.exit('%s: %s has no disassembly' % (args.image, args.function))
    base = body[0][0]

    # movw/movt pairs, in order, tracking which registers currently hold half a constant.
    # objdump writes the resolved value as a trailing comment on these two mnemonics
    # (`movw r1, #51520        ; 0xc940`), so the instruction is split at the `;` before it is
    # matched - the first version of this file matched the whole line and therefore admitted
    # **nothing at all**, reporting "0 sites" for a function whose fault site is one. The count of
    # immediates seen is printed for exactly that reason.
    pending = {}
    movw_seen = movt_seen = unpaired = 0
    sites = []
    for i, (addr, insn) in enumerate(body):
        clean = insn.split(';')[0].strip()
        m = IMM_PAIR_RE.match(clean)
        if m:
            kind, reg, val = m.group(1), m.group(2), int(m.group(3))
            if kind == 'movw':
                movw_seen += 1
                pending[reg] = val & 0xFFFF
            else:
                movt_seen += 1
                if reg in pending:
                    resolved = pending.pop(reg) | (val << 16)
                    if resolved in standins:
                        sites.append((addr, i, reg, resolved))
                else:
                    unpaired += 1
            continue
        # any other write to a register invalidates its half-constant
        m3 = re.match(r'^(\w+)\s+(r\d+)', clean)
        if m3 and m3.group(2) in pending and m3.group(1) != 'mov':
            pending.pop(m3.group(2), None)

    print('%s: %d instructions, 0x%08X..0x%08X' % (args.function, len(body), base, body[-1][0]))
    print('%d storage stand-ins in the image, %d sites reference one (from +0x%X)'
          % (len(standins), len(sites), args.from_off))
    print('%d movw / %d movt instructions read, %d movt with no tracked movw, '
          '%d movw never paired'
          % (movw_seen, movt_seen, unpaired, len(pending)))
    order = {'worth-reading': 0, 'read-through': 1, 'write': 2, 'address-only': 3}
    shown = []
    for addr, i, reg, resolved in sites:
        off = addr - base
        verdict, insn, effect = classify(body, i, reg, args.from_off, base)
        # `--from` filters on where the effect *lands*, not where the address is formed: a site
        # whose `ldr` is behind the cursor and whose dereference is ahead of it is exactly the
        # question "is there another hazard after where the run stopped".
        at = effect if effect else off
        if at < args.from_off:
            continue
        shown.append((order[verdict], at, resolved, reg, verdict, insn))
    for _, at, resolved, reg, verdict, insn in sorted(shown):
        print('  +0x%-6X 0x%08X %-22s %s' % (at, resolved, standins[resolved], reg))
        print('        %-14s %s' % (verdict, insn))
    if not shown:
        print('  none from +0x%X' % args.from_off)


if __name__ == '__main__':
    main()
