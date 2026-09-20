#!/usr/bin/env python3
"""
Check that the offsets this image reads `struct arm_saved_state` at are the ones the kernel built the
frame with - from three sources that must agree, none of which is a copy of the others.

A fourth source is checked because it exists and should not be trusted on its own:
`stages/stage90/xnu_arm_boot/assym.s` is a **hand-written** stand-in that mirrors part of this same
struct (`SS_R0`, `SS_R12`, `SS_LR`, `SS_PC`, `SS_CPSR`, `SS_SIZE`), and it is first on the entry
build's include path - so it is what `osfmk/arm/start.s` is assembled against in *this* image, while
`out/xnu_asm_obj/locore.o` (`dataabt_from_kernel`, which is what actually builds the frame at run
time) is assembled by `tools/assemble_arm_layer.sh` against the generated one. Two files, one struct,
one include path: the same shape as `tools/check_pthread_table_slots.py`'s table and the same shape as
this project's twenty-four "one value, two definitions" defects. Every constant that file declares is
now compared here, and the ones `start.s` never mentions are reported rather than assumed harmless -
`SS_SIZE` is load-bearing there (`sub sp, sp, SS_SIZE`), and a stand-in whose *value* is a placeholder
is worse than a missing one, because a missing one stops the assembler with the name in the message.

    ./tools/check_saved_state_offsets.py                       # this configuration, this image
    ./tools/check_saved_state_offsets.py --selftest             # mutate each source, require refusal
    ./tools/check_saved_state_offsets.py --config STAGE90_XNU --verbose

**The configuration is part of the comparison, and it defaults to the one this image is built with.**
`CPU_DECREMENTER` is 104 in `STAGE90_XNU` and 88 in `RELEASE` - `struct cpu_data` is a different size
in the two - so a run that compared the RELEASE file would be reading a *different structure* while
saying nothing about it. The FAIL line names the file it read for that reason: a comparison against the
wrong configuration is a fact about the path, and a reader cannot see it in the numbers.

Why this exists
---------------
Experiment 474 makes the abort record read the *frame*: `regs->pc` is the instruction that faulted and
`regs->cpsr` says whether it faulted in user mode or in the kernel, and those are the two readings
experiment 473's run could not produce - four aborts serviced, a fifth recorded with no address, and
no way to tell a kernel `copyin` of a user address from the process-1 thread faulting on its own
account.

`struct arm_saved_state` is Apple's layout, and this image cannot include Apple's header: neither
`entry_stubs.c` nor `entry_trace.c` has an XNU include path (that is why 467's record reads `DFSR` and
`DFAR` out of `cp15` with `mrc` rather than out of the frame). So the six offsets appear in
`stages/stage90/xnu_arm_boot/entry_saved_state.h` as six numbers, and six transcribed numbers is the
shape this project has paid for twenty-four times.

The three sources, and what each one is
---------------------------------------
  1. `external/xnu-4570.1.46/osfmk/mach/arm/thread_status.h`, **parsed** - the member names and their
     order are read out of Apple's own declaration and the offsets are computed from them the way a
     compiler would for eight `uint32_t` members. Nothing here is transcribed from that header; the
     parser is the same shape as `tools/check_pthread_table_slots.py`'s.
  2. `out/xnu_assym/$CONFIG/assym.s`'s `SS_*` - what `tools/gen_assym.sh` produced by compiling
     Apple's `osfmk/arm/genassym.c` against that same header. This is the source that matters for the
     *kernel*: `osfmk/arm/locore.s` stores the frame at `SS_PC`, `SS_CPSR`, `SS_STATUS` and
     `SS_VADDR`, so if this disagrees with (1) then the assembled kernel is stale, not the image.
  3. `stages/stage90/xnu_arm_boot/entry_saved_state.h` - what *this* image reads the frame at.

A fourth agreement is checked because it reaches the same six numbers from a different direction:
`ACT_PCBDATA` is the offset of the user PCB's saved state inside the thread's `arm_context`, and the
build declares `ACT_PCBDATA_PC` and `ACT_PCBDATA_R0` as well - so `ACT_PCBDATA_PC - ACT_PCBDATA` must
be `SS_PC` and `ACT_PCBDATA_R0 - ACT_PCBDATA` must be `SS_R0`. `dataabt_from_user` passes
`TPIDRPRW + ACT_PCBDATA` as `regs`, so that difference is a statement about the same frame arrived at
from the PCB side.

And `PSR_MODE_MASK`/`PSR_USER_MODE` are read out of `osfmk/arm/proc_reg.h` and compared with the two
constants in the same header, because the user/kernel reading *is* `cpsr & mask == user`, which is
Apple's own test in `sleh_abort`.

**What this check cannot see, and what does.** It compares definitions, not the running kernel: a
stale `assym.s` that agrees with the header would pass here and still disagree with the assembled
`locore.o` - which is experiment 468's defect class, and is why `tools/check_assym_cswitch.py`
compares `assym.s` against the *assembled object* before this build links it. And the offsets are
checked a third time **on the device**: the vector stored the same `cp15` numbers into
`SS_STATUS`/`SS_VADDR`, so the record's `xnu_live_sleh_frame_ok` says whether the frame it read
carried back the `DFSR`/`DFAR` it had just read for itself. Entries 1 to 4 of a run are aborts whose
two numbers are already known from 472's and 473's logs, so that comparison has a control.

481 added a second transcribed file and a fifth site for the same comparison
----------------------------------------------------------------------------
The timer step reads four words of `struct cpu_data` - the software decrementer and the three function
pointers `cpu_timebase_init` copies out of `rtclock_timebase_func` - and it reads them at a place
(inside `arm_init`, at the first `fiq_context_init`) where there is no frame to check them against. So
they are transcribed into `stages/stage90/xnu_arm_boot/entry_timebase.h` and compared here against the
same generated `assym.s`'s `CPU_*`, in both directions, plus one property the four numbers have to
have jointly: they are **four consecutive words**, in that order. That last one is what makes the read
a read of one block, and it is the property a per-value comparison cannot see. The failure it guards
against is specific to this step: `ml_get_decrementer` and `ml_set_decrementer` `blx`/`bxne` the word
they load out of `cpu_data`, so an offset a word off does not give a wrong timer reading - it calls a
data word.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

XNU = os.path.join(REPO_ROOT, "external/xnu-4570.1.46")
HEADER = os.path.join(XNU, "osfmk/mach/arm/thread_status.h")
PROC_REG = os.path.join(XNU, "osfmk/arm/proc_reg.h")
DEFINES = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot/entry_saved_state.h")
# The hand-written stand-in the entry image's own `start.s` is assembled against (see above).
BOOT_ASSYM = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot/assym.s")
START_S = os.path.join(XNU, "osfmk/arm/start.s")
# 481's four `cpu_data` offsets. A separate file from `DEFINES` because it is a separate decision -
# the timer, not the saved-state frame - and compared the same way, against the same generated assym.s.
TIMEBASE = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot/entry_timebase.h")

# The struct this image reads, and the members in the order the header declares them. `.` is a member
# whose width is one word; a name with a `[N]` is N words. Both are Apple's `uint32_t`s, so the
# offsets are the accumulated word counts and there is no padding to allow for - which is a property
# of the declaration (every member is `uint32_t`), not an assumption: the parse reads the types.
STRUCT_NAME = "arm_saved_state"

# `#define SS_PC #60` - `gen_assym.sh` writes the `#` because Apple's `genassym.s` is fed to `sed` and
# the marker is what distinguishes a numeric define from a text one.
ASSYM_DEFINE_RE = re.compile(r"^#define\s+(\w+)\s+#(\d+)\s*$")

# `#define STAGE90_SS_PC      60`   (the two mode constants are written with a `u` suffix)
LOCAL_DEFINE_RE = re.compile(r"^#define\s+(STAGE90_\w+)\s+(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*$")

# `#define PSR_MODE_MASK		0x0000001F	/* ... */`
PROC_REG_DEFINE_RE = re.compile(r"^#define\s+(PSR_MODE_MASK|PSR_USER_MODE|PSR_SVC_MODE)\s+(0x[0-9a-fA-F]+)\b")

# `    uint32_t    r[13];      /* General purpose register r0-r12 */`
MEMBER_RE = re.compile(r"^\s*uint32_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*(\d+)\s*\])?\s*;")

# `#define SS_PC 60` - the hand-written stand-in writes a plain number, with no `#` marker.
PLAIN_DEFINE_RE = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(-?\d+)\s*$")


def say(message):
    print(message)


def fail(message):
    print("FAIL: %s" % message, file=sys.stderr)
    sys.exit(1)


def strip_comments(text):
    """Block comments first, then line comments - the header uses both in the struct body."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_struct(text, name):
    """
    `[(member, offset, words)]` for `struct <name>`, in declaration order, with the offsets computed
    the way a compiler would for word-sized members - so a member inserted or moved changes every
    offset after it, which is the failure this is here for.
    """
    body = None
    for match in re.finditer(r"struct\s+%s\s*\{(.*?)\n\}" % re.escape(name), strip_comments(text),
                             flags=re.S):
        block = match.group(1)
        if MEMBER_RE.search(block):
            body = block
            break
    if body is None:
        fail("no `struct %s` with word-sized members in the header - the parser, not the check, is "
             "what needs fixing" % name)

    members = []
    offset = 0
    for line in body.splitlines():
        match = MEMBER_RE.match(line)
        if not match:
            continue
        member = match.group(1)
        words = int(match.group(2)) if match.group(2) else 1
        members.append((member, offset, words))
        offset += 4 * words
    if not members:
        fail("`struct %s` parsed to no members" % name)
    return members, offset


def read_assym(text):
    values = {}
    for line in text.splitlines():
        match = ASSYM_DEFINE_RE.match(line.rstrip("\n"))
        if match:
            values[match.group(1)] = int(match.group(2))
    return values


def read_local_defines(text):
    values = {}
    for line in text.splitlines():
        match = LOCAL_DEFINE_RE.match(line)
        if match:
            values[match.group(1)] = int(match.group(2), 0)
    return values


def read_plain_defines(text):
    """`{name: value}` for the hand-written `#define NAME 60` form (no `#` marker on the value)."""
    values = {}
    for line in text.splitlines():
        match = PLAIN_DEFINE_RE.match(line.rstrip("\n"))
        if match:
            values[match.group(1)] = int(match.group(2))
    return values


def read_proc_reg(text):
    values = {}
    for line in text.splitlines():
        match = PROC_REG_DEFINE_RE.match(line)
        if match:
            values[match.group(1)] = int(match.group(2), 0)
    return values


def compare(header_text, assym_text, defines_text, proc_reg_text, boot_assym_text,
            start_s_text=None, timebase_text=None):
    """
    Returns `(failures, notes)`. `failures` is empty when every arrangement holds. Every source is
    text, so `--selftest` can mutate one and re-run this exact comparison.
    """
    failures = []
    notes = []

    members, size = parse_struct(header_text, STRUCT_NAME)
    by_name = {member: offset for member, offset, _words in members}
    notes.append("header %s: %s" % (
        os.path.relpath(HEADER, REPO_ROOT),
        ", ".join("%s %d" % (m, o) for m, o, _w in members) + ", size %d" % size))

    # --- 1 vs 3: what this image reads, against Apple's declaration ---------------------------
    local = read_local_defines(defines_text)
    wanted = {"SP": "sp", "LR": "lr", "PC": "pc", "CPSR": "cpsr", "STATUS": "fsr", "VADDR": "far"}
    for suffix, member in sorted(wanted.items()):
        key = "STAGE90_SS_" + suffix
        if key not in local:
            failures.append("entry_saved_state.h defines no %s, so one of the six offsets the frame "
                            "is indexed at is missing from the image" % key)
            continue
        if member not in by_name:
            failures.append("struct %s has no member named %s in %s, so there is nothing for %s to "
                            "be the offset of" % (STRUCT_NAME, member, HEADER, key))
            continue
        if local[key] != by_name[member]:
            failures.append("%s is %s's offset %d in %s, and entry_saved_state.h says %d"
                            % (key, member, by_name[member], HEADER, local[key]))
    if "STAGE90_SS_SIZE" in local and local["STAGE90_SS_SIZE"] != size:
        failures.append("STAGE90_SS_SIZE is %d and sizeof(struct %s) is %d"
                        % (local["STAGE90_SS_SIZE"], STRUCT_NAME, size))

    # The mode test the frame's cpsr is read with.
    proc_reg = read_proc_reg(proc_reg_text)
    for key, name in (("STAGE90_PSR_MODE_MASK", "PSR_MODE_MASK"),
                      ("STAGE90_PSR_USER_MODE", "PSR_USER_MODE")):
        if key not in local or name not in proc_reg:
            failures.append("cannot compare %s with %s - one of them was not parsed" % (key, name))
            continue
        if local[key] != proc_reg[name]:
            failures.append("%s is 0x%08x and %s says 0x%08x"
                            % (key, local[key], name, proc_reg[name]))

    # --- 480: the two words the abort handler dereferences through the thread -----------------
    # Not frame offsets, and compared the same way: `entry_saved_state.h` against the generated
    # `assym.s`, which is where `genassym.c`'s own `offsetof` expressions land. `sleh_abort` reads
    # them in this order for every fault it services (`trap.c:446` picks the map, `trap.c:449` hands
    # `map->pmap` to `arm_fast_fault`), so a build whose image disagreed with Apple about either one
    # would fault at an address derived from a wrong offset instead of servicing the fault - which is
    # 474's stop, and is not something a run can distinguish from a zero.
    assym_for_thread = read_assym(assym_text)
    for key, name in (("STAGE90_ACT_MAP", "ACT_MAP"), ("STAGE90_MAP_PMAP", "MAP_PMAP")):
        if key not in local:
            failures.append("entry_saved_state.h defines no %s, so the offset the abort handler "
                            "dereferences to choose a map is missing from the image" % key)
        elif name not in assym_for_thread:
            failures.append("this configuration's assym.s declares no %s, so there is nothing for %s "
                            "to be compared with - the generated file is where the offset comes from"
                            % (name, key))
        elif local[key] != assym_for_thread[name]:
            failures.append("%s is %d and %s is %d"
                            % (key, local[key], name, assym_for_thread[name]))

    # --- 481: the four `cpu_data` words the timebase registration fills and this image reads -----
    # `entry_timebase.h` transcribes them for the reason `entry_saved_state.h` transcribes the frame:
    # the image cannot include Apple's headers. A wrong offset here is worse than a wrong frame
    # offset in one way - `ml_get_decrementer`/`ml_set_decrementer` `bxne` the word they load, so an
    # offset that is off by one word turns a data pointer into a call target and the boot jumps into
    # whatever `cpu_data` holds there. The comparison is the only thing that can refuse that at build
    # time, because on the device a wrong pointer and a working one both leave a plausible number.
    timebase = read_local_defines(timebase_text or "")
    timebase_names = (("STAGE90_CPU_DECREMENTER", "CPU_DECREMENTER"),
                      ("STAGE90_CPU_GET_DECREMENTER_FUNC", "CPU_GET_DECREMENTER_FUNC"),
                      ("STAGE90_CPU_SET_DECREMENTER_FUNC", "CPU_SET_DECREMENTER_FUNC"),
                      ("STAGE90_CPU_GET_FIQ_HANDLER", "CPU_GET_FIQ_HANDLER"))
    for key, name in timebase_names:
        if key not in timebase:
            failures.append("entry_timebase.h defines no %s, so one of the four `cpu_data` words the "
                            "timebase registration is read back through is missing from the image" % key)
        elif name not in assym_for_thread:
            failures.append("this configuration's assym.s declares no %s, so there is nothing for %s "
                            "to be compared with - `struct cpu_data` moved, or genassym no longer "
                            "emits it" % (name, key))
        elif timebase[key] != assym_for_thread[name]:
            failures.append("%s is %d and %s is %d"
                            % (key, timebase[key], name, assym_for_thread[name]))
    if all(name in assym_for_thread for _k, name in timebase_names):
        # The four are one table read as a block, which is only true if they are adjacent and in this
        # order - `cpu_decrementer`, then the three pointers `cpu_timebase_init` copies out of
        # `rtclock_timebase_func`. Asserting the spacing catches the case where the two numbers above
        # agree with each other and the block is not a block.
        previous = None
        for key, name in timebase_names:
            value = assym_for_thread[name]
            if previous is not None and value != previous + 4:
                failures.append("assym.s puts %s at %d and the word before it at %d, so these four "
                                "are not four consecutive words of `struct cpu_data` - the step reads "
                                "them as one block at the first fiq_context_init" % (name, value, previous))
            previous = value
        notes.append("assym.s: " + ", ".join("%s %d" % (n, assym_for_thread[n]) for _k, n in timebase_names))

    # --- 1 vs 2: Apple's declaration against this configuration's generated assym.s -----------
    assym = read_assym(assym_text)
    assym_names = {"SP": "SS_SP", "LR": "SS_LR", "PC": "SS_PC", "CPSR": "SS_CPSR",
                   "STATUS": "SS_STATUS", "VADDR": "SS_VADDR", "EXC": "SS_EXC", "SIZE": "SS_SIZE",
                   "R0": "SS_R0"}
    missing = [n for n in assym_names.values() if n not in assym]
    if missing:
        failures.append("%s has no numeric #define for %s - it is not the assym.s gen_assym.sh "
                        "writes, or those fields moved" % (assym_text, ", ".join(sorted(missing))))
    else:
        small = {suffix: assym[name] for suffix, name in assym_names.items()}
        for suffix, member in wanted.items():
            if by_name.get(member) != small[suffix]:
                failures.append("assym.s gives SS_%s %d and %s declares %s at %d - the assembled "
                                "kernel and Apple's header disagree, so re-run gen_assym.sh and "
                                "assemble_arm_layer.sh before linking anything"
                                % (suffix, small[suffix], HEADER, member, by_name[member]))
        if small["R0"] != by_name.get("r", 0):
            failures.append("assym.s gives SS_R0 %d and struct %s's first member is at %d"
                            % (small["R0"], STRUCT_NAME, by_name.get("r", 0)))
        if small["EXC"] != by_name.get("exception", -1):
            failures.append("assym.s gives SS_EXC %d and struct %s declares `exception` at %d - a "
                            "member was added to or removed from the struct"
                            % (small["EXC"], STRUCT_NAME, by_name.get("exception", -1)))
        if small["SIZE"] != size:
            failures.append("assym.s gives SS_SIZE %d and struct %s is %d bytes"
                            % (small["SIZE"], STRUCT_NAME, size))

        # --- the same numbers from the PCB side -------------------------------------------------
        if "ACT_PCBDATA" in assym and "ACT_PCBDATA_PC" in assym and "ACT_PCBDATA_R0" in assym:
            base = assym["ACT_PCBDATA"]
            for key, delta, suffix in (("ACT_PCBDATA_PC", assym["ACT_PCBDATA_PC"] - base, "PC"),
                                       ("ACT_PCBDATA_R0", assym["ACT_PCBDATA_R0"] - base, "R0")):
                if delta != small[suffix]:
                    failures.append("%s - ACT_PCBDATA is %d and SS_%s is %d, so the PCB and the "
                                    "stack frame do not agree on where that member is - "
                                    "dataabt_from_user passes the PCB as `regs`"
                                    % (key, delta, suffix, small[suffix]))
            notes.append("assym.s: ACT_PCBDATA %d, +PC %d, +R0 %d" % (
                base, assym["ACT_PCBDATA_PC"] - base, assym["ACT_PCBDATA_R0"] - base))
        else:
            failures.append("assym.s has no ACT_PCBDATA/ACT_PCBDATA_PC/ACT_PCBDATA_R0, so the "
                            "PC-side cross-check of the same offsets did not run")

        notes.append("assym.s: " + ", ".join("%s %d" % (n, assym[n]) for n in sorted(assym_names.values())))

    # --- 4: the hand-written stand-in the entry image's own start.s is assembled against ---------
    boot = read_plain_defines(boot_assym_text)
    mirrored = {name: value for name, value in boot.items()
                if name.startswith(("SS_", "VSS_")) and not name.endswith("_NUM")}
    if not mirrored:
        failures.append("stages/stage90/xnu_arm_boot/assym.s declares no SS_/VSS_ constant, so the "
                        "constants osfmk/arm/start.s is assembled with in this image are unchecked")
    for name in sorted(mirrored):
        if name not in assym:
            failures.append("stages/stage90/xnu_arm_boot/assym.s declares %s %d and this "
                            "configuration's generated assym.s has no such name - a name Apple's "
                            "genassym.c does not declare is a constant this image invented"
                            % (name, mirrored[name]))
        elif mirrored[name] != assym[name]:
            failures.append("stages/stage90/xnu_arm_boot/assym.s declares %s %d and this "
                            "configuration's generated assym.s gives %d - two files mirroring one "
                            "struct, and the one first on this build's include path is the wrong one"
                            % (name, mirrored[name], assym[name]))
    if mirrored:
        notes.append("stages/stage90/xnu_arm_boot/assym.s: "
                     + ", ".join("%s %d" % (k, v) for k, v in sorted(mirrored.items())))
        if start_s_text is not None:
            unused = [n for n in sorted(mirrored)
                      if not re.search(r"\b%s\b" % re.escape(n), start_s_text)]
            if unused:
                notes.append("  of those, osfmk/arm/start.s never mentions: " + ", ".join(unused))

    notes.append("entry_saved_state.h: " + ", ".join("%s %d" % (k, v) for k, v in sorted(local.items())))
    return failures, notes


def selftest(header_text, assym_text, defines_text, proc_reg_text, boot_assym_text,
             start_s_text=None, timebase_text=None):
    """
    Each source is mutated in memory, one at a time, and the comparison must refuse each one. Moving
    a value by one word is the smallest change that is still a change; swapping two adjacent members
    is the one a set comparison would miss.
    """
    mutations = []

    def mutate_define(text, key):
        pattern = re.compile(r"^(#define\s+%s\s+)(0x[0-9a-fA-F]+|\d+)([uUlL]*\s*)$" % re.escape(key), re.M)
        match = pattern.search(text)
        if not match:
            return None
        value = int(match.group(2), 0) + 4
        return text[:match.start()] + "%s%d%s" % (match.group(1), value, match.group(3)) + text[match.end():]

    for key in ("STAGE90_SS_PC", "STAGE90_SS_SP", "STAGE90_ACT_MAP", "STAGE90_MAP_PMAP"):
        mutated = mutate_define(defines_text, key)
        if mutated:
            mutations.append(("entry_saved_state.h's %s moved by one word" % key,
                              dict(defines_text=mutated)))
    for name in ("SS_PC", "SS_SP", "ACT_MAP", "MAP_PMAP"):
        mutated = re.sub(r"^(#define\s+%s\s+#)(\d+)\s*$" % name,
                         lambda m: "%s%d" % (m.group(1), int(m.group(2)) + 4),
                         assym_text, count=1, flags=re.M)
        if mutated != assym_text:
            mutations.append(("assym.s's %s moved by one word" % name, dict(assym_text=mutated)))

    # 481: the same two directions for the four `cpu_data` words - the header moved, and assym.s
    # moved. The second is the one that matters with the wrong sign: `CPU_GET_FIQ_HANDLER` at the
    # software decrementer's offset would make `ml_get_decrementer` call an integer.
    if timebase_text:
        for key in ("STAGE90_CPU_DECREMENTER", "STAGE90_CPU_GET_FIQ_HANDLER"):
            mutated = mutate_define(timebase_text, key)
            if mutated:
                mutations.append(("entry_timebase.h's %s moved by one word" % key,
                                  dict(timebase_text=mutated)))
    for name in ("CPU_DECREMENTER", "CPU_GET_DECREMENTER_FUNC", "CPU_SET_DECREMENTER_FUNC",
                 "CPU_GET_FIQ_HANDLER"):
        mutated = re.sub(r"^(#define\s+%s\s+#)(\d+)\s*$" % name,
                         lambda m: "%s%d" % (m.group(1), int(m.group(2)) + 4),
                         assym_text, count=1, flags=re.M)
        if mutated != assym_text:
            mutations.append(("assym.s's %s moved by one word" % name, dict(assym_text=mutated)))

    # Apple's declaration, with `pc` and `cpsr` swapped. Both keep their width, so a comparison that
    # looked at the *set* of offsets would accept this - and it is what a wrong assym.s produces when
    # two neighbouring fields are the same size.
    lines = header_text.splitlines()
    pc_at = next((i for i, l in enumerate(lines) if re.match(r"^\s*uint32_t\s+pc\s*;", l)), None)
    cpsr_at = next((i for i, l in enumerate(lines) if re.match(r"^\s*uint32_t\s+cpsr\s*;", l)), None)
    if pc_at is not None and cpsr_at is not None:
        swapped = list(lines)
        swapped[pc_at], swapped[cpsr_at] = swapped[cpsr_at], swapped[pc_at]
        mutations.append(("thread_status.h with pc and cpsr swapped", dict(header_text="\n".join(swapped))))

    mutated = re.sub(r"^(#define\s+PSR_MODE_MASK\s+)0x[0-9a-fA-F]+",
                     lambda m: m.group(1) + "0x0000003F", proc_reg_text, count=1, flags=re.M)
    if mutated != proc_reg_text:
        mutations.append(("proc_reg.h's PSR_MODE_MASK widened", dict(proc_reg_text=mutated)))

    mutated = re.sub(r"^#define\s+SS_SIZE\s+\d+\s*$", "#define SS_SIZE 88", boot_assym_text,
                     count=1, flags=re.M)
    if mutated != boot_assym_text:
        mutations.append(("the hand-written assym.s's SS_SIZE moved", dict(boot_assym_text=mutated)))

    if not mutations:
        fail("--selftest found nothing to mutate, so it cannot say the comparison reads anything")

    refused = 0
    for name, replacement in mutations:
        arguments = dict(header_text=header_text, assym_text=assym_text, defines_text=defines_text,
                         proc_reg_text=proc_reg_text, boot_assym_text=boot_assym_text,
                         start_s_text=start_s_text, timebase_text=timebase_text)
        arguments.update(replacement)
        failures, _notes = compare(**arguments)
        if failures:
            refused += 1
            if os.environ.get("STAGE90_CHECK_VERBOSE"):
                say("    refused: %s -> %s" % (name, failures[0]))
        else:
            say("FAIL: --selftest: the mutation '%s' was accepted, so this check does not compare "
                "what it says it compares" % name, file=sys.stderr)
    if refused != len(mutations):
        return 1
    say("  --selftest: all %d mutations were refused" % refused)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--header", default=HEADER)
    parser.add_argument("--proc-reg", default=PROC_REG)
    parser.add_argument("--defines", default=DEFINES)
    parser.add_argument("--boot-assym", default=BOOT_ASSYM)
    parser.add_argument("--start-s", default=START_S)
    parser.add_argument("--timebase", default=TIMEBASE)
    parser.add_argument("--assym", default=None,
                        help="defaults to out/xnu_assym/$XNU_KERNEL_CONFIG/assym.s")
    parser.add_argument("--config", default=None,
                        help="the XNU configuration whose assym.s to compare against; defaults to "
                             "$XNU_KERNEL_CONFIG, then STAGE90_XNU (the configuration this image is "
                             "built with). The four `CPU_*` offsets differ between configurations, so "
                             "the chosen file is named in the FAIL line.")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    config = args.config or os.environ.get("XNU_KERNEL_CONFIG") or "STAGE90_XNU"
    assym = args.assym or os.path.join(REPO_ROOT, "out", "xnu_assym", config, "assym.s")

    texts = {}
    for key, path in (("header_text", args.header), ("proc_reg_text", args.proc_reg),
                      ("defines_text", args.defines), ("assym_text", assym),
                      ("boot_assym_text", args.boot_assym), ("start_s_text", args.start_s),
                      ("timebase_text", args.timebase)):
        if not os.path.exists(path):
            fail("no %s" % path)
        with open(path, "r", errors="replace") as handle:
            texts[key] = handle.read()

    if args.selftest:
        return selftest(**texts)

    failures, notes = compare(**texts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the frame this image reads is not the frame this kernel built (assym.s read: %s):"
              % os.path.relpath(assym, REPO_ROOT), file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    local = read_local_defines(texts["defines_text"])
    say("  xnu_entry_474: struct arm_saved_state is read at the offsets Apple declares, this"
        " configuration's assym.s gives and entry_saved_state.h writes (SS_PC = %d, SS_CPSR = %d,"
        " SS_STATUS = %d, SS_VADDR = %d, and the PCB's own +PC and +R0 agree), so the record's pc is"
        " the faulting instruction and its cpsr is the mode it ran in"
        % (local["STAGE90_SS_PC"], local["STAGE90_SS_CPSR"], local["STAGE90_SS_STATUS"],
           local["STAGE90_SS_VADDR"]))
    if texts.get("timebase_text"):
        tb = read_local_defines(texts["timebase_text"])
        say("  xnu_entry_481: the four `struct cpu_data` words the timebase registration is read back"
            " through are this configuration's own (DECREMENTER = %d, GET_DECREMENTER_FUNC = %d,"
            " SET_DECREMENTER_FUNC = %d, GET_FIQ_HANDLER = %d, four consecutive words), so"
            " `ml_get_decrementer`/`ml_set_decrementer` load the pointers Apple's assembler would have"
            " them load and `bxne` a function and not a data word"
            % (tb["STAGE90_CPU_DECREMENTER"], tb["STAGE90_CPU_GET_DECREMENTER_FUNC"],
               tb["STAGE90_CPU_SET_DECREMENTER_FUNC"], tb["STAGE90_CPU_GET_FIQ_HANDLER"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
