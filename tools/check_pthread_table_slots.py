#!/usr/bin/env python3
"""
Every named word of `struct pthread_functions_s`, read out of the linked image and against Apple's own
header rather than against a copy of it.

Why this is a check and not a comment
-------------------------------------
This table is the one structure in the entry image that the kernel reads through *by word index*. The
shims in `bsd/kern/pthread_shims.c` are twenty bytes each - `movw`/`movt` of `pthread_functions`,
`ldr r1, [r1]`, `ldr r1, [r1, #N]`, `bx r1` - so a slot that points at the wrong body is neither a stop
nor a fault:

  - the NULL scan in `stages/stage90/xnu_supply/stage90_pthread_functions.c`'s constructor cannot see
    it, because two slots swapped are two non-NULL words;
  - and no device run can see it either, because the body that runs is a *real* body that names itself
    correctly - it just names the other slot. Experiment 465's own write-up is where this confusion is
    already on record: its text called `pth_proc_hashinit`'s slot "word 6" off the disassembly, and the
    identification came from `nm` instead. Word 6 is `workqueue_mark_exiting`.

473 is the step that made this checkable rather than argued: it retires two more slots by hand, so
there are four hand-written bodies and four designators to get right, and the file it edits is the one
Apple header's layout is transcribed into. So the check reads the 40 named words **out of the image** -
where the kernel will read them - and compares each against the symbol its member name implies.

Where each side of the comparison comes from
--------------------------------------------
  - the member names, their order, `_pad`'s length and `PTHREAD_FUNCTIONS_TABLE_VERSION` come from
    `external/xnu-4570.1.46/bsd/sys/pthread_shims.h` - Apple's header, which is also what the source
    file includes. **There is no second definition of the layout here to drift from**, which is the
    defect the source file's own header note about a hand-written mirror is about;
  - the table's address and the address of every body come from the image's own symbol table (`nm`);
  - the words themselves come from the image's bytes, read through the section table rather than
    scraped out of `objdump -s`'s text, so a formatting change cannot silently change what is compared.

The naming rule it checks is the one the source file follows: `stage90_pthread_functions_init` for
`pthread_init` and `stage90_pthread_slot_<member>` for every other slot. A member whose body is missing,
a designator that names the wrong member, a swapped pair, a slot left NULL and a `version` that is not
the header's are all the same kind of failure here and all named by member name.

`--selftest` mutates a copy of the table **in memory** - swapping two words, zeroing one, and moving
`version` - and requires each mutation to be refused, which is what says the comparison is a comparison
and not a restatement.

    ./tools/check_pthread_table_slots.py --selftest
    ./tools/check_pthread_table_slots.py                      # out/stage90/xnu_arm_entry.elf
    ./tools/check_pthread_table_slots.py --elf out/stage90/xnu_arm_entry.elf --verbose
"""

import argparse
import os
import re
import struct
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ELF = os.path.join(REPO_ROOT, "out/stage90/xnu_arm_entry.elf")
DEFAULT_HEADER = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/bsd/sys/pthread_shims.h")

TABLE_SYMBOL = "stage90_pthread_functions"
INIT_BODY = "stage90_pthread_functions_init"
SLOT_PREFIX = "stage90_pthread_slot_"

# `sizeof(struct pthread_functions_s)`: (1 + 39 + 87) words. Written out because it is the number the
# table's own file header states and the number the entry image links - so a header that grows a member
# fails here rather than moving every slot after it.
TABLE_BYTES = 508
NAMED_WORDS = 40  # `version` plus the 39 slots

STRUCT_OPEN = re.compile(r"^typedef const struct pthread_functions_s \{")
STRUCT_CLOSE = re.compile(r"^\} \* *pthread_functions_t;")
STRUCT_SLOT = re.compile(r"\(\*([A-Za-z0-9_]+)\)[ \t]*\(")
PAD_MEMBER = re.compile(r"^[ \t]*void[ \t]*\*[ \t]*_pad\[([0-9]+)\];")
VERSION_DEFINE = re.compile(r"^#define[ \t]+PTHREAD_FUNCTIONS_TABLE_VERSION[ \t]+([0-9]+)")


def say(message):
    print(message)


def fail(message):
    print("FAIL: %s" % message, file=sys.stderr)
    sys.exit(1)


class Header:
    """Apple's `struct pthread_functions_s`, as its own header states it."""

    def __init__(self, slots, version, pad):
        self.slots = slots
        self.version = version
        self.pad = pad

    @property
    def words(self):
        return 1 + len(self.slots)

    @property
    def size(self):
        return (self.words + self.pad) * 4


def read_header(path):
    with open(path, "r") as handle:
        lines = handle.read().splitlines()

    slots = []
    pad = None
    version = None
    inside = False
    for line in lines:
        if STRUCT_OPEN.match(line):
            inside = True
            continue
        if inside and STRUCT_CLOSE.match(line):
            inside = False
            continue
        if inside:
            match = STRUCT_SLOT.search(line)
            if match:
                slots.append(match.group(1))
                continue
            match = PAD_MEMBER.match(line)
            if match:
                pad = int(match.group(1))
            continue
        match = VERSION_DEFINE.match(line)
        if match and version is None:
            version = int(match.group(1))

    if not slots:
        fail("no member names were read out of %s - the reader, not the check, is what needs fixing" % path)
    if pad is None:
        fail("no 'void * _pad[N];' in the body of struct pthread_functions_s in %s, so the table's"
             " extent cannot be checked" % path)
    if version is None:
        fail("no PTHREAD_FUNCTIONS_TABLE_VERSION in %s, so the table's word 0 cannot be checked" % path)
    return Header(slots, version, pad)


class Elf:
    """The little this check needs of ELF32: symbols from `nm`, bytes from the file."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as handle:
            self.data = handle.read()
        if self.data[:4] != b"\x7fELF":
            fail("%s is not an ELF file" % path)
        shoff, = struct.unpack_from("<I", self.data, 0x20)
        shentsize, shnum, _shstrndx = struct.unpack_from("<HHH", self.data, 0x2e)
        sections = []
        for index in range(shnum):
            base = shoff + index * shentsize
            _name, _type, _flags, addr, offset, size = struct.unpack_from("<IIIIII", self.data, base)
            sections.append((addr, offset, size))
        self._sections = sections

    def read(self, vaddr, count):
        for addr, offset, size in self._sections:
            if addr and addr <= vaddr < addr + size:
                start = offset + (vaddr - addr)
                return self.data[start:start + count]
        fail("no section of %s covers 0x%08x, so the table cannot be read there" % (self.path, vaddr))

    def symbols(self):
        out = subprocess.run(
            ["arm-none-eabi-nm", "-S", "--defined-only", self.path],
            check=True, capture_output=True, text=True).stdout
        table = {}
        sizes = {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 4:
                addr, size, _kind, name = parts
                try:
                    table[name] = int(addr, 16)
                    sizes[name] = int(size, 16)
                except ValueError:
                    continue
            elif len(parts) == 3:
                addr, _kind, name = parts
                try:
                    table[name] = int(addr, 16)
                except ValueError:
                    continue
        self._sizes = sizes
        return table


def expected_body(member):
    return INIT_BODY if member == "pthread_init" else SLOT_PREFIX + member


def collect(elf, header, mutate=None):
    """Every way the image's table can disagree with the header, as a list of sentences."""
    symbols = elf.symbols()
    if TABLE_SYMBOL not in symbols:
        fail("the image defines no %s, so the table the kernel reads is not in it under that name" % TABLE_SYMBOL)
    table_va = symbols[TABLE_SYMBOL]
    raw = bytearray(elf.read(table_va, header.size))
    if len(raw) != header.size:
        fail("only %d of the table's %d bytes are in the image at 0x%08x" % (len(raw), header.size, table_va))
    if mutate is not None:
        mutate(raw, header)

    problems = []
    version = struct.unpack_from("<i", raw, 0)[0]
    if version != header.version:
        problems.append("the table's word 0 is %d and PTHREAD_FUNCTIONS_TABLE_VERSION is %d"
                        % (version, header.version))
    for index, member in enumerate(header.slots, start=1):
        got = struct.unpack_from("<I", raw, index * 4)[0]
        want_name = expected_body(member)
        if want_name not in symbols:
            fail("the image has no '%s', which slot %d ('%s') is written to point at - fix the check"
                 " rather than deleting it" % (want_name, index, member))
        want = symbols[want_name]
        if got != want:
            problems.append("table word %d ('%s') is 0x%08x and not %s (0x%08x)"
                            % (index, member, got, want_name, want))
    return problems, table_va, symbols


def swap(words, header, first, second):
    a, b = first * 4, second * 4
    words[a:a + 4], words[b:b + 4] = words[b:b + 4], words[a:a + 4]


def selftest(elf, header):
    """Each mutation is applied to a copy of the table in memory and must be refused."""
    mutations = [
        ("the two workqueue slots swapped (word 5 <-> word 6)", lambda w, h: swap(w, h, 5, 6)),
        ("`pthread_init` and `workqueue_mark_exiting` swapped (word 1 <-> word 6)", lambda w, h: swap(w, h, 1, 6)),
        ("word 5 (workqueue_exit) zeroed", lambda w, h: w.__setitem__(slice(5 * 4, 5 * 4 + 4), b"\0\0\0\0")),
        ("version moved off the header's value", lambda w, h: w.__setitem__(slice(0, 4), struct.pack("<i", 0))),
    ]
    refused = 0
    for name, mutation in mutations:
        problems, _va, _symbols = collect(elf, header, mutate=mutation)
        if not problems:
            fail("--selftest: the mutation '%s' was accepted, so this check does not compare what it"
                 " says it compares" % name)
        if os.environ.get("STAGE90_CHECK_VERBOSE"):
            say("    refused: %s -> %s" % (name, problems[0]))
        refused += 1
    say("  --selftest: all %d mutations were refused" % refused)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--elf", default=DEFAULT_ELF)
    parser.add_argument("--header", default=DEFAULT_HEADER)
    parser.add_argument("--selftest", action="store_true",
                        help="mutate a copy of the table and require every mutation to be refused")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    for path in (args.elf, args.header):
        if not os.path.exists(path):
            fail("no %s" % path)

    header = read_header(args.header)
    if header.words != NAMED_WORDS:
        fail("the header's struct pthread_functions_s has %d named words and the entry image is built for"
             " %d - fix this reader rather than the check" % (header.words, NAMED_WORDS))
    if header.size != TABLE_BYTES:
        fail("the header says the table is (1 + %d + %d) words = %d bytes and not the %d the entry image"
             " and this check are built on - a member was added or removed, and every slot after it has"
             " moved" % (len(header.slots), header.pad, header.size, TABLE_BYTES))

    elf = Elf(args.elf)

    if args.selftest:
        selftest(elf, header)
        return

    problems, table_va, _symbols = collect(elf, header)
    if problems:
        for problem in problems:
            say("FAIL: %s" % problem, file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        for index, member in enumerate(header.slots, start=1):
            got = struct.unpack_from("<I", elf.read(table_va + index * 4, 4))[0]
            say("    %2d  %-36s %s" % (index, member, expected_body(member)))
    say("  xnu_entry_473: all %d named words of struct pthread_functions_s at 0x%08x read back as the"
        " functions their member names say (both channels agree on 1 + %d + %d = %d bytes; version %d),"
        " so a shim's [r1, #N] reaches the body its slot names"
        % (len(header.slots), table_va, len(header.slots), header.pad, header.size, header.version))


if __name__ == "__main__":
    main()
