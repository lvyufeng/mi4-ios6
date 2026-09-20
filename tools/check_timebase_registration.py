#!/usr/bin/env python3
"""
Check the five things 481's registration rests on - before the run that measures it, and against the
*kernel's own object code* rather than against a comment or against the generated `assym.s`.

481 is the step that gives the decrementer an owner: it wraps `PE_init_platform` at `arm_init`'s own
registering call and calls `ml_init_timebase(BootCpuData, &stage90_tbd_ops, 0, 0)` before Apple's
code runs, so that the table `cpu_timebase_init` copies into `cpu_data` is this image's rather than
zero. Every part of that sentence is a claim about a different file, and each one can be false in a
way no device run would report as an error:

  1. **the table's shape.** `ml_init_timebase` does `rtclock_timebase_func = *tbd_funcs` - a whole
     struct copy - and `cpu_timebase_init` then reads the three members *by name* out of it. So the
     struct in `entry_timebase.c` must have Apple's three members in Apple's order, and its members
     must be the same kinds: a swapped first two would put the FIQ handler in
     `cpu_get_decrementer_func`, and the boot would call it as `ml_get_decrementer`'s timestamp.
  2. **the pointerness of the second parameter.** `tbd_ops_t` is `struct tbd_ops *`
     (`machine_routines.h:247`), and `entry_timebase.c` cannot include that header. A declaration that
     spelled the parameter as the struct rather than as a pointer would compile, pass twelve bytes
     where the callee reads one word, and hand `ml_init_timebase` a pointer to whatever followed the
     table in `.rodata`. Nothing in the compiler can see this: the definition is in another object.
  3. **the window is unoccupied.** `ml_init_timebase`'s own guard is one-way - it copies only
     `if (rtclock_timebase_func.tbd_fiq_handler == NULL)` (`machine_routines.c:447`). If Apple's own
     call site were live and got there first, with `pe_arm_init_timer`'s `generic_funcs`
     (`{&fleh_fiq_generic, NULL, NULL}`), then `tbd_fiq_handler` would be non-NULL and this image's
     registration would be *silently refused*: `registered=0` in the log, and the timer would still be
     zero. That is the reason this check reads `nm -u` of `pexpert/arm/pe_identify_machine.o` for
     `ml_init_timebase` rather than trusting the `#if` structure of the source.
  4. **the register pairs.** `entry_timebase.c`'s four accessors are inline `mrc`/`mcr`/`mrrc`, and
     they must be the pairs Apple's own `__ARM_TIME__` code uses (`c14, c3, 0` for `CNTV_TVAL`,
     `c14, c3, 1` for `CNTV_CTL`, `mrrc p15, 1, ..., c14` for `CNTVCT`), and must not touch `c14, c2` -
     `CNTP`, which the payload arms for its own dead-man (`watchdog_cntp_ctl_armed`), one of the two
     recovery nets that keep this device from needing a person.
  5. **the linking.** All of the above is inert unless the linker put the wrapper on the *registering*
     call and the three calls `arm_init` makes are still in the order
     `PE_init_platform` -> `cpu_timebase_init` -> `fiq_context_init`, because the registration has to
     happen before the copy and the copy has to happen before anything reads it.

What this checks that `tools/check_saved_state_offsets.py` cannot
---------------------------------------------------------------
That check compares `entry_timebase.h`'s four `cpu_data` offsets against the generated `assym.s`, and
its own docstring says what it cannot see: "definitions, not the running kernel". This one reads the
same four numbers out of the **assembled** `osfmk/arm/machine_routines_asm.o` - the object whose
`ml_get_decrementer`/`ml_set_decrementer`/`fiq_context_init` are what actually load them - and out of
the linked image's `cpu_timebase_init`, which is what actually stores them. Three sources, none of
which is a copy of another, and the third is code rather than data.

It also asserts the *direction* the offsets are used in, which a table of equal numbers cannot: the
built `ml_get_decrementer` loads `[cpu_data + CPU_GET_DECREMENTER_FUNC]`, `cmp r2, #0`, `bxne r2` -
i.e. it calls the word it loads - so an offset a word off turns a function pointer into a data word,
and the run would not report a wrong timer, it would call an integer.

    ./tools/check_timebase_registration.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_timebase_registration.py --selftest        # mutate each claim, require refusal

The one thing this check cannot say is that the timer *counts* on this SoC. That is the run's reading
(`xnu_live_cntv_tval_a` against `_b`), and 143 already measured the neighbouring negative: the
interrupt is not delivered here, which is why 481 arms with `IMASK` set and 482 owns the routing.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

XNU = os.path.join(REPO_ROOT, "external/xnu-4570.1.46")
BOOT = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot")

ENTRY_H = os.path.join(BOOT, "entry_timebase.h")
ENTRY_C = os.path.join(BOOT, "entry_timebase.c")
MR_HDR = os.path.join(XNU, "osfmk/arm/machine_routines.h")
MR_SRC = os.path.join(XNU, "osfmk/arm/machine_routines.c")
PE_SRC = os.path.join(XNU, "pexpert/arm/pe_identify_machine.c")
CPU_SRC = os.path.join(XNU, "osfmk/arm/cpu.c")
MR_ASM = os.path.join(XNU, "osfmk/arm/machine_routines_asm.s")

# The three build products the checks below read. Each is produced by an earlier step of the build,
# and each is refused rather than guessed at when it is missing (the same rule
# `tools/check_undef_handler.py` applies to a symbol with no size).
MR_ASM_OBJ = os.path.join(REPO_ROOT, "out/xnu_asm_obj/machine_routines_asm.o")
CPU_OBJ = os.path.join(REPO_ROOT, "out/xnu_kernel_obj/osfmk_arm_cpu.o")
PE_OBJ = os.path.join(REPO_ROOT, "out/xnu_obj/pexpert_arm_pe_identify_machine.o")

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"

# `#define STAGE90_CPU_GET_DECREMENTER_FUNC   108` and the `0x...u` forms in the same file.
LOCAL_DEFINE_RE = re.compile(r"^#define\s+(STAGE90_\w+)\s+(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*$")

# `    80007b50:\teb000553 \tbl\t800090a4 <PE_init_platform>` in the linked image, and
# ` 86c:\tee1dcf90 \tmrc\t15, 0, ip, cr13, cr0, {4}` in an unlinked object - the address is not
# padded in an object, so the width is a range rather than eight.
INSN_RE = re.compile(r"^\s*([0-9a-f]{1,8}):\s+([0-9a-f]{8})\s+(\S+)\s*(.*?)\s*$")

# The four words, and the name Apple's `genassym` gives each one. The header's names are
# `STAGE90_` + the assym name, so one list is enough.
WORDS = ("CPU_DECREMENTER", "CPU_GET_DECREMENTER_FUNC", "CPU_SET_DECREMENTER_FUNC",
         "CPU_GET_FIQ_HANDLER")

# What each of the three assembled functions must load, and what it must therefore be reading. These
# are *uses*, not offsets: the check below is that the object's own immediates are the header's
# numbers, in the roles the source says they are in.
ASM_USES = {
    "ml_get_decrementer": ("CPU_GET_DECREMENTER_FUNC", "CPU_DECREMENTER"),
    "ml_set_decrementer": ("CPU_SET_DECREMENTER_FUNC", "CPU_DECREMENTER"),
    "fiq_context_init": ("CPU_GET_FIQ_HANDLER",),
}

# The CNTV triples, as `(CRm, opc2)` of a `cp15 ... c14, CRm, opc2` access, and the CNTP pair that
# must not appear in the image at all.
CNTV_TVAL = (3, 0)
CNTV_CTL = (3, 1)
CNTP = 2

CP15_RE = re.compile(r"\bc(1[0-5]),\s*c(\d+),\s*(\d+)\b")


def say(message):
    print(message)


def fail(message):
    print("FAIL: %s" % message, file=sys.stderr)
    sys.exit(1)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def local_defines(text):
    values = {}
    for line in text.splitlines():
        match = LOCAL_DEFINE_RE.match(line)
        if match:
            values[match.group(1)] = int(match.group(2), 0)
    return values


def disasm(path, name):
    """`objdump -d` over one function. The range is `--disassemble`'s, so the object must have a
    symbol table - every object below is built with one."""
    if not os.path.exists(path):
        fail("no %s - it is a build product of the kernel build, so run that first" % path)
    return subprocess.run([OBJDUMP, "-d", "--disassemble=%s" % name, path],
                          check=True, capture_output=True, text=True).stdout


def instructions(text):
    """`[(addr, encoding, mnemonic, operands)]` for every disassembled instruction in `text`. The
    trailing `\\t; 0x...` objdump appends to an immediate is dropped here, so every parser below
    reads operands and not comments - a `#-2147483648` in the operand field is the value and the
    `0x80000000` after the semicolon is the same number written the other way round."""
    out = []
    for line in text.splitlines():
        match = INSN_RE.match(line)
        if match:
            out.append((int(match.group(1), 16), int(match.group(2), 16), match.group(3),
                        re.sub(r"\t;.*$", "", match.group(4))))
    return out


def bl_nodes(insns):
    """`[(addr, target)]` for the `bl`s, with the target *computed* from the encoding rather than
    read off objdump's symbol column - so a `bl` to a local label, which objdump prints without a
    name, is still a node in the list."""
    out = []
    for addr, encoding, mnemonic, _ops in insns:
        if mnemonic in ("bl", "blx") and (encoding & 0xFF000000) == 0xEB000000:
            immediate = encoding & 0x00FFFFFF
            if immediate & 0x00800000:
                immediate -= 0x01000000
            out.append((addr, addr + 8 + (immediate << 2)))
    return out


def nm_undefined(path):
    if not os.path.exists(path):
        fail("no %s - it is a build product of the kernel build, so run that first" % path)
    out = subprocess.run([NM, "-u", path], check=True, capture_output=True, text=True).stdout
    return set(line.split()[-1] for line in out.splitlines() if line.split())


def nm_defined(path):
    out = subprocess.run([NM, "-S", "--defined-only", path],
                         check=True, capture_output=True, text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            try:
                table[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
            except ValueError:
                continue
        elif len(parts) == 3:
            try:
                table[parts[2]] = (int(parts[0], 16), 0)
            except ValueError:
                continue
    return table


def struct_members(text, opener_re):
    """`[(return-type, member-name)]` in declaration order for the first `struct { ... }` block the
    opener matches. Only function-pointer members are read, which is all either struct has, and the
    parse is of the declaration - the names and the order come from the file, not from a list here."""
    match = re.search(opener_re, text, re.M)
    if not match:
        return None
    rest = text[match.end():]
    depth = 1
    body = []
    for line in rest.splitlines():
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            break
        body.append(line)
    members = []
    for line in body:
        member = re.search(r"\(\s*\*\s*(\w+)\s*\)", line)
        if not member:
            continue
        head = line[:member.start()].strip()
        ret = head.split()[-1] if head.split() else ""
        members.append((ret, member.group(1)))
    return members


def crm_opc2_after_c14(text):
    """The `(CRm, opc2)` pairs of `c14` accesses, keyed without the CRn. Apple's asm says
    `mcr p15, 0, r0, c14, c3, 1` and this image's inline asm says `mcr p15, 0, %0, c14, c3, 1`, so
    the `CRn` is `c14` in both and the pair that identifies the register is `(CRm, opc2)`."""
    pairs = {}
    for match in re.finditer(r"\bc14,\s*c(\d+),\s*(\d+)\b", text):
        key = (int(match.group(1)), int(match.group(2)))
        pairs[key] = pairs.get(key, 0) + 1
    return pairs


def register_value(insns, index, register):
    """What `register` holds at `insns[index]`, from the nearest preceding instruction that defines
    it, or `None` when that definition is not one of the two immediates this check knows how to read
    (`mov`/`mvn`). The scan walks *back* from the use and stops at the first write of the register -
    either a value or a refusal - rather than at a fixed distance, because `cpu_timebase_init`'s
    constant lands five instructions above its store in both the object and the linked image."""
    for back in range(index - 1, max(-1, index - 12), -1):
        _addr, _encoding, mnemonic, ops = insns[back]
        fields = ops.split(",")
        if not fields or fields[0].strip() != register:
            continue
        if mnemonic in ("mov", "movs"):
            return int(ops.split("#")[-1])
        if mnemonic == "mvn":
            return (~int(ops.split("#")[-1])) & 0xFFFFFFFF
        return None
    return None


def call_sites(root, needle, skip_files=()):
    """Every line in `root` that *calls* `needle`. A declaration, a definition and a mention in a
    comment are not calls, and the three are told apart by the text around the name rather than by
    the file: this is the same rule `tools/check_macho_...`-style scans use, and it exists because
    "there is one call site" is a claim about every file in the tree."""
    out = []
    pattern = re.compile(r"\b%s\s*\(" % re.escape(needle))
    found = subprocess.run(["grep", "-rn", "--include=*.c", "--include=*.h", "--include=*.cpp",
                            "--include=*.s", needle, root],
                           capture_output=True, text=True).stdout
    for line in found.splitlines():
        path, _, rest = line.partition(":")
        _number, _, text = rest.partition(":")
        if path.endswith(skip_files) if skip_files else False:
            continue
        stripped = text.strip()
        if stripped.startswith(("/*", "*", "//")):
            continue
        if re.match(r"^(void|extern|static|int|uint\w*|kern_return_t)\s+%s\s*\(" % re.escape(needle),
                    stripped):
            continue
        if pattern.search(stripped):
            out.append((os.path.relpath(path, REPO_ROOT), text.strip()))
    return out


def gather(image):
    facts = {
        "entry_h": read(ENTRY_H),
        "entry_c": read(ENTRY_C),
        "mr_hdr": read(MR_HDR),
        "mr_src": read(MR_SRC),
        "pe_src": read(PE_SRC),
        "cpu_src": read(CPU_SRC),
        "mr_asm": read(MR_ASM),
        # Per function, because the checks below are about which word each *one* of them loads and
        # `--selftest` mutates one of these texts to prove the comparison reads it.
        "mr_asm_functions": dict((name, disasm(MR_ASM_OBJ, name)) for name in ASM_USES),
        "cpu_obj": disasm(CPU_OBJ, "cpu_timebase_init"),
        "pe_obj_undef": nm_undefined(PE_OBJ),
        "calls": call_sites(XNU, "ml_init_timebase"),
    }
    if image:
        after = nm_undefined(image)  # refuses a missing file the same way, before nm_defined
        del after
        facts["image_symbols"] = nm_defined(image)
        facts["image_arm_init"] = disasm(image, "arm_init")
        facts["image_cpu_obj"] = disasm(image, "cpu_timebase_init")
    return facts


def compare(facts, mutate=None):
    if mutate:
        facts = mutate(dict(facts))
    failures = []
    notes = []

    # --- 1: the table's shape, Apple's declaration against this image's --------------------------
    apple = struct_members(facts["mr_hdr"], r"^struct\s+tbd_ops\s*\{")
    mine = struct_members(facts["entry_c"], r"^typedef\s+struct\s*\{")
    if not apple:
        failures.append("osfmk/arm/machine_routines.h has no `struct tbd_ops { ... }` for this image's"
                        " table to be compared with")
    if not mine:
        failures.append("entry_timebase.c declares no `typedef struct { ... }` table, so there is"
                        " nothing to compare with `struct tbd_ops`")
    if apple and mine:
        if [name for _ret, name in apple] != [name for _ret, name in mine]:
            failures.append("the table's members are %s in machine_routines.h and %s in"
                            " entry_timebase.c - `ml_init_timebase` copies the struct whole and"
                            " `cpu_timebase_init` reads the members by name, so a different order"
                            " puts the FIQ handler where the decrementer getter belongs"
                            % ([n for _r, n in apple], [n for _r, n in mine]))
        elif apple != mine:
            failures.append("the table's members are named the same but typed differently: %s against"
                            " %s - a return type that changed would still link, because the callee"
                            " only ever calls through the pointer" % (apple, mine))
        else:
            notes.append("tbd_ops: " + ", ".join("%s (*%s)()" % (ret, name) for ret, name in apple))

    # --- 2: `tbd_ops_t` is a pointer, and this image passes one --------------------------------
    if not re.search(r"typedef\s+struct\s+tbd_ops\s*\*\s*tbd_ops_t\s*;", facts["mr_hdr"]):
        failures.append("`tbd_ops_t` is no longer `struct tbd_ops *` in machine_routines.h - if the"
                        " typedef stopped being a pointer, `ml_init_timebase`'s second parameter"
                        " changed shape and this image's call passes the wrong thing")
    declaration = re.search(r"extern\s+void\s+ml_init_timebase\s*\(([^;]*)\)\s*;", facts["entry_c"])
    if not declaration:
        failures.append("entry_timebase.c declares no `extern void ml_init_timebase(...)`, so the"
                        " parameter shapes the call is made through were not compared")
    elif "*" not in declaration.group(1).split(",")[1]:
        failures.append("entry_timebase.c declares `ml_init_timebase`'s second parameter without a"
                        " `*`: %r - `tbd_ops_t` is a pointer, so this would pass the twelve bytes of"
                        " the table where the callee reads one word, and `ml_init_timebase` would"
                        " dereference whatever followed it in .rodata"
                        % declaration.group(1).split(",")[1].strip())

    # --- 3: the window is unoccupied ------------------------------------------------------------
    calls = [entry for entry in facts["calls"] if "arm64" not in entry[0]]
    if len(calls) != 1:
        failures.append("`ml_init_timebase` has %d call sites in the arm tree (%s) and this check"
                        " asserts exactly one, in pexpert/arm/pe_identify_machine.c - a second one"
                        " could fill `rtclock_timebase_func` before or after this image's"
                        " registration, and its guard is one-way"
                        % (len(calls), ", ".join("%s %s" % c for c in calls) or "none"))
    elif not calls[0][0].endswith("pexpert/arm/pe_identify_machine.c"):
        failures.append("`ml_init_timebase`'s only call site is in %s and this check expects"
                        " pexpert/arm/pe_identify_machine.c - if it moved, the reading of the whole"
                        " step moved with it" % calls[0][0])
    if "ml_init_timebase" in facts["pe_obj_undef"]:
        failures.append("pexpert/arm/pe_identify_machine.o *does* reference `ml_init_timebase`, so"
                        " Apple's own call site is live in this configuration - and"
                        " `pe_arm_init_timer`'s `generic_funcs` has `&fleh_fiq_generic` in"
                        " `tbd_fiq_handler`, which would make `ml_init_timebase`'s one-way guard"
                        " refuse this image's registration silently (it copies only while"
                        " `rtclock_timebase_func.tbd_fiq_handler == NULL`)")
    else:
        notes.append("pe_identify_machine.o: no undefined `ml_init_timebase`, so the window this"
                     " image registers into is unoccupied")

    # --- 4: the register pairs ------------------------------------------------------------------
    asm_pairs = crm_opc2_after_c14(facts["mr_asm"])
    image_pairs = crm_opc2_after_c14(facts["entry_c"])
    for name, pair in (("CNTV_TVAL", CNTV_TVAL), ("CNTV_CTL", CNTV_CTL)):
        if pair not in asm_pairs:
            failures.append("machine_routines_asm.s has no `c14, c%d, %d` access, so there is nothing"
                            " to compare %s against - Apple's `__ARM_TIME__` code is where the"
                            " register pair comes from" % (pair[0], pair[1], name))
        if pair not in image_pairs:
            failures.append("entry_timebase.c makes no `c14, c%d, %d` access, so this image does not"
                            " drive %s the way Apple's own code does" % (pair[0], pair[1], name))
    if CNTP in set(crm for crm, _opc2 in image_pairs):
        failures.append("entry_timebase.c accesses `c14, c2, *` - that is CNTP, the *physical* timer,"
                        " and the payload arms it for its own dead-man"
                        " (`watchdog_cntp_ctl_armed`), one of the two recovery nets that keep this"
                        " device from needing a person")
    for pair in image_pairs:
        if pair not in (CNTV_TVAL, CNTV_CTL):
            failures.append("entry_timebase.c makes a `c14, c%d, %d` access, which is neither"
                            " `CNTV_TVAL` nor `CNTV_CTL` - a timer register that is not the one"
                            " Apple's `__ARM_TIME__` code writes" % pair)
    # The free-running counter: `ml_get_timebase` reads CNTPCT with `mrrc p15, 0`, and the mirror
    # is the check - this image's CNTVCT must differ in opc1 and in nothing else.
    if not re.search(r"mrrc\s+p15,\s*1,", facts["entry_c"]):
        failures.append("entry_timebase.c has no `mrrc p15, 1, ...` - the virtual counter `CNTVCT`"
                        " is opc1 1, and without it the sample below cannot say the counter ran")
    if not re.search(r"mrrc\s+p15,\s*0,", facts["mr_asm"]):
        failures.append("machine_routines_asm.s has no `mrrc p15, 0, ...`, so `ml_get_timebase` no"
                        " longer reads `CNTPCT` with the opc1 this image's `CNTVCT` read is the"
                        " mirror of")
    notes.append("c14 pairs: " + ", ".join("c%d,%d x%d" % (crm, opc2, count)
                                           for (crm, opc2), count in sorted(image_pairs.items())))

    # --- 5a: the four offsets, out of the assembled object --------------------------------------
    header = local_defines(facts["entry_h"])
    # The disassembly in `mr_asm_obj` is three functions concatenated, so the roles are read per
    # function: which word each one loads is the thing being compared, not that the number appears.
    per_function = {}
    for name in ASM_USES:
        used = set()
        for _addr, _encoding, mnemonic, ops in instructions(facts["mr_asm_functions"][name]):
            if mnemonic in ("ldr", "str"):
                match = re.search(r"\[\w+,\s*#(\d+)\]", ops)
                if match:
                    used.add(int(match.group(1)))
        per_function[name] = used
    for name, wanted in ASM_USES.items():
        for word in wanted:
            key = "STAGE90_" + word
            if key not in header:
                failures.append("entry_timebase.h defines no %s" % key)
                continue
            if header[key] not in per_function[name]:
                failures.append("the assembled `%s` touches no `[cpu_data + #%d]` - that is %s, and"
                                " this image reads %s there" % (name, header[key], word, key))
    # `fiq_context_init` under `__ARM_TIME__` writes `CNTV_CTL`; this configuration compiles the
    # `#else` path, and that is a fact about the object rather than about the source's `#if`s.
    fiq = facts["mr_asm_functions"]["fiq_context_init"]
    if re.search(r"mcr\s+15, 0, r\d+, cr14, cr3, 1", fiq):
        failures.append("the assembled `fiq_context_init` writes `c14, c3, 1` (`CNTV_CTL`), i.e."
                        " `__ARM_TIME__` is ON in this configuration - then Apple's own FIQ path is"
                        " the owner and 481's routing argument is about a different vector")
    elif not re.search(r"msr\s+CPSR_c,\s*#\s*(?:209|0xd1)\b", fiq):
        failures.append("the assembled `fiq_context_init` neither writes `CNTV_CTL` nor enters FIQ"
                        " mode (`msr CPSR_c, #209`, i.e. `PSR_FIQ_MODE|PSR_FIQF|PSR_IRQF`), so it is"
                        " neither of Apple's two paths - the read-back at the first call is measured"
                        " against a function this check does not recognise")
    else:
        notes.append("fiq_context_init: the `#else` path (FIQ mode, offsets 116/120/124), so"
                     " `__ARM_TIME__` is off and this image's table is the only owner")

    # --- 5b: the four offsets, out of the linked `cpu_timebase_init` -----------------------------
    # The copy into `cpu_data` is where the four numbers stop being this image's and become the
    # kernel's, so the *stores* are the reading that matters - and the one store whose value is a
    # number rather than a pointer is the prediction the run is read against.
    cpu_insns = instructions(facts["image_cpu_obj"] if "image_cpu_obj" in facts
                             else facts["cpu_obj"])
    cdp_stores = set()
    decrementer_value = None
    # The three function pointers do not have to be three `str`s: this compiler emits one `str` for
    # the first and a `stm` for the next three words, whose base it built with an `add` a few
    # instructions earlier. So the offsets are collected from single stores *and* from a multiple
    # store whose base register was set by an `add` - which is a fact about the code the kernel
    # actually runs rather than about the shape of the source that produced it.
    bases = {}
    for index, (_addr, _encoding, mnemonic, ops) in enumerate(cpu_insns):
        if mnemonic == "str":
            match = re.search(r"^(\w+),\s*\[(\w+),\s*#(\d+)\]", ops)
            if not match:
                continue
            register, offset = match.group(1), int(match.group(3))
            cdp_stores.add(offset)
            if offset == header.get("STAGE90_CPU_DECREMENTER"):
                decrementer_value = register_value(cpu_insns, index, register)
        elif mnemonic in ("add", "adds"):
            match = re.search(r"^(\w+),\s*(\w+),\s*#(\d+)", ops)
            if match:
                bases[match.group(1)] = int(match.group(3))
        elif mnemonic in ("stm", "stmia", "stmdb"):
            match = re.search(r"^(\w+),\s*\{([^}]*)\}", ops)
            if match and match.group(1) in bases:
                base = bases[match.group(1)]
                for word, _register in enumerate(match.group(2).split(",")):
                    cdp_stores.add(base + 4 * word)
    for word in WORDS:
        key = "STAGE90_" + word
        if key not in header or word == "CPU_DECREMENTER":
            continue
        if header[key] not in cdp_stores:
            failures.append("`cpu_timebase_init` never stores to `[cpu_data + #%d]`, which is %s in"
                            " entry_timebase.h - the copy this image's read-back is measured after"
                            " does not write that word" % (header[key], key))
    if "STAGE90_CPU_DECREMENTER" in header:
        if header["STAGE90_CPU_DECREMENTER"] not in cdp_stores:
            failures.append("`cpu_timebase_init` never stores to `[cpu_data + #%d]`, which is"
                            " `CPU_DECREMENTER` in entry_timebase.h"
                            % header["STAGE90_CPU_DECREMENTER"])
        elif decrementer_value != header.get("STAGE90_CPU_DECREMENTER_INITIAL"):
            failures.append("`cpu_timebase_init` leaves 0x%08x in `[cpu_data + #%d]` and"
                            " entry_timebase.h says the first `fiq_context_init` must report 0x%08x"
                            " there - this is the one word of the read-back whose value is a number"
                            " rather than a pointer, so it is the one a wrong offset cannot fake"
                            % (decrementer_value if decrementer_value is not None else 0xFFFFFFFF,
                               header["STAGE90_CPU_DECREMENTER"],
                               header.get("STAGE90_CPU_DECREMENTER_INITIAL", 0)))

    # --- 5c: the linking -------------------------------------------------------------------------
    # The triple is found by its *shape* - the `PE_init_platform` call that is followed by
    # `cpu_timebase_init` and then `fiq_context_init` - and only then is the first of the three asked
    # to be the wrapper. Finding it the other way round would pass an image in which the wrapper had
    # been attached to `arm_init`'s `(FALSE, boot_args)` call and the registering one left bare.
    if "image_arm_init" in facts:
        symbols = facts["image_symbols"]
        insns = instructions(facts["image_arm_init"])
        by_address = dict((addr, name) for name, (addr, _size) in symbols.items())
        named = [(addr, by_address.get(target, "0x%08x" % target))
                 for addr, target in bl_nodes(insns)]
        want = ["__wrap_PE_init_platform", "cpu_timebase_init", "__wrap_fiq_context_init"]
        register_at = None
        for index in range(len(named) - 2):
            if [name for _addr, name in named[index + 1:index + 3]] == want[1:]:
                register_at = index
        if register_at is None:
            failures.append("`arm_init` has no `%s` call triple in this image, so the place 481's"
                            " registration has to sit in front of is not there -"
                            " `cpu_timebase_init` is what copies the table into `cpu_data`, and the"
                            " two wrappers are the registering call and the read-back" % " -> ".join(want))
        else:
            got = [name for _addr, name in named[register_at:register_at + 3]]
            if "__wrap_PE_init_platform" not in symbols:
                failures.append("this image defines no `__wrap_PE_init_platform`, so 481's"
                                " registration was not linked in - the wrap list in build_entry.sh"
                                " and this image disagree")
            elif got != want:
                failures.append("`arm_init`'s three calls around the timebase are %s and must be"
                                " %s - the first one is the registering call, and with the wrapper"
                                " missing the table `cpu_timebase_init` copies is still zero"
                                % (got, want))
            else:
                # The registering call's own arguments, read out of the instructions before it.
                at = named[register_at][0]
                before = [insn for insn in insns if at - 16 <= insn[0] < at]
                vm_init = None
                arg = None
                for _a, _e, mnemonic, ops in before:
                    if mnemonic in ("mov", "movs") and ops.startswith("r0,"):
                        vm_init = int(ops.split("#")[-1])
                    if mnemonic == "movw" and ops.startswith("r1,"):
                        arg = int(ops.split("#")[-1])
                    if mnemonic == "movt" and ops.startswith("r1,"):
                        arg = (arg or 0) | (int(ops.split("#")[-1]) << 16)
                if vm_init != 1:
                    failures.append("the registering `PE_init_platform` call passes r0 = %r, and"
                                    " the wrapper fires on `vm_initialized` being true -"
                                    " `arm_init`'s other call at this point is `(FALSE, boot_args)`"
                                    % vm_init)
                bootcpu = symbols.get("BootCpuData", (None, 0))[0]
                if arg != bootcpu:
                    failures.append("the registering `PE_init_platform` call passes r1 = 0x%s and"
                                    " `BootCpuData` is at 0x%s - `ml_init_timebase` compares its own"
                                    " first argument against `&BootCpuData`, so a call with any other"
                                    " pointer would be registered by the wrapper and then refused by"
                                    " the callee" % ("%08x" % arg if arg is not None else "?",
                                                     "%08x" % bootcpu if bootcpu else "?"))
                else:
                    notes.append("arm_init: %s at 0x%08x with r0=1, r1=BootCpuData(0x%08x)"
                                 % (" -> ".join(want), at, bootcpu))

    return failures, notes


def selftest(facts):
    """Each mutation changes exactly one of the five claims, and the comparison must refuse it. The
    mutations are over the *gathered* facts rather than over files, so the same `compare()` runs and
    nothing is written anywhere."""
    mutations = []

    def swap_table(f):
        """The table's first two members swapped, line by line. Both declarations are one member per
        line, so this is a swap of two *lines* - which is what a hand-edit that got the order wrong
        produces, and what a comparison of the two names as a *set* would accept."""
        lines = f["entry_c"].splitlines(True)
        at = [i for i, line in enumerate(lines) if re.match(r"^\s*(?:void|uint32_t)\s+\(\*\w+\)", line)]
        if len(at) < 2:
            return None
        lines[at[0]], lines[at[1]] = lines[at[1]], lines[at[0]]
        f["entry_c"] = "".join(lines)
        return f

    def move_header_word(f):
        text = f["entry_h"]
        mutated = re.sub(r"^(#define\s+STAGE90_CPU_GET_DECREMENTER_FUNC\s+)(\d+)",
                         lambda m: m.group(1) + str(int(m.group(2)) + 4), text, count=1, flags=re.M)
        if mutated == text:
            return None
        f["entry_h"] = mutated
        return f

    def move_initial(f):
        text = f["entry_h"]
        mutated = text.replace("0x7FFFFFFFu", "0x7FFFFFFEu", 1)
        if mutated == text:
            return None
        f["entry_h"] = mutated
        return f

    def add_cntp(f):
        text = f["entry_c"]
        marker = 'mrc p15, 0, %0, c14, c3, 0" : "=r" (value));'
        if marker not in text:
            return None
        f["entry_c"] = text.replace(marker, marker + '\n    __asm__ volatile ("mrc p15, 0, %0, c14, c2, 0" : "=r" (value));',
                                    1)
        return f

    def move_ctl(f):
        text = f["entry_c"]
        mutated = text.replace("c14, c3, 1", "c14, c3, 2", 1)
        if mutated == text:
            return None
        f["entry_c"] = mutated
        return f

    def second_call_site(f):
        f["calls"] = list(f["calls"]) + [("osfmk/arm/cpu.c", "ml_init_timebase(args, tbd_funcs, 0, 0);")]
        return f

    def move_call_site(f):
        f["calls"] = [("pexpert/arm/pe_identify_machine-copy.c", f["calls"][0][1])] \
            if f["calls"] else f["calls"]
        return f

    def occupied_window(f):
        f["pe_obj_undef"] = set(f["pe_obj_undef"]) | {"ml_init_timebase"}
        return f

    def move_asm_object_offset(f):
        """`ml_get_decrementer`'s `[cpu_data + CPU_GET_DECREMENTER_FUNC]` moved one word up, in the
        assembled object the check reads rather than in the header - so this is the object side of
        the same claim, and it is the direction that turns a function pointer into a data word."""
        text = f["mr_asm_functions"]["ml_get_decrementer"]
        mutated = text.replace("[r3, #108]", "[r3, #112]", 1)
        if mutated == text:
            return None
        f["mr_asm_functions"] = dict(f["mr_asm_functions"])
        f["mr_asm_functions"]["ml_get_decrementer"] = mutated
        return f

    def swap_image_calls(f):
        """`cpu_timebase_init` and `fiq_context_init` given each other's addresses in the image's
        symbol table - which is what the check actually resolves a `bl` through, so this is the
        mutation of the fact rather than of a name in a comment."""
        symbols = f["image_symbols"]
        if "cpu_timebase_init" not in symbols or "__wrap_fiq_context_init" not in symbols:
            return None
        f["image_symbols"] = dict(symbols)
        f["image_symbols"]["cpu_timebase_init"], f["image_symbols"]["__wrap_fiq_context_init"] = (
            symbols["__wrap_fiq_context_init"], symbols["cpu_timebase_init"])
        return f

    def unwrap_image_call(f):
        """The wrapper not in the image at all - the case 481's first build actually hit, where the
        generator had stubbed `__real_PE_init_platform` and the wrapper called the stand-in."""
        if "__wrap_PE_init_platform" not in f["image_symbols"]:
            return None
        f["image_symbols"] = dict(f["image_symbols"])
        del f["image_symbols"]["__wrap_PE_init_platform"]
        return f

    def move_bootcpu(f):
        """`BootCpuData` one word higher, which is the claim the registering call's `r1` is compared
        against: `ml_init_timebase`'s own guard is `args == &BootCpuData`, so a call whose literal
        was emitted for a different address is registered by the wrapper and refused by the callee."""
        symbols = f["image_symbols"]
        if "BootCpuData" not in symbols:
            return None
        f["image_symbols"] = dict(symbols)
        addr, size = symbols["BootCpuData"]
        f["image_symbols"]["BootCpuData"] = (addr + 4, size)
        return f

    for name, fn in (("entry_timebase.c's table with its first two members swapped", swap_table),
                     ("entry_timebase.h's CPU_GET_DECREMENTER_FUNC moved by one word",
                      move_header_word),
                     ("entry_timebase.h's CPU_DECREMENTER_INITIAL moved by one",
                      move_initial),
                     ("entry_timebase.c with a CNTP (`c14, c2, 0`) access added", add_cntp),
                     ("entry_timebase.c's CNTV_CTL triple moved off `c14, c3, 1`", move_ctl),
                     ("a second `ml_init_timebase` call site", second_call_site),
                     ("`ml_init_timebase`'s call site moved to another file", move_call_site),
                     ("an object that references `ml_init_timebase`", occupied_window),
                     ("the assembled `ml_get_decrementer` with `#108` moved to `#112`",
                      move_asm_object_offset)):
        mutations.append((name, fn))

    if "image_arm_init" in facts:
        mutations.append(("the linked image with `cpu_timebase_init` and `fiq_context_init` given"
                          " each other's addresses", swap_image_calls))
        mutations.append(("the linked image with no `__wrap_PE_init_platform`",
                          unwrap_image_call))
        mutations.append(("the linked image's `BootCpuData` one word high", move_bootcpu))

    refused = 0
    for name, fn in mutations:
        mutated = fn(dict(facts))
        if mutated is None:
            print("FAIL: --selftest: the mutation '%s' could not be constructed, so this check is"
                  " not being exercised by it" % name, file=sys.stderr)
            continue
        failures, _notes = compare(mutated)
        if failures:
            refused += 1
            if os.environ.get("STAGE90_CHECK_VERBOSE"):
                say("    refused: %s -> %s" % (name, failures[0]))
        else:
            print("FAIL: --selftest: the mutation '%s' was accepted, so this check does not compare"
                  " what it says it compares" % name, file=sys.stderr)
    if refused != len(mutations):
        return 1
    say("  --selftest: all %d mutations were refused" % refused)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--image", default=None,
                        help="the linked entry image, for the three checks that need it")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    facts = gather(args.image)

    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the timer this image registers is not the timer this kernel would copy:",
              file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    header = local_defines(facts["entry_h"])
    say("  xnu_entry_481: `ml_init_timebase` is called from `arm_init`'s own registering"
        " `PE_init_platform` before Apple's copy runs, its table is `struct tbd_ops` in Apple's order,"
        " it drives `CNTV_TVAL`/`CNTV_CTL` (`c14, c3, 0`/`c14, c3, 1`) and not `CNTP`, and the four"
        " `cpu_data` words it is read back through are the ones the assembled kernel loads (%d, %d,"
        " %d, %d) - so `ml_get_decrementer`/`ml_set_decrementer` call this image's functions, and the"
        " first `fiq_context_init` must report 0x%08x in `CPU_DECREMENTER`"
        % (header["STAGE90_CPU_DECREMENTER"], header["STAGE90_CPU_GET_DECREMENTER_FUNC"],
           header["STAGE90_CPU_SET_DECREMENTER_FUNC"], header["STAGE90_CPU_GET_FIQ_HANDLER"],
           header["STAGE90_CPU_DECREMENTER_INITIAL"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
