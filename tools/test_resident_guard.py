#!/usr/bin/env python3
"""Check the residence rung's pet against the image and the tree, and refuse each way it can be wrong.

    tools/test_resident_guard.py --image out/stage90/xnu_arm_entry.elf --verbose
    tools/test_resident_guard.py --image out/stage90/xnu_arm_entry.elf --selftest

**909 takes the image's own ending OUT and, in its place, feeds the SoC watchdog. Both halves are a
property of the linked bytes, and neither is a comment.** The ending's removal is an *arm* property
(`STAGE90_XNU_RESIDENT=1` requires `POST_END_TICKS=0`, `POST_END_RUN=0`, `SEAM_END_RUN=0`) and is
checked in `build_entry.sh` where the arm is resolved; what is left for this check is the pet.

The pet has one job and four ways to do it wrong, each with a failure mode this project has already
paid for:

1. **The base and its offsets are one definition, not two.** `hw_watchdog.c` is the file that arms the
   net (`MSM8974_WDT_BASE`, `REG_RST`=0x04, `REG_STS`=0x0c, `REG_BARK`=0x10); the pet re-states them in
   `entry_trace.c`. Two spellings of one register is [[mi4-one-value-two-definitions]] with a store to
   the wrong word as the failure mode - and a pet to the wrong word is *silent*. So the two files'
   values are compared here, and the linked store is read **by value** ([[mi4-linked-code-order-is-not
   -source-order]]).
2. **The section is installed before the first load, and a refused install skips the pet.** 908 proved
   the *mechanism* (`entry_mmio_section` installed and read the eMMC controller post-jump), not this
   *address*; a pet that reads `0xf9017000` through an install that did not land faults at the pet -
   which is readable, but only if the code cannot reach the read when the install was refused. So
   `entry_mmio_section(...)` must precede the first `wdt_read`, and a `mapped == 0` must return.
3. **The count is the vendor's encoding.** `(sts >> 1) & 0xfffff` is the live count the driver already
   uses; a pet threshold computed on the raw `sts` would compare against the wrong field.
4. **The pet is the wrapper's tail, once.** The wrapper's `sp` **is** the exit's frame slot
   ([[mi4-idle-exit-l2-line]]), so the call is a single tail call and the wrapper's frame stays 8 bytes;
   the pet's state lives in `entry_wdt_pet`'s own frame. A second call, or a call that is not last,
   would put a live register in the slot.

The check reads the source for the tree claims and the linked image for the artifact claims, and its
`--selftest` mutates each in turn and requires a refusal - because a claim in a comment is not a check
([[mi4-a-claim-in-a-comment-is-not-a-check]]).
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

ENTRY_TRACE_C = os.path.join(REPO_ROOT, "src/entry/entry_trace.c")
HW_WATCHDOG_C = os.path.join(REPO_ROOT, "src/hw_watchdog.c")
BUILD_ENTRY = os.path.join(REPO_ROOT, "src/entry/build_entry.sh")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    """A claim satisfied by a comment is not a claim (this project's 482 defect 217). Every source
    test below runs on comment-stripped text, so prose in the pet's header block cannot stand in for
    the code it describes."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def defines(text):
    """`#define NAME value`, with the `[ \\t]+` discipline 482 had to learn: a `\\s+` here can cross a
    newline and read the *next* `#define` as the value."""
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
    """name -> (address, type-letter). The type letter matters: a pet that linked as an undefined `U`
    moves no store."""
    out = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            out[parts[2]] = (int(parts[0], 16), parts[1])
    return out


def symbols_with_size(path):
    """name -> (address, size); objdump's `-S` sizes bound a function body to its own object."""
    out = {}
    for line in run([NM, "-S", "-n", path]).splitlines():
        parts = line.split()
        if len(parts) == 4:
            try:
                out[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
            except ValueError:
                continue
    return out


def body_of(image, name):
    """The disassembly of a function, bounded by its own nm size. `b`/`bl` targets are resolved from
    the same symbol table, so a caller can be asked *which* function a branch reaches."""
    syms = symbols_with_size(image)
    if name not in syms:
        return None
    addr, size = syms[name]
    text = run([OBJDUMP, "-d", "--start-address=0x%x" % addr,
                "--stop-address=0x%x" % (addr + size), image])
    return text


def function_body(text, name):
    """The braces-balanced body of `name`. Balanced rather than `.*?\\n\\}`, because a one-line body
    puts its closing brace on the same line and an extractor that returned `None` would make every
    claim about it vacuously true. Function attributes (`__attribute__((noinline))`) are removed first:
    their parentheses would otherwise break the `name(` match and silently return `None`."""
    text = re.sub(r"__attribute__\s*\(\([^)]*\)\)\s*", "", text)
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


def guarded_region(text, macro):
    """The text from `#if <macro>` to its `#endif`, for the one claim that is about a conditional.
    The `#endif` carries a trailing comment naming the macro in this file, which is what lets the
    region be bounded without a preprocessor."""
    match = re.search(r"^#if[ \t]+%s[ \t]*$(.*?)^#endif[ \t]*/\*[ \t]*%s[ \t]*\*/"
                      % (re.escape(macro), re.escape(macro)), text, re.M | re.S)
    return match.group(1) if match else None


# ------------------------------------------------------------------------------------------------
# Facts
# ------------------------------------------------------------------------------------------------

def gather(image):
    facts = {
        "trace_text": read(ENTRY_TRACE_C),
        "wdt_text": read(HW_WATCHDOG_C),
        "build_text": read(BUILD_ENTRY),
        "image": image,
    }
    facts["trace"] = strip_comments(facts["trace_text"])
    facts["wdt"] = strip_comments(facts["wdt_text"])
    facts["build"] = strip_comments(facts["build_text"])
    facts["trace_defs"] = defines(facts["trace_text"])
    facts["wdt_defs"] = defines(facts["wdt_text"])
    facts["pet"] = function_body(facts["trace"], "entry_wdt_pet")
    facts["count_fn"] = function_body(facts["trace"], "wdt_count")
    facts["wrapper"] = function_body(facts["trace"], "__wrap_platform_cache_idle_exit")
    # On the RAW text: the region's `#endif` names the macro in a trailing comment, and comment-stripping
    # would delete the very anchor that bounds the region.
    facts["gated"] = guarded_region(facts["trace_text"], "STAGE90_XNU_RESIDENT")
    facts["symbols"] = nm(image) if image else {}
    facts["pet_body"] = body_of(image, "entry_wdt_pet") if image else None
    facts["wrap_body"] = body_of(image, "__wrap_platform_cache_idle_exit") if image else None
    return facts


# ------------------------------------------------------------------------------------------------
# Claims
# ------------------------------------------------------------------------------------------------

def claim_arm_is_defined(facts, failures, notes):
    if "STAGE90_XNU_RESIDENT" not in facts["trace_defs"]:
        failures.append("entry_trace.c does not define STAGE90_XNU_RESIDENT (the `#ifndef`/`#define 0` "
                        "fallback): a build with the macro unset would compile the pet out under "
                        "`#if STAGE90_XNU_RESIDENT` and the arm would be silently a no-op")
        return
    if facts["trace_defs"]["STAGE90_XNU_RESIDENT"] != 0:
        failures.append("entry_trace.c's `#define STAGE90_XNU_RESIDENT` fallback is not 0: the pet's "
                        "default must be off, or every arm inherits it")
        return
    notes.append("STAGE90_XNU_RESIDENT default 0 (the fallback define)")


def claim_base_is_one_definition(facts, failures, notes):
    """The pet's base and offsets equal the arming file's, by value. A second spelling of a register
    is the defect class this project repeats most, and here its failure mode is a store to a word
    nothing reads - a pet that never pets and never complains."""
    pairs = [("STAGE90_WDT_BASE", "MSM8974_WDT_BASE"),
             ("STAGE90_WDT_REG_RST", "MSM8974_WDT_REG_RST"),
             ("STAGE90_WDT_REG_STS", "MSM8974_WDT_REG_STS"),
             ("STAGE90_WDT_BARK", "MSM8974_WDT_REG_BARK")]
    for mine, theirs in pairs:
        if mine not in facts["trace_defs"]:
            failures.append("entry_trace.c does not define %s: the pet's register address has no name "
                            "in the file that uses it" % mine)
            continue
        if theirs not in facts["wdt_defs"]:
            failures.append("hw_watchdog.c no longer defines %s: the register the pet must agree with "
                            "moved or was renamed" % theirs)
            continue
        if facts["trace_defs"][mine] != facts["wdt_defs"][theirs]:
            failures.append("entry_trace.c's %s (0x%x) != hw_watchdog.c's %s (0x%x): the pet and the "
                            "arm disagree about one register, so one of them writes a word the other "
                            "does not read"
                            % (mine, facts["trace_defs"][mine], theirs, facts["wdt_defs"][theirs]))
            continue
    notes.append("the pet's base 0x%x and its RST/STS/BARK offsets are hw_watchdog.c's own values"
                 % facts["trace_defs"].get("STAGE90_WDT_BASE", 0))


def claim_pet_installs_before_it_reads(facts, failures, notes):
    body = facts["gated"]
    if body is None or facts["pet"] is None:
        failures.append("entry_trace.c has no `entry_wdt_pet` inside an `#if STAGE90_XNU_RESIDENT` "
                        "block: the pet this arm turns on does not exist, or is not gated")
        return
    pet = facts["pet"]
    install_at = pet.find("entry_mmio_section(")
    first_read_at = pet.find("wdt_read(")
    if install_at < 0:
        failures.append("entry_wdt_pet never calls entry_mmio_section: the watchdog's section is not "
                        "installed, so the first load of 0xf9017000 is through whatever the post-jump "
                        "tables happen to hold - and a wrong mapping here is a fault at the pet")
        return
    if first_read_at >= 0 and first_read_at < install_at:
        failures.append("entry_wdt_pet reads a watchdog register before it installs the section: the "
                        "load is taken through a mapping this arm has not established")
        return
    if not re.search(r"if\s*\(\s*mapped\s*==\s*0u\s*\)\s*return\s*;", pet):
        failures.append("entry_wdt_pet does not return after a refused `entry_mmio_section` (no "
                        "`if (mapped == 0u) return;`): a pet would then fault through an install that "
                        "did not land, which is the one cell the design says must stay bounded")
        return
    notes.append("the pet installs 0xf9017000 once and returns before its first read when the install "
                 "is refused")


def claim_count_is_the_vendors_encoding(facts, failures, notes):
    body = facts["count_fn"]
    if body is None:
        failures.append("entry_trace.c no longer defines wdt_count: the count the pet thresholds on "
                        "has no single definition")
        return
    if not re.search(r">>\s*1\s*\)\s*&\s*0xfffff", body):
        failures.append("wdt_count does not compute `(sts >> 1) & 0xfffff`, the vendor driver's own "
                        "live-count encoding (hw_watchdog.c): a threshold on another field would pet "
                        "at the wrong moment")
        return
    notes.append("the count is `(sts >> 1) & 0xfffff`, hw_watchdog.c's own hw_wdt_countdown encoding")


def claim_pet_threshold_and_store(facts, failures, notes):
    if facts["pet"] is None:
        return
    pet = facts["pet"]
    if "g_wdt_bark_ticks / 2u" not in pet:
        failures.append("entry_wdt_pet does not threshold at half the bark (`g_wdt_bark_ticks / 2u`): "
                        "the pet's moment has no reading behind it")
        return
    if "(STAGE90_WDT_BASE + STAGE90_WDT_REG_RST) = 1u" not in pet:
        failures.append("entry_wdt_pet's pet store does not target STAGE90_WDT_BASE + "
                        "STAGE90_WDT_REG_RST: the store must be the reset word by name, so the clause "
                        "that reads the linked value has a source to bind to")
        return
    notes.append("the pet thresholds at half the bark and stores 1 to base+RST")


def claim_call_is_the_wrapper_tail(facts, failures, notes):
    wrapper = facts["wrapper"]
    if wrapper is None:
        failures.append("entry_trace.c has no __wrap_platform_cache_idle_exit: the site the pet is "
                        "called from does not exist")
        return
    if wrapper.count("entry_wdt_pet(") != 1:
        failures.append("__wrap_platform_cache_idle_exit calls entry_wdt_pet %d time(s), not once: the "
                        "wrapper's `sp` is the exit's frame slot, so the pet must be a single call"
                        % wrapper.count("entry_wdt_pet("))
        return
    tail = re.sub(r"#\s*(?:if|ifdef|ifndef|else|endif)[^\n]*\n", "", wrapper)
    tail = strip_comments(tail)
    if not re.search(r"entry_wdt_pet\([^;]*\)\s*;\s*\}\s*$", tail.strip()):
        failures.append("entry_wdt_pet is not the LAST statement of "
                        "__wrap_platform_cache_idle_exit: anything the wrapper does after it would "
                        "need the slot's registers, which is the frame 690 forbids")
        return
    notes.append("the pet is the wrapper's tail call, exactly once")


def claim_build_carries_the_switch_and_refusals(facts, failures, notes):
    build = facts["build"]
    if not re.search(r"^\s*STAGE90_XNU_RESIDENT\s*$", build, re.M):
        failures.append("build_entry.sh's ENTRY_ARM_KEYS does not list STAGE90_XNU_RESIDENT: the arm "
                        "would not be recorded, so an image could not say whether it is the residence "
                        "arm")
        return
    if "STAGE90_XNU_RESIDENT=$RESIDENT" not in build:
        failures.append("build_entry.sh's record writer does not echo STAGE90_XNU_RESIDENT=$RESIDENT: "
                        "the arm key would be required but never written")
        return
    needles = {
        "POST_END_TICKS": r"STAGE90_XNU_RESIDENT=1 with STAGE90_XNU_POST_END_TICKS",
        "POST_END_RUN": r"STAGE90_XNU_RESIDENT=1 with STAGE90_XNU_POST_END_RUN",
        "SEAM_END_RUN": r"STAGE90_XNU_RESIDENT=1 with STAGE90_XNU_SEAM_END_RUN",
        "the watchdog armed": r"STAGE90_XNU_RESIDENT=1 with STAGE90_HW_WATCHDOG",
    }
    missing = [name for name, pattern in needles.items() if not re.search(pattern, build)]
    if missing:
        failures.append("build_entry.sh is missing the RESIDENT refusal(s) for: %s - a residence arm "
                        "that still carries the ending, or runs with no watchdog net, would be an "
                        "image whose record names a residence it is not" % ", ".join(missing))
        return
    notes.append("build_entry.sh carries the RESIDENT arm key, its record writer, and all four refusals")


def claim_linked_image(facts, failures, notes):
    if not facts["image"]:
        notes.append("no --image: the linked-image claims were not evaluated")
        return
    sym = facts["symbols"].get("entry_wdt_pet")
    if sym is None or sym[1].lower() != "t":
        failures.append("entry_wdt_pet is not a defined (t) symbol in the linked image: the pet linked "
                        "as an undefined reference and moves no store")
        return
    pet_body = facts["pet_body"]
    if not pet_body:
        failures.append("entry_wdt_pet has no body in the linked image")
        return
    # The store is read BY VALUE and BOUND to the base register: 0xf9017000 is materialised as
    # `movt rX, #0xf901` (63745) plus a low word of 0x7000 (28672, spelled `mov` or `movw`), and the
    # store's base must be one of those registers. A store to [rY, #4] through a register that was
    # never loaded with this base is not a pet to the watchdog.
    base_regs = set(re.findall(r"movt\s+(r\d+), #63745", pet_body))
    has_base = bool(base_regs) and re.search(r"mov[w]?\s+r\d+, #28672", pet_body)
    store4 = any(re.search(r"str\s+r\d+, \[%s, #4\]" % re.escape(reg), pet_body) for reg in base_regs)
    if not has_base or not store4:
        failures.append("entry_wdt_pet's linked body has no store to [base+4] through a register loaded "
                        "with 0xf9017000: the pet's one device write is not in the image, whatever the "
                        "source says")
        return
    if not re.search(r"ubfx\s+r\d+, r\d+, #1, #20", pet_body):
        failures.append("entry_wdt_pet's linked body does not extract `(sts >> 1) & 0xfffff` (no "
                        "`ubfx rX, rY, #1, #20`): the count read in the image is not the vendor's field")
        return
    wrap_body = facts["wrap_body"]
    if not wrap_body:
        failures.append("__wrap_platform_cache_idle_exit has no body in the linked image")
        return
    # The wrapper's frame stays 8 bytes: `str r4, [sp, #-8]!` / `add sp, sp, #8`.
    if not re.search(r"str\s+r\d+, \[sp, #-8\]!", wrap_body):
        failures.append("__wrap_platform_cache_idle_exit's linked frame is not 8 bytes: the slot the "
                        "exit's push/pop read is at the wrong address (mi4-idle-exit-l2-line)")
        return
    if not re.search(r"b\s+[0-9a-f]+ <entry_wdt_pet>", wrap_body):
        failures.append("__wrap_platform_cache_idle_exit does not tail-branch to entry_wdt_pet in the "
                        "linked image: the pet is not reached from the idle-exit pass")
        return
    notes.append("the linked image carries the pet: 8-byte wrapper frame, one tail branch to "
                 "entry_wdt_pet, the f9017000 base with the +4 store, and the ubfx #1/#20 count")


def compare(facts, mutate=None):
    if mutate:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    claim_arm_is_defined(facts, failures, notes)
    claim_base_is_one_definition(facts, failures, notes)
    claim_pet_installs_before_it_reads(facts, failures, notes)
    claim_count_is_the_vendors_encoding(facts, failures, notes)
    claim_pet_threshold_and_store(facts, failures, notes)
    claim_call_is_the_wrapper_tail(facts, failures, notes)
    claim_build_carries_the_switch_and_refusals(facts, failures, notes)
    claim_linked_image(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert needle in text, needle
    return text.replace(needle, replacement, 1)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["trace_defs"] = dict(facts["trace_defs"])
    facts["symbols"] = dict(facts["symbols"])

    def rederive_trace(text):
        facts["trace_text"] = text
        facts["trace"] = strip_comments(text)
        facts["trace_defs"] = defines(text)
        facts["pet"] = function_body(facts["trace"], "entry_wdt_pet")
        facts["count_fn"] = function_body(facts["trace"], "wdt_count")
        facts["wrapper"] = function_body(facts["trace"], "__wrap_platform_cache_idle_exit")
        facts["gated"] = guarded_region(facts["trace"], "STAGE90_XNU_RESIDENT")

    def rederive_build(text):
        facts["build_text"] = text
        facts["build"] = strip_comments(text)

    if mutate == "base_moved":
        facts["trace_defs"]["STAGE90_WDT_BASE"] = 0xF9000000
    elif mutate == "reg_rst_moved":
        facts["trace_defs"]["STAGE90_WDT_REG_RST"] = 0x08
    elif mutate == "no_install":
        rederive_trace(_bump(facts["trace_text"],
                             "uint32_t mapped = entry_mmio_section(STAGE90_WDT_BASE, STAGE90_WDT_BASE, 0, 0);",
                             "uint32_t mapped = 1u;"))
    elif mutate == "install_after_the_first_read":
        block = ("        uint32_t mapped = entry_mmio_section(STAGE90_WDT_BASE, "
                 "STAGE90_WDT_BASE, 0, 0);\n")
        gated = facts["gated"]
        assert gated.count(block) == 1, gated.count(block)
        moved = gated.replace(block, "", 1)
        anchor = "    count = wdt_count();\n"
        assert moved.count(anchor) == 1
        moved = moved.replace(anchor, anchor + block, 1)
        rederive_trace(facts["trace_text"].replace(gated, moved, 1))
    elif mutate == "refused_install_does_not_return":
        rederive_trace(_bump(facts["trace_text"],
                             "        if (mapped == 0u)\n            return;\n", ""))
    elif mutate == "count_shift_wrong":
        rederive_trace(_bump(facts["trace_text"],
                             "(wdt_read(STAGE90_WDT_REG_STS) >> 1) & 0xfffffu",
                             "(wdt_read(STAGE90_WDT_REG_STS) >> 2) & 0xfffffu"))
    elif mutate == "threshold_is_not_half":
        rederive_trace(_bump(facts["trace_text"], "g_wdt_bark_ticks / 2u", "g_wdt_bark_ticks / 4u"))
    elif mutate == "store_targets_sts":
        rederive_trace(_bump(facts["trace_text"],
                             "(STAGE90_WDT_BASE + STAGE90_WDT_REG_RST) = 1u",
                             "(STAGE90_WDT_BASE + STAGE90_WDT_REG_STS) = 1u"))
    elif mutate == "the_call_is_not_last":
        rederive_trace(_bump(facts["trace_text"],
                             "    entry_wdt_pet(g_slot_post.calls);\n#endif\n}",
                             "    entry_wdt_pet(g_slot_post.calls);\n#endif\n    g_slot_post.calls += 0;\n}"))
    elif mutate == "the_call_is_removed":
        rederive_trace(_bump(facts["trace_text"], "    entry_wdt_pet(g_slot_post.calls);\n", ""))
    elif mutate == "the_pet_is_not_gated":
        rederive_trace(_bump(facts["trace_text"], "#if STAGE90_XNU_RESIDENT\n/*\n * **909:",
                             "#if 1\n/*\n * **909:"))
    elif mutate == "the_fallback_is_not_zero":
        rederive_trace(_bump(facts["trace_text"], "#define STAGE90_XNU_RESIDENT 0",
                             "#define STAGE90_XNU_RESIDENT 1"))
    elif mutate == "a_refusal_is_dropped":
        rederive_build(_bump(facts["build_text"],
                             "REFUSING: STAGE90_XNU_RESIDENT=1 with STAGE90_XNU_POST_END_TICKS",
                             "accepting POST_END_TICKS anyway"))
    elif mutate == "the_arm_key_is_dropped":
        rederive_build(_bump(facts["build_text"], "                STAGE90_XNU_RESIDENT\n", ""))
    elif mutate == "the_record_writer_is_dropped":
        rederive_build(_bump(facts["build_text"], 'echo "STAGE90_XNU_RESIDENT=$RESIDENT"', "true"))
    elif mutate == "the_pet_is_not_in_the_image":
        facts["symbols"].pop("entry_wdt_pet", None)
    elif mutate == "the_wrapper_tail_branches_elsewhere":
        facts["wrap_body"] = facts["wrap_body"].replace("<entry_wdt_pet>", "<entry_post_clock>")
    elif mutate == "the_wrapper_frame_is_16":
        facts["wrap_body"] = facts["wrap_body"].replace("[sp, #-8]!", "[sp, #-16]!")
    elif mutate == "the_store_is_not_at_plus_4":
        facts["pet_body"] = facts["pet_body"].replace("[r3, #4]", "[r3, #12]")
    elif mutate == "the_count_is_not_the_vendors_field":
        # The count is taken in wdt_count, which the linker inlines into the pet: it is the ONLY
        # `ubfx ..., #1, #20` in the body (the others are `#0, #20` on the bark and would be match
        # noise). Widening or clearing the shift is the mutation.
        facts["pet_body"] = facts["pet_body"].replace("#1, #20", "#2, #20")
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "base_moved", "reg_rst_moved", "no_install", "install_after_the_first_read",
    "refused_install_does_not_return", "count_shift_wrong", "threshold_is_not_half",
    "store_targets_sts", "the_call_is_not_last", "the_call_is_removed", "the_pet_is_not_gated",
    "the_fallback_is_not_zero", "a_refusal_is_dropped", "the_arm_key_is_dropped",
    "the_record_writer_is_dropped", "the_pet_is_not_in_the_image",
    "the_wrapper_tail_branches_elsewhere", "the_wrapper_frame_is_16", "the_store_is_not_at_plus_4",
    "the_count_is_not_the_vendors_field",
)


def selftest(facts):
    accepted = []
    for name in MUTATIONS:
        if not facts["image"] and name.startswith("the_"):
            continue
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
        print("FAIL: --image is required: the pet's base, its store and the wrapper's frame are claims "
              "about the linked bytes, and there is no default that can stand in for them",
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
        print("FAIL: the resident arm's pet is not the pet this image has:", file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    say("  xnu_entry_909: the residence rung's pet is in the tree and in the linked image - "
        "entry_wdt_pet installs the watchdog's 0x%x section with entry_mmio_section and returns "
        "before its first read when the install is refused, thresholds at half the bark on the "
        "vendor's own `(sts >> 1) & 0xfffff` count, stores 1 to base+RST, and is the wrapper's single "
        "tail call with the frame still 8 bytes"
        % facts["trace_defs"].get("STAGE90_WDT_BASE", 0))
    return 0


if __name__ == "__main__":
    sys.exit(main())