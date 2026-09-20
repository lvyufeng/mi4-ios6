#!/usr/bin/env python3
"""
Check the six things 482's routing measurement rests on - and the two facts about this image's vector
slots that say what 483 has to change, before anything is enabled.

482 answers one question without enabling anything: **which INTID does the line this kernel programs
appear on**. The question only exists because of what the vector page currently does, so the check
asserts both halves: the state of the two timer slots, and the measurement's own safety and premises.
Every claim below is a claim about a *different* file, and each one can be false in a way no device
run would report as an error:

  1. **the GIC register offsets are one definition.** The GIC is not Apple's - MSM8974's interrupt
     controller is described by this repository's own `stages/stage90/gic.c` (its DT assertions and
     its register map) and by experiment 143's `xnu_msm8974_fiq_probe.c`. `entry_gic.h` transcribes
     eleven of those numbers plus two bases and every bit it writes, so the check compares **both
     directions**: each name the header defines must equal the payload's, and each name the payload
     defines that this step reads must be present in the header. That is the "one value, two
     definitions" guard applied to the half of this step whose owner is this repository rather than
     the tree - and it is the first check in this walk that compares two of *this project's* files
     for the same value.

  2. **the two candidate lines are the payload's, and they are distinct.** The device tree's timer
     node is `interrupts = <1 2 0 1 3 0>` and the xlate maps PPI n to INTID n + 16. `gic.c`'s
     `GIC_TIMER_PPI0_ID`/`PPI1_ID` are what the payload arms for its own dead-man, so "which line the
     timer is on" is a decision two files answer; a header that said 19 for both candidates would
     measure nothing, and the check requires the two to be different *and* to be the payload's.

  3. **what slot 6 and slot 7 hold today.** The vector page's literals are in the image and the check
     reads them: slot 6 holds **this image's** `fleh_irq` (`entry_stubs.c:6055`, which sets
     `g_irq_report_pending` and calls `entry_epilogue`), not Apple's `locore_fleh_irq` - which is in
     the image at 0x80015954 and in no slot. An interrupt today ends the run with a report; that is
     the state 308 measured, and it is *not* a dispatch.

  4. **what Apple's handler would do if 483 put it in slot 6.** `fleh_irq_handler`'s first six
     instructions load the five `assym.s` `INTERRUPT_*` words out of `cpu_data` and `blx r5` the
     fifth. Nothing in this image has ever stored one, so the moment the slot changes, the dispatch
     is an indirect branch to 0 - which is why the routing is decided before it is enabled.

  5. **the two ARM paths differ in exactly that.** `fleh_decirq_handler` calls `rtclock_intr`
     directly and never loads `cpu_data->interrupt_handler`, so the FIQ route would not need 483's
     work even if 143 had not measured it dead. The check asserts the *absence* in one and the
     presence in the other, because "which of the two paths needs a handler" is the routing decision
     stated as code.

  6. **the probe cannot deliver anything, in the three places it claims.** The CPU interface is
     written with both group enables clear and `PMR` 0 *before* the first countdown is armed; the
     candidate lines are disabled and cleared *before* `PMR` and `CTLR` are restored (the other order
     leaves a pending group-1 interrupt and a live interface for one instruction); `GICC_IAR` is
     never read, because reading it acknowledges; and the exception is masked with `cpsid if` around
     the whole window and restored bit by bit. Those are source-order properties, so they are checked
     as source order - a reordered restore is a probe that works until it does not.

    ./tools/check_gic_routing.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_gic_routing.py --image out/stage90/xnu_arm_entry.elf --selftest

What this check cannot say
--------------------------
Which candidate INTID the virtual timer actually asserts. That is the run's reading
(`xnu_live_gic_hits`, with `xnu_live_gic_id18_pend_unmasked` and its masked control beside it), and
the whole point of the step is that the answer is not derivable from any file - the device tree lists
two lines, 143 measured one of them to be `CNTP`'s, and which sibling `CNTV` is on is a fact about
this silicon. Every number the check *can* pin is pinned here so that the run's answer is the only
unknown left in it.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

BOOT_DIR = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot")
ENTRY_GIC_C = os.path.join(BOOT_DIR, "entry_gic.c")
ENTRY_GIC_H = os.path.join(BOOT_DIR, "entry_gic.h")
ENTRY_STUBS_C = os.path.join(BOOT_DIR, "entry_stubs.c")
PAYLOAD_GIC_C = os.path.join(REPO_ROOT, "stages/stage90/gic.c")
PAYLOAD_FIQ_PROBE_C = os.path.join(REPO_ROOT, "stages/stage90/xnu_msm8974_fiq_probe.c")
ASSYM = os.path.join(REPO_ROOT, "out/xnu_assym/STAGE90_XNU/assym.s")

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"
READELF = "arm-none-eabi-readelf"

# ------------------------------------------------------------------------------------------------
# The one definition, spelled once: header name -> (payload file, payload name)
# ------------------------------------------------------------------------------------------------

# The spelling difference is deliberate and is *here* rather than silently mapped in a parser: the
# architecture calls the register `GICD_ITARGETSR<n>` and the payload calls it `GICD_ITARGETS0`. One
# value with two spellings is 206's family, and the honest way to record it is an entry that says so.
SPELLING_DIFFERS = {
    "STAGE90_GICD_ITARGETSR0": ("GICD_ITARGETS0", "the architectural spelling has the R, the payload's does not"),
}

OFFSET_MAP = (
    ("STAGE90_GIC_DIST_BASE", PAYLOAD_FIQ_PROBE_C, "FIQ_GIC_DIST_BASE"),
    ("STAGE90_GIC_CPU_BASE", PAYLOAD_FIQ_PROBE_C, "FIQ_GIC_CPU_BASE"),
    ("STAGE90_GICD_CTLR", PAYLOAD_GIC_C, "GICD_CTLR"),
    ("STAGE90_GICD_TYPER", PAYLOAD_GIC_C, "GICD_TYPER"),
    ("STAGE90_GICD_IGROUPR0", PAYLOAD_FIQ_PROBE_C, "FIQ_GICD_IGROUPR0"),
    ("STAGE90_GICD_ISENABLER0", PAYLOAD_GIC_C, "GICD_ISENABLER0"),
    ("STAGE90_GICD_ICENABLER0", PAYLOAD_GIC_C, "GICD_ICENABLER0"),
    ("STAGE90_GICD_ISPENDR0", PAYLOAD_GIC_C, "GICD_ISPENDR0"),
    ("STAGE90_GICD_ICPENDR0", PAYLOAD_GIC_C, "GICD_ICPENDR0"),
    ("STAGE90_GICD_IPRIORITY0", PAYLOAD_GIC_C, "GICD_IPRIORITY0"),
    ("STAGE90_GICD_ITARGETSR0", PAYLOAD_GIC_C, "GICD_ITARGETS0"),
    ("STAGE90_GICC_CTLR", PAYLOAD_GIC_C, "GICC_CTLR"),
    ("STAGE90_GICC_PMR", PAYLOAD_GIC_C, "GICC_PMR"),
    ("STAGE90_GICC_IAR", PAYLOAD_GIC_C, "GICC_IAR"),
    ("STAGE90_GICC_EOIR", PAYLOAD_GIC_C, "GICC_EOIR"),
    ("STAGE90_GIC_TIMER_PPI0", PAYLOAD_GIC_C, "GIC_TIMER_PPI0_ID"),
    ("STAGE90_GIC_TIMER_PPI1", PAYLOAD_GIC_C, "GIC_TIMER_PPI1_ID"),
)

# Names the payload defines that this check requires to agree with *each other* where both files have
# them: a register 482 reads whose two payload sources disagree is a value nobody can transcribe.
CROSS_CHECKED_IN_PAYLOAD = ("GICD_CTLR", "GICC_CTLR", "GICC_IAR", "GICC_EOIR")


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def defines(text):
    """`#define NAME value` for values that are a single literal, optionally `A | B` of literals.

    Two forms have to be handled and both are present in the files this reads: a C header's
    `0x080u`, and `assym.s`'s `#180` - `genassym.sh` writes the hash because Apple's `genassym.s` is
    fed to `sed`. And comments are stripped first, because a trailing `/* ... */` after a value is
    how every one of these files documents the line it is on; a parser anchored at the end of the
    line would silently read only the defines nobody had annotated yet, which is the shape of a check
    that stops checking what it was written for (205/206).
    """
    out = {}
    for match in re.finditer(r"^#define[ \t]+([A-Za-z_]\w*)[ \t]+([^\n]+)$", strip_comments(text),
                             re.M):
        name, value = match.group(1), match.group(2).strip()
        value = re.sub(r"[uU]\b", "", value).replace("#", "").strip()
        if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", value):
            out[name] = int(value, 0)
        elif re.fullmatch(r"0[xX][0-9a-fA-F]+\s*\|\s*(0[xX][0-9a-fA-F]+|\d+)(\s*\|\s*0[xX][0-9a-fA-F]+)*",
                         value):
            total = 0
            for part in value.split("|"):
                total |= int(part.strip(), 0)
            out[name] = total
    return out


def run(argv):
    proc = subprocess.run(argv, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit("FAIL: %s exited %d\n%s" % (" ".join(argv), proc.returncode, proc.stderr))
    return proc.stdout


def nm(path):
    symbols = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    return symbols


def sections(path):
    """(vaddr, size, offset) for every `PROGBITS`/`NOBITS` section, from `readelf -S -W`."""
    out = []
    for line in run([READELF, "-S", "-W", path]).splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
                         line)
        if not match:
            continue
        name, kind, addr, off, size = match.groups()
        if kind in ("PROGBITS", "NOBITS"):
            out.append((name, int(addr, 16), int(size, 16), int(off, 16)))
    return out


def image_reader(path):
    secs = sections(path)

    def word_at(vaddr):
        for _name, addr, size, off in secs:
            if addr <= vaddr < addr + size:
                with open(path, "rb") as handle:
                    handle.seek(off + (vaddr - addr))
                    raw = handle.read(4)
                return int.from_bytes(raw, "little")
        return None

    return word_at


def disasm(path, start, size):
    text = run([OBJDUMP, "-d", "--start-address=0x%x" % start,
                "--stop-address=0x%x" % (start + size), path])
    out = []
    for line in text.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*?)\s*$", line)
        if match:
            ops = match.group(4).split(";")[0].strip()
            out.append((int(match.group(1), 16), match.group(3), ops))
    return out


def payload_assertions(text):
    """The payload's own `ok &= (X == 0xNNN)` lines: the DT-derived base addresses it refuses to
    proceed without. These are a *third* source for the two bases, upstream of both constants - the
    expression's left-hand side is a struct member (`GIC_state_stage90.distBase`), so the name is
    taken as its last component and the fact that it is a member is not lost, only not needed."""
    out = {}
    for match in re.finditer(r"ok\s*&=\s*\(\s*([\w.]+)\s*==\s*(0[xX][0-9a-fA-F]+)\s*[uU]?\s*\)",
                             strip_comments(text)):
        out[match.group(1).split(".")[-1]] = int(match.group(2), 0)
    return out


def function_body(text, name):
    """The braces-balanced body of `name`, for the two reporting handlers and the two CPSR helpers.
    Balanced rather than `.*?\\n\\}`, because `entry_stubs.c`'s one-line handlers put their closing
    brace on the same line and a body extractor that returned nothing would make every claim about
    them vacuously true."""
    match = re.search(r"^\s*(?:static\s+)?[\w \t*]+?\b%s\s*\([^)]*\)\s*\{" % re.escape(name),
                      text, re.M)
    if not match:
        return None
    start = text.index("{", match.start())
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    return None


def source_index(text, needle):
    index = text.find(needle)
    return index


def gather(image):
    facts = {
        "header_text": read(ENTRY_GIC_H),
        "probe_text": read(ENTRY_GIC_C),
        "stubs_text": read(ENTRY_STUBS_C),
        "payload_text": read(PAYLOAD_GIC_C),
        "payload_fiq_text": read(PAYLOAD_FIQ_PROBE_C),
        "assym_text": read(ASSYM),
    }
    facts["header"] = defines(facts["header_text"])
    facts["payload"] = defines(facts["payload_text"])
    facts["payload_fiq"] = defines(facts["payload_fiq_text"])
    facts["assym"] = defines(facts["assym_text"])
    facts["payload_asserts"] = payload_assertions(facts["payload_text"])

    if image:
        symbols = nm(image)
        read_word = image_reader(image)
        facts["image_symbols"] = symbols
        facts["slot_words"] = {}
        for slot, name in ((6, "vec_tramp_6_handler"), (7, "vec_tramp_7_handler")):
            addr = symbols.get(name)
            facts["slot_words"][slot] = read_word(addr) if addr else None
        handler = symbols.get("fleh_irq_handler")
        facts["dispatch"] = disasm(image, handler, DISPATCH_WINDOW) if handler else []
        dec = symbols.get("fleh_decirq_handler")
        facts["dec_dispatch"] = disasm(image, dec, DISPATCH_WINDOW) if dec else []
    else:
        facts["image_symbols"] = {}
        facts["slot_words"] = {}
        facts["dispatch"] = []
        facts["dec_dispatch"] = []
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

DISPATCH_LOAD_ORDER = ("INTERRUPT_TARGET", "INTERRUPT_REFCON", "INTERRUPT_NUB", "INTERRUPT_SOURCE",
                       "INTERRUPT_HANDLER")

# The five loads land in the call's own argument registers, and the fifth is also the callee. This is
# AAPCS and not a choice: `handler(target, refCon, nub, source)`.
DISPATCH_CALL_REGS = ("r0", "r1", "r2", "r3", "r5")

# How far to disassemble a handler. Not the function's extent - this image's symbol table carries no
# size for the two handlers (`nm -S` prints `80015a70 t fleh_irq_handler` with no size field) and the
# `L_*` labels the assembler emits sit *inside* the function, so "the next symbol" is not the end
# either. It is a bound: generous enough to contain the dispatch, and short enough that only a
# coincidental run of five `ldr rX, [rY, #N]` with these exact offsets could match inside it.
DISPATCH_WINDOW = 0x100

LOAD_RE = re.compile(r"^(r\d+), \[(r\d+), #(\d+)\]$")


def dispatch_window(dispatch, expected):
    """The index of Apple's IRQ dispatch inside `dispatch`, or None if the image does not have that
    shape. Five `ldr`s, in `expected`'s order, out of one base register, into `DISPATCH_CALL_REGS`,
    then `blx` through the fifth. One function rather than two copies, because the claim and the
    mutation that breaks it must look at the same six instructions: the first version of the mutation
    selected by regex alone, caught four `str`s, and swapped two instructions that were not in the
    window at all - so it changed the facts and left the claim standing."""
    if len(expected) != len(DISPATCH_CALL_REGS) or None in expected:
        return None
    for i in range(len(dispatch) - 5):
        window = dispatch[i:i + 6]
        if window[5][1] != "blx" or window[5][2] != DISPATCH_CALL_REGS[4]:
            continue
        if not all(window[k][1] == "ldr" for k in range(5)):
            continue
        loads = [LOAD_RE.match(window[k][2]) for k in range(5)]
        if not all(loads):
            continue
        if len({load.group(2) for load in loads}) != 1:
            continue
        if [load.group(1) for load in loads] != list(DISPATCH_CALL_REGS):
            continue
        if [int(load.group(3)) for load in loads] != list(expected):
            continue
        return i
    return None


def claim_offsets(facts, failures, notes):
    header = facts["header"]
    payloads = {"gic.c": facts["payload"], "xnu_msm8974_fiq_probe.c": facts["payload_fiq"]}

    for header_name, path, payload_name in OFFSET_MAP:
        if header_name not in header:
            failures.append("entry_gic.h declares no %s, so the value the probe writes is not "
                            "transcribed anywhere this check can see" % header_name)
            continue
        source = payloads[os.path.basename(path)]
        if payload_name not in source:
            failures.append("%s no longer defines %s - the payload's name for %s moved or was "
                            "renamed, and a mapped name that is absent is a comparison that silently "
                            "stopped happening" % (os.path.basename(path), payload_name, header_name))
            continue
        if header[header_name] != source[payload_name]:
            failures.append("%s is 0x%x in entry_gic.h and 0x%x in %s's %s"
                            % (header_name, header[header_name], source[payload_name],
                               os.path.basename(path), payload_name))
    for header_name, (payload_name, why) in SPELLING_DIFFERS.items():
        if header_name in header and payload_name in facts["payload"]:
            notes.append("%s and %s are the same register with two spellings (%s), and both are 0x%x"
                         % (header_name, payload_name, why, header[header_name]))

    # The reverse direction, and the one that catches a *rename*: every name the two payload files
    # use for a GIC register that 482 reads must be one this header has heard of.
    known = {payload_name for _h, _p, payload_name in OFFSET_MAP}
    known |= {payload_name for payload_name, _why in SPELLING_DIFFERS.values()}
    for name in CROSS_CHECKED_IN_PAYLOAD:
        values = {label: table[name] for label, table in payloads.items() if name in table}
        if len(set(values.values())) > 1:
            failures.append("the two payload files disagree about %s: %s"
                            % (name, ", ".join("%s = 0x%x" % kv for kv in sorted(values.items()))))
        if name not in known:
            failures.append("%s is read by the payload's GIC code and is not in entry_gic.h's map, "
                            "so 482 could transcribe it from nowhere" % name)


def claim_bases(facts, failures, notes):
    header = facts["header"]
    asserts = facts["payload_asserts"]
    for header_name, local_name in (("STAGE90_GIC_DIST_BASE", "distBase"),
                                    ("STAGE90_GIC_CPU_BASE", "cpuBase")):
        if local_name not in asserts:
            failures.append("gic.c has no `ok &= (%s == 0x...)` assertion, so the payload's own "
                            "statement of this base is gone" % local_name)
            continue
        if header.get(header_name) != asserts[local_name]:
            failures.append("%s is 0x%x in entry_gic.h and 0x%x in gic.c's own assertion"
                            % (header_name, header.get(header_name, -1), asserts[local_name]))
    cpu = header.get("STAGE90_GIC_CPU_BASE")
    dist = header.get("STAGE90_GIC_DIST_BASE")
    if cpu is not None and dist is not None and cpu != dist + 0x2000:
        failures.append("the CPU interface is at 0x%x and the distributor at 0x%x: the CPU interface "
                        "is the distributor's second 4 KB pair on every GICv2 this project has "
                        "measured, and the probe's two windows assume it" % (cpu, dist))


def claim_candidates(facts, failures, notes):
    header = facts["header"]
    ppi0 = header.get("STAGE90_GIC_TIMER_PPI0")
    ppi1 = header.get("STAGE90_GIC_TIMER_PPI1")
    if ppi0 is None or ppi1 is None:
        failures.append("entry_gic.h declares no timer PPI candidate")
        return
    if ppi0 == ppi1:
        failures.append("both timer candidates are INTID %d, so the probe measures one line twice "
                        "and its `_hits` mask cannot distinguish anything" % ppi0)
    for name, value in (("STAGE90_GIC_TIMER_PPI0", ppi0), ("STAGE90_GIC_TIMER_PPI1", ppi1)):
        if value >= 32:
            failures.append("%s is INTID %d, which is an SPI rather than a PPI: the timer's lines "
                            "are per-CPU, and a PPI's enable, group, priority and target registers "
                            "are the banked ones this probe reads" % (name, value))
    payload0 = facts["payload"].get("GIC_TIMER_PPI0_ID")
    payload1 = facts["payload"].get("GIC_TIMER_PPI1_ID")
    if payload0 is None or payload1 is None:
        failures.append("gic.c no longer names the timer PPIs, so the payload's half of this decision "
                        "is gone")
    else:
        if {ppi0, ppi1} != {payload0, payload1}:
            failures.append("the header's candidates are {%d, %d} and the payload arms {%d, %d}: "
                            "the two files do not agree about which lines the timer is on"
                            % (ppi0, ppi1, payload0, payload1))


def claim_vector(facts, failures, notes):
    symbols = facts["image_symbols"]
    if not symbols:
        failures.append("no image was read, so what the vector page holds today is uncompared")
        return
    for slot, reporting, apple in ((6, "fleh_irq", "locore_fleh_irq"),
                                   (7, "fleh_decirq", "locore_fleh_decirq")):
        word = facts["slot_words"].get(slot)
        want = symbols.get(reporting)
        apple_addr = symbols.get(apple)
        if word is None or want is None:
            failures.append("slot %d's trampoline literal or the image's %s is missing, so neither "
                            "the slot nor its target can be named" % (slot, reporting))
            continue
        if word != want:
            failures.append("slot %d holds 0x%08x and the image's %s is 0x%08x: the vector page no "
                            "longer points at the reporting handler this step's risk statement is "
                            "written against" % (slot, word, reporting, want))
        if apple_addr is None:
            failures.append("the image has no %s, so the handler 483 would move into slot %d is not "
                            "linked into it at all" % (apple, slot))
        elif word == apple_addr:
            failures.append("slot %d already holds Apple's %s (0x%08x): the vector page changed, and "
                            "with it every claim about what an interrupt does today"
                            % (slot, apple, apple_addr))

    # And the property that makes them *reporting* handlers rather than dispatchers: the source of
    # each is one call into the epilogue. Comments are stripped first - `fleh_irq`'s own body
    # *mentions* `entry_epilogue` in prose ("the MMU does not come off until `entry_epilogue` is
    # inside"), so a test on the raw body is satisfied by the comment that describes the call. The
    # first version of this check was, and its mutation was accepted.
    for name in ("fleh_irq", "fleh_decirq"):
        body = function_body(facts["stubs_text"], name)
        if body is None:
            failures.append("entry_stubs.c no longer defines %s, so the symbol the vector page points "
                            "at is defined somewhere else" % name)
            continue
        code = strip_comments(body)
        if not re.search(r"\bentry_epilogue\s*\(", code):
            failures.append("entry_stubs.c's %s no longer calls entry_epilogue: the slot stopped being "
                            "a report-and-stop, and the risk this step states is about a different "
                            "machine" % name)
        if re.search(r"\binterrupt_handler\b", code):
            failures.append("entry_stubs.c's %s reads cpu_data's interrupt_handler, so it dispatches "
                            "and the IRQ slot is already 483's" % name)


def claim_dispatch(facts, failures, notes):
    header = facts["header"]
    assym = facts["assym"]

    # The five offsets are one definition: the header's, and `assym.s`'s, in Apple's own order.
    names = ("INTERRUPT_HANDLER", "INTERRUPT_NUB", "INTERRUPT_SOURCE", "INTERRUPT_TARGET",
             "INTERRUPT_REFCON")
    values = []
    for name in names:
        local = header.get("STAGE90_CPU_" + name)
        if local is None:
            failures.append("entry_gic.h declares no STAGE90_CPU_%s" % name)
            continue
        if name not in assym:
            failures.append("this configuration's assym.s declares no %s, so there is nothing for "
                            "STAGE90_CPU_%s to be compared against" % (name, name))
            continue
        if local != assym[name]:
            failures.append("STAGE90_CPU_%s is %d and assym.s says %s is %d"
                            % (name, local, name, assym[name]))
        values.append(local)
    if len(values) == len(names) and values != sorted(values):
        failures.append("the five interrupt words are %s in the header, and `fleh_irq_handler` loads "
                        "them as a struct: the offsets must be strictly increasing"
                        % ", ".join(str(v) for v in values))

    # Apple's IRQ dispatcher, out of the image: five loads in the ABI's own register order and the
    # indirect call. This is the claim that says the dispatch goes to a slot, and which slot.
    dispatch = facts["dispatch"]
    if not dispatch:
        failures.append("the image has no fleh_irq_handler to disassemble, so the dispatch this step "
                        "is deciding about was not read")
        return
    index = None
    if len(values) == len(names):
        expected = [header.get("STAGE90_CPU_" + name) for name in DISPATCH_LOAD_ORDER]
        index = dispatch_window(dispatch, expected)
    if index is None:
        failures.append("fleh_irq_handler loads no five-word window ending in `blx r5` whose offsets "
                        "are the five in the header, in %s, out of one base and into %s: the dispatch "
                        "is not the shape this step's routing argument is built on"
                        % (", ".join(DISPATCH_LOAD_ORDER), ", ".join(DISPATCH_CALL_REGS)))
    else:
        notes.append("fleh_irq_handler's dispatch is at 0x%08x: %s"
                     % (dispatch[index][0], "; ".join("%s %s" % (kind, ops)
                                                      for _addr, kind, ops
                                                      in dispatch[index:index + 6])))

    # The other path, and the difference that is the routing decision: the decrementer handler calls
    # rtclock_intr directly and needs no `cpu_data` handler at all.
    dec = facts["dec_dispatch"]
    if not dec:
        failures.append("the image has no fleh_decirq_handler to disassemble, so the path 143 "
                        "measured dead was not read")
    else:
        if not any(kind == "bl" and "rtclock_intr" in ops for _addr, kind, ops in dec):
            failures.append("fleh_decirq_handler no longer calls rtclock_intr: the decrementer path "
                            "stopped being the one that needs no handler, and the two routes are no "
                            "longer distinguishable by this check")
        if any(kind == "ldr" and "[r4, #180]" in ops for _addr, kind, ops in dec):
            failures.append("fleh_decirq_handler now loads cpu_data's interrupt_handler, so the FIQ "
                            "route needs 483's work as well and the routing decision is a different "
                            "one")


def claim_guards(facts, failures, notes):
    text = facts["probe_text"]

    guard = text.find("gicc_write(STAGE90_GICC_CTLR, cpu_ctlr & ~STAGE90_GICC_CTLR_ANY)")
    guard_pmr = text.find("gicc_write(STAGE90_GICC_PMR, 0u)")
    first_probe = text.find("added18 = gic_probe_candidate(STAGE90_GIC_TIMER_PPI0,")
    disable = text.find("gicd_write(STAGE90_GICD_ICENABLER0, (1u << STAGE90_GIC_TIMER_PPI0)")
    restore = text.find("gicc_write(STAGE90_GICC_PMR, cpu_pmr)")
    mask_fn = text.find("static uint32_t gic_irq_mask(void)")
    restore_fn = text.find("static void gic_irq_restore(uint32_t saved)")

    if min(guard, guard_pmr, first_probe, disable, restore, mask_fn, restore_fn) < 0:
        failures.append("entry_gic.c no longer contains all of the guard, the first probe, the "
                        "candidate disable, the restore and the two CPSR functions: this check's "
                        "source-order claims would be measuring nothing")
        return
    if not (guard < first_probe and guard_pmr < first_probe):
        failures.append("the CPU interface is guarded after the first countdown is armed: the arm "
                        "would raise an enabled line into a live interface")
    if not disable < restore:
        failures.append("the candidates are disabled *after* the CPU interface is restored, which "
                        "leaves a pending group-1 interrupt and a live interface for one instruction")
    for fn_start, fn_name, needle in ((mask_fn, "gic_irq_mask", "cpsid if"),
                                      (restore_fn, "gic_irq_restore", "msr cpsr_c")):
        end = text.find("\n}\n", fn_start)
        body = text[fn_start:end if end > 0 else fn_start + 400]
        if needle not in body:
            failures.append("entry_gic.c's %s no longer contains `%s`, so the exception mask the "
                            "probe's safety rests on is not the one written" % (fn_name, needle))
    if re.search(r"gicc_(read|write)\(STAGE90_GICC_IAR", text):
        failures.append("entry_gic.c touches GICC_IAR: a read of it acknowledges the interrupt, and "
                        "this step must not acknowledge anything it has not decided to handle")
    if "gic_irq_mask()" not in text or "gic_irq_restore(cpsr)" not in text:
        failures.append("entry_gic.c declares the two CPSR functions but does not use both, so the "
                        "mask is not actually held across the window")

    # What each candidate raised, and that it clears it. Three readings of the first run's log, all
    # three of them properties of `gic_probe_candidate` rather than of the whole file: the masked
    # `CNTV_CTL` read was taken *before* its spin and the unmasked one *after* it, so the pair a
    # reader compares differs in *when* it was read and not only in IMASK; the candidate cleared its
    # own INTID while the line its countdown raised stayed pending, into the next candidate's
    # negative control and into the distributor after the probe returned; and the answer this step
    # exists to produce has to be derived from the bits the probe raised, not written down.
    candidate = function_body(text, "gic_probe_candidate")
    if candidate is None:
        failures.append("entry_gic.c no longer defines gic_probe_candidate, so the three properties "
                        "its numbers rest on are not the ones written")
        return
    for window in ("ctl_masked", "ctl_unmasked"):
        if not re.search(r"gic_spin\(STAGE90_GIC_PROBE_SPIN\);\s*\n\s*%s = stage90_cntv_ctl_read\(\);"
                         % window, candidate):
            failures.append("entry_gic.c's gic_probe_candidate reads %s with no spin immediately "
                            "before it: the masked and unmasked windows then report two different "
                            "moments of the countdown, which is what the first run's 0x3/0x5 pair "
                            "looked like" % window)
    if not re.search(r"added\s*=\s*pend_unmasked\s*&\s*~pre_pending\s*;", candidate):
        failures.append("entry_gic.c's gic_probe_candidate no longer derives `added` from the bits "
                        "that appeared while its own countdown was the only variable")
    if not re.search(r"ICENABLER0,\s*bit\s*\|\s*added\)", candidate) \
            or not re.search(r"ICPENDR0,\s*bit\s*\|\s*added\)", candidate):
        failures.append("entry_gic.c's gic_probe_candidate clears only its own INTID: the line its "
                        "countdown raised stays pending into the next candidate's negative control "
                        "and into the distributor after the probe returns")
    if "gic_single_intid(added)" not in text:
        failures.append("entry_gic.c no longer derives the measured INTID from the bits the probe "
                        "raised, so the answer to this step's question is a literal somewhere")

    # The margin, from the source side. A spin too short for its own countdown is indistinguishable
    # from "the line is not this one", so the probe brackets its two candidates with the counter and
    # writes the elapsed reading - which is what turns the margin into a number in the log instead of
    # an argument in a comment. (The comment that used to argue it was wrong twice: 481's 813 ticks
    # over 20000 iterations is 41 per 1000, not the 24 it quoted, and 481's counter is a register
    # while this one is `volatile`.)
    t_a = text.find("t_a = stage90_cntvct_read();")
    t_b = text.find("t_b = stage90_cntvct_read();")
    elapsed = text.find('GIC_LIVE("xnu_live_gic_ticks_elapsed"')
    if min(t_a, t_b, elapsed) < 0:
        failures.append("entry_gic.c no longer brackets its windows with `stage90_cntvct_read` and "
                        "records the elapsed count, so whether the spin outlasted the countdown is a "
                        "claim in a comment rather than a reading in the log")
    elif not (t_a < first_probe < t_b < elapsed):
        failures.append("entry_gic.c's counter pair no longer brackets both candidates "
                        "(t_a=%d, first candidate=%d, t_b=%d, elapsed=%d): the elapsed reading covers "
                        "a span that is not the two windows" % (t_a, first_probe, t_b, elapsed))


def compare(facts, mutate=None):
    if mutate:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    claim_offsets(facts, failures, notes)
    claim_bases(facts, failures, notes)
    claim_candidates(facts, failures, notes)
    claim_vector(facts, failures, notes)
    claim_dispatch(facts, failures, notes)
    claim_guards(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest: every claim, broken in the way it would really break
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["header"] = dict(facts["header"])
    facts["payload"] = dict(facts["payload"])
    facts["assym"] = dict(facts["assym"])
    facts["image_symbols"] = dict(facts["image_symbols"])
    facts["slot_words"] = dict(facts["slot_words"])
    facts["dispatch"] = list(facts["dispatch"])
    facts["dec_dispatch"] = list(facts["dec_dispatch"])

    if mutate == "gicd_offset_moved":
        facts["header"]["STAGE90_GICD_ISPENDR0"] += 4
    elif mutate == "gicc_offset_moved":
        facts["header"]["STAGE90_GICC_EOIR"] += 4
    elif mutate == "base_moved":
        facts["header"]["STAGE90_GIC_DIST_BASE"] = 0xF9001000
    elif mutate == "cpu_if_not_the_second_pair":
        facts["header"]["STAGE90_GIC_CPU_BASE"] = 0xF9004000
    elif mutate == "payload_offset_moved":
        facts["payload"]["GICD_ISENABLER0"] += 4
    elif mutate == "payload_assertion_moved":
        facts["payload_asserts"] = dict(facts["payload_asserts"])
        facts["payload_asserts"]["distBase"] = 0xF9001000
    elif mutate == "payload_name_renamed":
        facts["payload"] = {k: v for k, v in facts["payload"].items() if k != "GICD_ISPENDR0"}
    elif mutate == "payload_names_disagree":
        facts["payload_fiq"] = dict(facts["payload_fiq"])
        facts["payload_fiq"]["GICC_CTLR"] = 0x00000008
    elif mutate == "candidates_the_same":
        facts["header"]["STAGE90_GIC_TIMER_PPI1"] = facts["header"]["STAGE90_GIC_TIMER_PPI0"]
    elif mutate == "candidate_not_the_payloads":
        facts["header"]["STAGE90_GIC_TIMER_PPI0"] = 17
    elif mutate == "candidate_is_an_spi":
        facts["header"]["STAGE90_GIC_TIMER_PPI0"] = 34
        facts["payload"]["GIC_TIMER_PPI0_ID"] = 34
    elif mutate == "slot6_moved":
        facts["slot_words"][6] = facts["image_symbols"]["fleh_decirq"]
    elif mutate == "slot7_is_apples":
        facts["slot_words"][7] = facts["image_symbols"]["locore_fleh_decirq"]
    elif mutate == "reporting_handler_dispatches":
        facts["stubs_text"] = _bump(
            facts["stubs_text"], "    g_irq_report_pending = 1u;",
            "    g_irq_report_pending = 1u;\n    interrupt_handler = 0;")
    elif mutate == "reporting_handler_stops_reporting":
        facts["stubs_text"] = _bump(
            facts["stubs_text"], '    entry_epilogue("exception: irq");',
            '    g_irq_report_pending = g_irq_report_pending;')
    elif mutate == "assym_handler_moved":
        facts["assym"]["INTERRUPT_HANDLER"] += 4
    elif mutate == "header_handler_moved":
        facts["header"]["STAGE90_CPU_INTERRUPT_HANDLER"] += 4
    elif mutate == "dispatch_load_dropped":
        kept = [insn for insn in facts["dispatch"] if "#180" not in insn[2]]
        facts["dispatch"] = kept
    elif mutate == "dispatch_blx_dropped":
        facts["dispatch"] = [insn for insn in facts["dispatch"] if insn[1] != "blx"]
    elif mutate == "dispatch_window_short":
        facts["dispatch"] = facts["dispatch"][:24]
    elif mutate == "dispatch_order_swapped":
        expected = [facts["header"].get("STAGE90_CPU_" + name) for name in DISPATCH_LOAD_ORDER]
        at = dispatch_window(facts["dispatch"], expected)
        assert at is not None, "dispatch_order_swapped: the image has no dispatch window to swap in"
        swapper = dict(zip((4, 3), (3, 4)))
        for k in (3, 4):
            addr, kind, ops = facts["dispatch"][at + k]
            load = LOAD_RE.match(ops)
            facts["dispatch"][at + k] = (addr, kind, "%s, [%s, #%s]"
                                         % (DISPATCH_CALL_REGS[swapper[k]], load.group(2),
                                            load.group(3)))
    elif mutate == "decrementer_path_reads_the_handler":
        facts["dec_dispatch"] = list(facts["dec_dispatch"]) + [
            (0x80015b40, "ldr", "r5, [r4, #180]"), (0x80015b44, "blx", "r5")]
    elif mutate == "decrementer_path_stops_calling_rtclock":
        facts["dec_dispatch"] = [insn for insn in facts["dec_dispatch"]
                                 if "rtclock_intr" not in insn[2]]
    elif mutate == "guard_after_first_probe":
        # Into the restore, not merely later in the pre-probe region: the first version of this
        # mutation re-inserted the guard after `t_a = stage90_cntvct_read()`, which is still *before*
        # every candidate - so the mutant satisfied the property and the selftest accepted it.
        old = ("    gicc_write(STAGE90_GICC_CTLR, cpu_ctlr & ~STAGE90_GICC_CTLR_ANY);\n"
               "    gicc_write(STAGE90_GICC_PMR, 0u);")
        facts["probe_text"] = _bump(facts["probe_text"], old, "")
        facts["probe_text"] = _bump(facts["probe_text"],
                                    "    gicc_write(STAGE90_GICC_PMR, cpu_pmr);",
                                    old + "\n    gicc_write(STAGE90_GICC_PMR, cpu_pmr);")
    elif mutate == "restore_before_disable":
        disable = ("    gicd_write(STAGE90_GICD_ICENABLER0, (1u << STAGE90_GIC_TIMER_PPI0) | "
                   "(1u << STAGE90_GIC_TIMER_PPI1));\n"
                   "    gicd_write(STAGE90_GICD_ICPENDR0, (1u << STAGE90_GIC_TIMER_PPI0) | "
                   "(1u << STAGE90_GIC_TIMER_PPI1));\n")
        facts["probe_text"] = _bump(facts["probe_text"], disable, "")
        facts["probe_text"] = _bump(facts["probe_text"],
                                    "    restore_pmr = gicc_read(STAGE90_GICC_PMR);",
                                    disable + "    restore_pmr = gicc_read(STAGE90_GICC_PMR);")
    elif mutate == "ctl_read_before_spin":
        facts["probe_text"] = _bump(
            facts["probe_text"],
            "    gic_spin(STAGE90_GIC_PROBE_SPIN);\n    ctl_masked = stage90_cntv_ctl_read();",
            "    ctl_masked = stage90_cntv_ctl_read();\n    gic_spin(STAGE90_GIC_PROBE_SPIN);")
    elif mutate == "clears_own_bit_only":
        facts["probe_text"] = _bump(facts["probe_text"], "bit | added", "bit")
    elif mutate == "intid_is_a_literal":
        facts["probe_text"] = _bump(facts["probe_text"], "gic_single_intid(added)", "20u")
    elif mutate == "probe_windows_unbracketed":
        facts["probe_text"] = _bump(
            facts["probe_text"], "    t_b = stage90_cntvct_read();\n",
            "")
        facts["probe_text"] = _bump(
            facts["probe_text"], "    added18 = gic_probe_candidate(",
            "    t_b = stage90_cntvct_read();\n    added18 = gic_probe_candidate(")
    elif mutate == "iar_acknowledged":
        facts["probe_text"] = _bump(
            facts["probe_text"], "    dist_ctlr = gicd_read(STAGE90_GICD_CTLR);",
            "    dist_ctlr = gicc_read(STAGE90_GICC_IAR);")
    elif mutate == "mask_without_cpsid":
        facts["probe_text"] = _bump(facts["probe_text"], '    __asm__ volatile ("cpsid if" ::: "memory");',
                                    "    __asm__ volatile (\"\" ::: \"memory\");")
    elif mutate == "restore_without_msr":
        facts["probe_text"] = _bump(
            facts["probe_text"], '    __asm__ volatile ("msr cpsr_c, %0" :: "r" (now) : "memory");',
            '    __asm__ volatile ("nop" :: "r" (now) : "memory");')
    elif mutate == "mask_declared_but_unused":
        facts["probe_text"] = _bump(facts["probe_text"], "    cpsr = gic_irq_mask();",
                                    "    cpsr = 0u;")
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "gicd_offset_moved", "gicc_offset_moved", "base_moved", "cpu_if_not_the_second_pair",
    "payload_offset_moved", "payload_assertion_moved", "payload_name_renamed",
    "payload_names_disagree", "candidates_the_same", "candidate_not_the_payloads",
    "candidate_is_an_spi", "slot6_moved", "slot7_is_apples", "reporting_handler_dispatches",
    "reporting_handler_stops_reporting", "assym_handler_moved", "header_handler_moved",
    "dispatch_load_dropped", "dispatch_blx_dropped", "dispatch_window_short",
    "dispatch_order_swapped", "decrementer_path_reads_the_handler",
    "decrementer_path_stops_calling_rtclock", "guard_after_first_probe", "restore_before_disable",
    "iar_acknowledged", "mask_without_cpsid", "restore_without_msr", "mask_declared_but_unused",
    "ctl_read_before_spin", "clears_own_bit_only", "intid_is_a_literal",
    "probe_windows_unbracketed",
)


def selftest(facts):
    accepted = []
    for name in MUTATIONS:
        failures, _notes = compare(facts, mutate=name)
        if not failures:
            accepted.append(name)
            print("      ACCEPTED: %s" % name, file=sys.stderr)
    if accepted:
        print("FAIL: %d of %d mutations were not refused: %s"
              % (len(accepted), len(MUTATIONS), ", ".join(accepted)), file=sys.stderr)
        return 1
    say("  --selftest: all %d mutations were refused" % len(MUTATIONS))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--image", default=None, help="the linked entry image")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if not args.image:
        print("FAIL: --image is required: three of this check's claims are about the linked image",
              file=sys.stderr)
        return 1

    facts = gather(args.image)

    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the routing this step measures is not the routing this image has:",
              file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    header = facts["header"]
    say("  xnu_entry_482: the GIC offsets this probe reads are the payload's own (both of this "
        "repository's files, compared), the two candidates are the payload's timer PPIs (%d and %d, "
        "distinct), slot 6 holds this image's reporting `fleh_irq` while Apple's `locore_fleh_irq` "
        "sits in no slot, Apple's handler would dispatch through `cpu_data`+%d with nothing ever "
        "stored - and the probe's three guards are in the source in the order that makes them guards"
        % (header["STAGE90_GIC_TIMER_PPI0"], header["STAGE90_GIC_TIMER_PPI1"],
           header["STAGE90_CPU_INTERRUPT_HANDLER"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
