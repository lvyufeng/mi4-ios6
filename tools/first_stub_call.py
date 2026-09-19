#!/usr/bin/env python3
"""
The first `bl` in one function's straight line that lands on a stub, in execution order.

    ./tools/first_stub_call.py <function> [--image <elf>] [--from <offset>]

`tools/xnu_entry_callwalk.py` answers the transitive question ("what is the first stub reachable
from here, and how is everything on the path reached"); this answers the **local, ordered** one:
reading the function's own disassembly from its entry, which `bl` is the first to a name the image
only stubs. That is the question a stop prediction actually needs when the function is one the walk
has already entered - the answer is not the closure's answer, because the closure walks callees the
straight line never reaches.

Stubs are identified **by name**, against the set of `T` symbols in the generated
`xnu_arm_entry_realstubs.o`: that object defines exactly one function per stub, so its symbol list
*is* the stub list, and a `bl` whose target carries one of those names is a stub while every other
name in the image is real. That is 342's rule - a name is a stub by its definition, not by its
address range.

**And it is by name rather than by address for a reason the first version of this file got wrong.**
The first version read `nm` on `realstubs.o` into an `{address: name}` table and compared that
against the addresses in the *linked image's* disassembly - two different address spaces, since the
generated object is unlinked and starts at 0. Nothing ever matched, and the tool reported "no stub
call anywhere on the straight line" for a function whose very next call was a stub. It took a
cross-check against `xnu_arm_entry_stubnames.txt` to see it. Comparing the *names* removes the
address spaces from the question entirely.

`--from <offset>` starts the scan at a byte offset into the function, which is how a step whose
previous run stopped inside this function reads "the next stub after where I am".
"""
import argparse
import re
import subprocess
import sys

OBJDUMP = 'arm-none-eabi-objdump'
NM = 'arm-none-eabi-nm'
DEFAULT_IMAGE = 'out/stage90/xnu_arm_entry.elf'
DEFAULT_REALSTUBS = 'out/stage90/xnu_arm_entry_realstubs.o'


def stub_names(realstubs):
    """The set of function stub names: every `T` symbol the generated stub object defines."""
    out = subprocess.run([NM, '--defined-only', realstubs], capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] == 'T':
            names.add(f[2])
    return names


def function_body(image, name):
    """[(address, instruction, target)] for `name`, stopping at the next symbol."""
    out = subprocess.run([OBJDUMP, '-d', '--section=.text', image],
                         capture_output=True, text=True).stdout
    lines = out.splitlines()
    start = None
    for i, l in enumerate(lines):
        if l.endswith('<%s>:' % name):
            start = i
            break
    if start is None:
        sys.exit('%s: no symbol %r in %s' % (image, name, image))
    body = []
    for l in lines[start + 1:]:
        if re.match(r'^[0-9a-f]{8} <', l):
            break
        m = re.match(r'\s*([0-9a-f]+):\t[0-9a-f ]+\t(.*)$', l)
        if m:
            body.append((int(m.group(1), 16), m.group(2).strip()))
    return body


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('function')
    ap.add_argument('--image', default=DEFAULT_IMAGE)
    ap.add_argument('--realstubs', default=DEFAULT_REALSTUBS)
    ap.add_argument('--from', dest='from_off', type=lambda s: int(s, 0), default=0,
                    help='byte offset into the function to start the scan at')
    args = ap.parse_args()

    stubs = stub_names(args.realstubs)
    body = function_body(args.image, args.function)
    if not body:
        sys.exit('%s: %s has no disassembly' % (args.image, args.function))
    base = body[0][0]

    print('%s: %d instructions, 0x%08X..0x%08X' % (args.function, len(body), base, body[-1][0]))
    calls = []
    for addr, insn in body:
        m = re.match(r'bl\s+([0-9a-f]+)\s+<([^>]+)>', insn)
        if m:
            calls.append((addr, int(m.group(1), 16), m.group(2)))
    print('%d direct calls' % len(calls))
    shown = 0
    for addr, target, named in calls:
        off = addr - base
        if off < args.from_off:
            continue
        if named in stubs:
            print('first stub call: +0x%X  %s  (stub defined by the generated stub object)'
                  % (off, named))
            print('  the run should stop with stub_hit=%s and the caller key at +0x%X' % (named, off + 4))
            return
        shown += 1
        if shown <= 6:
            print('  +0x%-5X %-58s real' % (off, named))
    print('no stub call anywhere on the straight line from +0x%X' % args.from_off)


if __name__ == '__main__':
    main()
