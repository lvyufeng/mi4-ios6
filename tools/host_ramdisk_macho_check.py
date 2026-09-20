#!/usr/bin/env python3
"""
Check the Mach-O the entry image carries as the first userland process's executable.

    ./tools/host_ramdisk_macho_check.py                       # uses out/stage90/xnu_arm_entry.elf
    ./tools/host_ramdisk_macho_check.py path/to/entry.elf
    ./tools/host_ramdisk_macho_check.py --selftest            # prove the checks bite

Since experiment 468, `g_stage90_ramdisk` (`stages/stage90/xnu_arm_boot/entry_ramdisk.s`) is a
Mach-O at offset 0, and the bytes at that offset **are** `/sbin/launchd` - `mockfs` maps its one file
node directly onto the device's physical pages (`mockfs_fsnode.c:333-344`) and takes the file's size
from the device (`:80`). So this object is read by `exec_mach_imgact` -> `get_macho_vnode` ->
`parse_machfile` -> `load_segment` -> `load_unixthread`, and a field that is wrong is a *load*
failure on the device that no log names: `load_return_to_errno` maps `LOAD_BADMACHO` to `EBADMACHO`
and `LOAD_FAILURE` to its own errno, so the report is a number and not a cause.

What this checks, and it checks the artifact rather than the source:

  - the bytes **emitted into the linked ELF**, at `g_stage90_ramdisk`'s address, so what is tested is
    what the device will read;
  - that the section holding them is `PROGBITS` and `ALLOC` - i.e. in the part of the image the
    payload *copies* and not the part it zeroes. A `.bss` disk would be erased between the copy and
    the jump, and that is a whole-class failure this asserts rather than assumes;
  - every load command's fields against the rules `parse_machfile` and `load_machfile` apply, each
    one quoted from the source where it is applied, and the two rules that are *shapes* rather than
    values: `sizeofcmds` must be the commands' own extent (walked with their own `cmdsize` chains)
    and the entry point must be inside the R+X segment it is loaded from (`validentry`);
  - the numbers that come from Apple's headers are **read out of those headers** - `mach-o/loader.h`,
    `mach/machine.h`, `mach/vm_prot.h`, `mach/arm/thread_status.h` and `mach/arm/_structs.h` - so the
    assembler's spelling and the kernel's cannot drift apart silently. The one thing transcribed here
    is the *struct layout* of `mach_header`/`segment_command`/`thread_command`, which is the same
    concession `host_entry_macho_check.sh` makes and records: XNU's own reader compiling against that
    layout is what the device run tests.

What it does not do, stated rather than implied: it does not run XNU's parser, and it cannot say
whether the *entry point* is reachable in the user's address space - only that the file describes it
the way the loader requires. `--selftest` mutates a copy of the blob one field at a time and requires
every one of those mutations to be refused, because a checker that cannot fail proves nothing.

Exit 0 clean, 1 a check failed, 2 a tool problem.
"""

import argparse
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
DEFAULT_ELF = os.path.join(REPO_ROOT, "out", "stage90", "xnu_arm_entry.elf")
MMAN = os.path.join(XNU, "bsd/sys/mman.h")

PAGE = 0x1000

# The one transcription: `mach-o/loader.h`'s 32-bit struct layouts. `struct mach_header` is seven
# `uint32_t`s; `struct segment_command` and `struct thread_command` are checked field by field, so the
# offsets below are what they are *used* at rather than a second definition of the struct - a wrong
# one here fails the build instead of reaching the device.
SEG = {
    "cmd": 0, "cmdsize": 4, "segname": 8, "vmaddr": 24, "vmsize": 28, "fileoff": 32,
    "filesize": 36, "maxprot": 40, "initprot": 44, "nsects": 48, "flags": 52,
}
SEG_SIZE = 56
THREAD_HDR = 8       # cmd, cmdsize
THREAD_STATE = 8     # flavor, count - the two words load_unixthread skips over
THREAD_CMD_MIN = 16  # the header plus flavor and count

failures = []
notes = []


def fail(msg):
    failures.append(msg)


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def u64(blob, off):
    return struct.unpack_from("<Q", blob, off)[0]


def hdr_define(path, name, base=0):
    """The value of `#define name ...` in a header, as an int.

    Apple spells these several ways - `0xfeedface`, `12`, `((vm_prot_t) 0x01)`, an expression over
    other defines - so the extractor takes the first `0x`-or-decimal literal on the line and refuses
    rather than guesses when there is none.
    """
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        sys.exit(f"cannot read {path}: {e}")
    for line in text.split("\n"):
        m = re.match(r"\s*#\s*define\s+%s\b(.*)$" % re.escape(name), line)
        if not m:
            continue
        rest = m.group(1)
        lit = re.search(r"0[xX][0-9a-fA-F]+|\b\d+\b", rest)
        if lit:
            return int(lit.group(0), 0)
        # An expression such as `(1 << 2)`: evaluate it with only the earlier defines in scope.
        return None
    sys.exit(f"{path} does not define {name} - the header this check reads has moved")


def syscall_getpid():
    """The fixture's syscall, from the place XNU writes it down.

    `bsd/kern/syscalls.master`'s own line - `20 AUE_GETPID ALL { int getpid(void); }` - is the number
    the BSD dispatcher indexes `sysent` with, and the prototype is what makes `sy_narg` zero:
    `unix_syscall` only calls `arm_get_syscall_args` when `callp->sy_narg != 0`
    (`bsd/dev/arm/systemcalls.c:117`), so the program's argument registers are read by nothing at all.
    The *sign* is the ABI and it is checked here: `osfmk/arm/locore.s`'s `fleh_swi` computes
    `r5 = -r12` and branches to `fleh_swi_unix` when that is `<= 0`, so a positive number in r12 is the
    unix path and a negative one is a mach trap - 478's fixture called the mach side (`-61`,
    `thread_switch`), and this one calls the BSD side.

    The other end of the same decision is the image's own table, and it is checked where it can be:
    `tools/check_sysent_table.py` reads `sysent[20].sy_call` out of the linked image and requires it to
    be `--wrap=getpid`'s wrapper on `getpid`. A number here that disagreed with the image's table would
    otherwise be a fixture calling a syscall the image does not dispatch where this file says.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+AUE_GETPID\s+ALL\s+\{\s*int\s+getpid\s*\(\s*void\s*\)\s*;", master,
                  re.M)
    if not m:
        sys.exit("bsd/kern/syscalls.master no longer has an `AUE_GETPID ALL { int getpid(void); }` "
                 "line - the fixture's syscall number cannot be checked against the master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts getpid at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    return number


def syscall_mmap():
    """The program's second syscall, and the prototype that decides how its arguments arrive.

    `bsd/kern/syscalls.master`'s own line - `197 AUE_MMAP ALL { user_addr_t mmap(caddr_t addr,
    size_t len, int prot, int flags, int fd, off_t pos) NO_SYSCALL_STUB; }` - is the line Apple
    generates `sysent[197]` from, and the parameter list is what decides the **munger**:
    `munge_wwwwwl`, five 4-byte arguments and one 8-byte `off_t`, which is the function
    `arm_get_u32_syscall_args` (`bsd/dev/arm/systemcalls.c:337`) calls to turn the saved registers
    into the eight words the wrapper in `entry_trace.c` reads. The number is returned, and the
    parameter list is checked against the one the fixture's header comment was written for - because a
    prototype change moves every argument word, and none of it would show on the device: `MAP_ANON`
    makes `fd` and `pos` inoperative, so a wrong argument arrangement is still a working `mmap`.

    The two ends of that bridge are both machine-checked - this line, and the munger the *image* names
    in `sysent[197].sy_arg_munge32` (`tools/check_sysent_table.py`). Only Apple's own rule for
    spelling a munger's name (`w` for a 4-byte argument, `l` for an 8-byte one, in order) is stated in
    prose rather than derived.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+AUE_MMAP\s+ALL\s+\{\s*user_addr_t\s+mmap\s*\(([^)]*)\)", master, re.M)
    if not m:
        sys.exit("bsd/kern/syscalls.master no longer has the `AUE_MMAP ALL { user_addr_t mmap(...) }` "
                 "line - the fixture's second syscall number and its argument layout cannot be "
                 "checked against the master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts mmap at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    params = " ".join(m.group(2).split())
    want = "caddr_t addr, size_t len, int prot, int flags, int fd, off_t pos"
    if params != want:
        sys.exit(f"syscalls.master's mmap takes `{params}`, and the armv7k reading this check encodes "
                 f"was written for `{want}`: the munger's name, the register order in "
                 f"entry_ramdisk.s, and the eight words the wrapper in entry_trace.c reads all have "
                 f"to be re-derived from the new prototype before the fixture can be believed")
    return number


def arm_pgshift():
    """The kernel's page shift, from the file that defines it for this architecture.

    `mmap`'s length is rounded up to a page by `vm_map_enter`, so the fixture's request is "one page"
    only if the kernel's page is 4096 - and on this target it is: `osfmk/arm/proc_reg.h`'s
    `ARM_PGSHIFT` is 12 for `__arm__`, where the arm64 file's is 14. The length is read from here
    rather than written as `0x1000` so that the fixture's claim is compared with the kernel's own
    definition of a page instead of with a literal that happens to agree with it.
    """
    return hdr_define(os.path.join(XNU, "osfmk/arm/proc_reg.h"), "ARM_PGSHIFT")


def init_pid():
    """The pid the kernel gives the process the RAM disk is exec'd into, from Apple's own source.

    `bsd_utaskbootstrap` clones the init process out of `kernproc` and then holds it by name:
    `initproc = proc_find(1)` (`bsd/kern/bsd_init.c`), with a `panic("bsd_utaskbootstrap: initproc not
    set")` in the same block for the case where it is not there. `load_init_program(p)` in
    `bsd_do_post` execs this Mach-O into that process. The number is read out of the source rather
    than written here because the fixture's `cmp r0, #N` *is* the claim, and a literal in this file
    would be a second definition of it with nothing comparing the two - the shape this project's
    check-everything rule exists for. 478's console measured the same identity from the other side
    (`pid 1 exited -- exit reason namespace 2 subcode 0x4`).
    """
    src = open(os.path.join(XNU, "bsd/kern/bsd_init.c"), encoding="utf-8", errors="replace").read()
    m = re.search(r"initproc\s*=\s*proc_find\(\s*(\d+)\s*\)", src)
    if not m:
        sys.exit("bsd/kern/bsd_init.c no longer has an `initproc = proc_find(N)` line - the pid the "
                 "fixture requires getpid to return cannot be checked against the kernel's own source")
    return int(m.group(1))


def sign24(word):
    """The 24-bit branch offset of an ARM `b`/`bl`, sign-extended and scaled."""
    imm = word & 0xFFFFFF
    if imm & 0x800000:
        imm -= 0x1000000
    return imm * 4


# How many instructions the program at the entry point is. `entry_ramdisk.s` asserts the same length
# in an `.if` over its own labels, so the two are a pair: a program that grew would fail to assemble
# and a program that shrank would fail here.
PROGRAM_WORDS = 27

# The ARM condition codes the program's branches use, by name: `bne`, `bcs` and `b`. The names are what
# the *reading* rests on for one of them - `unix_syscall`'s error convention is the carry bit
# (`arm_prepare_u32_syscall_return`, `bsd/dev/arm/systemcalls.c:279`), so the test after `mmap` has to
# be `bcs` and not `blt`.
COND = {"eq": 0x0, "ne": 0x1, "cs": 0x2, "mi": 0x4, "lt": 0xB, "al": 0xE}


def imm12(word):
    """The 12-bit immediate of an ARM data-processing form, rotated as the encoding says.

    `mov r0, #N` is not always `0xE3A00000 | N`: bits 11:8 are a rotate count, and the value is
    `imm8 ROR (2 * rot)`. Returning the *rotated* value is what makes a `cmp r0, #0x100` written as a
    rotated `1` fail the comparison below instead of passing as the number it stands for.
    """
    imm, rot = word & 0xFF, (word >> 8) & 0xF
    return ((imm >> (2 * rot)) | (imm << (32 - 2 * rot))) & 0xFFFFFFFF if rot else imm, rot


def decode_arm(word, index):
    """The instruction `word` is, as `(mnemonic, operands)`, for the forms `entry_ramdisk.s` uses.

    Not a disassembler: it recognises the shapes the program is written in - `svc`, `udf`, `b`, the
    three `mov` forms, `cmp` against a register and against an immediate, and `ldr`/`str` with a
    zero-offset immediate addressing mode - and returns `("?", (word,))` for anything else. That is
    deliberate: a program word outside those shapes has to fail the comparison below rather than be
    described by a guess, and a *disassembler* here would be a second opinion about ARM that could be
    wrong in the same direction as the fixture.

    Branches are decoded with their target as an **index into the program** rather than a byte
    address, because that is what makes "the failure branch lands on the `udf`" a statement the table
    below can make; a target that is unaligned, negative or past the last word is returned as a string
    so that it compares unequal to every index and the message shows the address the device would have
    jumped to.
    """
    cond = word >> 28

    if word & 0xFFFFFF00 == 0xEF000000:                       # svc #imm24
        return ("svc", (word & 0xFF,))

    if word & 0x0FF00000 == 0x07F00000 and word & 0xF0 == 0xF0:         # udf #imm16
        # `cond 0111 1111 imm12 1111 imm4`, so the 16-bit immediate is `imm12:imm4` - bits 19:8 and
        # bits 3:0 - and the `1111` in between is what tells a `udf` from any other data-processing
        # word. Read as `(word >> 8) & 0xFFF << 4 | word & 0xF`, and not as the top byte of the word:
        # `udf #1` is 0xe7f000f1, whose immediate is in the last nibble.
        return ("udf", ((((word >> 8) & 0xFFF) << 4) | (word & 0xF),))

    if word & 0x0E000000 == 0x0A000000:                       # b (24-bit offset, not bl)
        target = index * 4 + 8 + sign24(word)
        if target % 4 or target < 0 or target >= PROGRAM_WORDS * 4:
            return ("b", (cond, f"0x{target:x} outside the program"))
        return ("b", (cond, target // 4))

    if word & 0x0FF00000 == 0x03A00000:                       # mov rd, #imm
        value, rot = imm12(word)
        return ("mov", ((word >> 12) & 0xF, value, rot))

    if word & 0x0FF00000 == 0x03000000:                       # movw rd, #imm16
        return ("movw", ((word >> 12) & 0xF,
                         (((word >> 4) & 0xF000) | (word & 0xFFF)), 0))

    if word & 0x0FF00000 == 0x03E00000:                       # mvn rd, #imm
        value, rot = imm12(word)
        return ("mvn", ((word >> 12) & 0xF, value, rot))

    if word & 0x0FF00000 == 0x03500000:                       # cmp rn, #imm
        value, rot = imm12(word)
        return ("cmp_i", ((word >> 16) & 0xF, value, rot))

    if word & 0x0FF00FF0 == 0x01500000:                       # cmp rn, rm
        return ("cmp_r", ((word >> 16) & 0xF, word & 0xF))

    if word & 0x0FB00000 == 0x05900000 or word & 0x0FB00000 == 0x05800000:
        # `ldr`/`str rd, [rn, #imm12]`: P=1, U=1, B=0, W=0, L = bit 20. Bit 25 (register offset) is
        # excluded by the mask, so a register-addressed load is not this shape.
        return ("ldr" if word & 0x00100000 else "str",
                ((word >> 12) & 0xF, (word >> 16) & 0xF, word & 0xFFF))

    return ("?", (word,))


def describe(instruction):
    """A decoded instruction as one line, for a failure message."""
    mnemonic, operands = instruction

    def num(value):
        # An operand is an int for every instruction this decoder recognises, and a decoded *word*
        # for the one it does not - so the formatter has to survive a value it cannot print as a
        # number, or a failure message about an unrecognised word would raise instead of reporting.
        return f"#{value:#x}" if isinstance(value, int) else f"#{value!r}"

    if mnemonic == "?":
        return f"the word 0x{operands[0]:08x}"
    if mnemonic in ("svc", "udf"):
        return f"{mnemonic} {num(operands[0])}"
    if mnemonic == "b":
        names = {0x0: "eq", 0x1: "ne", 0x2: "cs", 0x4: "mi", 0xB: "lt", 0xE: ""}
        target = operands[1] if isinstance(operands[1], str) else f"word {operands[1]}"
        return f"b{names.get(operands[0], '?')} to {target}"
    if mnemonic in ("mov", "movw", "mvn"):
        return f"{mnemonic} r{operands[0]}, {num(operands[1])}"
    if mnemonic == "cmp_i":
        return f"cmp r{operands[0]}, {num(operands[1])}"
    if mnemonic == "cmp_r":
        return f"cmp r{operands[0]}, r{operands[1]}"
    if mnemonic in ("ldr", "str"):
        return f"{mnemonic} r{operands[0]}, [r{operands[1]}, #{operands[2]:#x}]"
    return f"{mnemonic} {operands}"


def program_expectations(decoded, K):
    """What each of the program's 27 words must decode to, in the order they are loaded.

    The values come from the headers and from Apple's source (`K`), never from a literal here: the pids
    from `bsd_init.c`'s `initproc = proc_find(N)`, the two syscall numbers from `syscalls.master`, the
    page length from the kernel's own `ARM_PGSHIFT`, and `prot`/`flags` from `bsd/sys/mman.h`'s
    `PROT_READ|PROT_WRITE` and `MAP_PRIVATE|MAP_ANON`. **One word is deliberately not** - see the
    marker below - because the property it has to have is a property and not a value.

    Word by word, and what a wrong value there would cost on the device:

      - 0..2 ask `getpid` and check the answer, which is 479's program unchanged: the `udf` behind the
        compare is what makes a wrong answer loud, and 478's run is what a `udf` here costs.
      - 3..11 are `mmap`'s six arguments in the registers the armv7k munger reads - `addr`, `len`,
        `prot`, `flags`, `fd`, and the 64-bit `pos` as the pair `r6`/`r8` - with `r5` in the word of
        `struct mmap_args` that is padding (see the fixture's header). `fd` is -1 because `MAP_ANON`
        needs no vnode, and `addr` is 0 so the kernel chooses.
      - 12..13 make the call and test **the carry**: `unix_syscall`'s error convention is
        `regs->cpsr |= PSR_CF`, so a `cmp`/`blt` here would read the same flags it just destroyed and
        a failed `mmap` would be taken for a valid address.
      - 14..20 are the two accesses that are the point of the step: a load of a page nothing has
        touched, then a store to a page the load mapped read-only, each checked afterwards. The `str`
        writes the address into the address it names, so 18..19 are the whole proof that the page is
        the process's own and writable.
      - 21..25 are 479's loop, kept so that the log shows the fault was serviced rather than fatal,
        and 26 is the failure marker.
    """
    # The one value this check does not fix: the word the program puts in r5. It has to be a marker -
    # nonzero, and different from every argument the program loads - because its whole job is to be
    # recognisable in the word of `struct mmap_args` that `mmap` never reads, and "recognisable" is a
    # property of the program rather than a number in a header. The properties are checked below; the
    # shape (a `movw` into r5) is checked here. `None` when word 8 is not that shape at all, which is
    # what makes the `movw` claim a comparison instead of an assumption - the failure message then
    # reads `movw r5, #None` beside the word that is really there.
    word8 = decoded[8]
    marker = word8[1][1] if (word8[0] == "movw" and len(word8[1]) == 3 and
                             isinstance(word8[1][1], int)) else None

    return [
        (0, ("svc", (0x80,))),
        (1, ("cmp_i", (0, K["INIT_PID"], 0))),
        (2, ("b", (COND["ne"], 26))),
        (3, ("mov", (0, 0, 0))),
        (4, ("movw", (1, K["MMAP_LENGTH"], 0))),
        (5, ("mov", (2, K["MMAP_PROT"], 0))),
        (6, ("movw", (3, K["MMAP_FLAGS"], 0))),
        (7, ("mvn", (4, 0, 0))),
        (8, ("movw", (5, marker, 0))),
        (9, ("mov", (6, 0, 0))),
        (10, ("mov", (8, 0, 0))),
        (11, ("mov", (12, K["SYSCALL_MMAP"], 0))),
        (12, ("svc", (0x80,))),
        (13, ("b", (COND["cs"], 26))),
        (14, ("ldr", (3, 0, 0))),
        (15, ("cmp_i", (3, 0, 0))),
        (16, ("b", (COND["ne"], 26))),
        (17, ("str", (0, 0, 0))),
        (18, ("ldr", (1, 0, 0))),
        (19, ("cmp_r", (1, 0))),
        (20, ("b", (COND["ne"], 26))),
        (21, ("mov", (12, K["SYSCALL_GETPID"], 0))),
        (22, ("svc", (0x80,))),
        (23, ("cmp_i", (0, K["INIT_PID"], 0))),
        (24, ("b", (COND["ne"], 26))),
        (25, ("b", (COND["al"], 21))),
        (26, ("udf", (1,))),
    ]


def check_program(blob, fpc, pc, reg, K):
    """The 27 words of `entry_ramdisk.s`'s program, decoded against what they are for.

    This is the assertion experiment 468 wrote for one word, applied to the program 479 replaced it
    with and 480 grew: the one-word version asked "is the entry point a `udf #0`", which measured only
    that the user's mapping was where the file said it was. Every word of a program this size is a way
    to be wrong - a `cmp` against the wrong register or the wrong pid, a branch that lands one
    instruction away, a syscall number in the wrong register, an argument in the wrong register, the
    sign convention instead of the carry - and every one of them is a *silent* difference whose only
    symptom on the device would be a process that runs when it should have stopped, or an init death
    where 478 already had one.
    """
    p = f"{pc:#x}: "
    words = [u32(blob, fpc + i * 4) for i in range(PROGRAM_WORDS)]
    decoded = [decode_arm(word, i) for i, word in enumerate(words)]

    for index, expect in program_expectations(decoded, K):
        if decoded[index] != expect:
            fail(f"{p}word {index} at 0x{pc + index * 4:x} is {describe(decoded[index])}, and this "
                 f"check requires {describe(expect)} - the program the device runs is not the one "
                 f"`entry_ramdisk.s` describes, and nothing else in this build would say so")

    # **The marker's properties, which are the reason word 8 is not fixed above.** Two things make the
    # word in r5 a *reading* rather than a value: it must be nonzero (a zero there is
    # indistinguishable from a register the program never set, which is the defect this project has
    # found in stand-ins three times) and it must differ from every argument the program loads (a
    # marker equal to `len`, `prot`, `flags` or `fd` would make the log's word ambiguous exactly where
    # it is supposed to be unambiguous).
    marker = decoded[8][1][1] if decoded[8][0] == "movw" else None
    arguments = {index: decoded[index][1][1]
                 for index in (4, 5, 6, 7, 9, 10) if decoded[index][0] in ("mov", "movw")}
    if marker == 0:
        fail(f"{p}the word the program puts in r5 is zero, and a zero is what a register nothing "
             f"wrote holds: the marker would stop being a marker, and the log's word of "
             f"`struct mmap_args` would read the same whether the munger copied r5 or nothing")
    for index, value in arguments.items():
        if value == marker:
            fail(f"{p}the word in r5 ({marker:#x}) is also the argument loaded by word {index} "
                 f"({value:#x}), so the marker cannot be told apart from a real argument in the "
                 f"munger's output - which is the only place it is read")

    # Every branch lands inside the program, stated once for all of them: a branch out would leave the
    # user's `__TEXT` for unmapped memory and fault, which the run would show as an abort rather than
    # as a syscall - and the `b` at word 25 has to come back to the loop's first word for the liveness
    # reading the step is built on.
    for index, instruction in enumerate(decoded):
        if instruction[0] == "b" and not isinstance(instruction[1][1], int):
            fail(f"{p}the branch at word {index} (0x{pc + index * 4:x}) targets "
                 f"{instruction[1][1]} - every branch in this program has to land on another "
                 f"instruction of it")


def check_thread_registers(pc, reg, K):
    """The registers `LC_UNIXTHREAD` starts the process with, which the program's words do not cover.

    Two claims, and they are different in kind:

      - **r12**, which is the one register of the twelve the kernel reads for the *first* instruction:
        `fleh_swi` turns it into the index and `arm_get_syscall_number` reads the same word back out of
        the saved state as the `sysent` index. The program reloads it twice more before the syscalls
        that follow, so this is the first call's number and only that.
      - **r0..r11 are zero**, and here that is a statement about the *first* call only: `getpid`'s
        `sy_narg` is 0, so `arm_get_syscall_args` is not called and these registers are read by
        nothing. 480's program then loads them itself - r0..r8 are `mmap`'s arguments by word 11 - so a
        nonzero value *here* would be a stale value the kernel never reads, which is why the check is a
        note rather than a failure and why it is worth reading: it says the file describes a process
        that starts with nothing rather than one that starts mid-conversation.
    """
    p = f"{pc:#x}: "
    number = K["SYSCALL_GETPID"]
    if reg[12] != number:
        fail(f"{p}r12 is {reg[12]}, not {number} (SYS_getpid, from bsd/kern/syscalls.master) - "
             f"`fleh_swi` computes `-r12` = {number} and takes the *unix* path for it, so a different "
             f"number here is a different syscall or the mach path")
    for k in range(13):
        if k != 12 and reg[k] != 0:
            notes.append(f"{p}r{k} is {reg[k]}, which the kernel reads as no argument at all for the "
                         f"first call (sy_narg is 0 for getpid); 480's program loads r0..r8 itself "
                         f"before its second")


def arm_thread_state_count():
    """sizeof(arm_thread_state_t)/4, from the struct rather than from a number here.

    `ARM_THREAD_STATE_COUNT` is `sizeof (arm_thread_state_t)/sizeof(uint32_t)` in
    `osfmk/mach/arm/thread_status.h`, which is an expression and not a literal - so the check counts
    the struct's own members: `__r[13]` plus `__sp`, `__lr`, `__pc`, `__cpsr`. The kernel's
    `machine_thread_set_state` copies exactly `sizeof(struct arm_thread_state)` bytes out of the
    Mach-O's thread state (`osfmk/arm/status.c:286-296`), so this is the number that bounds it.
    """
    text = open(os.path.join(XNU, "osfmk/mach/arm/_structs.h"), encoding="utf-8",
                errors="replace").read()
    m = re.search(r"_STRUCT_ARM_THREAD_STATE\s*\{(.*?)\}", text, re.S)
    if not m:
        sys.exit("osfmk/mach/arm/_structs.h has no _STRUCT_ARM_THREAD_STATE")
    # The comments in this struct sit *after* each member's semicolon, so splitting the body on
    # semicolons first gives every member but the first a leading `/* ... */` - and a member that
    # starts with a comment is still a member. Comments go first; the count was 13 until they did.
    body = re.sub(r"/\*.*?\*/", " ", m.group(1), flags=re.S)
    total = 0
    for member in body.split(";"):
        member = member.strip()
        if not member:
            continue
        arr = re.search(r"\[(\d+)\]", member)
        if arr:
            total += int(arr.group(1))
        elif re.search(r"uint32_t", member):
            total += 1
    if total == 0:
        sys.exit("could not count the members of _STRUCT_ARM_THREAD_STATE")
    return total


def elf_section_for(elf, addr, size):
    """(name, type, file offset, alloc flag) of the section containing [addr, addr+size).

    The disk must be in a section that is both `PROGBITS` and `ALLOC`: `ALLOC` is what puts it in
    the bytes `objcopy` produces, and `PROGBITS` is what keeps it out of the range the payload
    zeroes. `g_stage90_ramdisk` was in `.bss` until 468, so this is a check with a history rather
    than a formality. A section is matched on the address range alone and the type is reported, so a
    `NOBITS` disk is named as such instead of being called missing.
    """
    out = subprocess.run(["readelf", "-S", "-W", elf], capture_output=True, text=True)
    if out.returncode:
        sys.exit(f"readelf failed on {elf}: {out.stderr.strip()}")
    sections = []
    for line in out.stdout.split("\n"):
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
                     r"[0-9a-f]+\s+([A-Z]*)\s", line)
        if not m:
            continue
        sections.append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16),
                         int(m.group(5), 16), "A" in m.group(6)))
    for name, typ, saddr, soff, ssize, alloc in sections:
        if alloc and saddr <= addr and addr + size <= saddr + max(ssize, 1) and ssize:
            return name, typ, soff + (addr - saddr), alloc
    for name, typ, saddr, soff, ssize, alloc in sections:
        if saddr <= addr and addr + size <= saddr + max(ssize, 1) and ssize:
            return name, typ, soff + (addr - saddr), alloc
    fail("no section of %s contains 0x%x + %d - the RAM disk is not in this image at all"
         % (elf, addr, size))
    return None


def nm_value(elf, symbol):
    out = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True, text=True)
    if out.returncode:
        sys.exit(f"nm failed on {elf}: {out.stderr.strip()}")
    for line in out.stdout.split("\n"):
        parts = line.split()
        if len(parts) == 4 and parts[3] == symbol:
            return int(parts[0], 16), int(parts[1], 16)
    for line in out.stdout.split("\n"):
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16), 0
    sys.exit(f"{symbol} is not in {elf}")


def parse(blob, K, size, label=""):
    """Every check, over one blob. `label` names the mutation when --selftest runs it."""
    p = (label + ": ") if label else ""

    if len(blob) < size:
        fail(f"{p}the blob is {len(blob)} bytes and the symbol is {size}")
        return

    magic, cputype, cpusubtype = u32(blob, 0), u32(blob, 4), u32(blob, 8)
    filetype, ncmds, sizeofcmds, flags = u32(blob, 12), u32(blob, 16), u32(blob, 20), u32(blob, 24)

    # --- the header, and what claims the file ------------------------------------------------
    # `exec_mach_imgact` (bsd/kern/kern_exec.c:855) claims by magic, refuses anything but MH_EXECUTE
    # (:859) and refuses MH_CIGAM explicitly (:845). `parse_machfile`'s first test
    # (bsd/kern/mach_loader.c:611-614) is the cputype masked against the host's, its second is
    # `grade_binary` - which for a V7K host accepts only V7K (bsd/dev/arm/kern_machdep.c:105-113).
    if magic != K["MH_MAGIC"]:
        fail(f"{p}magic is 0x{magic:08x}, not MH_MAGIC (0x{K['MH_MAGIC']:08x}) - exec_mach_imgact "
             f"declines the file and the exec is ENOEXEC, which is exactly 467's stop")
    if cputype != K["CPU_TYPE_ARM"]:
        fail(f"{p}cputype is {cputype}, not CPU_TYPE_ARM ({K['CPU_TYPE_ARM']}) - parse_machfile's "
             f"first test fails and the exec is EBADARCH")
    if cpusubtype != K["CPU_SUBTYPE_ARM_V7K"]:
        fail(f"{p}cpusubtype is {cpusubtype}, not CPU_SUBTYPE_ARM_V7K "
             f"({K['CPU_SUBTYPE_ARM_V7K']}) - grade_binary grades this 0 on a V7K host, so the exec "
             f"is EBADARCH")
    if filetype != K["MH_EXECUTE"]:
        fail(f"{p}filetype is {filetype}, not MH_EXECUTE ({K['MH_EXECUTE']})")
    if flags & K["MH_PIE"]:
        fail(f"{p}flags 0x{flags:x} set MH_PIE (0x{K['MH_PIE']:x}): parse_machfile then slides every "
             f"segment by the ASLR offset, and the addresses this check asserts are the unslid ones")
    if flags & K["MH_DYLDLINK"]:
        fail(f"{p}flags 0x{flags:x} set MH_DYLDLINK (0x{K['MH_DYLDLINK']:x}): the file claims to be "
             f"dynamic, which needs a dylinker vnode (mach_loader.c:1155) and CS_DYLD_PLATFORM "
             f"(:1173)")
    if flags != 0:
        notes.append(f"{p}flags are 0x{flags:x}; neither PIE nor DYLDLINK is set, which is what "
                     f"makes this a static executable - legal only because DEVELOPMENT is on "
                     f"(mach_loader.c:631-636)")

    # --- the commands, walked by their own cmdsize chain -------------------------------------
    # `parse_machfile` reads `mach_header_sz + sizeofcmds` bytes from the file (:654-657) and walks
    # `ncmds` commands, rejecting any `cmdsize < sizeof(struct load_command)` or any that runs past
    # the reserved section (:806-812). `sizeofcmds` is asserted against the walk rather than trusted,
    # because a header whose extent is larger than its commands is bytes the kernel reads as a
    # command that is not there.
    end = 28 + sizeofcmds
    if 28 + sizeofcmds > size:
        fail(f"{p}sizeofcmds {sizeofcmds} puts the commands' end at {end}, past the file's {size} "
             f"bytes - parse_machfile refuses that (:654-657)")
    off = 28
    seen = []
    for _ in range(ncmds):
        if off + 8 > end:
            fail(f"{p}command at +{off} runs past sizeofcmds' end {end}")
            break
        cmd, cmdsize = u32(blob, off), u32(blob, off + 4)
        if cmdsize < 8 or off + cmdsize > end:
            fail(f"{p}command 0x{cmd:x} at +{off} has cmdsize {cmdsize}, which is not a command the "
                 f"walk can take (mach_loader.c:806-812)")
            break
        seen.append((off, cmd, cmdsize))
        off += cmdsize
    if off != end:
        fail(f"{p}the {ncmds} commands walk to +{off} and sizeofcmds says +{end}: the header "
             f"describes {end - off} bytes that are not a command")

    kinds = [c for _o, c, _s in seen]
    if kinds != [K["LC_SEGMENT"], K["LC_SEGMENT"], K["LC_UNIXTHREAD"]]:
        fail(f"{p}the commands are {[hex(k) for k in kinds]}, not two LC_SEGMENTs and one "
             f"LC_UNIXTHREAD - the three this file is built from")

    # --- the two segments --------------------------------------------------------------------
    want = [
        # (index, segname, vmaddr, vmsize, fileoff, filesize, maxprot, initprot)
        (0, b"__PAGEZERO", 0, PAGE, 0, 0, 0, 0),
        (1, b"__TEXT", PAGE, PAGE, 0, PAGE, 0x7, 0x5),
    ]
    for idx, name, vmaddr, vmsize, fileoff, filesize, maxprot, initprot in want:
        if idx >= len(seen):
            fail(f"{p}LC_SEGMENT {idx} ({name.decode()}) is missing")
            continue
        o = seen[idx][0]
        got = (blob[o + SEG["segname"]:o + SEG["segname"] + 16].split(b"\0")[0],
               u32(blob, o + SEG["vmaddr"]), u32(blob, o + SEG["vmsize"]),
               u32(blob, o + SEG["fileoff"]), u32(blob, o + SEG["filesize"]),
               u32(blob, o + SEG["maxprot"]), u32(blob, o + SEG["initprot"]),
               u32(blob, o + SEG["nsects"]), u32(blob, o + SEG["flags"]))
        gname, gvmaddr, gvmsize, gfileoff, gfilesize, gmaxprot, ginitprot, gnsects, gflags = got
        if gname != name:
            fail(f"{p}segment {idx} is named {gname!r}, not {name!r}")
        for what, g, w in (("vmaddr", gvmaddr, vmaddr), ("vmsize", gvmsize, vmsize),
                           ("fileoff", gfileoff, fileoff), ("filesize", gfilesize, filesize),
                           ("maxprot", gmaxprot, maxprot), ("initprot", ginitprot, initprot)):
            if g != w:
                fail(f"{p}{name.decode()} {what} is 0x{g:x}, not 0x{w:x}")
        if gnsects != 0:
            # load_segment's own check is `total_section_size / sizeof(section) < nsects`
            # (mach_loader.c:1583-1585), so sections that do not fit are LOAD_BADMACHO.
            fail(f"{p}{name.decode()} declares {gnsects} sections in a {SEG_SIZE}-byte command")
        if gflags != 0:
            notes.append(f"{p}{name.decode()} flags are 0x{gflags:x}; SG_PROTECTED_VERSION_1 would "
                         f"route the segment through the DSMOS decrypt path")

    if len(seen) == 3:
        # `map_segment` panics for a 32-bit binary on a mapping that is not page-aligned in both
        # address spaces (mach_loader.c:1339-1350), and `load_segment` refuses a file offset that is
        # not page-aligned (:1610-1622). Both are panics/refusals rather than warnings, so they are
        # asserted for both segments rather than for `__TEXT` alone.
        for idx, (o, _c, _s) in enumerate(seen[:2]):
            for what, field in (("vmaddr", "vmaddr"), ("vmsize", "vmsize"),
                                ("fileoff", "fileoff"), ("filesize", "filesize")):
                v = u32(blob, o + SEG[field])
                if v % PAGE:
                    fail(f"{p}segment {idx}'s {what} is 0x{v:x}, which is not page-aligned: "
                         f"map_segment panics on that for a 32-bit binary")
        # `load_segment`'s bounds test (:1570-1573): fileoff + filesize must be inside the file.
        for idx, (o, _c, _s) in enumerate(seen[:2]):
            fo, fs = u32(blob, o + SEG["fileoff"]), u32(blob, o + SEG["filesize"])
            if fo + fs > size:
                fail(f"{p}segment {idx} reads file [{fo}, {fo + fs}) and the file is {size} bytes")
        # The header segment's vmsize is its filesize. A larger vmsize is legal Mach-O - the loader
        # maps it and reads only filesize bytes - but the tail would be zero fill *inside* a segment
        # this check requires to be R|X, i.e. executable zeros after the one instruction, which is a
        # different experiment from the one this file is.
        o2 = seen[1][0]
        if u32(blob, o2 + SEG["vmsize"]) != u32(blob, o2 + SEG["filesize"]):
            fail(f"{p}__TEXT maps 0x{u32(blob, o2 + SEG['vmsize']):x} bytes and reads "
                 f"0x{u32(blob, o2 + SEG['filesize']):x} from the file: the difference is zero fill "
                 f"inside an R|X segment")
        # `vm_map_has_hard_pagezero(map, 0x1000)` must be true after the parse
        # (mach_loader.c:452-479) and the only thing that raises the map's min_offset is a segment
        # with `vmaddr == 0 && filesize == 0 && vmsize != 0` and both protections NONE (:1645-1650).
        o = seen[0][0]
        if not (u32(blob, o + SEG["vmaddr"]) == 0 and u32(blob, o + SEG["filesize"]) == 0
                and u32(blob, o + SEG["vmsize"]) >= PAGE
                and u32(blob, o + SEG["initprot"]) == 0 and u32(blob, o + SEG["maxprot"]) == 0):
            fail(f"{p}the first segment is not a page zero: without it vm_map_has_hard_pagezero is "
                 f"false and load_machfile answers LOAD_BADMACHO before mapping anything")
        # `found_header_segment` (:906-916): exactly one segment with fileoff 0, filesize > 0 and
        # R|X, and no second one.
        headers = [i for i, (o, _c, _s) in enumerate(seen[:2])
                   if u32(blob, o + SEG["fileoff"]) == 0 and u32(blob, o + SEG["filesize"]) > 0]
        if headers != [1]:
            fail(f"{p}segments with a non-empty mapping at file offset 0 are {headers}, not [1]")
        else:
            o = seen[1][0]
            prot = u32(blob, o + SEG["initprot"])
            if (prot & (K["VM_PROT_READ"] | K["VM_PROT_EXECUTE"])) != (K["VM_PROT_READ"]
                                                                       | K["VM_PROT_EXECUTE"]):
                fail(f"{p}__TEXT's initprot 0x{prot:x} is not R|X, which found_header_segment and "
                     f"validentry both require")

    # --- the thread state ---------------------------------------------------------------------
    if len(seen) == 3:
        o = seen[2][0]
        flavor = u32(blob, o + THREAD_STATE)
        count = u32(blob, o + THREAD_STATE + 4)
        if flavor != K["ARM_THREAD_STATE"]:
            # thread_entrypoint (osfmk/arm/status.c:683-705) accepts ARM_THREAD_STATE and nothing
            # else: any other flavor is KERN_INVALID_ARGUMENT, which load_threadentry turns into
            # LOAD_FAILURE.
            fail(f"{p}the thread flavor is {flavor}, not ARM_THREAD_STATE "
                 f"({K['ARM_THREAD_STATE']}) - thread_entrypoint refuses every other flavor")
        if count < K["ARM_THREAD_STATE_COUNT"]:
            # thread_userstack (status.c:580) and machine_thread_set_state (status.c:281) both
            # refuse a count smaller than the state struct.
            fail(f"{p}the thread state count is {count}, less than ARM_THREAD_STATE_COUNT "
                 f"({K['ARM_THREAD_STATE_COUNT']}) - thread_userstack and "
                 f"machine_thread_set_state both refuse it")
        elif count != K["ARM_THREAD_STATE_COUNT"]:
            notes.append(f"{p}the thread state count is {count}, not "
                         f"{K['ARM_THREAD_STATE_COUNT']}")
        if o + THREAD_CMD_MIN + count * 4 > 28 + sizeofcmds:
            fail(f"{p}the thread state's {count} words run past the command's own cmdsize")
        else:
            # struct arm_thread_state: r[13], sp, lr, pc, cpsr.
            reg = [u32(blob, o + THREAD_STATE + 8 + k * 4) for k in range(13)]
            sp = u32(blob, o + THREAD_STATE + 8 + 13 * 4)
            pc = u32(blob, o + THREAD_STATE + 8 + 15 * 4)
            cpsr = u32(blob, o + THREAD_STATE + 8 + 16 * 4)
            if sp != 0:
                notes.append(f"{p}sp is 0x{sp:x}: thread_userstack then sets customstack and the "
                             f"kernel does not allocate the stack itself")
            # validentry (:1878-1890): inside an R+X segment or 0.
            o2 = seen[1][0]
            vmaddr, vmsize = u32(blob, o2 + SEG["vmaddr"]), u32(blob, o2 + SEG["vmsize"])
            if not (vmaddr <= pc < vmaddr + vmsize):
                fail(f"{p}pc is 0x{pc:x}, outside __TEXT [0x{vmaddr:x}, 0x{vmaddr + vmsize:x}) - "
                     f"validentry would be 0 and parse_machfile's pass 3 answers LOAD_FAILURE")
            else:
                fo = u32(blob, o2 + SEG["fileoff"])
                fs = u32(blob, o2 + SEG["filesize"])
                fpc = fo + (pc - vmaddr)
                if fpc + PROGRAM_WORDS * 4 > fo + fs:
                    fail(f"{p}pc 0x{pc:x} is at file offset 0x{fpc:x}, past __TEXT's filesize: the "
                         f"program is {PROGRAM_WORDS} instructions and this leaves room for fewer")
                else:
                    check_program(blob, fpc, pc, reg, K)
                    check_thread_registers(pc, reg, K)
                    notes.append(f"{p}__TEXT's file [{fo}, {fo + fs}) maps to "
                                 f"[0x{vmaddr:x}, 0x{vmaddr + vmsize:x}); "
                                 f"pc 0x{pc:x} is file offset 0x{fpc:x}, and the {PROGRAM_WORDS} "
                                 f"words there are the program this file describes")
                    notes.append(f"{p}cpsr 0x{cpsr:x} - machine_thread_set_state keeps only the flags "
                                 f"from this word and takes the mode from PSR_USERDFLT (0x10), so "
                                 f"the thread starts in User mode whatever it says")

    return len(seen)


def load(elf):
    """The disk's bytes, or None if the section that holds them is the wrong kind.

    The type is checked before the bytes are read rather than after: for a `NOBITS` section there are
    no bytes in the file at the symbol's address, so parsing whatever is at that file offset would
    report a dozen failures derived from the ELF header and bury the one that matters.
    """
    addr, size = nm_value(elf, "g_stage90_ramdisk")
    sec = elf_section_for(elf, addr, size)
    if sec is None:
        return None, size
    name, typ, off, alloc = sec
    if typ != "PROGBITS":
        fail(f"the section holding g_stage90_ramdisk ({name}) is {typ}, not PROGBITS - a NOBITS "
             f"region is zeroed by the payload after the copy and would erase the executable")
        return None, size
    if not alloc:
        fail(f"the section holding g_stage90_ramdisk ({name}) is not ALLOC, so it is not in the "
             f"bytes objcopy produces and the device's page would hold whatever was there before")
        return None, size
    blob = open(elf, "rb").read()[off:off + size]
    notes.append(f"g_stage90_ramdisk is 0x{addr:x} + {size} (0x{size:x}), in {name} ({typ}, ALLOC) "
                 f"at file offset 0x{off:x}")
    return blob, size


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("elf", nargs="?", default=DEFAULT_ELF)
    ap.add_argument("--selftest", action="store_true",
                    help="mutate the blob one field at a time and require every mutation to be refused")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        sys.exit(f"no {args.elf} - build the entry image first")

    K = {
        "MH_MAGIC": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"), "MH_MAGIC"),
        "MH_EXECUTE": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                 "MH_EXECUTE"),
        "LC_SEGMENT": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                 "LC_SEGMENT"),
        "LC_UNIXTHREAD": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                    "LC_UNIXTHREAD"),
        "MH_PIE": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"), "MH_PIE"),
        "MH_DYLDLINK": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                  "MH_DYLDLINK"),
        "CPU_TYPE_ARM": hdr_define(os.path.join(XNU, "osfmk/mach/machine.h"), "CPU_TYPE_ARM"),
        "CPU_SUBTYPE_ARM_V7K": hdr_define(os.path.join(XNU, "osfmk/mach/machine.h"),
                                          "CPU_SUBTYPE_ARM_V7K"),
        "VM_PROT_READ": hdr_define(os.path.join(XNU, "osfmk/mach/vm_prot.h"), "VM_PROT_READ"),
        "VM_PROT_WRITE": hdr_define(os.path.join(XNU, "osfmk/mach/vm_prot.h"), "VM_PROT_WRITE"),
        "VM_PROT_EXECUTE": hdr_define(os.path.join(XNU, "osfmk/mach/vm_prot.h"), "VM_PROT_EXECUTE"),
        "ARM_THREAD_STATE": hdr_define(os.path.join(XNU, "osfmk/mach/arm/thread_status.h"),
                                       "ARM_THREAD_STATE"),
        "ARM_THREAD_STATE_COUNT": arm_thread_state_count(),
        # 479: the program's syscall number and the pid it compares the answer with - each read out of
        # the file that defines it, never written here. See `syscall_getpid` and `init_pid`.
        "SYSCALL_GETPID": syscall_getpid(),
        "INIT_PID": init_pid(),
        # 480: the second syscall, the page its length comes from, and the two mman flags by name.
        "SYSCALL_MMAP": syscall_mmap(),
        "MMAP_LENGTH": 1 << arm_pgshift(),
        "MMAP_PROT": hdr_define(MMAN, "PROT_READ") | hdr_define(MMAN, "PROT_WRITE"),
        "MMAP_FLAGS": hdr_define(MMAN, "MAP_PRIVATE") | hdr_define(MMAN, "MAP_ANON"),
    }
    notes.append("from the headers: MH_MAGIC=0x%(MH_MAGIC)x MH_EXECUTE=%(MH_EXECUTE)d "
                 "MH_PIE=0x%(MH_PIE)x MH_DYLDLINK=0x%(MH_DYLDLINK)x "
                 "LC_SEGMENT=0x%(LC_SEGMENT)x LC_UNIXTHREAD=0x%(LC_UNIXTHREAD)x "
                 "CPU_TYPE_ARM=%(CPU_TYPE_ARM)d CPU_SUBTYPE_ARM_V7K=%(CPU_SUBTYPE_ARM_V7K)d "
                 "VM_PROT=R|X=0x5 ARM_THREAD_STATE=%(ARM_THREAD_STATE)d "
                 "ARM_THREAD_STATE_COUNT=%(ARM_THREAD_STATE_COUNT)d "
                 "SYSCALL_GETPID=%(SYSCALL_GETPID)d (bsd/kern/syscalls.master) "
                 "INIT_PID=%(INIT_PID)d (bsd/kern/bsd_init.c's `initproc = proc_find(N)`) "
                 "SYSCALL_MMAP=%(SYSCALL_MMAP)d (bsd/kern/syscalls.master) "
                 "MMAP_LENGTH=0x%(MMAP_LENGTH)x (1 << ARM_PGSHIFT) "
                 "MMAP_PROT=0x%(MMAP_PROT)x MMAP_FLAGS=0x%(MMAP_FLAGS)x (bsd/sys/mman.h)" % K)

    blob, size = load(args.elf)
    if blob is None:
        return report()
    parse(blob, K, size)
    if failures:
        return report()

    if args.selftest:
        # Every mutation below is a field this check has an opinion about. If any of them survives,
        # the check is decorative and this is the only place that can say so. The offsets are the
        # file's own: the header is 28 bytes, each segment command 56, the thread command 84 with its
        # state starting 16 bytes in.
        S1 = 28 + SEG_SIZE             # the second command's first byte
        T = S1 + SEG_SIZE              # the thread command's first byte
        STATE = T + THREAD_HDR + THREAD_STATE   # r[0]
        mutations = [
            ("magic", 0, K["MH_MAGIC"] ^ 0x1),
            ("cputype", 4, K["CPU_TYPE_ARM"] + 1),
            ("cpusubtype", 8, K["CPU_SUBTYPE_ARM_V7K"] + 1),
            ("filetype", 12, K["MH_EXECUTE"] + 1),
            ("ncmds", 16, 2),
            ("flags: MH_PIE", 24, K["MH_PIE"]),
            ("flags: MH_DYLDLINK", 24, K["MH_DYLDLINK"]),
            ("sizeofcmds", 20, 0xC4 - 4),
            ("__PAGEZERO vmsize", 28 + SEG["vmsize"], 0),
            ("__PAGEZERO filesize", 28 + SEG["filesize"], PAGE),
            ("__PAGEZERO initprot", 28 + SEG["initprot"], 0x1),
            ("__TEXT vmaddr", S1 + SEG["vmaddr"], 0),
            ("__TEXT vmsize", S1 + SEG["vmsize"], 0x2000),
            ("__TEXT fileoff", S1 + SEG["fileoff"], PAGE),
            ("__TEXT filesize", S1 + SEG["filesize"], 0),
            ("__TEXT initprot", S1 + SEG["initprot"], 0x1),
            ("__TEXT nsects", S1 + SEG["nsects"], 4),
            ("__TEXT prot not page aligned", S1 + SEG["vmaddr"], PAGE + 1),
            ("thread flavor", STATE - 8, 2),
            ("thread count", STATE - 4, 4),
            ("entry pc", STATE + 15 * 4, PAGE),
            ("r12: the syscall number", STATE + 12 * 4, K["SYSCALL_GETPID"] + 1),
            # 479's program: the five words at the entry point, one at a time, and the register the
            # syscall number lives in. A mutation that survived any of these would mean the device run
            # could differ from the program `entry_ramdisk.s` describes without this check saying so.
            # The first two are kept from that version because they are the two *defects the checks
            # themselves had*: the compare's register (a mask that forgot `Rn`, 205) and a rotated
            # immediate written as the value it stands for.
            ("the instruction", 0xE0, 0xE1A00000),
            ("the syscall immediate", 0xE0, 0xEF000081),
            ("the compare's register", 0xE4, 0xE3510001),
            ("the compare with a rotated immediate", 0xE4, 0xE3500101),
            ("the failure marker", 0xE0 + 26 * 4, 0xE7F000F2),
            # 480's program, as *every one of its words except 8*: the low bit is the cheapest change
            # that leaves a word a word, and it moves the `svc`'s immediate, a branch's target, a
            # `mov`'s value or a load's register - so no word of the program can differ from what the
            # check requires without the check saying so. `entry_ramdisk.s`'s `.if` fixes the program's
            # length, so this list cannot silently stop covering the program.
            #
            # **Word 8 is left out on purpose, and it is the one place this list is not complete.**
            # That word is the marker in r5, and the check requires it to be a nonzero `movw` into r5
            # that repeats no argument - properties, not a value - so a low-bit flip there changes only
            # the property the check deliberately leaves free and *should* be accepted (see
            # `program_expectations`). What has to be refused is a change of shape or of the properties,
            # and the mutations below are exactly those: a nop, a zero, a repeated argument, and a
            # rotated `mov` where the `movw` is.
            *[(f"program word {i}", 0xE0 + i * 4, u32(blob, 0xE0 + i * 4) ^ 1)
              for i in range(PROGRAM_WORDS) if i != 8],
            # And the mutations that are about a *shape* rather than a value, each a plausible way to
            # write the same intent wrongly. The first is the one this step exists for:
            # `arm_prepare_u32_syscall_return` reports an error by setting the carry bit
            # (`regs->cpsr |= PSR_CF`), so a `blt` there tests a sign that means nothing and a failed
            # `mmap` would be taken for a valid address. The second drops the test altogether.
            ("the errno test is a sign test", 0xE0 + 13 * 4, 0xBA00000B),
            ("the errno test is unconditional", 0xE0 + 13 * 4, 0xEA00000B),
            ("the pad word is never set (a nop)", 0xE0 + 8 * 4, 0xE1A05005),
            ("the pad word is zero", 0xE0 + 8 * 4, 0xE3A05000),
            ("the pad word is a rotated mov", 0xE0 + 8 * 4, 0xE3A05C5A),
            ("the pad word repeats an argument", 0xE0 + 8 * 4, 0xE3015000),
            ("len in prot's register", 0xE0 + 4 * 4, 0xE3012000),
            ("the failure marker is 478's udf #0", 0xE0 + 26 * 4, 0xE7F000F0),
        ]
        survived = []
        for name, off, value in mutations:
            if off + 4 > len(blob):
                continue
            mutated = bytearray(blob)
            struct.pack_into("<I", mutated, off, value)
            del failures[:]
            kept = len(notes)
            parse(bytes(mutated), K, size, label=f"selftest:{name}")
            del notes[kept:]   # a mutated blob's notes describe a file that does not exist
            if not failures:
                survived.append(name)
        # The last mutation's own failures are still in the list, and they are *expected* - every
        # mutation must be refused. They are dropped here rather than reported, or the selftest would
        # print a failure for the last thing it proved.
        del failures[:]
        if survived:
            fail("--selftest: these mutations were accepted, so the checks above do not see them: "
                 + ", ".join(survived))
        else:
            notes.append(f"--selftest: all {len(mutations)} mutations were refused")

    return report()


def report():
    if failures:
        print("the RAM disk is not a Mach-O parse_machfile would load:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        return 1
    print("ok: the RAM disk's Mach-O is one parse_machfile and load_machfile would load")
    for n in notes:
        print(f"    {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
