#!/usr/bin/env python3
"""
The ARM syscall table, read out of the linked image and against Apple's own `syscalls.master`.

Why this is a check and not a comment
-------------------------------------
Experiment 479 makes the RAM disk's first instructions a `svc #0x80` asking for Unix syscall 20, and
it puts `--wrap=getpid`'s wrapper in that syscall's slot; experiment 480 adds a second `svc` asking
for 197, with `--wrap=mmap`'s wrapper in *that* slot and six words of arguments marshalled into it by
the armv7k munger; experiment 503 adds two more asking for 230, whose slot holds `--wrap=poll`'s
wrapper; and experiment 504 adds the pair that reach a driver - three words for 3 (`read`) and three
for 5 (`open`), each in its own slot. Three things can be true of the *image* while the device still
runs the program `entry_ramdisk.s` describes:

  - `--wrap` rewrites an *address reference* exactly as it rewrites a call - 458 read that fact out of
    `cons_ops[1].putc` and 463 out of `IOService::getState`'s name - so `bsd/kern/init_sysent.c`'s
    initialiser is what has to end up holding `__wrap_getpid`, `__wrap_mmap`, `__wrap_poll`,
    `__wrap_read` and `__wrap_open`. If any
    slot holds the function instead, the wrapper is never entered, no record is written, and a run in
    which process 1 calls that syscall looks exactly like a run in which the instrument is not there.
    Nothing else in the build can see that: the symbol is defined either way, the undefined-symbol
    count does not move, and the census in `build_entry.sh` counts both as reachable *by address* -
    which is a statement about a reference existing, not about which address it holds. For `mmap` and
    `poll` the distinction is sharper than for `getpid`: both are ordinary functions with callers, so
    an image with the wrapper linked and *not* in the slot is byte-for-byte a working kernel whose
    fixture silently says nothing. 504's two are of that sharper kind twice over, and they add a
    consequence none of the first three has: the fixture's two `open`s are a *pair*, so a slot without
    its wrapper loses the control as well as the reading.
  - and the *index* is a second, independent claim: the fixture puts 20, 197, 230, 3 and 5 in `r12`,
    `fleh_swi`
    routes a positive number to the unix path, and `arm_get_syscall_number` hands that same word to
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
  - and the stride is not asserted anywhere. It is *proved* by the witness list: ten entries spread
    from 0 to 230, each checked against the master's line for that index. A stride that were not 16
    bytes, or a table that were indexed from a different base, cannot satisfy ten of them at once -
    which is what makes "entry 197 is `mmap`" a reading rather than a restatement of this file;
  - and for the slots that have one, a fourth claim: `sysent[197].sy_arg_munge32` must be
    `munge_wwwwwl`, the munger Apple's generation derives from `mmap`'s prototype, and
    `sysent[230].sy_arg_munge32` must be `munge_www`, the one it derives from `poll`'s. That word is
    what decides which register lands in which word of the argument struct, so it is the fact 480's
    argument reading and 503's *no-padding* reading both stand on - and it is invisible everywhere else
    in the image, because a build with a different munger there is a working kernel that answers the
    call with different numbers. The pair is the sharper claim of the two: `mmap`'s six argument words
    and `poll`'s three are the difference between a struct with an alignment gap and one without.

The witnesses are not decoration: `fork` at 2, `read` at 3, `open` at 5 and `rename` at 128 are four
different points of the same table, and `enosys` at 8 is the one Apple's own generation turns argument
mismatches into - so a build whose table had moved would have to move all of them consistently. 20, 197
and 230 are the three the fixture reaches, and 20 and 197 are the pair whose *distance* matters: 177
entries apart, which no off-by-one in the stride survives.

`--selftest` mutates a copy of the table **in memory** - swapping two entries, zeroing a wrapper's
slot, putting a real function back, moving the mmap munger, and truncating `nsysent` - and requires
each mutation to be refused, which is what says the comparison is a comparison and not a restatement.

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

# The syscalls the fixture makes, and the slot each one's wrapper has to occupy. The first is the one
# the program's `r12` starts with; the second is the call whose *arguments* the armv7k munger marshals,
# and the one whose slot has to hold a wrapper for a function this image also calls from elsewhere; the
# third is 503's pair of timed asks, whose slot has to hold a wrapper for the same reason and whose
# arguments are three words with no gap between them.
WRAPPED = [
    (20, "getpid"),         # 479
    (197, "mmap"),          # 480
    (230, "poll"),          # 503
    (3, "read"),            # 504
    (5, "open"),            # 504
]
SYSCALL_INDEX, SYSCALL_NAME = WRAPPED[0]

# `sizeof(struct sysent)` on this target. Nothing here *relies* on the number: it is the stride the
# witnesses below either satisfy or do not, and a build that changed it would have to satisfy them at
# the new stride.
SYSENT_STRIDE = 16

# (index, name) pairs, from `syscalls.master`'s own lines. Ten of them, so that the stride and the
# numbering are both over-determined rather than assumed - and the last three are the ones the fixture
# reaches, 20 and 197 being 177 entries apart so that no small error in the stride can leave both of
# them satisfied.
WITNESSES = [
    (0, "nosys"),
    (1, "exit"),
    (2, "fork"),
    (3, "read"),
    (5, "open"),
    (8, "enosys"),
    (20, "getpid"),
    (128, "rename"),
    (197, "mmap"),
    (230, "poll"),
]

# The munger each wrapped slot has to name, where it has one. **These are the words the whole argument
# reading rests on**: `arm_get_u32_syscall_args` (`bsd/dev/arm/systemcalls.c:337`) calls
# `callp->sy_arg_munge32`, and *which* munger it is decides which registers land in which word of the
# argument struct. `munge_wwwwwl` is Apple's generated name for `mmap`'s prototype (five 4-byte
# arguments and one 8-byte `off_t`, `bsd/kern/syscalls.master`), it is what
# `out/xnu_generated/init_sysent.c`'s own line for 197 writes, and `entry_ramdisk.s`'s header derives
# the register order from it. `munge_www` is the same for `poll`'s three 4-byte arguments - and there
# the claim is not only the order but the *absence* of a gap: the two mungers differ by exactly the
# word `off_t`'s alignment inserts, so a slot naming the wrong one would put the fixture's timeout
# somewhere the syscall never reads, and `poll` would be called with a timeout that is not the one the
# program asked for. `getpid` has none: its argument list is empty, so its slot's munger word is NULL
# and there is nothing to name.
MUNGERS = {
    197: "munge_wwwwwl",
    230: "munge_www",
    # 504's two, and they are the same word for the same reason `poll`'s is: `read`'s three arguments
    # (`int fd`, `user_addr_t cbuf`, `user_size_t nbyte`) and `open`'s (`user_addr_t path`, `int
    # flags`, `int mode`) are all four bytes on this 32-bit target, so the munger copies r0..r2 and
    # nothing else. **The claim they carry is that an *address* is one of the three**: the fixture's
    # first `open` argument is the address of a string inside its own `__TEXT`, and the munger's shape
    # is what decides that the number the fixture put in r0 is the number `copyinstr` will read from.
    3: "munge_www",
    5: "munge_www",
}

# `struct sysent` (`bsd/sys/sysent.h:45`) on this target, where the armv7k `#if` is on:
# `sy_call`, `sy_arg_munge32`, `sy_return_type`, `sy_narg`, `sy_arg_bytes`. The munger is the second
# word, and the stride above agrees with the five members because the first three are 4 bytes each.
MUNGER_WORD_OFFSET = 4

failures = []
notes = []
# The `ok:` line, assembled by `collect` from the slots it read back. A module-level list and not
# `notes[-1]` (207): the witness read-back and the selftest each append a note after the readings, so
# "the last note is the summary" named the wrong entry twice before this was made explicit.
summary = []


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
    wanted = [TABLE_SYMBOL, COUNT_SYMBOL]
    for _index, name in WRAPPED:
        wanted += [name, "__wrap_" + name]
    wanted += list(MUNGERS.values())
    for symbol in wanted:
        if symbol not in symbols:
            fail("the image defines no %s, so the table this check reads (or a wrapper it looks for) "
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

    for index, name in WRAPPED:
        if count <= index:
            fail("nsysent is %d, so the kernel's own bound puts index %d outside the table it "
                 "dispatches through - the fixture's `r12` would reach sysent[SYS_invalid] instead of "
                 "%s" % (count, index, name))

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

    # **The wrappers' slots, which are the readings this step's device run depends on.** `--wrap=X`
    # rewrites the address reference in `init_sysent.c`, so each word has to be the wrapper and must
    # not be the function. Both halves are stated, because the failure mode this replaces is the
    # silent one: a slot holding the real function produces no records at all.
    for index, name in WRAPPED:
        got, = struct.unpack_from("<I", raw, index * SYSENT_STRIDE)
        wrapper = symbols["__wrap_" + name]
        real = symbols[name]
        if got == real:
            fail("sysent[%d] holds the real %s (0x%08x) and not __wrap_%s (0x%08x): `--wrap` rewrites "
                 "the initialiser in init_sysent.c, so this means the flag is not in the link line (or "
                 "the table is not the one the dispatcher indexes) and the wrapper would never run - "
                 "and a run with no %s records looks exactly like a wrapper that is not there"
                 % (index, name, real, name, wrapper, name))
        elif got != wrapper:
            fail("sysent[%d] is 0x%08x, which is neither __wrap_%s (0x%08x) nor %s (0x%08x)"
                 % (index, got, name, wrapper, name, real))
        else:
            summary.append("sysent[%d] = 0x%08x = __wrap_%s (%s is at 0x%08x)"
                           % (index, got, name, name, real))

        # **And the munger word beside it, for the slot that has one.** The wrapper's presence says
        # the call reaches this file; the munger says which registers the call is *handed* - and that
        # is a claim about the ABI that lives in the image and in nothing else. The name is checked
        # against `nm`, not written into a message: an image whose slot named `munge_wwwww` or
        # `munge_wwwwl` would dispatch a call whose arguments the fixture's own header comment does
        # not describe, and the device run would show a zero where `0x5a5a` is expected.
        if index in MUNGERS:
            want = MUNGERS[index]
            at = index * SYSENT_STRIDE + MUNGER_WORD_OFFSET
            got, = struct.unpack_from("<I", raw, at)
            if got != symbols[want]:
                fail("sysent[%d].sy_arg_munge32 at 0x%08x is 0x%08x and `%s` is at 0x%08x: the "
                     "munger this slot names is not the one Apple's generation put there for this "
                     "prototype, so the fixture's argument registers are not the words this step "
                     "documents - and the syscall would still work, because MAP_ANON makes fd and "
                     "pos inoperative, so nothing else would show it"
                     % (index, table_va + at, got, want, symbols[want]))
            else:
                summary.append("sysent[%d].sy_arg_munge32 = 0x%08x = %s, the munger the fixture's "
                               "registers are read by" % (index, got, want))

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
        mmap_offset = WRAPPED[1][0] * SYSENT_STRIDE   # 197's slot, 177 entries away
        poll_offset = WRAPPED[2][0] * SYSENT_STRIDE   # 230's slot, 33 entries past 197
        read_offset = WRAPPED[3][0] * SYSENT_STRIDE   # 504: 3's slot, in the table's first words
        open_offset = WRAPPED[4][0] * SYSENT_STRIDE   # 504: 5's slot, one entry on from it
        other = 24 * SYSENT_STRIDE         # another entry's slot, inside the checked span
        symbols = elf.symbols()
        wrapper = symbols["__wrap_" + SYSCALL_NAME]
        real = symbols[SYSCALL_NAME]
        mmap_wrapper = symbols["__wrap_" + WRAPPED[1][1]]
        mmap_real = symbols[WRAPPED[1][1]]
        poll_wrapper = symbols["__wrap_" + WRAPPED[2][1]]
        poll_real = symbols[WRAPPED[2][1]]
        read_wrapper = symbols["__wrap_" + WRAPPED[3][1]]
        read_real = symbols[WRAPPED[3][1]]
        open_wrapper = symbols["__wrap_" + WRAPPED[4][1]]
        open_real = symbols[WRAPPED[4][1]]

        def word(state, where):
            return struct.unpack_from("<I", state["table"], where)[0]

        def put(state, where, value):
            struct.pack_into("<I", state["table"], where, value)

        def zero_the_slot(state):
            put(state, offset, 0)

        def the_real_function(state):
            put(state, offset, real)

        def the_real_mmap(state):
            # 480's half of the same defect, and the one no other check in this tree can see: the
            # image still calls the wrapper from nowhere, so a slot holding `mmap` is a correct kernel
            # whose fixture writes one record fewer than a run that has no instrument at all.
            put(state, mmap_offset, mmap_real)

        def each_wrapper_in_the_other_slot(state):
            # The plausible mistake this replaces is an off-by-one in the wrapped list: two `--wrap`s
            # that swapped slots still leave a *working* kernel, and the fixture's two calls would
            # reach the wrong wrapper - `mmap`'s registers read by the getpid wrapper, and a `getpid`
            # counted as a mapping. Slot 197 + 1 is outside the range this check reads, so the swap is
            # the shape of "somewhere else" that is testable here.
            put(state, offset, mmap_wrapper)
            put(state, mmap_offset, wrapper)

        def zero_the_munger(state):
            put(state, mmap_offset + MUNGER_WORD_OFFSET, 0)

        def the_wrapper_in_the_munger_slot(state):
            put(state, mmap_offset + MUNGER_WORD_OFFSET, mmap_wrapper)

        def the_real_poll(state):
            # 503's half of the defect `the_real_mmap` is about, and the one this step cannot do
            # without: the two `poll`s are the only calls in the program whose *effect* is a block, so a
            # slot holding `poll` instead of its wrapper is a fixture that blocks for 5 ms and 40 ms and
            # publishes nothing at all - a run indistinguishable from one where the kernel's timer does
            # not work, because in both the ledger holds no `xnu_live_poll_*` key.
            put(state, poll_offset, poll_real)

        def the_mmap_munger_in_polls_slot(state):
            # The difference between the two mungers is exactly the word `off_t`'s alignment inserts:
            # `munge_wwwwwl` copies r0..r5, r6 and r8; `munge_www` copies r0..r2. So a slot naming the
            # six-argument munger would marshal three words the program's registers happen to hold (the
            # timeout among them in the wrong place) into a struct `poll` reads as three words - a
            # working kernel calling `poll` with a timeout the fixture never asked for.
            put(state, poll_offset + MUNGER_WORD_OFFSET, symbols[MUNGERS[197]])

        def the_real_read(state):
            # 504's copy of the defect `the_real_mmap` and `the_real_poll` are about: an image whose
            # slot holds `read` instead of its wrapper is a *working* kernel whose fixture reads the
            # driver's bytes and publishes nothing - the log would hold no `xnu_live_read_*` key at
            # all, which reads exactly like a read that never happened.
            put(state, read_offset, read_real)

        def the_real_open(state):
            # And the same for `open`, with one more consequence than the read: the fixture makes two
            # opens and one of them is the control, so a slot without its wrapper loses the *pair* -
            # and a pair is the only thing that can say whether the first open's answer was a lookup.
            put(state, open_offset, open_real)

        def the_two_new_slots_swapped(state):
            # The off-by-one this step is most exposed to: the fixture's `read` and `open` are adjacent
            # in its program and 3 and 5 are adjacent in the table's first eight entries, so two
            # `--wrap`s listed in the wrong order put each wrapper in the other's slot - every call is
            # still dispatched and every record written, and both wrappers read a `uap` the other call
            # filled.
            put(state, read_offset, open_wrapper)
            put(state, open_offset, read_wrapper)

        def the_poll_wrapper_in_opens_slot(state):
            # And the one no *shape* comparison could catch, because 230, 3 and 5 all name
            # `munge_www`: `poll`'s wrapper reads three words of `uap` exactly as `open`'s does, so a
            # slot holding it publishes `xnu_live_poll_*` keys for an open. The record set would be
            # complete and every number in it about the wrong call.
            put(state, open_offset, poll_wrapper)

        def one_entry_late(state):
            put(state, offset, 0)
            put(state, offset + SYSENT_STRIDE, wrapper)

        def swapped_with_24(state):
            mine, theirs = word(state, offset), word(state, other)
            put(state, offset, theirs)
            put(state, other, mine)

        def shifted_one_word(state):
            # A stride that is not 16 bytes moves every witness at once, which is the property the
            # witness list exists to over-determine. 197 is the witness that makes this mutation
            # expensive: at the wrong stride it is not 177 entries away from 20 but some other slot.
            state["table"] = bytearray(b"\0\0\0\0") + state["table"][:-4]

        mutations = [
            ("the wrapper's slot zeroed", zero_the_slot),
            ("the real getpid in the slot", the_real_function),
            ("the real mmap in its slot", the_real_mmap),
            ("the real poll in its slot", the_real_poll),
            ("the mmap wrapper in each other's slot", each_wrapper_in_the_other_slot),
            ("197's munger word zeroed", zero_the_munger),
            ("the wrapper in 197's munger word", the_wrapper_in_the_munger_slot),
            ("the six-argument munger in 230's slot", the_mmap_munger_in_polls_slot),
            ("the real read in its slot", the_real_read),
            ("the real open in its slot", the_real_open),
            ("the read and open wrappers in each other's slot", the_two_new_slots_swapped),
            ("the poll wrapper in the open slot", the_poll_wrapper_in_opens_slot),
            ("the wrapper one entry late", one_entry_late),
            ("entries 20 and 24 swapped", swapped_with_24),
            ("the table shifted by one word (any other stride)", shifted_one_word),
            ("nsysent equal to the fixture's index", lambda s: s.__setitem__("nsysent", SYSCALL_INDEX)),
            ("nsysent below the mmap witness", lambda s: s.__setitem__("nsysent", 128)),
            ("nsysent below the last witness", lambda s: s.__setitem__("nsysent", 1)),
        ]
        survived = []
        for name, mutation in mutations:
            del failures[:]
            del notes[:]
            del summary[:]
            collect(elf, master, mutate=mutation)
            if not failures:
                survived.append(name)
        # **The table is read once more, unmutated, before anything is printed.** The mutations above
        # work on a copy and each one stops as soon as it is refused, so whatever the last of them left
        # in `notes` and `summary` describes a table that does not exist - and with `--verbose` that
        # would be this check printing a wrong reading of a correct image.
        del failures[:]
        del notes[:]
        del summary[:]
        collect(elf, master)
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
        # The success line is what was read back out of the image - each wrapper's slot, and the
        # munger word beside the mmap one - and it is assembled in `summary` rather than taken from the
        # end of `notes`: the witness read-back and the selftest each append after the readings, so
        # "the last note is the summary" named sysent[128] on a correct build and, with --selftest, only
        # the selftest (207).
        print("ok: %s" % ("; ".join(summary) if summary else "the syscall table reads as built"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
