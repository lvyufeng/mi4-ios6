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

  - the NULL scan in `src/supply/stage90_pthread_functions.c`'s constructor cannot see
    it, because two slots swapped are two non-NULL words;
  - and no device run can see it either, because the body that runs is a *real* body that names itself
    correctly - it just names the other slot. Experiment 465's own write-up is where this confusion is
    already on record: its text called `pth_proc_hashinit`'s slot "word 6" off the disassembly, and the
    identification came from `nm` instead. Word 6 is `workqueue_mark_exiting`.

473 is the step that made this checkable rather than argued: it retires two more slots by hand, so
there are four hand-written bodies and four designators to get right, and the file it edits is the one
Apple header's layout is transcribed into. So the check reads the 40 named words **out of the image** -
where the kernel will read them - and compares each against the symbol its member name implies.

**507 adds the second half of that claim, because the first half cannot see it: a slot's *symbol name*
is the same whether the word points at a body or at a stand-in.** `stage90_pthread_slot_<member>` is the
name the macro gives a stand-in and the name this file gives each hand-written body, so a member whose
body was never written and a member whose body was written but never wired up both pass the comparison
above once the designator is right. The two are told apart by the **bytes of the object**, and by the
one thing each kind of body is defined by:

  - a **stand-in** is defined by the name it hands to `entry_stub_hit` - `STAGE90_PTHREAD_SLOT_DEF`
    pastes `"stage90_pthread_functions." #name` into the function it generates, so a literal per
    unretired member is in the object and *only* an unretired member has one;
  - a **body** is defined by the live key it publishes - `xnu_live_pth_hashinit_seq` and its siblings are
    literals the body passes to `entry_note_live`, and a stand-in has none.

So the third clause is a set equality against Apple's own member list: the object's stand-in name
literals are **exactly** the members that have no body, and every member that has one carries its live
key. Which members those are is the only thing this check is told (the `BODIES` table below, which is
the same list the source file's own paragraph keeps); both sides of the comparison are read out of the
artifacts. A slot wired to a stand-in whose body was written, a body's key spelled differently from the
one the run's log is searched for, and a body silently reverted to a stand-in are the same kind of
failure here.

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
and not a restatement. It does the same to a copy of the object's bytes, one mutation per direction of
the third clause: a body's live key literal destroyed, a body's stand-in name planted over another
slot's, and an unretired slot's stand-in name destroyed.

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
DEFAULT_OBJECT = os.path.join(REPO_ROOT, "out/xnu_platform_obj/stage90_pthread_functions.o")

TABLE_SYMBOL = "stage90_pthread_functions"
INIT_BODY = "stage90_pthread_functions_init"
SLOT_PREFIX = "stage90_pthread_slot_"

# The stand-in name literals the macro pastes together, and the two other names this object passes to
# `entry_stub_hit` that are not members: the constructor's NULL scan and the registration check.
STANDIN_PREFIX = "stage90_pthread_functions."
NON_MEMBER_NAMES = ("stage90_pthread_functions.c", STANDIN_PREFIX + "null_slot",
                    STANDIN_PREFIX + "not_registered")

# **The five slots this image fills with a body of its own, and the live key each body publishes.** This
# is the only thing the check is told; the members' names, their order and their count come from Apple's
# header, and both the presence of a key and the absence of a stand-in name are read out of the object.
# Keeping a body's reason in one line here is what makes a body added without its key - or a retired slot
# that quietly went back to being a stand-in - a build failure rather than a device run.
BODIES = {
    "pthread_init": "xnu_live_pthread_init_ptr",
    "pth_proc_hashinit": "xnu_live_pth_hashinit_seq",
    "workqueue_mark_exiting": "xnu_live_pth_wqmark_seq",
    "workqueue_exit": "xnu_live_pth_wqexit_seq",
    "pth_proc_hashdelete": "xnu_live_pth_delete_seq",
}

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


def object_strings(path, mutate=None):
    """Every NUL-terminated string in the object's own bytes, as a set.

    Read through the section table rather than shelled out to `strings(1)`, for the reason this check
    reads the table the same way: a formatting change must not be able to change what is compared. The
    two literals it is asked about - a stand-in's name and a body's live key - are both bytes the linker
    will place, and `mutate` is what lets the selftest change them the way it changes the table.
    """
    with open(path, "rb") as handle:
        blob = bytearray(handle.read())
    if blob[:4] != b"\x7fELF":
        fail("%s is not an ELF file" % path)
    shoff, = struct.unpack_from("<I", blob, 0x20)
    shentsize, shnum, _shstrndx = struct.unpack_from("<HHH", blob, 0x2e)
    if mutate is not None:
        mutate(blob)
    strings = set()
    for index in range(shnum):
        base = shoff + index * shentsize
        _name, kind, _flags, _addr, offset, size = struct.unpack_from("<IIIIII", blob, base)
        if kind == 8:                       # SHT_NOBITS: no bytes in the file to read
            continue
        for piece in bytes(blob[offset:offset + size]).split(b"\0"):
            if piece and all(0x20 <= byte < 0x7f for byte in piece):
                strings.add(piece.decode("ascii"))
    return strings


def body_problems(strings, header):
    """The object's bytes against the claim that `BODIES` are bodies and the rest are stand-ins."""
    problems = []
    members = set(header.slots)

    named_stand_ins = {s for s in strings
                       if s.startswith(STANDIN_PREFIX) and s not in NON_MEMBER_NAMES}
    want_stand_ins = {STANDIN_PREFIX + m for m in members if m not in BODIES}
    # The retired-slot-still-has-a-stand-in direction first, because that is the failure this clause
    # was added for and the sentence an operator should read first.
    for name in sorted(named_stand_ins - want_stand_ins):
        member = name[len(STANDIN_PREFIX):]
        problems.append("the object carries the stand-in literal '%s' for '%s', and '%s' is in this"
                        " check's list of slots with hand-written bodies - the slot was retired by a"
                        " body and its stand-in is still in the object, which is what a body written"
                        " below the table but left out of the macro list, or a revert, looks like"
                        % (name, member, member))
    for name in sorted(want_stand_ins - named_stand_ins):
        member = name[len(STANDIN_PREFIX):]
        problems.append("the object carries no '%s' literal, so slot '%s' is not a stand-in - but this"
                        " check is told that it has no body either, so the slot is wired to something"
                        " neither: a body added without being added to BODIES, or a stand-in macro"
                        " removed from the list" % (name, member))

    for member in sorted(BODIES):
        key = BODIES[member]
        if key not in strings:
            problems.append("no '%s' literal in the object, so the body this check says '%s' has does"
                            " not publish the key the run's log is searched for" % (key, member))
    return problems


def selftest(elf, header, object_path):
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

    # The object's bytes, mutated the two ways a body's existence can be falsified: the key literal it
    # publishes taken away, and its stand-in name put back.
    strings = object_strings(object_path)
    key = BODIES["pth_proc_hashdelete"]
    stand_in = STANDIN_PREFIX + "pth_proc_hashdelete"

    def kill_key(blob):
        at = blob.find(key.encode("ascii"))
        if at < 0:
            fail("--selftest: no '%s' in %s to destroy - fix this selftest, not the check" % (key, object_path))
        blob[at] = 0x21                        # '!' - the literal is no longer the key

    def restore_stand_in(blob):
        victim = STANDIN_PREFIX + "workq_threadreq_modify"
        at = blob.find(victim.encode("ascii"))
        if at < 0:
            fail("--selftest: no '%s' in %s to overwrite - fix this selftest, not the check"
                 % (victim, object_path))
        blob[at:at + len(victim) + 1] = stand_in.encode("ascii") + b"\0"

    def kill_a_stand_in(blob):
        victim = STANDIN_PREFIX + "fill_procworkqueue"
        at = blob.find(victim.encode("ascii"))
        if at < 0:
            fail("--selftest: no '%s' in %s to destroy - fix this selftest, not the check"
                 % (victim, object_path))
        blob[at] = 0x21

    for name, mutate in (("a body's live key literal destroyed", kill_key),
                         ("a body's stand-in name planted over another slot's", restore_stand_in),
                         ("an unretired slot's stand-in name destroyed", kill_a_stand_in)):
        problems = body_problems(object_strings(object_path, mutate=mutate), header)
        if not problems:
            fail("--selftest: the mutation '%s' was accepted, so the object clause does not compare"
                 " what it says it compares" % name)
        if os.environ.get("STAGE90_CHECK_VERBOSE"):
            say("    refused: %s -> %s" % (name, problems[0]))
        refused += 1

    say("  --selftest: all %d mutations were refused" % refused)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--elf", default=DEFAULT_ELF)
    parser.add_argument("--object", default=DEFAULT_OBJECT,
                        help="the object this table's file builds into - read for the stand-in name and"
                             " live key literals")
    parser.add_argument("--header", default=DEFAULT_HEADER)
    parser.add_argument("--selftest", action="store_true",
                        help="mutate a copy of the table and require every mutation to be refused")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    for path in (args.elf, args.header, args.object):
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
        selftest(elf, header, args.object)
        return

    problems, table_va, _symbols = collect(elf, header)
    problems += body_problems(object_strings(args.object), header)
    if problems:
        for problem in problems:
            # 507: this line used to be `say(..., file=sys.stderr)`, and `say` takes no `file=` -
            # so the one path that reports a problem exited on a TypeError instead of printing
            # the sentence. The build stops either way, with a traceback where the finding
            # should be; the check had never failed before this step, which is why it survived.
            print("FAIL: %s" % problem, file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        for index, member in enumerate(header.slots, start=1):
            got = struct.unpack_from("<I", elf.read(table_va + index * 4, 4))[0]
            say("    %2d  %-36s %s" % (index, member, expected_body(member)))
    say("  xnu_entry_473: all %d named words of struct pthread_functions_s at 0x%08x read back as the"
        " functions their member names say (both channels agree on 1 + %d + %d = %d bytes; version %d),"
        " so a shim's [r1, #N] reaches the body its slot names"
        % (len(header.slots), table_va, len(header.slots), header.pad, header.size, header.version))
    say("  xnu_entry_507: the object's own literals say which slots are stand-ins and which are bodies -"
        " %d of the %d members have no '%s<member>' name and carry their live key instead (%s), so a slot"
        " this check calls retired is one the kernel can reach and the log can see"
        % (len(BODIES), len(header.slots), STANDIN_PREFIX,
           ", ".join("%s -> %s" % (m, BODIES[m]) for m in sorted(BODIES))))


if __name__ == "__main__":
    main()
