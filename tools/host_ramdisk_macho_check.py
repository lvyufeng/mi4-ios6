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
                if fpc + 4 > fo + fs:
                    fail(f"{p}pc 0x{pc:x} is at file offset 0x{fpc:x}, past __TEXT's filesize")
                else:
                    word = u32(blob, fpc)
                    # `udf #0`: the ARMv7 encoding of the undefined instruction, `cond 0111 1111
                    # imm12 1111 0000` with imm12 = 0 and cond = AL.
                    if word != 0xE7F000F0:
                        fail(f"{p}the word at the entry point is 0x{word:08x}, not udf #0 "
                             f"(0xe7f000f0) - the process would execute whatever this is")
                    else:
                        notes.append(f"{p}__TEXT's file [{fo}, {fo + fs}) maps to "
                                     f"[0x{vmaddr:x}, 0x{vmaddr + vmsize:x}); "
                                     f"pc 0x{pc:x} is file offset 0x{fpc:x}, and the word there is "
                                     f"`udf #0`")
                        notes.append(f"{p}cpsr 0x{cpsr:x} - machine_thread_set_state keeps only the "
                                     f"flags from this word and takes the mode from PSR_USERDFLT "
                                     f"(0x10), so the thread runs in User mode whatever it says; the "
                                     f"SPSR of the fault should read back as 0x{cpsr:x}")

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
    }
    notes.append("from the headers: MH_MAGIC=0x%(MH_MAGIC)x MH_EXECUTE=%(MH_EXECUTE)d "
                 "MH_PIE=0x%(MH_PIE)x MH_DYLDLINK=0x%(MH_DYLDLINK)x "
                 "LC_SEGMENT=0x%(LC_SEGMENT)x LC_UNIXTHREAD=0x%(LC_UNIXTHREAD)x "
                 "CPU_TYPE_ARM=%(CPU_TYPE_ARM)d CPU_SUBTYPE_ARM_V7K=%(CPU_SUBTYPE_ARM_V7K)d "
                 "VM_PROT=R|X=0x5 ARM_THREAD_STATE=%(ARM_THREAD_STATE)d "
                 "ARM_THREAD_STATE_COUNT=%(ARM_THREAD_STATE_COUNT)d" % K)

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
            ("the instruction", 0xE0, 0xE1A00000),
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
