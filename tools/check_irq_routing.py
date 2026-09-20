#!/usr/bin/env python3
"""
Check the six things 483's interrupt depends on, before any of them can deliver anything.

482 measured *which line* the virtual timer asserts. 483 is the step that lets a countdown on that line
become an interrupt, and what it adds to the machine is not a number - it is a *chain*: a handler word in
`cpu_data`, a line enabled at the distributor, a countdown unmasked last. Each link can be wrong in a way
that produces a run rather than an error, so each is a claim here. The claims that are about *shared*
artifacts - the GIC register offsets, the two timer candidates, the five `cpu_data` interrupt words and
the dispatch window - are `tools/check_gic_routing.py`'s and are not repeated here: one claim, one file,
so that a later edit cannot leave two copies of it disagreeing (this project's oldest defect class).

  1. **the handler is installed the way Apple installs one.** `entry_irq.c` must install through
     `ml_install_interrupt_handler` - the only writer of those words in the tree - and must **not** store
     them itself. A second writer is a second definition, and the read-back that follows would then be
     measuring this file's own store. The interrupt stack pointer is read as a *gate* before the call,
     because Apple's entry loads `sp` from it on both paths and a zero there is a load of address 0
     inside the exception entry.

  2. **the read-back is a gate and not a report.** The two words that decide where the dispatch goes are
     the handler (r5) and the target (r0); both are compared against this file's own symbols, and a
     disagreement returns 0 rather than enabling a line whose dispatch would call something else.

  3. **the `ICFGR` arithmetic is derived and not hardcoded.** `GICD_ICFGR<n>` is the one offset in this
     header with no second source in this repository - the payload's `gic.c` does not name it - so what is
     checked is that the word and the field shift come from the intid in the expression the header
     documents, rather than from a second literal pair.

  4. **the handler's three cases are three cases.** It reads `GICC_IAR`; the line 482 measured gets an
     `EOIR` and `rtclock_intr(0)`; a spurious read gets no `EOIR` (the architecture says a spurious read
     acknowledges nothing); and any other intid is acknowledged, recorded, **and stopped** - because an
     unhandled line that returns is an interrupt that arrives again immediately, a hang with no message.

  5. **the enable is a switch in the source and not a flag on a command line.** `STAGE90_IRQ_ENABLE_LINE`
     is what decides whether a countdown may become an interrupt (this file) and which handler slot 6 must
     hold (`tools/check_gic_routing.py`). One value, read from two places, defined in one - and defined in
     the source because an image and the source that describes it must not be able to disagree.

  6. **the handler is installed, and the line enabled, before the mask comes off.** Source-order
     properties in two files: 482's probe, then the arming, then the kernel's own deadline, then the
     unmask - and the arming after `g_dec_writes++`, because `ml_install_interrupt_handler` ends in
     `initialize_screen`, which can re-enter the function the arming is called from.

    ./tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest

What this check cannot say
--------------------------
Whether an interrupt is ever *delivered*, and whether `rtclock_intr` finishes. Those are the run's:
`xnu_live_irq_handler_entered`/`xnu_live_irq_first_iar` for the arrival, the `xnu_live_dec_min` pair for
whether a deadline was near enough to arrive inside the window at all, and the block census for whether
the kernel's clock did anything with it. The minimum-deadline pair is in the log for the reason 482's
masked control is: "no interrupt arrived" has two causes and only one of them is about this step.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

BOOT_DIR = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot")
ENTRY_GIC_H = os.path.join(BOOT_DIR, "entry_gic.h")
ENTRY_IRQ_C = os.path.join(BOOT_DIR, "entry_irq.c")
ENTRY_TIMEBASE_C = os.path.join(BOOT_DIR, "entry_timebase.c")

NM = "arm-none-eabi-nm"


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    """A claim satisfied by a comment is not a claim: 482's own defect 217 was a substring test that
    `fleh_irq`'s prose passed after the call it described had been deleted. Every test below runs on
    comment-stripped source."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def defines(text):
    """`#define NAME value`, with the `[ \\t]+` discipline 482 had to learn: a `\\s+` here can cross a
    newline and read the *next* `#define` as the value (that step's defect 216)."""
    out = {}
    for match in re.finditer(r"^#define[ \t]+([A-Za-z_]\w*)[ \t]+([^\n]+)$", strip_comments(text), re.M):
        value = match.group(2).strip()
        value = re.sub(r"\(.*?\)", "", value.replace("u", "").replace("U", "")).strip()
        try:
            out[match.group(1)] = int(value, 0)
        except ValueError:
            continue
    return out


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("%s failed: %s" % (" ".join(command), result.stderr.strip()))
    return result.stdout


def nm(path):
    symbols = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    return symbols


def function_body(text, name):
    """The braces-balanced body of `name`. Balanced rather than `.*?\\n\\}`, because a one-line body puts
    its closing brace on the same line and an extractor that returned nothing would make every claim
    about it vacuously true (482's defect 217 found that the hard way)."""
    match = re.search(r"^\s*(?:static\s+)?[\w \t*]+?\b%s\s*\([^)]*\)\s*\{" % re.escape(name), text, re.M)
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


def preprocessor_arm(text, macro):
    """What follows `#if <macro>` up to its `#else`/`#endif`, for the one claim that is about a
    conditional."""
    match = re.search(r"^#if\s+%s\s*$(.*?)^#(?:else|endif)" % re.escape(macro), text, re.M | re.S)
    return match.group(1) if match else None


def irq_enable_line(text):
    return defines(text).get("STAGE90_IRQ_ENABLE_LINE")


# ------------------------------------------------------------------------------------------------
# Gather
# ------------------------------------------------------------------------------------------------

def gather(image):
    facts = {
        "header_text": read(ENTRY_GIC_H),
        "irq_text": read(ENTRY_IRQ_C),
        "timebase_text": read(ENTRY_TIMEBASE_C),
        "image": image,
    }
    facts["header"] = defines(facts["header_text"])
    facts["flag"] = irq_enable_line(facts["irq_text"])
    facts["irq"] = strip_comments(facts["irq_text"])
    facts["timebase"] = strip_comments(facts["timebase_text"])
    facts["symbols"] = nm(image) if image else {}
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_install(facts, failures, notes):
    body = function_body(facts["irq"], "entry_irq_arm")
    if body is None:
        failures.append("entry_irq.c no longer defines entry_irq_arm, so the arming sequence this "
                        "check's ordering claims are about does not exist")
        return
    if "ml_install_interrupt_handler(" not in body:
        failures.append("entry_irq.c's entry_irq_arm does not call ml_install_interrupt_handler: the "
                        "handler would have to be stored by this file, and a second writer of the "
                        "five words is a second definition of them")

    # The forbidden form, and it is specific: a *store* through `BootCpuData + STAGE90_CPU_INTERRUPT_*`.
    # The read-back loop names all five and must keep doing so, so the pattern is the assignment that
    # follows the cast rather than the offset itself.
    store = re.search(r"BootCpuData\s*\+\s*STAGE90_CPU_INTERRUPT_\w+\s*\)\s*=", body)
    if store:
        failures.append("entry_irq.c's entry_irq_arm stores a `cpu_data` interrupt word itself, at "
                        "`%s`: whatever it writes there is a second definition of what "
                        "ml_install_interrupt_handler writes, and the read-back below would then be "
                        "measuring this file's own store" % store.group(0).strip())

    # The read-back is the gate, and it is about *both* words that decide where the dispatch goes: the
    # handler in r5 and the target in r0.
    if not re.search(r"after\[4\]\s*!=\s*\(uint32_t\)\(uintptr_t\)&entry_irq_handler", body):
        failures.append("entry_irq.c's entry_irq_arm does not compare the read-back handler against "
                        "`entry_irq_handler`, so nothing stops a build enabling a line whose dispatch "
                        "would call some other address")
    if not re.search(r"after\[0\]\s*!=\s*\(uint32_t\)\(uintptr_t\)&g_irq_target", body):
        failures.append("entry_irq.c's entry_irq_arm does not compare the read-back target against "
                        "`g_irq_target`, so the first argument the dispatcher would pass is uncompared")
    if not re.search(r"IRQ_LIVE\(\"xnu_live_irq_install_ok\",\s*0u\);\s*return\s+0u\s*;", body):
        failures.append("entry_irq.c's read-back branch does not return 0 immediately after recording "
                        "the failed install, so a build whose dispatch would call the wrong address "
                        "carries on and enables the line anyway")
    if not re.search(r"\breturn\s+0u\s*;", body):
        failures.append("entry_irq.c's entry_irq_arm never returns 0, so neither the interrupt-stack "
                        "precondition nor the read-back can leave the machine as it was")

    # The interrupt stack is a precondition, and it is the *gate* that has to come first - not the read
    # of the word, which the report above makes anyway. The distinction is the defect 482's
    # `guard_after_first_probe` mutation was written to catch: a guard that is still in the function but
    # no longer before the thing it guards satisfies a test on the name.
    install = body.find("ml_install_interrupt_handler(")
    gate = body.find("if (g_irq_istack == 0u)")
    if gate < 0:
        failures.append("entry_irq.c's entry_irq_arm never reads CPU_ISTACKPTR as a gate, so whether "
                        "the interrupt stack exists is an assumption rather than a check")
    elif not gate < install:
        failures.append("entry_irq.c's interrupt-stack gate comes *after* installing the handler, so an "
                        "interrupt arriving during the installation finds a possibly-zero stack: the "
                        "gate has to be the last thing before the call, not a later report")
    notes.append("the installation is Apple's own `ml_install_interrupt_handler`, the read-back gates "
                 "both the handler word and the target, and the interrupt stack is checked before the "
                 "call")


def claim_icfgr(facts, failures, notes):
    body = function_body(facts["irq"], "entry_irq_arm")
    if body is None:
        return
    if not re.search(r"STAGE90_GICD_ICFGR0\s*\+\s*\(STAGE90_GIC_TIMER_INTID\s*/\s*16u\)\s*\*\s*4u", body):
        failures.append("entry_irq.c no longer derives the `ICFGR` word from the intid with "
                        "`STAGE90_GICD_ICFGR0 + (STAGE90_GIC_TIMER_INTID / 16u) * 4u`: a literal word "
                        "number is the same derivation written twice, and this is the one offset in "
                        "this header with no second source to compare it against")
    if not re.search(r"\(STAGE90_GIC_TIMER_INTID\s*%\s*16u\)\s*\*\s*2u", body):
        failures.append("entry_irq.c no longer derives the `ICFGR` field shift from the intid with "
                        "`(STAGE90_GIC_TIMER_INTID % 16u) * 2u`")
    if facts["header"].get("STAGE90_GICD_ICFGR0") != 0xC00:
        failures.append("entry_gic.h's STAGE90_GICD_ICFGR0 is 0x%x, not the architectural 0xc00 the "
                        "derivation above starts from"
                        % facts["header"].get("STAGE90_GICD_ICFGR0", -1))
    if not re.search(r"STAGE90_GICD_IPRIORITY0\s*\+\s*\(STAGE90_GIC_TIMER_INTID\s*&\s*~3u\)", body):
        failures.append("entry_irq.c's priority word index is no longer the intid rounded down to a "
                        "multiple of four, which is how a byte-per-intid register is read")
    if not re.search(r"STAGE90_GICD_ITARGETSR0\s*\+\s*\(STAGE90_GIC_TIMER_INTID\s*&\s*~3u\)", body):
        failures.append("entry_irq.c's targets word index is no longer the intid rounded down to a "
                        "multiple of four")
    if not re.search(r"STAGE90_GICD_ICPENDR0,\s*bit\s*\)", body):
        failures.append("entry_irq.c no longer clears this line's pending bit before enabling it, so "
                        "a probe's or the payload's leftover assertion would be delivered as though it "
                        "were this step's timer")
    notes.append("the `ICFGR` word and field, and both byte-per-intid words, are derived from the intid "
                 "in one place")


def claim_armed_line(facts, failures, notes):
    """The one part of the line decision that is not `tools/check_gic_routing.py`'s: that the intid this
    step *arms* is not either of the two the payload's device tree names. 482 measured it to be neither,
    so a build that went back to 18 or 19 would be arming a line the run falsified."""
    header = facts["header"]
    intid = header.get("STAGE90_GIC_TIMER_INTID")
    ppi0 = header.get("STAGE90_GIC_TIMER_PPI0")
    ppi1 = header.get("STAGE90_GIC_TIMER_PPI1")
    if intid is None:
        failures.append("entry_gic.h declares no STAGE90_GIC_TIMER_INTID, so the line the handler arms "
                        "is not stated anywhere")
        return
    if ppi0 is None or ppi1 is None:
        failures.append("entry_gic.h no longer declares both candidate PPIs, so 'the armed line is not "
                        "one of them' cannot be decided")
    elif intid in (ppi0, ppi1):
        failures.append("STAGE90_GIC_TIMER_INTID is %d, which is one of the two lines the payload's "
                        "device tree names (%d, %d) - and 482 measured the virtual timer to be on "
                        "neither of them, so this build is back to a line the run falsified"
                        % (intid, ppi0, ppi1))
    if not 16 <= intid <= 31:
        failures.append("STAGE90_GIC_TIMER_INTID is %d, which is not a PPI (16..31): the xlate this "
                        "tree's timer nodes go through maps PPI n to INTID n + 16, so a number outside "
                        "that range is not the line 482 measured" % intid)
    notes.append("the handler arms INTID %d, which is neither %s - the disagreement is 482's measurement"
                 % (intid, "%d nor %d" % (ppi0, ppi1)
                    if ppi0 is not None and ppi1 is not None else "of the payload's two candidates"))


def claim_handler(facts, failures, notes):
    body = function_body(facts["irq"], "entry_irq_handler")
    if body is None:
        failures.append("entry_irq.c no longer defines entry_irq_handler, so the function the "
                        "dispatcher would `blx` does not exist")
        return

    iar = body.find("gicc_read(STAGE90_GICC_IAR)")
    eoir = body.find("gicc_write(STAGE90_GICC_EOIR")
    timer = body.find("rtclock_intr(0)")
    spurious = body.find("STAGE90_GICC_SPURIOUS_ID")
    stop = body.find("entry_epilogue(")
    line = body.find("STAGE90_GIC_TIMER_INTID")

    for find, name in ((iar, "reads GICC_IAR"), (eoir, "writes GICC_EOIR"),
                       (timer, "calls the kernel's timer service"),
                       (spurious, "handles the spurious read the architecture defines"),
                       (stop, "names and stops on a line it did not expect")):
        if find < 0:
            failures.append("entry_irq.c's entry_irq_handler no longer %s" % name)
    if min(iar, eoir, timer, spurious, stop, line) < 0:
        return

    if not re.search(r"intid\s*=\s*iar\s*&\s*STAGE90_GICC_IAR_INTID_MASK", body):
        failures.append("entry_irq.c's handler no longer masks the acknowledged value with "
                        "`STAGE90_GICC_IAR_INTID_MASK`, so the number it branches on is the raw IAR "
                        "word - which on this GIC also carries the CPU id in its high bits")
    if not re.search(r"intid\s*==\s*STAGE90_GIC_TIMER_INTID", body):
        failures.append("entry_irq.c's handler does not compare the masked intid against "
                        "STAGE90_GIC_TIMER_INTID, so the line it services is not the line it declares")
    if not re.search(r"intid\s*==\s*STAGE90_GICC_SPURIOUS_ID", body):
        failures.append("entry_irq.c's handler does not compare the masked intid against "
                        "STAGE90_GICC_SPURIOUS_ID, so the spurious read is not distinguished from a "
                        "real line")
    if not iar < eoir:
        failures.append("entry_irq.c's handler writes GICC_EOIR before it reads GICC_IAR, so it "
                        "acknowledges a value it has not read")
    if not eoir < timer:
        failures.append("entry_irq.c's handler calls rtclock_intr before the EOIR write, so the kernel "
                        "re-arms the countdown while the CPU interface still believes the old one is "
                        "active")
    if stop < timer:
        failures.append("entry_irq.c's handler's stop path comes before its timer path, so the line "
                        "this step exists for may never be reached")
    if body.count("entry_epilogue(") != 1:
        failures.append("entry_irq.c's handler has %d `entry_epilogue` calls: the unexpected-line path "
                        "is the one that must stop, and a second one is a path this check did not "
                        "reason about" % body.count("entry_epilogue("))
    if body.count("gicc_write(STAGE90_GICC_EOIR") != 2:
        failures.append("entry_irq.c's handler has %d `GICC_EOIR` writes: exactly two are expected - "
                        "the timer's, and the unexpected line's, which is acknowledged so that it "
                        "cannot be asserted straight back. A spurious read has none, because the "
                        "architecture says it acknowledges nothing."
                        % body.count("gicc_write(STAGE90_GICC_EOIR"))
    # And the spurious case must be the one with no EOI, which is the count above's other half.
    tail = body[spurious:]
    if "gicc_write(STAGE90_GICC_EOIR" in tail[:tail.find("return")]:
        failures.append("entry_irq.c's spurious case writes GICC_EOIR with `0x3ff`, which is an "
                        "acknowledgement of an interrupt that does not exist")
    notes.append("the handler reads IAR, EOIRs and services the line 482 measured, and stops on any "
                 "other - the shape that cannot loop")


def claim_switch(facts, failures, notes):
    text = facts["irq"]
    flag = facts["flag"]
    if flag is None:
        failures.append("entry_irq.c no longer defines STAGE90_IRQ_ENABLE_LINE, so what this build does "
                        "with the line is unstated")
        return
    if flag not in (0, 1):
        failures.append("STAGE90_IRQ_ENABLE_LINE is %d: it is a two-state switch, and a third value is "
                        "a build whose behaviour no claim in this file covers" % flag)
    if not re.search(r"^\s*#define\s+STAGE90_IRQ_ENABLE_LINE", strip_comments(facts["irq_text"]), re.M):
        failures.append("STAGE90_IRQ_ENABLE_LINE is defined somewhere other than a `#define` in "
                        "entry_irq.c: tools/check_gic_routing.py reads it from there to decide which "
                        "handler slot 6 must hold, so a value arriving from a command line would let "
                        "the image and the source disagree")
    arm = preprocessor_arm(text, "STAGE90_IRQ_ENABLE_LINE")
    if arm is None:
        failures.append("entry_irq.c's `#if STAGE90_IRQ_ENABLE_LINE` block is gone, so the enable is "
                        "no longer conditional on the switch")
        return
    # The switch decides a *write*, and exactly one. The `#else` branch reads the same register for its
    # report - which is correct and is what makes a build with the switch off distinguishable from one
    # where the read-back never happened - so the test is on the write and not on the name.
    writes = "gicd_write(STAGE90_GICD_ISENABLER0"
    if writes not in arm:
        failures.append("entry_irq.c's `#if STAGE90_IRQ_ENABLE_LINE` block does not write "
                        "STAGE90_GICD_ISENABLER0, so the switch no longer decides whether the line is "
                        "enabled at the distributor")
    body = function_body(text, "entry_irq_arm") or ""
    if body.count(writes) != 1:
        failures.append("entry_irq.c's entry_irq_arm writes STAGE90_GICD_ISENABLER0 %d times: the "
                        "enable must be one write, inside the switch, so that the switch is the only "
                        "thing that can turn the line on" % body.count(writes))
    if "gicd_read(STAGE90_GICD_ISENABLER0)" not in arm:
        failures.append("entry_irq.c's enabled branch does not read STAGE90_GICD_ISENABLER0 back, so "
                        "whether the distributor took the enable is unmeasured")
    notes.append("STAGE90_IRQ_ENABLE_LINE is %d in the source, and the one enable and its read-back are "
                 "inside the `#if` that reads it" % flag)


def claim_order(facts, failures, notes):
    body = function_body(facts["timebase"], "stage90_tbd_set_decrementer")
    if body is None:
        failures.append("entry_timebase.c no longer defines stage90_tbd_set_decrementer, so the order "
                        "between the arming and the unmask cannot be read")
        return

    arm = body.find("g_irq_unmasked = entry_irq_arm()")
    unmask = body.find("stage90_cntv_ctl_write(STAGE90_CNTV_CTL_ENABLE)")
    mask = body.find("stage90_cntv_ctl_write(STAGE90_CNTV_ARM_MASK)")
    tval = body.find("stage90_cntv_tval_write(dec_value)")
    probe = body.find("entry_gic_probe()")

    if arm < 0:
        failures.append("entry_timebase.c never calls entry_irq_arm, so nothing installs the handler "
                        "and any unmask below would be unconditional")
        return
    if unmask < 0:
        failures.append("entry_timebase.c never writes STAGE90_CNTV_CTL_ENABLE without IMASK, so the "
                        "countdown can never become an interrupt and this step's whole object is absent")
    if mask < 0:
        failures.append("entry_timebase.c's first-call sample no longer writes STAGE90_CNTV_ARM_MASK: "
                        "the sample's premise is that nothing can be delivered while it runs")
    if probe < 0:
        failures.append("entry_timebase.c no longer calls entry_gic_probe, so 482's measurement is gone "
                        "from this image and the intid this step arms has no reading behind it")
    if tval < 0:
        failures.append("entry_timebase.c no longer writes the kernel's own deadline to CNTV_TVAL")
    if min(arm, unmask, mask, tval, probe) < 0:
        return

    if not probe < arm:
        failures.append("entry_timebase.c arms the interrupt before 482's probe has run, so the line "
                        "would be enabled while the probe is still driving its own countdowns")
    if not arm < unmask:
        failures.append("entry_timebase.c unmasks the countdown before entry_irq_arm has said the line "
                        "is enabled, so a deadline could become an interrupt with no handler installed")
    if not tval < unmask:
        failures.append("entry_timebase.c unmasks the countdown before the kernel's own deadline is "
                        "programmed, so the window between 'armed' and 'can fire' is the kernel's next "
                        "call instead of two stores")
    if not re.search(r"if\s*\(g_irq_unmasked\s*!=\s*0u\)\s*\{[^}]*STAGE90_CNTV_CTL_ENABLE", body, re.S):
        failures.append("entry_timebase.c's STAGE90_CNTV_CTL_ENABLE write is not conditional on "
                        "`g_irq_unmasked`: the value entry_irq_arm returns is then recorded and not "
                        "used, and a build with the switch off would still unmask")

    # And the arming is at the *end* of the first call, after the write count has advanced - the
    # re-entrancy property. `ml_install_interrupt_handler` ends in `initialize_screen`, which can reach
    # `setPop` and so this function again.
    writes = body.find("g_dec_writes++")
    if writes < 0:
        failures.append("entry_timebase.c no longer increments g_dec_writes, so the first-call branch "
                        "this ordering rests on is gone")
    elif not writes < arm:
        failures.append("entry_timebase.c arms the interrupt before `g_dec_writes++`: the installation "
                        "can reach `setPop` through `initialize_screen`, and re-entering this function "
                        "with `g_dec_writes` still 0 would run the first-call sample a second time")
    notes.append("the order is probe -> arm -> the kernel's deadline -> unmask, with the arming after "
                 "`g_dec_writes++` and the unmask conditional on what it returned")


def claim_image(facts, failures, notes):
    symbols = facts["symbols"]
    if not symbols:
        failures.append("no image was read, so none of the linked-state claims could be made")
        return
    for name, why in (("entry_irq_handler", "the second-level handler the dispatcher would `blx`"),
                      ("entry_irq_arm", "the arming sequence")):
        if name not in symbols:
            failures.append("the image has no `%s`, which is %s" % (name, why))
    if "entry_irq_handler" in symbols and "entry_irq_arm" in symbols:
        notes.append("both of this step's functions are in the image; the five `cpu_data` words the "
                     "dispatcher loads to reach the first of them are "
                     "`tools/check_gic_routing.py`'s claim")


def compare(facts, mutate=None):
    if mutate:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    claim_install(facts, failures, notes)
    claim_icfgr(facts, failures, notes)
    claim_armed_line(facts, failures, notes)
    claim_handler(facts, failures, notes)
    claim_switch(facts, failures, notes)
    claim_order(facts, failures, notes)
    claim_image(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["header"] = dict(facts["header"])
    facts["symbols"] = dict(facts["symbols"])

    def rederive(text):
        facts["irq_text"] = text
        facts["irq"] = strip_comments(text)
        facts["flag"] = irq_enable_line(facts["irq_text"])

    def rederive_tb(text):
        facts["timebase_text"] = text
        facts["timebase"] = strip_comments(text)

    def rederive_header(text):
        facts["header_text"] = text
        facts["header"] = defines(text)

    if mutate == "armed_line_is_a_candidate":
        facts["header"]["STAGE90_GIC_TIMER_INTID"] = facts["header"]["STAGE90_GIC_TIMER_PPI0"]
    elif mutate == "armed_line_is_the_other_candidate":
        facts["header"]["STAGE90_GIC_TIMER_INTID"] = facts["header"]["STAGE90_GIC_TIMER_PPI1"]
    elif mutate == "armed_line_is_an_spi":
        facts["header"]["STAGE90_GIC_TIMER_INTID"] = 34
    elif mutate == "armed_line_undeclared":
        del facts["header"]["STAGE90_GIC_TIMER_INTID"]
    elif mutate == "install_by_hand":
        rederive(_bump(facts["irq_text"], "    (void)ml_install_interrupt_handler(",
                       "    *(volatile uint32_t *)((uintptr_t)BootCpuData + "
                       "STAGE90_CPU_INTERRUPT_HANDLER) = (uint32_t)(uintptr_t)entry_irq_handler;\n"
                       "    (void)ml_install_interrupt_handler("))
    elif mutate == "readback_not_a_gate":
        rederive(_bump(facts["irq_text"],
                       "    if (after[4] != (uint32_t)(uintptr_t)&entry_irq_handler ||\n"
                       "        after[0] != (uint32_t)(uintptr_t)&g_irq_target) {",
                       "    if (0) {"))
    elif mutate == "target_not_a_gate":
        rederive(_bump(facts["irq_text"],
                       "        after[0] != (uint32_t)(uintptr_t)&g_irq_target) {", "        0) {"))
    elif mutate == "no_zero_return":
        rederive(_bump(facts["irq_text"],
                       "        IRQ_LIVE(\"xnu_live_irq_install_ok\", 0u);\n        return 0u;",
                       "        IRQ_LIVE(\"xnu_live_irq_install_ok\", 0u);"))
    elif mutate == "istack_checked_after_install":
        rederive(_bump(facts["irq_text"],
                       "    if (g_irq_istack == 0u)\n        return 0u;\n", ""))
        rederive(_bump(facts["irq_text"], "    IRQ_LIVE(\"xnu_live_irq_armed\", g_irq_armed);",
                       "    IRQ_LIVE(\"xnu_live_irq_armed\", g_irq_armed);\n"
                       "    if (g_irq_istack == 0u)\n        return 0u;"))
    elif mutate == "icfgr_word_hardcoded":
        rederive(_bump(facts["irq_text"],
                       "STAGE90_GICD_ICFGR0 + (STAGE90_GIC_TIMER_INTID / 16u) * 4u", "0xc04u"))
    elif mutate == "icfgr_shift_hardcoded":
        rederive(_bump(facts["irq_text"], "(STAGE90_GIC_TIMER_INTID % 16u) * 2u", "8u"))
    elif mutate == "icfgr_base_moved":
        rederive_header(_bump(facts["header_text"], "#define STAGE90_GICD_ICFGR0     0xc00u",
                              "#define STAGE90_GICD_ICFGR0     0xc04u"))
    elif mutate == "priority_index_truncated":
        rederive(_bump(facts["irq_text"],
                       "STAGE90_GICD_IPRIORITY0 + (STAGE90_GIC_TIMER_INTID & ~3u)",
                       "STAGE90_GICD_IPRIORITY0 + STAGE90_GIC_TIMER_INTID"))
    elif mutate == "targets_index_truncated":
        rederive(_bump(facts["irq_text"],
                       "STAGE90_GICD_ITARGETSR0 + (STAGE90_GIC_TIMER_INTID & ~3u)",
                       "STAGE90_GICD_ITARGETSR0 + STAGE90_GIC_TIMER_INTID"))
    elif mutate == "pending_not_cleared":
        rederive(_bump(facts["irq_text"], "    gicd_write(STAGE90_GICD_ICPENDR0, bit);", "    (void)bit;"))
    elif mutate == "handler_no_iar":
        rederive(_bump(facts["irq_text"], "    iar = gicc_read(STAGE90_GICC_IAR);",
                       "    iar = STAGE90_GIC_TIMER_INTID;"))
    elif mutate == "intid_not_masked":
        rederive(_bump(facts["irq_text"], "iar & STAGE90_GICC_IAR_INTID_MASK", "iar"))
    elif mutate == "timer_case_is_unconditional":
        rederive(_bump(facts["irq_text"], "if (intid == STAGE90_GIC_TIMER_INTID) {", "if (1) {"))
    elif mutate == "handler_eoirs_after_rtclock":
        rederive(_bump(facts["irq_text"],
                       "        gicc_write(STAGE90_GICC_EOIR, iar);\n"
                       "        before = gicd_read(STAGE90_GICD_ISPENDR0);\n"
                       "        g_irq_timer_count++;\n"
                       "        rtclock_intr(0);",
                       "        before = gicd_read(STAGE90_GICD_ISPENDR0);\n"
                       "        g_irq_timer_count++;\n"
                       "        rtclock_intr(0);\n"
                       "        gicc_write(STAGE90_GICC_EOIR, iar);"))
    elif mutate == "handler_no_rtclock":
        rederive(_bump(facts["irq_text"], "        rtclock_intr(0);", "        (void)before;"))
    elif mutate == "handler_loops_on_the_unknown_line":
        rederive(_bump(facts["irq_text"], '    entry_epilogue("exception: irq line");', "    return;"))
    elif mutate == "handler_eoirs_the_spurious":
        rederive(_bump(facts["irq_text"], "        g_irq_spurious_count++;",
                       "        gicc_write(STAGE90_GICC_EOIR, iar);\n        g_irq_spurious_count++;"))
    elif mutate == "handler_drops_the_spurious_case":
        rederive(_bump(facts["irq_text"], "if (intid == STAGE90_GICC_SPURIOUS_ID) {", "if (0) {"))
    elif mutate == "switch_removed":
        # Both of these must be written against the *line* and not against its current value: this step
        # has two states and the selftest has to refuse the same set of mutations in both, or a check
        # that passes in run A would silently stop refusing them in run B.
        rederive(re.sub(r"^#define[ \t]+STAGE90_IRQ_ENABLE_LINE[ \t]+\d+",
                        "/* no switch */", facts["irq_text"], count=1, flags=re.M))
    elif mutate == "switch_is_a_third_value":
        rederive(re.sub(r"^#define[ \t]+STAGE90_IRQ_ENABLE_LINE[ \t]+\d+",
                        "#define STAGE90_IRQ_ENABLE_LINE 2", facts["irq_text"], count=1, flags=re.M))
    elif mutate == "enable_outside_the_switch":
        rederive(_bump(facts["irq_text"], "#if STAGE90_IRQ_ENABLE_LINE", "#if 1"))
    elif mutate == "enable_not_in_the_switch":
        arm = preprocessor_arm(facts["irq"], "STAGE90_IRQ_ENABLE_LINE")
        rederive(_bump(facts["irq_text"], arm,
                       arm.replace("STAGE90_GICD_ISENABLER0", "STAGE90_GICD_CTLR")))
    elif mutate == "no_arm_call":
        rederive_tb(_bump(facts["timebase_text"], "    if (g_irq_unmasked == 0u)\n"
                                                  "        g_irq_unmasked = entry_irq_arm();\n", ""))
    elif mutate == "unmask_before_arm":
        rederive_tb(_bump(facts["timebase_text"], "    if (g_irq_unmasked == 0u)\n"
                                                  "        g_irq_unmasked = entry_irq_arm();\n", ""))
        rederive_tb(_bump(facts["timebase_text"], "    stage90_cntv_tval_write(dec_value);",
                          "    stage90_cntv_tval_write(dec_value);\n"
                          "    stage90_cntv_ctl_write(STAGE90_CNTV_CTL_ENABLE);\n"
                          "    if (g_irq_unmasked == 0u)\n"
                          "        g_irq_unmasked = entry_irq_arm();"))
    elif mutate == "unmask_unconditional":
        rederive_tb(_bump(facts["timebase_text"], "    if (g_irq_unmasked != 0u) {", "    if (1) {"))
    elif mutate == "sample_not_masked":
        rederive_tb(_bump(facts["timebase_text"], "stage90_cntv_ctl_write(STAGE90_CNTV_ARM_MASK);",
                          "stage90_cntv_ctl_write(STAGE90_CNTV_CTL_ENABLE);"))
    elif mutate == "no_probe":
        rederive_tb(_bump(facts["timebase_text"], "        entry_gic_probe();\n", ""))
    elif mutate == "no_tval_write":
        rederive_tb(_bump(facts["timebase_text"], "    stage90_cntv_tval_write(dec_value);",
                          "    (void)dec_value;"))
    elif mutate == "arm_before_the_write_count":
        rederive_tb(_bump(facts["timebase_text"], "    if (g_irq_unmasked == 0u)\n"
                                                  "        g_irq_unmasked = entry_irq_arm();\n", ""))
        rederive_tb(_bump(facts["timebase_text"], "    g_dec_writes++;",
                          "    if (g_irq_unmasked == 0u)\n"
                          "        g_irq_unmasked = entry_irq_arm();\n    g_dec_writes++;"))
    elif mutate == "handler_not_in_the_image":
        facts["symbols"].pop("entry_irq_handler", None)
    elif mutate == "arm_not_in_the_image":
        facts["symbols"].pop("entry_irq_arm", None)
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "armed_line_is_a_candidate", "armed_line_is_the_other_candidate", "armed_line_is_an_spi",
    "armed_line_undeclared", "install_by_hand", "readback_not_a_gate", "target_not_a_gate",
    "no_zero_return", "istack_checked_after_install", "icfgr_word_hardcoded", "icfgr_shift_hardcoded",
    "icfgr_base_moved", "priority_index_truncated", "targets_index_truncated", "pending_not_cleared",
    "handler_no_iar", "intid_not_masked", "timer_case_is_unconditional", "handler_eoirs_after_rtclock",
    "handler_no_rtclock", "handler_loops_on_the_unknown_line", "handler_eoirs_the_spurious",
    "handler_drops_the_spurious_case", "switch_removed",
    "switch_is_a_third_value", "enable_outside_the_switch", "enable_not_in_the_switch", "no_arm_call",
    "unmask_before_arm", "unmask_unconditional", "sample_not_masked", "no_probe", "no_tval_write",
    "arm_before_the_write_count", "handler_not_in_the_image", "arm_not_in_the_image",
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
        print("FAIL: --image is required: the symbols this step adds are a claim about the linked "
              "image, and there is no default that can stand in for it", file=sys.stderr)
        return 1
    facts = gather(args.image)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the interrupt this step installs is not the interrupt this image has:",
              file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    header = facts["header"]
    say("  xnu_entry_483: the handler is installed through Apple's own "
        "`ml_install_interrupt_handler` and read back before anything is enabled, the line it arms is "
        "INTID %d - 482's measurement, and neither of the payload's two - the `ICFGR` word and field "
        "are derived from it, and the countdown is unmasked only after the arming said the line is "
        "enabled" % header["STAGE90_GIC_TIMER_INTID"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
