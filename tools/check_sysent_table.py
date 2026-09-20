#!/usr/bin/env python3
"""
The ARM syscall table, read out of the linked image and against Apple's own `syscalls.master`.

Why this is a check and not a comment
-------------------------------------
Experiment 479 makes the RAM disk's five instructions a `svc #0x80` asking for Unix syscall 20, and it
puts `--wrap=getpid`'s wrapper in that syscall's slot. Two things can be true of the *image* while the
device still runs the program `entry_ramdisk.s` describes:

  - `--wrap` rewrites an *address reference* exactly as it rewrites a call - 458 read that fact out of
    `cons_ops[1].putc` and 463 out of `IOService::getState`'s name - so `bd/kern/init_sysent.c`'s
    initialiser is what has to end up holding `__wrap_getpid`. If it holds `getpid`, the wrapper is
    never entered, no record is written, and a run in which process 1 loops on `getpid` looks exactly
    like a run in which the instrument is not there. Nothing else in the build can see that: the symbol
    is defined either way, the undefined-symbol count does not move, and the census in
    `build_entry.sh` counts `getpid` as reachable *by address* - which is a statement about a reference
    existing, not about which address it holds.
  - and the *index* is a second, independent claim: the fixture puts 20 in `r12`, `fleh_swi` routes a
    positive number to the unix path, and `arm_get_syscall_number` hands that same word to
    `sysent[code]`. A table whose stride or whose numbering is not the one this project believes would
    make the fixture call a different syscall than the one this step documents - silently, because
    every slot in the table is a real function.

Where each side of the comparison comes from
--------------------------------------------
  - the index, the name and the empty argument list come from
    `external/xnu-4570.1.46/bsd/kern/syscalls.master`, which is the file Apple generates the table
    *from*;
  - the addresses and the bytes come from the image itself: symbols from `nm`, and the table read
    through the section table rather than scraped out of `objdump -s`'s text, so a formatting change
    cannot silently change what is compared;
  - and the stride is not asserted anywhere. It is *proved* by the witness list: eight entries spread
    from 0 to 128, each checked against the master's line for that index. A stride that were not 16
    bytes, or a table that were indexed from a different base, cannot satisfy eight of them at once -
    which is what makes "entry 20 is `getpid`" a reading rather than a restatement of this file.

The witnesses are not decoration: `fork` at 2, `read` at 3, `open` at 5 and `rename` at 128 are four
different points of the same table, and `enosys` at 8 is the one Apple's own generation turns argument
mismatches into - so a build whose table had moved would have to move all of them consistently.

`--selftest` mutates a copy of the table **in memory** - swapping two entries, zeroing the wrapper's
slot, putting the real `getpid` back, and truncating `nsysent` - and requires each mutation to be
refused, which is what says the comparison is a comparison and not a restatement.

    ./tools/check_sysent_table.py --selftest
    ./tools/check_sysent_table.py                        # out/stage90/xnu_arm_entry.elf
    ./tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf --verbose
"""

import argparse
import os
import re
import struct
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ELF = os.path.join(REPO_ROOT, "out/stage90/xnu_arm_entry.elf")
DEFAULT_MASTER = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/bsd/kern/syscalls.master")

TABLE_SYMBOL = "sysent"
COUNT_SYMBOL = "nsysent"

# The syscall the fixture calls, and the one whose slot the wrapper has to occupy.
SYSCALL_INDEX = 20          # the number in the fixture's `r12`, claimed by `entry_ramdisk.s`
SYSCALL_NAME = "getpid"

# `sizeof(struct sysent)` on this target. Nothing here *relies* on the number: it is the stride the
# witnesses below either satisfy or do not, and a build that changed it would have to satisfy them at
# the new stride.
SYSENT_STRIDE = 16

# (index, name) pairs, from `syscalls.master`'s own lines. Eight of them, so that the stride and the
# numbering are both over-determined rather than assumed.
WITNESSES = [
    (0, "nosys"),
    (1, "exit"),
    (2, "fork"),
    (3, "read"),
    (5, "open"),
    (8, "enosys"),
    (20, "getpid"),
    (128, "rename"),
]

failures = []
notes = []


def fail(msg):
    failures.append(msg)


def say(msg):
    notes.append(msg)


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
                if start + count > offset + size:
                    fail("the range [0x%08x, 0x%08x) crosses out of the section holding 0x%08x, so the "
                         "table cannot be read as one range" % (vaddr, vaddr + count, vaddr))
                    return None
                return self.data[start:start + count]
        fail("no section of %s covers 0x%08x, so the table cannot be read there" % (self.path, vaddr))
        return None

    def symbols(self):
        out = subprocess.run(["arm-none-eabi-nm", self.path], check=True, capture_output=True,
                             text=True).stdout
        table = {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                addr, _kind, name = parts
                try:
                    table[name] = int(addr, 16)
                except ValueError:
                    continue
        return table


def master_entries(path):
    """`syscalls.master`'s own `number -> function name` map.

    The lines are `number<TAB>AUE_...<TAB>...<TAB>{ prototype }`, and the function's name is the
    identifier before the first `(` inside the braces - which is how `{ user_ssize_t read(...) }` gives
    `read` and not `user_ssize_t`. Lines that carry no prototype are skipped rather than guessed at.
    """
    entries = {}
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("#") or not line[:1].isdigit():
                continue
            m = re.match(r"(\d+)\s", line)
            if not m:
                continue
            braces = re.search(r"\{(.*)\}", line)
            if not braces:
                continue
            name = re.search(r"\b(\w+)\s*\(", braces.group(1))
            if name:
                entries[int(m.group(1))] = name.group(1)
    return entries


def collect(elf, master, mutate=None):
    """Every way the image's table can disagree with the master and with the wrapper, as sentences."""
    symbols = elf.symbols()
    for symbol in (TABLE_SYMBOL, COUNT_SYMBOL, SYSCALL_NAME, "__wrap_" + SYSCALL_NAME):
        if symbol not in symbols:
            fail("the image defines no %s, so the table this check reads (or the wrapper it looks for) "
                 "is not in it under that name" % symbol)
    if failures:
        return

    table_va = symbols[TABLE_SYMBOL]
    count_va = symbols[COUNT_SYMBOL]
    span = (max(index for index, _ in WITNESSES) + 1) * SYSENT_STRIDE
    raw = elf.read(table_va, span)
    count_bytes = elf.read(count_va, 4)
    if raw is None or count_bytes is None:
        return
    count = struct.unpack_from("<i", count_bytes)[0]

    # The mutation hook works on a copy of both numbers, so the selftest can move the table *or* the
    # bound and require each to be refused.
    state = {"table": bytearray(raw), "nsysent": count}
    if mutate is not None:
        mutate(state)
    raw = bytes(state["table"])
    count = state["nsysent"]

    say("sysent at 0x%08x, nsysent at 0x%08x = %d, stride %d, %d witnesses"
        % (table_va, count_va, count, SYSENT_STRIDE, len(WITNESSES)))

    if count <= SYSCALL_INDEX:
        fail("nsysent is %d, so the kernel's own bound puts index %d outside the table it dispatches "
             "through - the fixture's `r12` would reach sysent[SYS_invalid] instead of %s"
             % (count, SYSCALL_INDEX, SYSCALL_NAME))

    for index, name in WITNESSES:
        want = master.get(index)
        if want != name:
            fail("syscalls.master's line for %d names `%s` and this check expects `%s`: the number the "
                 "fixture's r12 carries and the number the master gives %s are two definitions of one "
                 "decision and they have to agree" % (index, want, name, name))
            continue
        # **The address is the wrapper's when there is one.** `--wrap=X` renames every reference to
        # `X`, including an initialiser, so a slot that names a wrapped symbol holds `__wrap_X` - and
        # comparing it with `X` would call the image wrong for being right. `nm` is what says which
        # reference the link line produced, and index 20 is checked again below as the two-sided fact
        # this step's run depends on.
        at = table_va + index * SYSENT_STRIDE
        expected = "__wrap_" + name if ("__wrap_" + name) in symbols else name
        got, = struct.unpack_from("<I", raw, index * SYSENT_STRIDE)
        if got != symbols[expected]:
            fail("sysent[%d] at 0x%08x is 0x%08x and `%s` is at 0x%08x: the table's numbering or its "
                 "stride is not the one `syscalls.master` gives, so the fixture's syscall number does "
                 "not name the function this step documents"
                 % (index, at, got, expected, symbols[expected]))

    # **The wrapper's slot, which is the reading this step's device run depends on.** `--wrap=getpid`
    # rewrites the address reference in `init_sysent.c`, so the word has to be the wrapper and must not
    # be the function. Both halves are stated, because the failure mode this replaces is the silent
    # one: a slot holding `getpid` produces no records at all.
    got, = struct.unpack_from("<I", raw, SYSCALL_INDEX * SYSENT_STRIDE)
    wrapper = symbols["__wrap_" + SYSCALL_NAME]
    real = symbols[SYSCALL_NAME]
    if got == real:
        fail("sysent[%d] holds the real %s (0x%08x) and not __wrap_%s (0x%08x): `--wrap` rewrites the "
             "initialiser in init_sysent.c, so this means the flag is not in the link line (or the "
             "table is not the one the dispatcher indexes) and the wrapper would never run - and a run "
             "with no getpid records looks exactly like a wrapper that is not there"
             % (SYSCALL_INDEX, SYSCALL_NAME, real, SYSCALL_NAME, wrapper))
    elif got != wrapper:
        fail("sysent[%d] is 0x%08x, which is neither __wrap_%s (0x%08x) nor %s (0x%08x)"
             % (SYSCALL_INDEX, got, SYSCALL_NAME, wrapper, SYSCALL_NAME, real))
    else:
        say("sysent[%d] = 0x%08x = __wrap_%s (%s is at 0x%08x), so the kernel dispatches the "
            "fixture's syscall into the wrapper" % (SYSCALL_INDEX, got, SYSCALL_NAME, SYSCALL_NAME,
                                                    real))

    # Every witness read back, for the log: an eight-line agreement is what makes the stride a reading.
    for index, name in WITNESSES:
        got, = struct.unpack_from("<I", raw, index * SYSENT_STRIDE)
        expected = "__wrap_" + name if ("__wrap_" + name) in symbols else name
        say("  sysent[%3d] = 0x%08x = %s (master line %d: %s)"
            % (index, got, expected, index, master.get(index)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--elf", default=DEFAULT_ELF)
    ap.add_argument("--master", default=DEFAULT_MASTER)
    ap.add_argument("--selftest", action="store_true",
                    help="mutate the table in memory and require every mutation to be refused")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        sys.exit("no %s - build the entry image first" % args.elf)
    if not os.path.isfile(args.master):
        sys.exit("no %s - the syscall numbering this check compares against is not there" % args.master)

    elf = Elf(args.elf)
    master = master_entries(args.master)
    if not master:
        sys.exit("%s has no numbered syscall lines - this check has nothing to compare against"
                 % args.master)

    collect(elf, master)

    if args.selftest:
        # Each mutation is a way the image could be wrong that the checks above have an opinion about.
        # If one survives, the check is decorative and this is the only place that can say so.
        offset = SYSCALL_INDEX * SYSENT_STRIDE
        other = 24 * SYSENT_STRIDE         # another entry's slot, inside the checked span
        symbols = elf.symbols()
        wrapper = symbols["__wrap_" + SYSCALL_NAME]
        real = symbols[SYSCALL_NAME]

        def word(state, where):
            return struct.unpack_from("<I", state["table"], where)[0]

        def put(state, where, value):
            struct.pack_into("<I", state["table"], where, value)

        def zero_the_slot(state):
            put(state, offset, 0)

        def the_real_function(state):
            put(state, offset, real)

        def one_entry_late(state):
            put(state, offset, 0)
            put(state, offset + SYSENT_STRIDE, wrapper)

        def swapped_with_24(state):
            mine, theirs = word(state, offset), word(state, other)
            put(state, offset, theirs)
            put(state, other, mine)

        def shifted_one_word(state):
            # A stride that is not 16 bytes moves every witness at once, which is the property the
            # witness list exists to over-determine.
            state["table"] = bytearray(b"\0\0\0\0") + state["table"][:-4]

        mutations = [
            ("the wrapper's slot zeroed", zero_the_slot),
            ("the real getpid in the slot", the_real_function),
            ("the wrapper one entry late", one_entry_late),
            ("entries 20 and 24 swapped", swapped_with_24),
            ("the table shifted by one word (any other stride)", shifted_one_word),
            ("nsysent equal to the fixture's index", lambda s: s.__setitem__("nsysent", SYSCALL_INDEX)),
            ("nsysent below the last witness", lambda s: s.__setitem__("nsysent", 1)),
        ]
        survived = []
        for name, mutation in mutations:
            del failures[:]
            kept = len(notes)
            collect(elf, master, mutate=mutation)
            del notes[kept:]
            if not failures:
                survived.append(name)
        del failures[:]
        if survived:
            fail("--selftest: these mutations were accepted, so the checks above do not see them: "
                 + ", ".join(survived))
        else:
            notes.append("--selftest: all %d mutations were refused" % len(mutations))

    if failures:
        print("the image's syscall table does not match the master or the wrapper:", file=sys.stderr)
        for item in failures:
            print("  " + item, file=sys.stderr)
        return 1
    if args.verbose:
        for item in notes:
            print(item)
    else:
        # The success line is the wrapper's slot, and not `notes[-1]`: the witness read-back and the
        # selftest each append a note after it, so a "the last note is the summary" spelling made this
        # check's own `ok:` line name sysent[128] (and, with --selftest, only the selftest). The
        # reading the step is built on is `notes[1]` whatever else ran.
        print("ok: %s" % (notes[1] if len(notes) > 1 else "the syscall table reads as built"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
