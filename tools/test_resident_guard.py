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
    # 909 arm 3: the guard that decides whether a refused install falls back to the GIC's block.
    facts["desc_guard"] = function_body(facts["trace"], "wdt_desc_maps_the_block")
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
    # **A refused install must not be read through unless the slot's own descriptor vouches for it.**
    # Arm 2 measured the guard this replaced: `xnu_live_wdt_map=0x00000000` x4, because the install
    # refuses whenever the GIC already owns the 0xf90 megabyte - and the watchdog is *readable through
    # the GIC's block* at the same VA, so the pet simply returned and never fed. Arm 3 falls back to
    # that mapping, but only behind `wdt_desc_maps_the_block(slot_before)`: a 1 MB section whose PA base
    # is the watchdog's own megabyte. The clause checks the guard exists, is reached only when the
    # install refused (`mapped == 0u`), and that the pet still has a `return` bound.
    if "wdt_desc_maps_the_block(slot_before)" not in pet:
        failures.append("entry_wdt_pet's refused-install path does not consult "
                        "`wdt_desc_maps_the_block(slot_before)`: the pet would either read the watchdog "
                        "through an unvouched-for mapping (a fault) or never read it at all (arm 2's "
                        "dead pet, xnu_live_wdt_map=0 x4)")
        return
    if not re.search(r"mapped\s*!=\s*0u", pet):
        failures.append("entry_wdt_pet never tests `mapped != 0u`: the descriptor fallback must be "
                        "reached only on a *refused* install (`if (mapped != 0u) … else if (guard) … "
                        "else return;`), not on every call")
        return
    if not re.search(r"\breturn\s*;", pet):
        failures.append("entry_wdt_pet has no `return` bound: with neither its own install nor a "
                        "vouched-for descriptor, the pet must skip the read (the design's bounded cell)")
        return
    # **The refusal must be REACHABLE, and the outputs are what make it so.** `entry_section_install`
    # writes `*slot_before_out` before its refusal test (`entry_stubs.c:2121`), so a NULL there faults at
    # address 0 *before* the `mapped == 0u` cell above can ever be taken - which is what 909's first
    # press did. Both outputs must therefore be real addresses. Every other caller in the tree passes
    # locals (`entry_gic.c:393`, `entry_storage.c:5261/10306/10332`); the pet is the arm that made the
    # mistake, so the check is on the pet.
    call = re.search(r"entry_mmio_section\s*\(([^;]*)\)", pet)
    args = call.group(1).strip() if call else ""
    if not re.search(r"&[A-Za-z_]\w*\s*,\s*&[A-Za-z_]\w*$", args):
        failures.append("entry_wdt_pet passes something other than two `&`-outputs to "
                        "`entry_mmio_section` (the design relies on `, 0, 0)` in the wrong hands being "
                        "a store to address 0 that faults *before* the refusal can be observed - 909's "
                        "first press): the `slot_before_out`/`desc_out` arguments must be addresses of "
                        "locals in the pet's own frame")
        return
    notes.append("the pet installs 0xf9017000 once with two real outputs and returns before its first "
                 "read when the install is refused")


def claim_descriptor_guard_is_a_real_predicate(facts, failures, notes):
    """The arm-3 fallback rests on one predicate, so it is checked by value. It must accept ONLY a
    1 MB section whose PA base is the watchdog's own megabyte: a fault entry, a page-table pointer, or
    a different megabyte all have to refuse, because reading `0xf9017000` through any of them is a
    read of an address the pet cannot vouch for - the fault class `mi4-a-device-address-can-be-right-
    and-undereferenceable` (the `addr >> 20` check before dereferencing a new device register)."""
    guard = facts["desc_guard"]
    if guard is None:
        failures.append("entry_trace.c no longer defines `wdt_desc_maps_the_block`: the arm-3 fallback "
                        "reads the watchdog through the GIC's section with no predicate behind it")
        return
    if "STAGE90_WDT_TTE_TYPE" not in guard or "STAGE90_WDT_TTE_BLOCK" not in guard:
        failures.append("`wdt_desc_maps_the_block` does not test the L1 descriptor's type bits "
                        "(`STAGE90_WDT_TTE_TYPE == STAGE90_WDT_TTE_BLOCK`): a fault entry (0/1) or a "
                        "page-table pointer (1) would pass, and the read would fault")
        return
    if "STAGE90_WDT_BLOCK_MASK" not in guard or "STAGE90_WDT_BASE" not in guard:
        failures.append("`wdt_desc_maps_the_block` does not test the section's PA base against "
                        "`STAGE90_WDT_BASE & STAGE90_WDT_BLOCK_MASK`: a section mapping a *different* "
                        "megabyte would pass, and `0xf9017000` would resolve to something else")
        return
    # The two constants must be a 1 MB mask and the section type, by value.
    defs = facts["trace_defs"]
    if defs.get("STAGE90_WDT_BLOCK_MASK") != 0xFFF00000:
        failures.append("STAGE90_WDT_BLOCK_MASK is not 0xfff00000 (the 1 MB PA field): the section "
                        "base test would compare the wrong bits")
        return
    if defs.get("STAGE90_WDT_TTE_BLOCK") != 2:
        failures.append("STAGE90_WDT_TTE_BLOCK is not 2 (an ARMv7 L1 1 MB section descriptor): the "
                        "type test would accept the wrong descriptor kind")
        return
    notes.append("the arm-3 fallback reads the watchdog only through a 1 MB section whose PA base is "
                 "0xf9000000 (the GIC's own block), tested by value")


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
    # **The install's second argument register must be a frame address, not zero.** The linked body
    # loads `r1` with the VA and then calls `entry_mmio_section`; `r2` is `slot_before_out`. The first
    # press's pet had `mov r2, r3` (r3 = 0) there, and the install stored through it to address 0
    # *before* its refusal test, so the run could not observe the refusal at all. The reachable-refusal
    # form loads `r2` from `sp` (`sub`/`add rX, sp, #N` then `mov r2, rX`, or `mov r2, sp`); a
    # `mov r2, #0` / `mov r2, rY` where `rY` is a zeroed register is the fault this clause exists for.
    before_call = pet_body[:pet_body.find("<entry_mmio_section>")] \
        if "<entry_mmio_section>" in pet_body else ""
    # The instruction that loads `r2` for the call must take it from `sp`. Scanning for any
    # sp-relative instruction would pass on an unrelated temporary (the pet's own frame setup uses
    # `add r3, sp, #4`), so the check is bound to the last write to `r2`.
    r2_writes = re.findall(r"\b(mov|movw|add|sub|ldr)\s+r2,\s*([^\n;]+)", before_call)
    if not r2_writes or not re.match(r"sp\b", r2_writes[-1][1].strip()):
        failures.append("entry_wdt_pet's linked body never materialises a stack address before its "
                        "`bl entry_mmio_section`: `slot_before_out` (the second argument, `r2`) is not a "
                        "real output, so the install's unconditional `*slot_before_out` store faults at "
                        "address 0 before the refusal test - 909's first press (fault_addr=0x0, "
                        "r1=0xf9017000), which never published xnu_live_wdt_map")
        return
    notes.append("the linked image carries the pet: 8-byte wrapper frame, one tail branch to "
                 "entry_wdt_pet, the f9017000 base with the +4 store, the ubfx #1/#20 count, and a "
                 "stack-address output so the install's refusal is reachable")


def compare(facts, mutate=None):
    if mutate:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    claim_arm_is_defined(facts, failures, notes)
    claim_base_is_one_definition(facts, failures, notes)
    claim_pet_installs_before_it_reads(facts, failures, notes)
    claim_descriptor_guard_is_a_real_predicate(facts, failures, notes)
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
        facts["desc_guard"] = function_body(facts["trace"], "wdt_desc_maps_the_block")
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
                             "uint32_t mapped = entry_mmio_section(STAGE90_WDT_BASE, STAGE90_WDT_BASE,\n"
                             "                                             &slot_before, &desc);",
                             "uint32_t mapped = 1u;"))
    elif mutate == "install_after_the_first_read":
        block = ("        uint32_t mapped = entry_mmio_section(STAGE90_WDT_BASE, STAGE90_WDT_BASE,\n"
                 "                                             &slot_before, &desc);\n")
        gated = facts["gated"]
        assert gated.count(block) == 1, gated.count(block)
        moved = gated.replace(block, "", 1)
        anchor = "    count = wdt_count();\n"
        assert moved.count(anchor) == 1
        moved = moved.replace(anchor, anchor + block, 1)
        rederive_trace(facts["trace_text"].replace(gated, moved, 1))
    elif mutate == "refused_install_does_not_return":
        rederive_trace(_bump(facts["trace_text"],
                             "        else\n"
                             "            return;     /* no mapping this arm can vouch for: stay "
                             "bounded, read nothing */\n", ""))
    elif mutate == "the_install_outputs_are_null":
        # 909's first press: `entry_mmio_section(..., 0, 0)` makes the install's unconditional
        # `*slot_before_out` a store to address 0, so the run faults before it can observe the refusal.
        rederive_trace(_bump(facts["trace_text"],
                             "&slot_before, &desc);", "0, 0);"))
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
    elif mutate == "the_install_output_is_not_a_stack_address":
        # The linked form of 909's fault: the pet's second argument (`r2`, `slot_before_out`) is a zero
        # register rather than a frame address, so the install stores through it to address 0.
        facts["pet_body"] = facts["pet_body"].replace("mov\tr2, sp", "mov\tr2, r5")
    elif mutate == "the_fallback_guard_is_dropped":
        # Arm 3's fallback: without the predicate, the pet reads the watchdog through whatever the
        # slot holds - the arm-2 dead pet's opposite failure (a fault instead of a skip).
        rederive_trace(_bump(facts["trace_text"],
                             "else if (wdt_desc_maps_the_block(slot_before))\n"
                             "            g_wdt_via = STAGE90_WDT_VIA_GIC;\n"
                             "        else\n            return;",
                             "else\n            g_wdt_via = STAGE90_WDT_VIA_GIC;"))
    elif mutate == "the_fallback_guard_drops_the_type_test":
        rederive_trace(_bump(facts["trace_text"],
                             "((desc & STAGE90_WDT_TTE_TYPE) == STAGE90_WDT_TTE_BLOCK) &&",
                             "((desc & STAGE90_WDT_TTE_TYPE) != 0xffffffffu) &&"))
    elif mutate == "the_fallback_guard_drops_the_base_test":
        rederive_trace(_bump(facts["trace_text"],
                             "((desc & STAGE90_WDT_BLOCK_MASK) == (STAGE90_WDT_BASE & STAGE90_WDT_BLOCK_MASK))",
                             "1u"))
    elif mutate == "the_fallback_guard_uses_the_wrong_type":
        # A page-table pointer (type 1) would then be accepted as a 1 MB section.
        facts["trace_defs"]["STAGE90_WDT_TTE_BLOCK"] = 1
    elif mutate == "the_fallback_guard_uses_a_wrong_mask":
        facts["trace_defs"]["STAGE90_WDT_BLOCK_MASK"] = 0xFFC00000
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "base_moved", "reg_rst_moved", "no_install", "install_after_the_first_read",
    "refused_install_does_not_return", "the_install_outputs_are_null",
    "count_shift_wrong", "threshold_is_not_half",
    "store_targets_sts", "the_call_is_not_last", "the_call_is_removed", "the_pet_is_not_gated",
    "the_fallback_is_not_zero", "a_refusal_is_dropped", "the_arm_key_is_dropped",
    "the_record_writer_is_dropped", "the_pet_is_not_in_the_image",
    "the_wrapper_tail_branches_elsewhere", "the_wrapper_frame_is_16", "the_store_is_not_at_plus_4",
    "the_count_is_not_the_vendors_field", "the_install_output_is_not_a_stack_address",
    "the_fallback_guard_is_dropped", "the_fallback_guard_drops_the_type_test",
    "the_fallback_guard_drops_the_base_test", "the_fallback_guard_uses_the_wrong_type",
    "the_fallback_guard_uses_a_wrong_mask",
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