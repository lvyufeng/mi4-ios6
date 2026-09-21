#!/usr/bin/env python3
"""
Check the eight things 483's interrupt depends on, before any of them can deliver anything.

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
     **496 made it four, and the fourth is a line a *driver* owns**: between the timer's case and the
     spurious one, the dispatcher reads a registration table and calls the client filed for that intid
     after writing its `EOIR`. The shape that makes that safe rather than a second dispatcher is the
     order of two statements - the scan `continue`s on an intid it does not hold, and the client path
     `return`s - so every intid this image did not hand out still reaches the stop below, and a driver's
     mistake is still a stop rather than a storm. That is why the stop count is checked as a *count*
     (exactly two) and the scan's position as a *position* (after the timer, before the spurious case).

  7. **the registration table's writer and its reader agree on what a slot is.** `entry_irq.c` gains a
     registry in 496, and the one thing about it a source can hold is the *order* the three fields of a
     slot are written in: the intid and the `refCon` first and the handler **last**, so a scan racing a
     registration either does not match the slot's intid yet or finds a zero handler and treats the line
     as unregistered. The reverse order publishes a handler for an intid the scan has not been told
     about. The unregister's order is the mirror of it for the same reason, and both are checked here
     because both are invisible in a run that never loses the race.

  5. **the enable is a switch in the source and not a flag on a command line.** `STAGE90_IRQ_ENABLE_LINE`
     is what decides whether a countdown may become an interrupt (this file) and which handler slot 6 must
     hold (`tools/check_gic_routing.py`). One value, read from two places, defined in one - and defined in
     the source because an image and the source that describes it must not be able to disagree.

  8. **and every file-scope record these two files declare is a record the image has.** 497's claim, and
     it is the only one here that needs the linked image rather than the source: seven names 496 declared
     were absent from the artifact entirely, because each was written and never read (or read only where
     the value was already known) and the optimizer removed it. The keys were still right, so no run could
     show it. The whole argument is in the claim's own docstring.

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
DRIVER_CPP = os.path.join(REPO_ROOT, "stages/stage90/xnu_platform/MSM8974GIC.cpp")

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
# File-scope storage: the records a file declares, and the names the linker gives them
# ------------------------------------------------------------------------------------------------

# A one-line declarator whose name is followed by `;`, `[` or `=`. **Not `\s*` and not a pattern that
# can cross a newline**, and both restrictions are load-bearing: `static void` on a line of its own
# (this tree's K&R style) must not be read as a declaration of `void`, and a pattern allowed to look
# past the end of the line would find the *next* `;` in the file and report some function's local as a
# file-scope record - 482's defect 216, a `\s+` that read the next line, in the other direction.
STATIC_DECL = re.compile(r"^static[ \t]+([^\n;=]*?)\b(\w+)[ \t]*(?=[;\[=])", re.M)

# `#if` expressions in these two files are macro names, integer literals and the usual operators.
# Anything outside this set is refused rather than guessed at.
COND_OK = re.compile(r"^[0-9A-Za-z_() \t<>=!&|+\-*/uU]*$")


def cond_true(expression, macros):
    """Evaluate a `#if` expression over a file's own `#define`s.

    Needed for one reason: a file-scope record may be declared inside a conditional block, and 496's
    driver declares eight of them inside `#if MSM8974_GIC_DRIVE_SGI`. A check that ignored the
    conditional would demand storage for variables that the switch, at 0, keeps out of the image
    entirely - so it would be wrong in exactly the state the switch exists to create.
    """
    if not COND_OK.match(expression):
        raise SystemExit("unparsable #if expression: %r" % expression)
    substituted = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: str(macros.get(m.group(0), 0)), expression)
    substituted = substituted.replace("u", "").replace("U", "")
    try:
        return bool(eval(substituted, {"__builtins__": {}}, {}))  # noqa: S307 - digits and operators only
    except Exception as error:                                    # noqa: BLE001 - reported, not raised
        raise SystemExit("could not evaluate #if %r (as %r): %s" % (expression, substituted, error))


def compiled_text(text, macros):
    """`text` with every conditional branch that is not taken removed."""
    kept, stack, live = [], [], True
    for line in text.split("\n"):
        stripped = line.strip()
        if stripped.startswith("#if"):
            enclosing = live
            if stripped.startswith("#ifdef"):
                condition = stripped[6:].strip() in macros
            elif stripped.startswith("#ifndef"):
                condition = stripped[7:].strip() not in macros
            elif stripped.startswith("#if "):
                condition = cond_true(stripped[4:].strip(), macros)
            else:
                raise SystemExit("unparsable directive: %r" % stripped)
            stack.append((enclosing, condition))
            live = enclosing and condition
            continue
        if stripped.startswith("#el"):
            if stripped.startswith("#elif") or not stack:
                raise SystemExit("unsupported conditional directive: %r" % stripped)
            enclosing, condition = stack[-1]
            stack[-1] = (enclosing, not condition)
            live = enclosing and not condition
            continue
        if stripped.startswith("#endif"):
            if not stack:
                raise SystemExit("#endif without #if")
            live = stack.pop()[0]
            continue
        if live:
            kept.append(line)
    if stack:
        raise SystemExit("unterminated #if")
    return "\n".join(kept)


def file_scope_statics(text):
    """Every `static` object declared at file scope, as `(lineno, name)`.

    `const` ones are excluded: a `static const` table is `__TEXT,__const` and may legitimately be
    folded, inlined or localized, so requiring it in the symbol table would be a claim about the
    compiler's choices rather than about the source. A named `static` *function* is excluded by the
    declarator rule above - this claim is about storage.
    """
    out = []
    for match in STATIC_DECL.finditer(text):
        if re.search(r"\bconst\b", match.group(1)):
            continue
        out.append((text.count("\n", 0, match.start()) + 1, match.group(2)))
    return out


def mangled(name, cxx):
    """The name the linker gives one of these records. A file-scope `static` at namespace scope in
    C++ is internal linkage, which `nm` prints as `_ZL<len><name>` - the length is what makes the
    lookup exact rather than a prefix match on a name that may be a prefix of another."""
    return "_ZL%d%s" % (len(name), name) if cxx else name


def static_function_body(text, name):
    """`function_body`, for a declarator whose return type is on its own line.

    `function_body`'s single-line `[\\w \\t*]+?` cannot cross the newline between `static void` and
    the name, which is this tree's K&R style. 496 collapsed two definitions in `entry_irq.c` to the
    one-line form so that matcher could see them; that was a change to a file for the benefit of a
    checker, and this is the other, better answer - the second style gets a matcher of its own.
    Widening `function_body` itself was rejected: allowing `\\n` in the declarator lets the match
    start a function earlier, and the `{` it then finds belongs to a *different* body.
    """
    match = re.search(r"^\s*(?:static\s+)?[\w \t*\n]+?\b%s\s*\(" % re.escape(name), text, re.M)
    if not match:
        return None
    open_brace = text.find("{", match.end())
    semicolon = text.find(";", match.end())
    if open_brace < 0 or (0 <= semicolon < open_brace):
        return None                     # a declaration, not a definition
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:index + 1]
    return None


# ------------------------------------------------------------------------------------------------
# Gather
# ------------------------------------------------------------------------------------------------

def gather(image):
    facts = {
        "header_text": read(ENTRY_GIC_H),
        "irq_text": read(ENTRY_IRQ_C),
        "timebase_text": read(ENTRY_TIMEBASE_C),
        "driver_text": read(DRIVER_CPP),
        "image": image,
    }
    facts["header"] = defines(facts["header_text"])
    facts["flag"] = irq_enable_line(facts["irq_text"])
    facts["irq"] = strip_comments(facts["irq_text"])
    facts["timebase"] = strip_comments(facts["timebase_text"])
    facts["driver"] = strip_comments(facts["driver_text"])
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
    scan = body.find("for (i = 0u; i < STAGE90_IRQ_CLIENTS; i++)")
    cli_eoir = body.find("gicc_write(STAGE90_GICC_EOIR", scan) if scan >= 0 else -1
    cli_cap = body.find("STAGE90_IRQ_CLIENT_CAP", scan) if scan >= 0 else -1
    cli_stop = body.find("entry_epilogue(", scan) if scan >= 0 else -1
    cli_call = body.find("client(", scan) if scan >= 0 else -1

    for find, name in ((iar, "reads GICC_IAR"), (eoir, "writes GICC_EOIR"),
                       (timer, "calls the kernel's timer service"),
                       (spurious, "handles the spurious read the architecture defines"),
                       (stop, "names and stops on a line it did not expect"),
                       (scan, "scans the client registration table"),
                       (cli_call, "calls the client registered for a line")):
        if find < 0:
            failures.append("entry_irq.c's entry_irq_handler no longer %s" % name)
    if min(iar, eoir, timer, spurious, stop, line, scan, cli_eoir, cli_cap, cli_stop, cli_call) < 0:
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
    # **496: which stops there are, and where the client's path sits between them.** The count is two
    # and the order is the claim: `scan` is found where no `SCAN` text exists in a build that predates
    # 496, so a file that lost the registry fails above at the presence test rather than here.
    if body.count("entry_epilogue(") != 2:
        failures.append("entry_irq.c's handler has %d `entry_epilogue` calls: two are expected - the "
                        "line the driver owns being asked for more times than this image will answer "
                        "for, and the unexpected-line path - and a third is a path this check did not "
                        "reason about" % body.count("entry_epilogue("))
    if not (timer < scan < spurious):
        failures.append("entry_irq.c's handler scans the client table at offset %d, the timer's case "
                        "is at %d and the spurious case at %d: the scan must sit between them, or it "
                        "either shadows a line this image services itself (the timer's) or makes the "
                        "spurious read reachable by a registration" % (scan, timer, spurious))
    if cli_cap > cli_call:
        failures.append("entry_irq.c's handler applies `STAGE90_IRQ_CLIENT_CAP` after it calls the "
                        "client: the bound would then be on the requests already answered rather than "
                        "on the ones this image is willing to answer")
    if not (cli_eoir > scan and cli_eoir < cli_call):
        failures.append("entry_irq.c's handler calls the client at offset %d and writes `EOIR` at "
                        "%d: the acknowledgement must come first, or a line asserted during the "
                        "client's own work is still active at the CPU interface" % (cli_call, cli_eoir))
    if not (cli_cap < cli_stop < cli_call):
        failures.append("entry_irq.c's handler's client-path stop is at offset %d, the cap test at %d "
                        "and the call at %d: the stop has to be the branch taken when the cap is "
                        "exceeded, so a line that keeps re-asserting itself ends the run with the "
                        "intid recorded instead of being answered until the watchdog" % (cli_stop, cli_cap, cli_call))
    if not re.search(r"if\s*\(\s*client\s*==\s*0\s*\|\|\s*g_irq_cli_intid\[i\]\s*!=\s*intid\s*\)\s*\n"
                     r"\s*continue\s*;", body):
        failures.append("entry_irq.c's handler does not skip a slot whose handler is zero or whose "
                        "intid is another line: the scan would either call the wrong client or fall "
                        "into the stop below on the first free slot")
    if not re.search(r"client\([^;]*\)\s*;\s*\n\s*return\s*;", body):
        failures.append("entry_irq.c's handler does not `return` after calling the client, so the "
                        "path continues into the unexpected-line stop - the client's line would be "
                        "serviced and then end the run")
    if not (cli_call < spurious):
        failures.append("entry_irq.c's handler calls the client before its spurious case, so a "
                        "registration can be reached by the `0x3ff` a CPU interface returns when "
                        "there is nothing to acknowledge")
    if body.count("gicc_write(STAGE90_GICC_EOIR") != 3:
        failures.append("entry_irq.c's handler has %d `GICC_EOIR` writes: exactly three are expected - "
                        "the timer's, the client's line, and the unexpected line's, which is "
                        "acknowledged so that it cannot be asserted straight back. A spurious read has "
                        "none, because the architecture says it acknowledges nothing."
                        % body.count("gicc_write(STAGE90_GICC_EOIR"))
    # And the spurious case must be the one with no EOI, which is the count above's other half.
    tail = body[spurious:]
    if "gicc_write(STAGE90_GICC_EOIR" in tail[:tail.find("return")]:
        failures.append("entry_irq.c's spurious case writes GICC_EOIR with `0x3ff`, which is an "
                        "acknowledgement of an interrupt that does not exist")
    notes.append("the handler reads IAR, EOIRs and services the line 482 measured, calls the client a "
                 "registration filed for any other line, and stops on the two cases that must stop - "
                 "the shape that cannot loop")


def claim_client_registry(facts, failures, notes):
    """7. A line can be handed to a driver, and a slot's three fields are written in one order.

    The registry is `entry_irq.c`'s in 496, and there are two properties of it that no run can show:
    the *order* a slot is filled in, which is the whole of its concurrency contract against the
    dispatcher that reads it, and the *set* of intids it refuses. Both are source-order claims about
    `entry_irq_register_client`, so both are read out of its body.

      * **the handler is stored last.** `g_irq_cli_intid[slot]` and `g_irq_cli_refcon[slot]` must
        precede `g_irq_cli_handler[slot]`. The registration runs in process context with interrupts
        open, so a delivery can be taken between any two of the three stores; the order is what makes
        the intermediate states describable - a slot is either not yet this line's, or not yet live.
        The unregister is the mirror of it for the same reason and is checked as such.
      * **two lines are refused by name.** `STAGE90_GIC_TIMER_INTID` is serviced by the dispatcher
        before the table is read, so registering it would file a handler that is never called; and
        `STAGE90_GICC_SPURIOUS_ID` is not a line at all. A refusal is a *value* (`_cli_refused` plus
        the intid), because a call that quietly did nothing is a driver that believes it owns a line
        it does not.
      * **a full table and a duplicate are refusals too, and not overwrites.** A second registration
        for a line that already has a client would either shadow the first handler or make the
        dispatcher's choice depend on the order two drivers started in.
    """
    body = function_body(facts["irq"], "entry_irq_register_client")
    if body is None:
        failures.append("entry_irq.c no longer defines entry_irq_register_client, so the table the "
                        "dispatcher scans has no writer")
        return
    into = body.find("g_irq_cli_intid[slot] =")
    refcon = body.find("g_irq_cli_refcon[slot] =")
    handler = body.find("g_irq_cli_handler[slot] =")
    for at, name in ((into, "g_irq_cli_intid[slot] ="), (refcon, "g_irq_cli_refcon[slot] ="),
                     (handler, "g_irq_cli_handler[slot] =")):
        if at < 0:
            failures.append("entry_irq.c's registration never writes `%s`, so the slot the "
                            "dispatcher scans is filled in only in part" % name)
    if min(into, refcon, handler) >= 0:
        if not (into < handler and refcon < handler):
            failures.append("entry_irq.c's registration stores the handler at offset %d and the intid "
                            "at %d / the refCon at %d: the handler has to be the last of the three, "
                            "because it is what makes the slot live - the other order publishes a "
                            "handler for an intid the dispatcher has not been told about"
                            % (handler, into, refcon))
        else:
            notes.append("a slot's intid and refCon are written before its handler, so a scan racing "
                         "a registration sees a slot that is not yet live rather than a live slot with "
                         "the wrong fields")
    if body.count("entry_live_write") + body.count("IRQ_LIVE") < 6:
        failures.append("entry_irq.c's registration publishes %d keys: the accepted registration has "
                        "to be visible as a sequence (its count, the slot, the intid, the handler "
                        "address, the refCon) and a refusal as a value - a registration whose outcome "
                        "is not in the log is a line whose ownership cannot be read out of a run"
                        % (body.count("entry_live_write") + body.count("IRQ_LIVE")))
    # The keys, by name and not by the variables behind them: `IRQ_LIVE("xnu_live_irq_cli_intid",
    # intid)` publishes the *parameter*, and a check that looked for `g_irq_cli_intid` in the call
    # would refuse a correct file - one value reachable by two spellings is this project's oldest
    # defect, and a claim is not the place to add a third.
    for key in ("xnu_live_irq_cli_seq", "xnu_live_irq_cli_slot", "xnu_live_irq_cli_intid",
                "xnu_live_irq_cli_handler", "xnu_live_irq_cli_refcon", "xnu_live_irq_cli_refused"):
        if '"%s"' % key not in body:
            failures.append("entry_irq.c's registration never publishes `%s`: the key that makes the "
                            "claim this file makes about the table readable is missing" % key)
    # The refusals, by name, as a *condition* rather than as prose.
    for macro in ("STAGE90_GIC_TIMER_INTID", "STAGE90_GICC_SPURIOUS_ID"):
        guard = body[:into] if into >= 0 else body
        if macro not in guard:
            failures.append("entry_irq.c's registration does not refuse `%s`: the dispatcher services "
                            "that case before it reads the table, so a client filed against it would "
                            "never be called while the driver believed it owned the line" % macro)
    if "handler == 0u" not in body:
        failures.append("entry_irq.c's registration accepts a zero handler, so a slot can be filed "
                        "with an address the dispatcher would take for a free one")

    unreg = function_body(facts["irq"], "entry_irq_unregister_client")
    if unreg is None:
        failures.append("entry_irq.c defines no way to withdraw a registration: the line a driver "
                        "owns would be one it can never stop owning, and the stop the dispatcher takes "
                        "for an unregistered line would be unreachable from the driver's side")
        return
    clear = unreg.find("g_irq_cli_handler[i] = 0")
    scoured = unreg.find("g_irq_cli_intid[i] = 0u")
    if clear < 0:
        failures.append("entry_irq.c's unregister never clears the handler, so the dispatcher keeps "
                        "calling a client that gave the line back")
    elif scoured >= 0 and not clear < scoured:
        failures.append("entry_irq.c's unregister clears the intid before the handler: the slot is "
                        "still live to the dispatcher while the intid it matches has been zeroed, "
                        "which is the one state the registration order exists to avoid")
    else:
        notes.append("the unregister clears the handler first, so a delivery racing it finds a line "
                     "with no client - the documented stop - and not a half-cleared slot")
    if "xnu_live_irq_cli_unreg_gone" not in unreg:
        failures.append("entry_irq.c's unregister answers nothing for a line it does not hold: a "
                        "driver that withdraws twice, or after a refused registration, cannot tell "
                        "those two cases apart in its own record")

    header = facts["header"]
    for name in ("STAGE90_IRQ_CLIENTS", "STAGE90_IRQ_CLIENT_CAP"):
        if header.get(name) is None:
            failures.append("entry_gic.h does not define `%s` as an integer: the table's size and the "
                            "bound on the calls one slot may take are what the dispatcher's scan and "
                            "its stop are arithmetic over, and neither can be inferred" % name)
    if header.get("STAGE90_IRQ_CLIENT_CAP") is not None and header["STAGE90_IRQ_CLIENT_CAP"] < 1:
        failures.append("entry_gic.h's `STAGE90_IRQ_CLIENT_CAP` is %d: a cap below one makes the "
                        "first delivery of any registered line a stop"
                        % header["STAGE90_IRQ_CLIENT_CAP"])


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


def claim_static_storage(facts, failures, notes):
    """8. Every file-scope record these two files declare is in the image, and the key that names one
    reads it.

    496 found this by accident while accounting for `.bss`: `nm` was asked for the new statics and
    seven names the source declares were not there at all - `g_irq_first_iar`, `g_irq_last_iar`,
    `g_irq_icfgr_word`, `g_irq_icfgr_shift`, `g_irq_cli_last` in `entry_irq.c`, and `g_gic_isr_last`
    and `g_gic_isr_done` in the driver. Six were written and never read - the key beside each store
    published the local or the parameter the record had just been copied from - so the store was dead,
    the variable was removed, and every value in the log was still right. The seventh was read but
    provably constant, so only the storage went.

    497's answer is that those seven were never records, and the measurement that settled it is the
    one a first plan would not have made: three of the six were given a far-away reader - the fix the
    report's own "owed" list asked for - and **two of the three were removed by the compiler anyway**,
    because their only reader is the key written beside the store and a store-to-load forward across
    one line is free. So the rule is not "a key must read the record"; it is **a record earns its
    storage by being read where its value is not already known**, and a source that declares one
    without that is describing a machine that does not exist. All seven are gone; the marker became a
    literal, whose position is checked below.

    One clause of this claim has no mutation that can test it, and it is worth naming rather than
    papering over: the conditional filtering (`compiled_text`) means the driver's nine records inside
    `#if MSM8974_GIC_DRIVE_SGI` are demanded only because the switch is 1 here. There is no source
    mutation that can show the filter working, because every mutation this selftest can make changes
    the *source* while the symbols come from one real build - and taking a record out of the compiled
    set leaves it present in that image, so the claim would pass either way. What was checked instead
    is the filter itself, by reading `file_scope_statics(compiled_text(driver, switch = 0))` and finding
    `g_gic_starts` alone; a mutation that merely *added* a name would test nothing about it.

    **The check that finds it is not a reading of the source, and cannot be.** A source-only liveness
    analysis would have to reproduce the optimizer on a file the check does not compile; one name
    looked up in the linked image is one instruction and no model. It is also the only form that can
    say *which* name is wrong: a hand-written note that the store was dead would have been 496's seven
    names, and 497's whole point is that the eighth edit anyone makes may be different.

    **And the reason it belongs to a checker rather than to `nm`**: the failure is invisible in every
    run. The keys hold the right values because a copy of the right value was published instead of the
    record - which is why this defect class ("one value, two definitions") is the oldest one here.
    """
    symbols = facts["symbols"]
    if not symbols:
        failures.append("no image was read, so the claim that every file-scope record this step "
                        "declares has storage could not be made")
        return

    checked = 0
    for path, text, cxx in ((ENTRY_IRQ_C, facts["irq"], False),
                            (DRIVER_CPP, facts["driver"], True)):
        source = compiled_text(text, defines(text))
        names = file_scope_statics(source)
        if not names:
            failures.append("%s declares no file-scope record at all, so this claim is vacuous about "
                            "it - which would mean the file had been emptied rather than fixed"
                            % os.path.basename(path))
            continue
        lines = source.split("\n")
        for lineno, name in names:
            checked += 1
            symbol = mangled(name, cxx)
            if symbol not in symbols:
                failures.append(
                    "%s:%d declares the file-scope record `%s` and the image has no `%s`: the record "
                    "is written and never read, or read only where its value is already known, so the "
                    "optimizer removed it - the source describes storage the machine does not have, "
                    "every key that names it publishes something else, and the run still reads "
                    "correctly. Either the record is a record (read it where its value is not already "
                    "known) or it should not be declared"
                    % (os.path.relpath(path, REPO_ROOT), lineno, name, symbol))
        notes.append("%d file-scope record(s) in %s are in the image"
                     % (len(names), os.path.basename(path)))

    # The marker: 497 made one of the seven a literal, because `g_gic_isr_done = 1u` followed by the
    # key that published it is a value the compiler folds - and a value defined by the *program point*
    # has no business in `.bss`. What keeps a literal honest is where it is written, so that is what
    # is checked: the marker must be published inside the handler and after the withdraw block, or it
    # would be reachable by a path that returned early or gave the line back.
    body = static_function_body(facts["driver"], "msm8974_gic_isr")
    if body is None:
        failures.append("MSM8974GIC.cpp no longer defines `msm8974_gic_isr`, so the tail the marker "
                        "key is about does not exist")
        return
    marker = body.find('"xnu_live_gicdrv_isr_done"')
    withdraw = body.find("entry_irq_unregister_client(")
    if marker < 0:
        failures.append("`msm8974_gic_isr` never publishes `xnu_live_gicdrv_isr_done`: the run would "
                        "then have no record that the handler reached its last statement, and "
                        "`_isr_calls = 2` would be indistinguishable from a handler that stopped "
                        "halfway")
    elif withdraw < 0:
        failures.append("`msm8974_gic_isr` has no withdraw block, so the marker's position is about "
                        "nothing")
    elif marker < withdraw:
        failures.append("`msm8974_gic_isr` publishes `xnu_live_gicdrv_isr_done` **before** its "
                        "withdraw block: the marker would then be written on a path that has not yet "
                        "decided whether the line is quiet, which is the opposite of what a marker "
                        "for 'reached the last statement' means")
    else:
        notes.append("`_isr_done` is a literal published in the handler's tail, after the withdraw, "
                     "and it is a marker rather than a record - its position is the claim")

    if not failures:
        notes.append("checked %d file-scope record(s) across both files" % checked)


def compare(facts, mutate=None):
    if mutate:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    claim_install(facts, failures, notes)
    claim_icfgr(facts, failures, notes)
    claim_armed_line(facts, failures, notes)
    claim_handler(facts, failures, notes)
    claim_client_registry(facts, failures, notes)
    claim_switch(facts, failures, notes)
    claim_order(facts, failures, notes)
    claim_image(facts, failures, notes)
    claim_static_storage(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def _relocate(text, block, anchor):
    """Move the single region matching `block` to just after the single line `anchor`.

    A mutation that has to be an *order* rather than a substitution - "the new case was appended at the
    end of the handler", "the acknowledgement was written after the call" - and it is a relocation
    rather than a rewrite so that the block's own text is moved verbatim: a mutation that retyped the
    block would be testing a second copy of it. Both counts are asserted, because a regex that matched
    twice would move a different region than the one the mutation names and the selftest would then be
    refusing something other than what it says.
    """
    found = re.findall(block, text, re.S)
    assert len(found) == 1, (block, len(found))
    assert text.count(anchor) == 1, (anchor, text.count(anchor))
    moved = found[0]
    rest = text.replace(moved, "", 1)
    return rest.replace(anchor, anchor + moved, 1)


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

    def rederive_driver(text):
        facts["driver_text"] = text
        facts["driver"] = strip_comments(text)

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
    elif mutate == "client_scan_after_the_spurious_case":
        # Appending the new case at the end of the handler is the edit this mutation is: it reads like
        # the natural place for it and it puts the scan *after* the spurious case, where the `0x3ff` a
        # CPU interface returns when there is nothing to acknowledge can reach a registration.
        rederive(_relocate(facts["irq_text"],
                           r"\n    \{\n        uint32_t i;\n\n"
                           r"        for \(i = 0u; i < STAGE90_IRQ_CLIENTS; i\+\+\) \{.*?\n    \}\n",
                           '    entry_epilogue("exception: irq line");\n'))
    elif mutate == "client_eoir_after_the_call":
        rederive(_relocate(facts["irq_text"],
                           r"            gicc_write\(STAGE90_GICC_EOIR, iar\);\n",
                           "            client((void *)(uintptr_t) g_irq_cli_refcon[i], intid);\n"))
    elif mutate == "client_cap_after_the_call":
        rederive(_relocate(facts["irq_text"],
                           r"            if \(g_irq_cli_calls\[i\] > STAGE90_IRQ_CLIENT_CAP\) \{.*?\n"
                           r"            \}\n",
                           "            client((void *)(uintptr_t) g_irq_cli_refcon[i], intid);\n"))
    elif mutate == "client_guard_removed":
        rederive(_bump(facts["irq_text"],
                       "            if (client == 0 || g_irq_cli_intid[i] != intid)\n"
                       "                continue;",
                       "            if (0)\n                continue;"))
    elif mutate == "client_falls_into_the_stop":
        rederive(_bump(facts["irq_text"],
                       "            client((void *)(uintptr_t) g_irq_cli_refcon[i], intid);\n"
                       "            return;",
                       "            client((void *)(uintptr_t) g_irq_cli_refcon[i], intid);"))
    elif mutate == "client_handler_stored_first":
        rederive(_bump(facts["irq_text"],
                       "    g_irq_cli_intid[slot] = intid;\n"
                       "    g_irq_cli_refcon[slot] = refCon;\n"
                       "    g_irq_cli_handler[slot] = (entry_irq_client_t)(uintptr_t) handler;",
                       "    g_irq_cli_handler[slot] = (entry_irq_client_t)(uintptr_t) handler;\n"
                       "    g_irq_cli_intid[slot] = intid;\n"
                       "    g_irq_cli_refcon[slot] = refCon;"))
    elif mutate == "client_records_no_slot":
        rederive(_bump(facts["irq_text"], '    IRQ_LIVE("xnu_live_irq_cli_slot", slot);\n', ""))
    elif mutate == "client_refuses_nothing":
        rederive(_bump(facts["irq_text"],
                       "        intid == STAGE90_GIC_TIMER_INTID ||\n"
                       "        intid == STAGE90_GICC_SPURIOUS_ID) {",
                       "        0) {"))
    elif mutate == "client_accepts_a_zero_handler":
        rederive(_bump(facts["irq_text"], "    if (handler == 0u ||", "    if (0 ||"))
    elif mutate == "unregister_keeps_the_handler":
        rederive(_bump(facts["irq_text"], "            g_irq_cli_handler[i] = 0;",
                       "            g_irq_cli_handler[i] = g_irq_cli_handler[i];"))
    elif mutate == "unregister_clears_the_intid_first":
        rederive(_bump(facts["irq_text"],
                       "            g_irq_cli_handler[i] = 0;\n"
                       "            g_irq_cli_intid[i] = 0u;",
                       "            g_irq_cli_intid[i] = 0u;\n"
                       "            g_irq_cli_handler[i] = 0;"))
    elif mutate == "unregister_is_silent_about_a_missing_line":
        rederive(_bump(facts["irq_text"],
                       '    IRQ_LIVE("xnu_live_irq_cli_unreg_gone", intid);\n', ""))
    elif mutate == "client_cap_removed_from_the_header":
        rederive_header(re.sub(r"^#define[ \t]+STAGE90_IRQ_CLIENT_CAP[ \t]+\d+[uU]?", "/* gone */",
                               facts["header_text"], count=1, flags=re.M))
    elif mutate == "client_cap_is_zero":
        rederive_header(_bump(facts["header_text"], "#define STAGE90_IRQ_CLIENT_CAP  64u",
                              "#define STAGE90_IRQ_CLIENT_CAP  0u"))
    # -- 497: the seven records ----------------------------------------------------------------
    elif mutate == "a_record_the_image_does_not_have":
        # The *shape* of all seven of 496's: a file-scope record that is written and never read. The
        # mutation introduces that shape rather than reverting one of the seven, because a mutation
        # written against the names a check was built for tests the names and not the rule.
        rederive(_bump(facts["irq_text"], "static uint32_t g_irq_late_count;",
                       "static uint32_t g_irq_late_count;\nstatic uint32_t g_irq_no_storage;"))
        rederive(_bump(facts["irq_text"], "    ++g_irq_seq;",
                       "    ++g_irq_seq;\n    g_irq_no_storage = 1u;"))
    elif mutate == "a_record_that_is_read_but_never_written":
        # The other shape, which the same rule has to refuse and which looks like a *use* in the
        # source: a record nothing ever writes is a compile-time constant, so the read folds and the
        # storage goes - and a key that names it publishes a zero that is about no machine.
        rederive(_bump(facts["irq_text"], "static uint32_t g_irq_late_count;",
                       "static uint32_t g_irq_late_count;\nstatic uint32_t g_irq_never_written;"))
        rederive(_bump(facts["irq_text"], "    ++g_irq_seq;",
                       "    g_irq_seq += g_irq_never_written + 1u;"))
    elif mutate == "a_driver_record_the_image_does_not_have":
        rederive_driver(_bump(facts["driver_text"], "static uint32_t g_gic_pends;",
                              "static uint32_t g_gic_pends;\nstatic uint32_t g_gic_no_storage;"))
        rederive_driver(_bump(facts["driver_text"], "    uint32_t pend = 0u;",
                              "    uint32_t pend = 0u;\n\n    g_gic_no_storage = 1u;"))
    elif mutate == "the_driver_marker_becomes_a_variable_again":
        # 496's shape for the seventh record, restored: a variable written `1u` and read on the next
        # line. The compiler folds the load into an immediate and the storage goes, so the key is a
        # literal either way - which is exactly why the fix is to say so in the source.
        rederive_driver(_bump(facts["driver_text"], "static uint32_t g_gic_isr_unreg_rc;",
                              "static uint32_t g_gic_isr_unreg_rc;\nstatic uint32_t g_gic_isr_done;"))
        rederive_driver(_bump(facts["driver_text"],
                              '    entry_live_write( "xnu_live_gicdrv_isr_done", 1u );',
                              "    g_gic_isr_done = 1u;\n"
                              '    entry_live_write( "xnu_live_gicdrv_isr_done", g_gic_isr_done );'))
    elif mutate == "the_marker_is_published_before_the_withdraw":
        rederive_driver(_relocate(facts["driver_text"],
                                  r"    if\( g_gic_isr_guard == 1u\) \{.*?\n    \}\n",
                                  '    entry_live_write( "xnu_live_gicdrv_isr_done", 1u );\n'))
    elif mutate == "the_marker_is_not_published":
        rederive_driver(_bump(facts["driver_text"],
                              '    entry_live_write( "xnu_live_gicdrv_isr_done", 1u );\n', ""))
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
    "client_scan_after_the_spurious_case", "client_eoir_after_the_call", "client_cap_after_the_call",
    "client_guard_removed", "client_falls_into_the_stop", "client_handler_stored_first",
    "client_records_no_slot", "client_refuses_nothing", "client_accepts_a_zero_handler",
    "unregister_keeps_the_handler", "unregister_clears_the_intid_first",
    "unregister_is_silent_about_a_missing_line", "client_cap_removed_from_the_header",
    "client_cap_is_zero",
    "a_record_the_image_does_not_have", "a_record_that_is_read_but_never_written",
    "a_driver_record_the_image_does_not_have", "the_driver_marker_becomes_a_variable_again",
    "the_marker_is_published_before_the_withdraw", "the_marker_is_not_published",
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
    say("  xnu_entry_497: every file-scope record `entry_irq.c` and `MSM8974GIC.cpp` declare is in "
        "the image, so no key in this step's record is publishing a copy of a variable the compiler "
        "removed - and the driver's completion marker is a literal published in the handler's tail, "
        "after its withdraw block")
    return 0


if __name__ == "__main__":
    sys.exit(main())
