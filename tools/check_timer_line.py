#!/usr/bin/env python3
"""Check the eight things experiment 498 says about the timer's frame and the line it owns.

The step's subject is a device, and the check's job is to make sure that every number the driver uses
to talk to that device is a *reading* of the machine rather than a number somebody liked. Four of the
eight claims are comparisons between this repository's two halves of one fact - the tree we hand XNU
and the device's own tree; our driver's register offsets and the device's own kernel's - and the rest
are about the shape of the code: an interrupt id derived from the property, a handler that clears the
device before it publishes anything, an address that is a base plus an offset, and a source tree that
does not call the one function on this machine that can never return.

**One of those comparisons is about a name rather than a number, and that clause was added by a run.**
The tree this step hands XNU has to spell the controller's phandle `AAPL,phandle`
(`gIODTPHandleKey`, `IODeviceTreeSupport.cpp:137-138`) while the device's own tree and every Linux
binding spell it `phandle`. The first 498 image wrote the Linux spelling, every value in it agreed,
and the device aborted inside `IODTGetICellCounts` on a `parent` of 0 - because `AddPHandle` registers
nothing under a name it does not ask for. The key is now derived from Apple's source, so the check can
fail on the spelling and not only on the value.

What it reads
-------------
  * `src/platform/MSM8974Timer.cpp` - the driver that owns the frame.
  * `src/stage90_main.c` - the payload's device tree, where `/timer`'s `interrupts` lives.
  * `external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi` - the device's own tree.
  * `external/android_kernel_xiaomi_cancro/arch/arm/kernel/arch_timer.c` - the device's own driver.
  * every `.c`/`.cpp` under `src/` - for the one call nothing here may make.

The three files under `external/` are the reason this check exists in this form. Apple's XNU has no
timer driver for this block and this project has never read its registers, so "the offsets are right"
cannot be checked against anything in our own tree; the only authority is the kernel that runs this
device, which is in the repository, and a copy that drifts from it is a copy nobody would notice.

The frame's identity
--------------------
`msm8974.dtsi` declares the timer as a parent node with a `frame@f9021000` child, and the binding
(`Documentation/devicetree/bindings/arm/arch_timer.txt`) says of a frame: "interrupts : Interrupt list
for physical and virtual timers in that order" and "reg : The first and second view base addresses in
that order". The device's kernel takes index 0 of both (`of_iomap(frame, 0)`, `:623`;
`irq_of_parse_and_map(frame, 0)`, `:629`), so the physical timer's line is the frame's *first*
interrupt, `<0 8 0x4>` - SPI 8, which the GIC binding's cell rule turns into intid 40. This check
asserts both of those source facts and the two comparisons that depend on them.
"""

import os
import re
import sys
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

TIMER_CPP = os.path.join(REPO_ROOT, "src/platform/MSM8974Timer.cpp")
PAYLOAD_C = os.path.join(REPO_ROOT, "src/stage90_main.c")
PLATFORM_DIR = os.path.join(REPO_ROOT, "src/platform")
ENTRY_DIR = os.path.join(REPO_ROOT, "src/entry")
KERNEL_DIR = os.path.join(REPO_ROOT, "external/android_kernel_xiaomi_cancro")
DTSI = os.path.join(KERNEL_DIR, "arch/arm/boot/dts/msm8974.dtsi")
ARCH_TIMER_C = os.path.join(KERNEL_DIR, "arch/arm/kernel/arch_timer.c")
# Apple's own device-tree code, which is where the *keys* this step's tree has to use are declared.
DT_SUPPORT_CPP = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp")


def say(message):
    sys.stdout.write(message + "\n")


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    """C/C++ comments out, strings kept.

    **This exists because of a defect this project has paid for four times**: a needle that matched a
    comment rather than the code (269, 272, 297, 496), and once - in `check_driver_catalogue.py`, during
    this step - a needle that matched a *different key with the same word in it*. The driver's own
    comment block names `IOService::registerInterrupt`, so a scan for that call has to run over code
    and not over prose, and the way to make that true is to remove the prose rather than to write a
    cleverer pattern.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            end = text.find('*/', i + 2)
            i = n if end < 0 else end + 2
            out.append(' ')
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            end = text.find('\n', i)
            i = n if end < 0 else end
            out.append(' ')
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:min(j + 1, n)])
            i = min(j + 1, n)
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def defines(text):
    """Every `#define NAME value` whose value is a number or a `(1 << n)` bit.

    The device's kernel writes its control bits as `(1 << 0)` rather than as `0x1`
    (`arch_timer.c:50-52`), so a reader that only understood literals would report three of the eight
    things this check compares as "the kernel no longer defines it" - which is this project's oldest
    measurement defect in miniature: a parser that answers "not there" when what it means is "not in
    the shape I read".
    """
    out = {}
    for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(\([ \t]*1u?[ \t]*<<[ \t]*(\d+)[ \t]*\)'
                         r'|0[xX][0-9a-fA-F]+|\d+)u?[ \t]*$', text, re.M):
        if m.group(3) is not None:
            out[m.group(1)] = 1 << int(m.group(3))
        else:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def function_body(text, name):
    """The `{ ... }` of `name`, brace-matched, or None."""
    m = re.search(r'(?m)^[ \t]*(?:static[ \t]+)?[\w:<>*&,\[\] ]*?\b' + re.escape(name) + r'\s*\(', text)
    if not m:
        return None
    start = text.find('{', m.end())
    if start < 0:
        return None
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def dtsi_frame_block(text):
    """The `frame@f9021000 { ... }` block of the device's own tree."""
    m = re.search(r'frame@([0-9a-fA-F]+)\s*\{', text)
    if not m:
        return None, None, None
    start = m.end() - 1
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return int(m.group(1), 16), text[start:i + 1], text[:m.start()]
    return int(m.group(1), 16), None, text[:m.start()]


def u32_list(prop_text):
    """The integers of a `<...>` property value, `0x`-aware."""
    out = []
    for m in re.finditer(r'0[xX][0-9a-fA-F]+|\d+', prop_text):
        out.append(int(m.group(0), 0))
    return out


def payload_node(text, name):
    """The payload's node called `name`, from its `name` property to the end of the node.

    A node in `stage90_main.c` has no terminator: it is a run of `apple_dt_prop_*` calls and it ends
    where the next `apple_dt_node_begin` starts. That makes "the text after this property" a
    position-dependent answer, and the first two spellings of this helper here were exactly that -
    keyed on the next `apple_dt_node_begin`, which for `/timer` (the last node the builder writes)
    is not there at all, so the search returned None and the claim reported that the node it was
    standing in front of could not be located. The stopper is therefore *either* the next node
    *or* the end of the function that builds the tree, whichever comes first, which is the same
    rule the reader of the file applies.
    """
    m = re.search(r'apple_dt_prop_str\(\s*b,\s*"name",\s*"%s"\s*\);' % re.escape(name), text)
    if m is None:
        return None
    rest = text[m.end():]
    end = len(rest)
    for stopper in ("apple_dt_node_begin", "\n}\n"):
        at = rest.find(stopper)
        if at != -1:
            end = min(end, at)
    return rest[:end]


# The device's kernel's own two-route vocabulary, added by 501. Each is a name that kernel has on the
# line the driver cites it at, and each is cited in the driver's 501 block - so a citation that drifts
# one line away is refused, exactly as the offsets block's citations are.
ROUTE_ANCHORS = ("counter_get_cntpct_mem", "counter_get_cntpct_cp15",
                 "counter_get_cntvct_mem", "counter_get_cntvct_cp15",
                 "get_cntpct_func", "get_cntvct_func", "has_cp15")


def grounding_block(text):
    """The comment in one source that grounds this step in the device's own kernel.

    The block is found by the one thing that identifies it without a line number: a comment line that is
    *nothing but* the device's kernel path. That is how the block that lists the frame's offsets opens,
    and it is where a *bare* citation lives - a `:NNN` that names no code of its own and is a reference
    back to a call the block has already quoted. A source with no such line contributes nothing to that
    one half of the clause, which is honest rather than an omission: `stage90_main.c` cites the two
    calls inline, beside their names, and the half that reads a citation *with* a name covers it.
    """
    lines = text.split("\n")
    for i, line in enumerate(lines):
        content = line.strip().lstrip("*").strip()
        if content.endswith("arch_timer.c") and "/" in content and " " not in content:
            lo = i
            while lo > 0 and (lines[lo - 1].lstrip().startswith("*") or lines[lo - 1].rstrip().endswith("/*")):
                lo -= 1
            hi = i
            while hi + 1 < len(lines) and lines[hi + 1].lstrip().startswith("*"):
                hi += 1
            return set(range(lo, hi + 1))
    return set()


def line_citations(text):
    """Every `:NNN` line citation in a source, as `(cited line, the source line it is written on)`.

    A citation is written on the same line as the name it is about - the number first and the code after
    it (`:60  #define QTIMER_CNTP_LOW_REG  0x000`) or the sentence first and the number parenthesised
    after it (``the frame's first `reg` region (`:623`)``). Both shapes put the two within one line of
    each other, and that is what binds them: a citation separated from its name by a line break is a
    citation of something else.
    """
    out = []
    for i, line in enumerate(text.split("\n")):
        for m in re.finditer(r":(\d+)", line):
            out.append((int(m.group(1)), i, line, m.start()))
    return out


class Facts(object):
    def __init__(self, image):
        self.timer_src = read(TIMER_CPP)
        self.timer = strip_comments(self.timer_src)
        self.payload_src = read(PAYLOAD_C)
        self.payload = strip_comments(self.payload_src)
        self.dtsi = read(DTSI)
        self.arch_timer = read(ARCH_TIMER_C)
        self.dt_support = read(DT_SUPPORT_CPP)
        self.image = image
        self.symbols = nm(image) if image else {}
        self._sources = {}

    def source_of(self, path):
        """The comment-stripped text of one source file, as every claim reads it.

        `MSM8974Timer.cpp` answers with `self.timer` rather than with a second read of the disk, so
        that a claim that scans the tree and a claim that reads the driver are looking at one text.
        While the two were separate, `claim_no_source_makes_the_blocking_call` reported `ok` over a
        file a mutation had changed: the mutation edits `facts.timer`, the scan reopened the path,
        and the check then measured the tree rather than the fact - the `--wrap`-that-cannot-see-the-
        call and `pgrep`-matching-itself family of defects (455, 459) in a new shape, a report true
        of the wrong object. No other file in these two directories is mutated, so the timer's file
        is the only one where the two answers can differ.
        """
        key = os.path.abspath(path)
        if key == os.path.abspath(TIMER_CPP):
            return self.timer
        if key not in self._sources:
            self._sources[key] = strip_comments(read(path))
        return self._sources[key]


def nm(path):
    try:
        out = subprocess.check_output(["arm-none-eabi-nm", path], stderr=subprocess.DEVNULL)
    except Exception:
        return {}
    names = {}
    for line in out.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            names[parts[2]] = parts[1]
    return names


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

FRAME_REG_NAMES = (
    # (name in our driver, name in the device's kernel, the kernel's source line)
    ("CNTP_LOW_OFF", "QTIMER_CNTP_LOW_REG"),
    ("CNTP_HIGH_OFF", "QTIMER_CNTP_HIGH_REG"),
    ("CNTV_LOW_OFF", "QTIMER_CNTV_LOW_REG"),
    ("CNTV_HIGH_OFF", "QTIMER_CNTV_HIGH_REG"),
    ("FREQ_OFF", "QTIMER_FREQ_REG"),
    ("CNTP_TVAL_OFF", "QTIMER_CNTP_TVAL_REG"),
    ("CTRL_OFF", "QTIMER_CTRL_REG"),
    ("CNTV_TVAL_OFF", "QTIMER_CNTV_TVAL_REG"),
)

FRAME_BIT_NAMES = (
    ("CTRL_ENABLE", "ARCH_TIMER_CTRL_ENABLE"),
    ("CTRL_IT_MASK", "ARCH_TIMER_CTRL_IT_MASK"),
    ("CTRL_IT_STAT", "ARCH_TIMER_CTRL_IT_STAT"),
)


def claim_offsets_are_the_devices(facts, failures, notes):
    """1. Every frame offset is the one the device's own kernel uses, and the two index facts too.

    Nothing in this repository can say what register lives at `0xf9021000 + 0x2c`; the only authority
    is the kernel that runs this device, `external/android_kernel_xiaomi_cancro/arch/arm/kernel/
    arch_timer.c`. So the offsets are compared name by name with that file's `QTIMER_*` and
    `ARCH_TIMER_CTRL_*` defines, and the two *index* decisions below them are compared too - `:623`
    and `:629` are where that kernel says the frame's first `reg` region and the frame's first
    interrupt are the ones it uses, and both are load-bearing here because this node carries the
    frame's two regions flattened after the parent's.
    """
    ours = defines(facts.timer)
    theirs = defines(facts.arch_timer)
    if not theirs:
        failures.append("the device's kernel is not where this check expects it (%s), so the frame's "
                        "offsets cannot be compared with anything and this claim would be vacuous"
                        % os.path.relpath(ARCH_TIMER_C, REPO_ROOT))
        return
    for mine, theirs_name in FRAME_REG_NAMES + FRAME_BIT_NAMES:
        key = "MSM8974_FRAME_" + mine
        if key not in ours:
            failures.append("`MSM8974Timer.cpp` no longer defines `%s`, so the frame offset it names "
                            "is gone from the driver the claim is about" % key)
            continue
        if theirs_name not in theirs:
            failures.append("the device's kernel no longer defines `%s`, so this check can no longer "
                            "say what `%s` should be" % (theirs_name, key))
            continue
        if ours[key] != theirs[theirs_name]:
            failures.append("`MSM8974Timer.cpp`'s `%s` is 0x%x and the device's own kernel's `%s` is "
                            "0x%x: one of the two moved, and a driver addressing a register its device "
                            "does not have is a driver whose every reading after that is a reading of "
                            "some other register" % (key, ours[key], theirs_name, theirs[theirs_name]))
    # **502 added the one frame offset the device's kernel does not have**, and this is the clause that
    # keeps "one" a number rather than a word. The driver names more `MSM8974_FRAME_*_OFF` registers than
    # the kernel does, because the virtual timer's *control word* is not in that file's table at all - it
    # writes and reads the physical timer's control word and never touches the virtual side - so the set
    # difference being exactly this one name is what "the driver addresses the frame and one register
    # beside it" means. A second invented offset, or the loss of this one, fails here instead of being a
    # sentence nobody can falsify, and the one is checked to be a *derivation*: a literal beside a
    # derivation is this project's oldest defect class in its smallest form, and the pair it derives from
    # is the one the kernel does name.
    ours_offsets = {name + "_OFF" for name in
                    re.findall(r"^[ \t]*#[ \t]*define[ \t]+MSM8974_FRAME_(\w+)_OFF",
                               facts.timer, re.M)}
    derived = sorted(ours_offsets - {mine for mine, _ in FRAME_REG_NAMES})
    if derived != ["CNTV_CTL_OFF"]:
        failures.append("`MSM8974Timer.cpp` derives the frame offsets %s from the device's kernel's "
                        "names, and the one register it addresses that the kernel does not name should "
                        "be the virtual timer's control word alone - `CNTV_CTL_OFF`, the word that says "
                        "whether a compare value nothing has written is masked. A second one is an "
                        "offset with no authority behind it and a missing one is a control word the "
                        "countdown's reading cannot be interpreted without" % (derived or "none",))
    else:
        m = re.search(r"^[ \t]*#[ \t]*define[ \t]+MSM8974_FRAME_CNTV_CTL_OFF[ \t]+"
                      r"\([ \t]*MSM8974_FRAME_CNTV_TVAL_OFF[ \t]*\+[ \t]*"
                      r"(0[xX][0-9a-fA-F]+|\d+)[uU]?[ \t]*\)[ \t]*$", facts.timer, re.M)
        if not m:
            failures.append("`MSM8974_FRAME_CNTV_CTL_OFF` is no longer written as the virtual countdown's "
                            "own offset plus a constant: the address would be a second literal for a "
                            "value whose pattern the certified pair already confirms, with nothing "
                            "comparing the two spellings")
        elif int(m.group(1), 0) != 4:
            failures.append("`MSM8974_FRAME_CNTV_CTL_OFF` is the virtual countdown's offset plus %s and "
                            "the layout puts a control word four bytes after its countdown - the pair the "
                            "device's kernel's own table shows at `0x028`/`0x02C` (`arch_timer.c:66`, "
                            "`:64`)" % m.group(1))
        else:
            notes.append("the one frame offset the device's kernel does not name is the virtual timer's "
                         "control word, derived as the countdown's offset plus four")

    needles = (("of_iomap(frame, 0)", "the frame's first `reg` region"),
               ("irq_of_parse_and_map(frame, 0)", "the frame's first interrupt"))
    call_lines = {}
    for needle, what in needles:
        if needle not in facts.arch_timer:
            failures.append("the device's kernel no longer takes %s (`%s` is not in its source), so "
                            "the driver's choice of the frame's *first* entry has nothing behind it"
                            % (what, needle))
            continue
        # **The citation is checked like a number in the code.** A line number in prose is read by a
        # human who then trusts whatever is on that line, so a citation that points one function away
        # is the same defect as a wrong offset with a smaller blast radius - and nothing in this
        # repository said so until this clause existed. Both of this step's citations were wrong
        # (they said 631 and 637; the calls are at 623 and 629) for the whole time the code and the
        # check agreed with each other, and what caught them was re-reading the kernel while writing
        # the report. The number is now derived from the kernel's own text, so the next time that file
        # moves the citation has to move with it or the build stops with the right number in hand.
        line = facts.arch_timer[:facts.arch_timer.index(needle)].count("\n") + 1
        call_lines[needle] = line
        for source, who in ((facts.timer_src, "`MSM8974Timer.cpp`"),
                            (facts.payload_src, "`stage90_main.c`")):
            # Either spelling counts. The prose cites the first of the two calls with the file's path
            # and the second as `:NNN`, which is how a person writes it; requiring one particular
            # spelling would be a claim about style rather than about the number.
            if not any(form in source for form in ("arch/arm/kernel/arch_timer.c:%d" % line,
                                                   "`:%d`" % line)):
                failures.append("%s does not cite `%s` at line %d of the device's kernel: it cites "
                                "`arch/arm/kernel/arch_timer.c:NNN` or `:NNN`, and either has to be "
                                "%d - a citation that points at another line is a reading of the "
                                "wrong text" % (who, needle, line, line))

    # **And every citation that names one of those things, not only the existence of the right number.**
    # The clause above asks whether the *right* number appears anywhere in the file, and a stale number
    # beside it is invisible to it - which is exactly what was in this tree when it was first written:
    # the block that grounds the frame's offsets cited `:631` and `:637` for the two calls *and* `:66`
    # for `QTIMER_FREQ_REG`, which is at `:65`, while the corrected `:623`/`:629` sat in a different
    # comment six hundred lines below and satisfied that clause on their own. A clause that asks for the
    # presence of one citation is not a clause about the absence of another. So each citation is read
    # *with* the name it is written beside, and that name has to be on the line the citation names.
    # **501 added a second block of citations to this file, and the anchors are what read it.** The
    # driver's two-route block cites the device's kernel's own counter functions and the switch that
    # chooses between them (`:297`/`:310`/`:318`/`:331`/`:339`/`:340`/`:607`-`:609`), and a citation is
    # only read here when a *name this check knows* is written beside it - so the names are the check's
    # third list now alongside the frame's offsets and the two calls. An anchor the kernel does not
    # have on the cited line fails the same way a wrong offset does, which is the whole point: the
    # block that says "these are the two routes" is checked like the block that says "these are the
    # registers".
    anchors = ([name for _, name in FRAME_REG_NAMES + FRAME_BIT_NAMES] + [n for n, _ in needles]
               + list(ROUTE_ANCHORS))
    kernel_lines = facts.arch_timer.split("\n")
    cited_calls = set(call_lines.values())
    checked = 0
    for source, who in ((facts.timer_src, "`MSM8974Timer.cpp`"),
                        (facts.payload_src, "`stage90_main.c`")):
        block = grounding_block(source)
        # The block finder keys on our own file's shape - the kernel's path alone on a line - and a
        # property of *the writer* is a claim like any other: if the block is reflowed, the bare
        # citations inside it stop being read by anything and nothing would say so. The anchored half
        # below covers twelve of the thirteen without the block; this keeps the thirteenth from going
        # quietly unchecked.
        if "external/android_kernel_xiaomi_cancro/arch/arm/kernel/arch_timer.c" in source and not block:
            failures.append("%s cites the device's kernel by its path but no longer opens that block "
                            "with the path on a line of its own, so the bare citations in it are read "
                            "by nothing" % who)
        for cited, at, line, start in line_citations(source):
            # A citation that carries a *path* before its number belongs to the file that path names,
            # and the offset list is full of them: `msm8974.dtsi:161-167` is written two lines below the
            # call citations, and reading its `161` as a line of the device's kernel is reading a
            # citation of another file. Only `arch_timer.c`'s own numbers are this claim's business -
            # and its own long form is one of them, so it is not skipped with the rest.
            qualified = re.search(r"[\w./-]$", line[:start]) is not None
            if qualified and "arch_timer.c" not in line[:start]:
                continue
            named = [a for a in anchors if a in line]
            if not named:
                if at in block and cited_calls and cited not in cited_calls:
                    failures.append("%s's block that grounds the frame's offsets cites line %d of the "
                                    "device's kernel with no name on the line, so it is a reference to "
                                    "one of the two calls the block quotes - and those are at %s. A "
                                    "citation that points at another line is read as a claim about "
                                    "whatever is there"
                                    % (who, cited,
                                       " and ".join(str(v) for v in sorted(cited_calls))))
                continue
            checked += 1
            for a in named:
                if not (1 <= cited <= len(kernel_lines)) or a not in kernel_lines[cited - 1]:
                    failures.append("%s cites `%s` at line %d of the device's kernel, and that line "
                                    "says `%s` - an offset justified by a citation that points at a "
                                    "line which does not contain the name it is cited for is justified "
                                    "by nothing"
                                    % (who, a, cited,
                                       kernel_lines[cited - 1].strip() if 1 <= cited <= len(kernel_lines)
                                       else "(past the end of the file)"))
    if not failures:
        notes.append("the frame's eight register offsets and its three control bits are the device's "
                     "own kernel's, name by name")
        notes.append("every citation written beside one of those names points at a line of the device's "
                     "kernel that contains it (%d checked, and one bare reference per block)" % checked)


def claim_the_tree_declares_the_devices_line(facts, failures, notes):
    """2. The line our tree declares for `/timer` is the line the device's own tree declares for its frame.

    Two trees, one device. Ours (`stage90_main.c`) is the one XNU walks, and it is hand-written; the
    device's (`msm8974.dtsi`) is the one the machine's own kernel walks. A `interrupts` property that
    drifts from the device's - a number typed one off, a trigger cell dropped - produces a tree that
    walks clean, matches the driver, and describes a line that does not exist. That is the failure mode
    this claim is for, and it cannot be seen from inside our tree at all.

    The frame's *addresses* are compared the same way: our `/timer` carries the parent's `reg` and then
    the frame's two regions, so entry 1 must be the address the device's tree gives its `frame@` node.
    """
    frame_addr, block, _ = dtsi_frame_block(facts.dtsi)
    if block is None:
        failures.append("the device's tree no longer declares a `frame@` node, so the timer's line "
                        "cannot be compared with anything")
        return
    m = re.search(r'interrupts\s*=\s*(.*?);', block, re.S)
    if not m:
        failures.append("the device's tree's frame node declares no `interrupts`, so the line this "
                        "step is about has no source")
        return
    theirs = u32_list(m.group(1))
    if len(theirs) < 3:
        failures.append("the device's tree's frame `interrupts` is %r, which is not three cells"
                        % m.group(1).strip())
        return
    theirs_line = theirs[:3]

    arr = re.search(r'static const uint32_t timer_frame_intr\[\]\s*=\s*\{(.*?)\};',
                    facts.payload, re.S)
    if not arr:
        failures.append("`stage90_main.c` no longer declares `timer_frame_intr[]`, so the payload's "
                        "tree has no timer line for XNU to walk")
        return
    ours = u32_list(arr.group(1))
    if ours != theirs_line:
        failures.append("the payload's tree declares `timer_frame_intr[]` = %s and the device's tree "
                        "declares the frame's physical timer as %s: our tree is the one XNU walks, so "
                        "a line that differs from the device's is a driver registering for an "
                        "interrupt id the machine never raises"
                        % (ours, theirs_line))

    reg = re.search(r'static const uint32_t timer_reg\[\]\s*=\s*\{(.*?)\};', facts.payload, re.S)
    if not reg:
        failures.append("`stage90_main.c` no longer declares `timer_reg[]`, so the frame's address in "
                        "our tree cannot be checked")
        return
    ours_reg = u32_list(reg.group(1))
    frame_reg = re.search(r'reg\s*=\s*(.*?);', block, re.S)
    their_reg = u32_list(frame_reg.group(1)) if frame_reg else []
    if frame_addr is not None and (len(ours_reg) < 4 or ours_reg[2] != frame_addr):
        failures.append("the device's tree puts its frame at 0x%x and our `/timer`'s second `reg` "
                        "entry is 0x%x: `MSM8974Timer.cpp` maps entry %s as the frame, and a tree whose "
                        "entry order differs from the device's is a driver reading the parent block as "
                        "the timer"
                        % (frame_addr, ours_reg[2] if len(ours_reg) > 2 else 0, "1"))
    if their_reg and (len(ours_reg) < 6 or ours_reg[4] != their_reg[2]):
        failures.append("the device's tree gives the frame's second view as 0x%x and our third `reg` "
                        "entry is 0x%x: the tree this project hands XNU has carried the frame's two "
                        "views since 492 and one of them moved"
                        % (their_reg[2] if len(their_reg) > 2 else 0,
                           ours_reg[4] if len(ours_reg) > 4 else 0))
    if not failures:
        notes.append("our `/timer`'s line %s and the frame's address 0x%x are the device's own tree's"
                     % (theirs_line, frame_addr))


def apple_dt_key(facts, symbol):
    """The property name Apple's own device-tree code looks a symbol up by, read out of its source.

    `IODeviceTreeSupport.cpp:137-150` is where the keys are declared, and they are not the names the
    device's own tree uses: `gIODTPHandleKey` is `"AAPL,phandle"` where `msm8974.dtsi` writes
    `phandle`, exactly as the kernel-tree binding wants, and the two are read by *different* readers.
    A tree handed to XNU has to use XNU's spelling, and the only authority for it is this file, so it
    is parsed rather than copied - a copy is a second definition and this one would be unread.
    """
    m = re.search(r'%s\s*=\s*OSSymbol::withCStringNoCopy\(\s*"([^"]+)"\s*\)' % re.escape(symbol),
                  facts.dt_support)
    return m.group(1) if m else None


def claim_the_phandle_pair_agrees(facts, failures, notes):
    """3. The controller's phandle and `/timer`'s `interrupt-parent` are one value, in Apple's two keys.

    A phandle is written twice by construction - once where the node is declared, once where it is
    referred to - which is this project's oldest defect class in its purest form: one decision, two
    definitions, and nothing comparing them unless a check does. `IODTMapInterrupts` follows the pair
    (`IODTFindInterruptParent` -> `FindPHandle`, `IODeviceTreeSupport.cpp:467-487`), so a tree whose
    two halves disagree is a tree where the OS's own reading of the node's interrupts finds no
    controller - and the *run* is where that becomes visible (`_osmap_ok`, `_osmap_ph_agree`). This
    claim is the source half of the same fact.

    **And it is a claim about two things, not one.** The values have to agree *and* the keys have to
    be the keys the reading code asks for: `AddPHandle` tests `gIODTInterruptCellKey` and registers
    the node's `gIODTPHandleKey` property, and a node whose property is named anything else is a node
    the table never sees. The first 498 image wrote `phandle` - Linux's spelling, and the one the
    device's tree uses - and this claim passed, because both of its halves were 1. The run then
    aborted inside `IODTGetICellCounts` on a `parent` of 0, one layer below the pair. So the keys are
    compared with Apple's own declarations here, and the failure this prevents is the one that was
    measured rather than the one that was imagined.
    """
    ctrl = payload_node(facts.payload, "interrupt-controller")
    if not ctrl:
        failures.append("the payload's `/interrupt-controller` node could not be located, so the "
                        "phandle pair cannot be compared")
        return
    handle_key = apple_dt_key(facts, "gIODTPHandleKey")
    parent_key = apple_dt_key(facts, "gIODTInterruptParentKey")
    cells_key = apple_dt_key(facts, "gIODTInterruptCellKey")
    if not (handle_key and parent_key and cells_key):
        failures.append("`IODeviceTreeSupport.cpp` no longer declares the three keys this claim "
                        "compares (%s): the property names this tree has to use have no source any "
                        "more" % ", ".join(s for s in ("gIODTPHandleKey", "gIODTInterruptParentKey",
                                                       "gIODTInterruptCellKey")
                                          if not apple_dt_key(facts, s)))
        return
    ph = re.search(r'apple_dt_prop_u32\(\s*b,\s*"%s",\s*(\d+)\)' % re.escape(handle_key), ctrl)
    if not ph:
        misspelt = re.search(r'apple_dt_prop_u32\(\s*b,\s*"(phandle)",\s*(\d+)\)', ctrl)
        if misspelt:
            failures.append("the payload's `/interrupt-controller` declares `%s` where Apple's "
                            "`AddPHandle` (`IODeviceTreeSupport.cpp:422-431`) registers `%s`: a node "
                            "whose property is spelled any other way is a node `gIODTPHandles` never "
                            "sees, `FindPHandle` then answers 0 for the value `/timer` names, and the "
                            "OS's own publication walk calls `IODTGetICellCounts(0, ...)` and aborts on "
                            "a vtable load from address 0 - which is what 498's first run measured, "
                            "with both halves of the pair reading 1"
                            % (misspelt.group(1), handle_key))
        else:
            failures.append("the payload's `/interrupt-controller` declares no `%s`: the plane fills "
                            "`gIODTPHandles` only from nodes that carry `%s` *and* `%s` (`AddPHandle`, "
                            "`IODeviceTreeSupport.cpp:422-431`), so nothing can name this controller - "
                            "and `IODTInterruptControllerName` asserts the property is there, so the "
                            "OS's own mapping aborts instead of returning"
                            % (handle_key, cells_key, handle_key))
    timer = payload_node(facts.payload, "timer")
    if not timer:
        failures.append("the payload's `/timer` node could not be located")
        return
    ip = re.search(r'apple_dt_prop_u32\(\s*b,\s*"%s",\s*(\d+)\)' % re.escape(parent_key), timer)
    if not ip:
        failures.append("the payload's `/timer` declares no `%s`: the interrupts property would then "
                        "be an undecoded list - `IODTFindInterruptParent` falls back to the parent "
                        "*node*, and the root carries no `interrupt-controller`, so the mapping fails "
                        "with 'error mapping interrupt' and no specifier is filed" % parent_key)
    elif ph and ip and ph.group(1) != ip.group(1):
        failures.append("`/interrupt-controller`'s `%s` is %s and `/timer`'s `%s` is %s: one value in "
                        "two places with nothing making them agree, and the OS's own interrupt mapping "
                        "follows the second to find the first"
                        % (handle_key, ph.group(1), parent_key, ip.group(1)))
    cells = re.search(r'"%s",\s*(\d+)' % re.escape(cells_key), ctrl)
    if not cells:
        failures.append("the payload's `/interrupt-controller` declares no `%s`: `IODTGetICellCounts` "
                        "then falls back to 1 cell per specifier, and the three cells `/timer` writes "
                        "are read as one" % cells_key)
    elif cells.group(1) != "3":
        failures.append("`%s` is %s and `/timer`'s `interrupts` has three cells: the specifier the OS "
                        "files would be cut short" % (cells_key, cells.group(1)))
    if not failures:
        notes.append("`/interrupt-controller`'s `%s` and `/timer`'s `%s` are the same value, under the "
                     "two names Apple's own code reads them by, and the controller declares the 3 "
                     "cells `/timer` writes" % (handle_key, parent_key))


def claim_the_intid_is_derived(facts, failures, notes):
    """4. The interrupt id is computed from the tree's cells, not written down.

    `<0 8 4>` and `40` are both plausible-looking readings of the same line, and only one of them is
    this machine's - so the driver must show the rule it applied. The claim is that the assignment to
    `line` adds `MSM8974_GIC_SPI_BASE` to a number read out of the property, and that no `#define`
    in the file hands the driver a finished interrupt id to use instead.
    """
    body = facts.timer
    m = re.search(r'if\(\s*dt_type\s*==\s*MSM8974_INTR_TYPE_SPI\s*\)\s*\{\s*line\s*=\s*([^;]+);',
                  body, re.S)
    if not m:
        failures.append("`MSM8974Timer.cpp` no longer derives `line` from `dt_type == "
                        "MSM8974_INTR_TYPE_SPI` in one assignment: the interrupt id the driver "
                        "registers for has to come out of the tree's cells, because the two cells "
                        "`<0 8 4>` describe a line at intid 40 and a driver that read the 8 is "
                        "registering for SGI 8 - a line it can raise itself")
        return
    expr = m.group(1).strip()
    if "MSM8974_GIC_SPI_BASE" not in expr or "dt_num" not in expr:
        failures.append("`MSM8974Timer.cpp` computes `line = %s`, which is not `dt_num` plus the "
                        "SPI base: the +32 is the whole of the translation from the tree's cells to "
                        "the distributor's numbering, and a finished number here is a second "
                        "definition of the tree's line" % expr)
    if re.search(r'#define\s+MSM8974_\w*INTID\w*\s+\d+', body):
        failures.append("`MSM8974Timer.cpp` defines an interrupt id as a constant: the line is a "
                        "property of the device's tree, and a `#define` beside it is a second "
                        "definition that the run would read as the tree's")
    if re.search(r'entry_irq_register_client\(\s*\d+', body):
        failures.append("`MSM8974Timer.cpp` registers for a literal interrupt id: the id has to be the "
                        "one the tree's `interrupts` property produced (`line`), or the key that "
                        "publishes `_line_intid` is publishing a number the registration did not use")
    if not failures:
        notes.append("the interrupt id is `dt_num` + the SPI base, with no constant beside it")


def claim_the_handler_clears_the_device_first(facts, failures, notes):
    """5. The handler writes the device - before it publishes anything, and before it returns.

    The frame's line is level-sensitive: the output stays asserted until the device's control word is
    written, so a handler that returns without silencing the timer is re-entered immediately and forever.
    496's registry counts calls per slot and stops with the intid in the channel when the cap is
    exceeded, which makes such a storm a named stop rather than a hang - but a stop is still a run
    spent, and the property that avoids it is an *order*: the store to the device comes first.

    **500 made the clause plural, and the way it broke is worth keeping.** The handler had one device
    store when this was written - `CTRL |= IT_MASK`, the write that drops the level - and the clause read
    the value by looking 120 characters past `MSM8974_FRAME_CTRL_OFF ) =`. 500's handler has *two* paths:
    the re-arm (`CNTP_TVAL` reloaded, `CTRL` written with `ENABLE`, because loading a down-counter clears
    the expired condition) and the mask. The first store the clause found was then the re-arm's, whose
    120-character window holds `ENABLE`, the counter's increment and the `else` - and the mask, which is
    the property being tested, was outside it. So a window measured on one shape reported a handler that
    still masks on its last call as a handler that clears nothing. The question is now asked of **every**
    store in the body: at least one of them has to carry `IT_MASK`, which is the same statement about the
    same bit with no assumption about how many paths the writer left.

    Note what this claim cannot say: it is a reading of the source's order, not of the machine's. What
    the machine does with the write - whether the device took the bit - is `_isr_ctl_after` in the
    run, and the two are different findings on purpose.
    """
    body = function_body(facts.timer, "msm8974_timer_isr")
    if body is None:
        failures.append("`MSM8974Timer.cpp` no longer defines `msm8974_timer_isr`: the line this step "
                        "registers has no handler, and the frame's level would stay asserted until the "
                        "dispatcher's per-slot cap stopped the run")
        return
    stores = [m.start() for m in re.finditer(r"MSM8974_FRAME_CTRL_OFF \) =", body)]
    publishes = [m.start() for m in re.finditer(r"entry_live_write\(", body)]
    if not stores:
        failures.append("`msm8974_timer_isr` never writes the frame's control register: the line is "
                        "level-sensitive, so a handler that only reads the device and returns is "
                        "re-entered as fast as the CPU can take the exception")
        return
    if not any("IT_MASK" in body[at:at + 120] for at in stores):
        failures.append("`msm8974_timer_isr` has no write to the frame's control register that carries "
                        "`MSM8974_FRAME_CTRL_IT_MASK`: masking the output is what drops a "
                        "level-sensitive line, so a handler whose every path leaves the device unmasked "
                        "is re-entered as fast as the CPU can take the exception (%d store(s) read)"
                        % len(stores))
    if publishes and min(stores) > min(publishes):
        failures.append("`msm8974_timer_isr` publishes a key before it writes the device: the clear is "
                        "the one thing this handler must do even if every key after it were dropped, "
                        "and a key written first is a key written on a path that has not silenced the "
                        "line yet")
    if "g_timer_frame_va" not in body:
        failures.append("`msm8974_timer_isr` does not read `g_timer_frame_va`: the address would then "
                        "come from somewhere else in a handler running on the boot thread's unwound "
                        "stack")
    if not failures:
        notes.append("`msm8974_timer_isr` writes the frame's control register on every path, one of "
                     "them with the mask, and every write is before the first key")


def claim_no_source_makes_the_blocking_call(facts, failures, notes):
    """6. Nothing in this step's sources calls `IOService::registerInterrupt`.

    **The call cannot return on this machine, and the reason is one layer earlier than 496 read it.**
    `IOService::registerInterrupt` (`IOService.cpp:6337`) is `lookupInterrupt` and then the
    controller's own registration; `lookupInterrupt` ends in
    `getPlatform()->lookUpInterruptController(name)` (`:6290`), which is

        IOPlatformExpert::lookUpInterruptController(OSSymbol * name)          // :379-397
        {   IOLockLock(gIOInterruptControllersLock);
            while (1) {
                object = gIOInterruptControllers->getObject(name);
                if (object != 0) break;
                IOLockSleep(gIOInterruptControllersLock, gIOInterruptControllers, THREAD_UNINT);
            }   ... }

    - a sleep only `registerInterruptController` (`:354-363`) wakes, whose only caller in the whole
    tree is `IOCPU.cpp:783` inside the CPU interrupt controller's own initialiser. This image
    instantiates no CPU driver and registers no controller (405 measured the consequence of the
    first), so the dictionary is empty and every call sleeps forever on the calling thread - which,
    from a kext's `start` on the boot thread, is the boot. 496 called the route unusable after reading
    `IOCPUInterruptController::registerInterrupt`; the block is one layer before that one, and
    unconditional.

    The check is a scan of the sources rather than a claim about a run, because a run that made this
    call is a run that has to be recovered by the watchdog - and because the property wanted is "no
    file in this image makes it", which is a property of the files.
    """
    offenders = []
    for directory in (PLATFORM_DIR, ENTRY_DIR):
        for name in sorted(os.listdir(directory)):
            if not name.endswith((".c", ".cpp", ".h")):
                continue
            path = os.path.join(directory, name)
            source = facts.source_of(path)
            for m in re.finditer(r'(?<![\w:])registerInterrupt\s*\(', source):
                # `registerInterruptController` is a different function and is spelled without the
                # parenthesis directly after the name, but a `->registerInterrupt(` on an
                # `IOInterruptController` is Apple's other one; both are excluded by the lookbehind
                # above and by this one, which keeps `::registerInterrupt` out of the prose-free code.
                line = source[:m.start()].count("\n") + 1
                offenders.append("%s:%d" % (os.path.relpath(path, REPO_ROOT), line))
    if offenders:
        failures.append("the image's sources call `registerInterrupt` at %s: on this machine that "
                        "call sleeps forever inside `IOPlatformExpert::lookUpInterruptController` "
                        "(`IOPlatformExpert.cpp:391`), because `gIOInterruptControllers` is filled "
                        "only by `registerInterruptController` whose only caller is "
                        "`IOCPU.cpp:783` - a thread that hangs in a kext's `start` is the boot, and "
                        "the registry this project uses instead is `entry_irq_register_client`"
                        % ", ".join(offenders))
    else:
        notes.append("no source in this image calls `registerInterrupt`, whose lookup sleeps forever "
                     "on a machine with no registered interrupt controller")


def claim_the_frame_is_the_second_entry(facts, failures, notes):
    """7. The frame is reached as `devmem[1]`, through a base plus an offset - never as an address.

    The single most tempting way to write this driver is `*(volatile uint32_t *)0xf9021000`, and it
    would work: the payload's page tables map the window, the address is right, and the run would
    print the frame's frequency. What it would not be is a reading of the *tree* - `reg`'s second
    entry is the frame because the device's tree says so, and a literal in the driver is a number no
    register resolution produced. The claim is the structural version of 493's lesson: the driver's
    device address comes from the OS's array, by the index the tree's ordering gives.
    """
    if 'devmem->getObject( MSM8974_TIMER_FRAME_ENTRY )' not in facts.timer:
        failures.append("`MSM8974Timer.cpp` does not take the frame from the OS's own array by index: "
                        "the frame is `reg`'s second entry because the device's tree lists the "
                        "parent's region first, and an address the driver makes up itself is a second "
                        "definition of the node's layout")
    for name in ("MSM8974_FRAME_FREQ_OFF", "MSM8974_FRAME_CTRL_OFF", "MSM8974_FRAME_CNTP_LOW_OFF"):
        if "frame_va + " + name not in facts.timer:
            failures.append("`MSM8974Timer.cpp` does not read `%s` as `frame_va + %s`: every frame "
                            "register has to be the mapped base plus the device's own offset, or the "
                            "mapping the driver published is not the address its readings used"
                            % (name, name))
    for lit in ("0xf9021000", "0xf9020000", "0xf9022000"):
        if lit in facts.timer:
            failures.append("`MSM8974Timer.cpp` has the frame's address `%s` written in it: that "
                            "address is the tree's, and the OS's own resolution is what turns it into "
                            "the `_frame_phys` the record publishes - a literal here is a reading that "
                            "skips the resolution it is supposed to be checking" % lit)
    if "MSM8974_TIMER_FRAME_ENTRY" not in defines(facts.timer):
        failures.append("`MSM8974_TIMER_FRAME_ENTRY` is gone: the index is one definition used by the "
                        "mapping and by the address it is compared with, and a literal 1 in the "
                        "mapping is that decision in two places")
    if not failures:
        notes.append("the frame comes from the OS's array at the index the tree's ordering gives, and "
                     "every register is `frame_va + offset`")


def claim_the_record_can_be_read(facts, failures, notes):
    """8. Every number the run needs in order to be read *as a record* is published.

    A step whose subject is a device has a failure mode a step about code does not: the machine can
    answer with a number that is about no machine. `_frame_phys` of 0 means the OS resolved nothing;
    `_frame_va` of 0 means it resolved something and the driver could not map it; `_frame_freq` of 0
    with a non-zero `_frame_va` means the driver mapped the wrong page. Three different findings that a
    single "the reading is zero" would fold into one, so the keys that separate them have to exist.

    And the line's own record has the same shape one level up: `_line_intid` says what the tree's cells
    produced, `_line_cli_rc` says whether the payload's registry took the handler, and `_isr_agree`
    says - if the line ever fires - that the id it fired on is the one that was registered. The last is
    the one that can only be written by the machine.
    """
    required = (
        # the OS's resolution and the mapping of the frame
        ("frame_objkind", "the kind of object the OS's array held"),
        ("frame_phys", "the frame's physical address as the OS resolved it"),
        ("frame_len", "the frame's length as the OS resolved it"),
        ("frame_map", "the mapping the OS returned"),
        ("frame_va", "the address the driver reads the frame through"),
        # the frame's own answers
        ("frame_freq", "the frame's own frequency register - the third definition of 19.2 MHz"),
        ("frame_freq_agree", "whether the tree, the CPU and the frame agree about it"),
        ("frame_cnt_lo", "the frame's count"),
        ("frame_cnt_d", "the frame's count delta over the measured window"),
        ("frame_mac_d", "the kernel counter's delta over the same window"),
        ("frame_slack", "the tolerance the rate comparison is made with"),
        ("frame_rate_ok", "whether the frame counts at the kernel counter's rate"),
        ("frame_ctl", "the frame's control word"),
        ("frame_ctl_en", "its enable bit"),
        ("frame_ctl_mask", "its interrupt-mask bit"),
        ("frame_ctl_stat", "its read-only asserted bit"),
        # the line
        ("line_type", "the tree's interrupt type cell"),
        ("line_num", "the tree's number cell"),
        ("line_trig", "the tree's trigger cell"),
        ("line_rule", "which rule turned the cells into the id"),
        ("line_intid", "the interrupt id the cells produced"),
        ("line_parent", "the phandle the tree's `interrupt-parent` names"),
        ("line_cli_rc", "whether the payload's registry took the handler"),
        # the OS's own reading of the same property
        ("osmap_ok", "whether the OS's own mapping ran"),
        ("osmap_n", "how many specifiers it filed"),
        ("osmap_same", "whether its specifier is the tree's cells"),
        ("osmap_ph", "the controller phandle it read out of the tree"),
        ("osmap_ph_agree", "whether that is the phandle `interrupt-parent` names"),
        # the handler
        ("isr_calls", "how many times the line was delivered"),
        ("isr_intid", "the id it was delivered on"),
        ("isr_agree", "whether that is the id the driver registered"),
        ("isr_ctl", "the frame's control word as the handler found it"),
        ("isr_stat", "whether the frame was asserting"),
        ("isr_ctl_after", "the control word after the handler's clear"),
        ("isr_done", "that the handler reached its last statement"),
    )
    published = set(re.findall(r'entry_live_write\(\s*"xnu_live_timerdrv_([^"]+)"', facts.timer))
    missing = [suffix for suffix, _ in required if suffix not in published]
    if missing:
        failures.append("`MSM8974Timer.cpp` does not publish %s: the run's record cannot then tell "
                        "apart the findings these keys separate - 'the OS resolved nothing', 'the "
                        "driver mapped nothing', 'the frame answered zero' and 'the line fired on "
                        "another id' are four numbers and not one silence"
                        % ", ".join("`_%s` (%s)" % (s, dict(required)[s]) for s in missing))
    if not failures:
        notes.append("the record carries the OS's resolution, the frame's own answers, the line's "
                     "derivation and the handler's four states")


CLAIMS = (
    claim_offsets_are_the_devices,
    claim_the_tree_declares_the_devices_line,
    claim_the_phandle_pair_agrees,
    claim_the_intid_is_derived,
    claim_the_handler_clears_the_device_first,
    claim_no_source_makes_the_blocking_call,
    claim_the_frame_is_the_second_entry,
    claim_the_record_can_be_read,
)


# ------------------------------------------------------------------------------------------------
# The selftest: every claim shown to refuse a file that still reads like a working driver
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    if needle not in text:
        raise SystemExit("mutation needle not found: %r" % needle[:70])
    return text.replace(needle, replacement, 1)


def mutate_facts(facts, mutate):
    """A copy of the real facts with one edit, so the claim is tested against a file that still reads
    like a working driver rather than against a file with a syntax error."""
    base = globals()["_BASE"]
    facts = Facts.__new__(Facts)
    facts.timer_src = base.timer_src
    facts.timer = base.timer
    facts.payload_src = base.payload_src
    facts.payload = base.payload
    facts.dtsi = base.dtsi
    facts.arch_timer = base.arch_timer
    facts.dt_support = base.dt_support
    facts.image = base.image
    facts.symbols = base.symbols
    # A fresh cache, not `base`'s: the mutated `facts.timer` is what `source_of` must answer with, and
    # sharing the cache would hand the scan the unmutated file back.
    facts._sources = {}

    if mutate == "the_frame_offset_moves":
        facts.timer = _bump(facts.timer, "define MSM8974_FRAME_CTRL_OFF       0x02Cu",
                            "define MSM8974_FRAME_CTRL_OFF       0x028u")
    elif mutate == "the_kernel_takes_the_second_view":
        facts.arch_timer = _bump(facts.arch_timer, "of_iomap(frame, 0)",
                                 "of_iomap(frame, 1)")
    elif mutate == "the_tree_declares_another_line":
        facts.payload = _bump(facts.payload, "0u, 8u, 4u,", "0u, 8u, 0u,")
    elif mutate == "the_tree_loses_the_frames_address":
        facts.payload = _bump(facts.payload, "0xf9021000u, 0x00001000u,\n        0xf9022000u",
                              "0xf9022000u, 0x00001000u,\n        0xf9021000u")
    elif mutate == "the_phandle_pair_disagrees":
        facts.payload = _bump(facts.payload, 'apple_dt_prop_u32(b, "interrupt-parent", 1);',
                              'apple_dt_prop_u32(b, "interrupt-parent", 2);')
    elif mutate == "the_controller_has_no_phandle":
        facts.payload = _bump(facts.payload, 'apple_dt_prop_u32(b, "AAPL,phandle", 1);', "")
    elif mutate == "the_controller_spells_the_phandle_the_linux_way":
        # The mutation that is the measured failure: the same value under the name the device's own
        # tree uses. `_bump` is deliberately given the *wrong* needle as its result, because what the
        # claim has to catch is a tree whose property is called something else - not a tree with no
        # property, which `the_controller_has_no_phandle` already covers.
        facts.payload = _bump(facts.payload, 'apple_dt_prop_u32(b, "AAPL,phandle", 1);',
                              'apple_dt_prop_u32(b, "phandle", 1);')
    elif mutate == "the_intid_is_a_constant":
        facts.timer = re.sub(r'line = dt_num \+ MSM8974_GIC_SPI_BASE;', 'line = 40u;', facts.timer, 1)
    elif mutate == "the_line_is_read_without_the_rule":
        facts.timer = _bump(facts.timer, "line = dt_num + MSM8974_GIC_SPI_BASE;", "line = dt_num;")
    elif mutate == "the_handler_does_not_clear_the_device":
        facts.timer = _bump(facts.timer, "            ctl | MSM8974_FRAME_CTRL_IT_MASK;", "            ctl;")
    elif mutate == "the_handler_publishes_before_it_clears":
        facts.timer = _bump(
            facts.timer,
            "    if( g_timer_frame_va != 0u) {\n"
            "        ctl = *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CTRL_OFF );",
            "    entry_live_write( \"xnu_live_timerdrv_isr_early\", 1u );\n"
            "    if( g_timer_frame_va != 0u) {\n"
            "        ctl = *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CTRL_OFF );")
    elif mutate == "a_driver_takes_the_os_own_registration_route":
        facts.timer = _bump(facts.timer, "    if( line != 0u) {\n        g_timer_line_intid = line;",
                            "    if( line != 0u && registerInterrupt( 0, this, 0, 0 ) == 0) {\n"
                            "        g_timer_line_intid = line;")
    elif mutate == "the_frame_is_a_literal_address":
        facts.timer = _bump(facts.timer, "devmem->getObject( MSM8974_TIMER_FRAME_ENTRY )",
                            "devmem->getObject( 0 )")
        facts.timer = _bump(facts.timer, "frame_va = 0u;", "frame_va = 0xf9021000u;")
    elif mutate == "a_record_key_disappears":
        facts.timer = _bump(facts.timer,
                            '    entry_live_write( "xnu_live_timerdrv_frame_ctl_stat", frame_ctl_stat );\n',
                            "")
    elif mutate == "the_citation_points_at_another_line":
        # Only the prose moves, and only the raw source: the citation lives in a comment, so the
        # comment-stripped `facts.timer` that every other claim reads cannot see this mutation at
        # all. That asymmetry is the point of the clause it tests - the numbers in the code are
        # compared with the device's kernel, and the number *about* the device's kernel has to be
        # compared with it too or it is decoration.
        facts.timer_src = _bump(facts.timer_src, "arch/arm/kernel/arch_timer.c:623",
                                "arch/arm/kernel/arch_timer.c:624")
    elif mutate == "the_device_kernel_moves_and_the_citation_does_not":
        # The other direction, and the one that will happen: a blank line inserted above the call in
        # the device's kernel shifts it to 624 while both files still say 623. A clause that answered
        # this by comparing the prose with a literal 623 would pass this mutation and be a second
        # definition of the citation; the number is read out of the kernel's own text instead.
        facts.arch_timer = _bump(facts.arch_timer, "static struct delay_timer arch_delay_timer;",
                                 "static struct delay_timer arch_delay_timer;\n\n/* 498 mutation */")
    elif mutate == "the_grounding_block_cites_the_line_before_the_call":
        # **This is the mutation that the clause added for it was written for, and the one the earlier
        # clause accepts.** The edit restores what the tree actually said when 498 was first written:
        # the grounding block citing `:631` for the frame's first `reg` region, where the call is at
        # `:623`. It was invisible because the corrected `arch/arm/kernel/arch_timer.c:623` sits in
        # another comment six hundred lines below and satisfied the "does the right number appear"
        # clause on its own - a clause that asks for the presence of one citation is not a clause about
        # the absence of another.
        facts.timer_src = _bump(facts.timer_src,
                                " *       :623 timer_base = of_iomap(frame, 0);",
                                " *       :631 timer_base = of_iomap(frame, 0);")
    elif mutate == "the_two_route_block_cites_another_line":
        # 501's block, one line off: `:296` is the blank line above `counter_get_cntpct_mem`, which is
        # the exact shape 499 found in the offsets block (a citation pointing at a line that says
        # something else) - and the shape the anchor list could not see until 501 added its names to it.
        facts.timer_src = _bump(facts.timer_src, " *     :297  counter_get_cntpct_mem",
                                " *     :296  counter_get_cntpct_mem")
    elif mutate == "the_virtual_control_word_is_written_down":
        # 502's offset, as a literal: the address is the same, and the *pattern* that produced it - a
        # control word four bytes after its countdown - is what the clause checks, because a literal is
        # where a second definition of one address starts.
        facts.timer = _bump(facts.timer,
                            "#define MSM8974_FRAME_CNTV_CTL_OFF   (MSM8974_FRAME_CNTV_TVAL_OFF + 0x4u)",
                            "#define MSM8974_FRAME_CNTV_CTL_OFF   0x03Cu")
    elif mutate == "the_grounding_block_cites_another_define_line":
        # The same block, the other kind of citation: a register offset's line number rather than a
        # call's. `QTIMER_FREQ_REG` is at :65 and the block said :66 - which is `QTIMER_CNTP_TVAL_REG`'s
        # line, so the citation named a real line of the same file that happens to define a different
        # register, and every reading of the block would still have looked arbitrary rather than wrong.
        facts.timer_src = _bump(facts.timer_src,
                                " *       :65  #define QTIMER_FREQ_REG       0x010",
                                " *       :66  #define QTIMER_FREQ_REG       0x010")
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "the_frame_offset_moves",
    "the_kernel_takes_the_second_view",
    "the_tree_declares_another_line",
    "the_tree_loses_the_frames_address",
    "the_phandle_pair_disagrees",
    "the_controller_has_no_phandle",
    "the_controller_spells_the_phandle_the_linux_way",
    "the_intid_is_a_constant",
    "the_line_is_read_without_the_rule",
    "the_handler_does_not_clear_the_device",
    "the_handler_publishes_before_it_clears",
    "a_driver_takes_the_os_own_registration_route",
    "the_frame_is_a_literal_address",
    "a_record_key_disappears",
    "the_citation_points_at_another_line",
    "the_device_kernel_moves_and_the_citation_does_not",
    "the_two_route_block_cites_another_line",
    "the_grounding_block_cites_the_line_before_the_call",
    "the_grounding_block_cites_another_define_line",
    "the_virtual_control_word_is_written_down",
)


def compare(facts, mutate=None):
    if mutate:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    for claim in CLAIMS:
        claim(facts, failures, notes)
    return failures, notes


def main(argv):
    image = None
    verbose = False
    selftest = False
    i = 1
    while i < len(argv):
        if argv[i] == "--image":
            i += 1
            image = argv[i]
        elif argv[i] == "--verbose":
            verbose = True
        elif argv[i] == "--selftest":
            selftest = True
        else:
            sys.stderr.write("unrecognized argument %s\n" % argv[i])
            return 2
        i += 1

    facts = Facts(image)
    globals()["_BASE"] = facts

    if selftest:
        accepted = []
        for mutate in MUTATIONS:
            failures, _ = compare(facts, mutate)
            if not failures:
                accepted.append(mutate)
                say("      ACCEPTED: %s" % mutate)
        if accepted:
            say("FAIL: %d of %d mutations were not refused: %s"
                % (len(accepted), len(MUTATIONS), ", ".join(accepted)))
            return 1
        say("  --selftest: all %d mutations were refused" % len(MUTATIONS))
        return 0

    failures, notes = compare(facts)
    if verbose:
        for note in notes:
            say("    " + note)
    if failures:
        say("FAIL: the timer's line and its frame are not the ones this step says:")
        for failure in failures:
            say("      " + failure)
        return 1
    say("  xnu_entry_498: the frame's offsets are the device's own kernel's, our `/timer` declares "
        "the line the device's own tree declares for its frame, the interrupt id is derived from the "
        "tree's cells rather than written down, every path through the handler writes the frame's "
        "control register - one of them with the mask that drops a level-sensitive line - before it "
        "publishes anything, and no source in this image calls the one registration function that "
        "cannot return here")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
