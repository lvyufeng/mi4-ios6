#!/usr/bin/env python3
"""
One object's section sizes, parsed so that a section name cannot be mistaken for a field.

    ./tools/obj_sections.py <object.o> [<object.o> ...]

**Why this file exists (experiment 339).** The object table for 339 was built with
`objdump -h | awk 'NR>4{print $2,$3,$4}'` and a loop that required exactly three fields and took
the first as the section name. That works for `.text 00000eec 00000000 00000000 00000034 2**2`, and
it fails for `__DATA, __data 00000060 00000000 00000000 00001058 2**3` - a Mach-O-style name
contains a space, so every field shifts by one, the "size" a fixed-field parse reads is the word
`__data`, and `int('__data', 16)` raises into an `except: continue` that *skips the section rather
than the line*. The table came out 0x60 short with no sign anything was missing, and the prediction
written from it said "this object brings no `.data` at all". One dropped row moved four addresses
in the linked image (`.data`, `.sysctl_set`, `.init_array`, `.bss`).

That is the same defect class as the stub-name list's two record shapes (331): **a parser that
drops a whole record class cannot report that it did.** So this file does the two things that make
a parse reportable:

  * the section line is matched **anchored at the end**, where the five fields that cannot move
    (size, VMA, LMA, file offset, alignment) always sit, and the name is whatever is left over -
    so a name with a space, a comma or a quote is one field, not three;
  * every non-blank body line must parse, and the **count of sections parsed is printed against
    the count the header implies**, so a line the pattern does not match is an error here rather
    than a missing term in someone's arithmetic later.

`.text.___*` and `.text._Z*` COMDAT sections are summed under one `COMDAT` key, because that is how
the walk's `.text` table has to count them; every other section keeps its own name.
"""
import re
import subprocess
import sys

OBJDUMP = 'arm-none-eabi-objdump'

# Anchored at the end: size, VMA, LMA, file offset, alignment. The name is everything between the
# leading index and the size, so names containing spaces (Mach-O's `__DATA, __data`) come through
# whole.
SECTION_RE = re.compile(
    r'^\s*\d+\s+(?P<name>.+?)\s+'
    r'(?P<size>[0-9a-f]{8})\s+(?P<vma>[0-9a-f]{8})\s+(?P<lma>[0-9a-f]{8})\s+'
    r'(?P<off>[0-9a-f]{8})\s+2\*\*\d+\s*$')

SECOND_LINE_RE = re.compile(r'^\s+(CONTENTS|ALLOC|LOAD|READONLY|RELOC|CODE|DATA|DEBUGGING|'
                            r'GROUP|LINK_ONCE_DISCARD|NOTE|TLS|MERGE|STRINGS|EXCLUDE|LARGE|'
                            r'NEVER_LOAD)(,|\s|$)')


def sections(path):
    """[(name, size)] in file order, or exit(1) naming the line that did not parse."""
    out = subprocess.run([OBJDUMP, '-h', path], capture_output=True, text=True).stdout
    lines = out.splitlines()
    try:
        header = next(i for i, l in enumerate(lines) if l.strip().startswith('Idx'))
    except StopIteration:
        sys.exit('%s: no section header line from %s -h' % (path, OBJDUMP))

    body = lines[header + 1:]
    # A section is a matching line optionally followed by its flags/attribute lines. Count what the
    # table claims first, so an unparsed line is a difference and not a silence.
    claimed = 0
    parsed = []
    for lineno, line in enumerate(body, header + 2):
        if not line.strip():
            continue
        if SECOND_LINE_RE.match(line):
            continue
        m = SECTION_RE.match(line)
        if not m:
            sys.exit('%s:%d: section line did not parse - %r\n'
                     '  (the pattern is end-anchored; a name may contain spaces, a size may not)'
                     % (path, lineno, line.rstrip()))
        claimed += 1
        parsed.append((m.group('name'), int(m.group('size'), 16)))

    # The table's own numbering is the count to check against: the last index printed plus one.
    indices = [int(l.split()[0]) for l in body if l.split() and l.split()[0].isdigit()]
    if indices:
        expected = max(indices) + 1
        if claimed != expected:
            sys.exit('%s: parsed %d sections but the table numbers %d (0..%d) - a line was taken '
                     'for flags or missed' % (path, claimed, expected, max(indices)))
    return parsed


def summarise(secs):
    """{name: bytes}, with every `.text._Z*`/`.text.___*` COMDAT folded into one `COMDAT` key."""
    totals = {}
    for name, size in secs:
        key = 'COMDAT' if re.match(r'^\.text\.(_Z|___)', name) else name
        totals[key] = totals.get(key, 0) + size
    return totals


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        secs = sections(path)
        totals = summarise(secs)
        print('== %s  (%d sections)' % (path, len(secs)))
        for key, size in totals.items():
            if size:
                print('   %-28s 0x%-8X (%d)' % (key, size, size))
        print()


if __name__ == '__main__':
    main()
