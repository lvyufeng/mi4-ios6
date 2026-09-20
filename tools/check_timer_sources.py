#!/usr/bin/env python3
"""
Check the seven things 484's timer census rests on, before the run that reads it.

483 gave the decrementer an owner and measured that it *runs*: `setPop` was called 4096 times and the
armed value settled at `0x4ab8` - one millisecond at this machine's 19.2 MHz - for everything after the
22nd call. What 483 could not say is *who* asked for it, and that is the whole of 484: `setPop`
(`osfmk/arm/rtclock.c:344`) is handed an absolute time and not its provenance, and the same millisecond
means opposite things depending on which of the three fields in `struct cpu_data` it came out of -
`quantum_timer_deadline` is the scheduler's preemption timer, which a compute-bound thread re-arms on
every quantum expiry and which nothing *waits* on, while the other two are deadline requests that
something is waiting for. The instrument added by this step wraps the three entry points this kernel reaches that write
those fields, publishes the *callback* each timer was set up with so that the `call` pointers in the log can be
resolved to names, and completes 483's census so that the range of armed values is bounded at both ends
by what actually happened rather than by what a minimum can be read as.

  1. **the three source numbers are the three wrappers, one each.** `entry_trace.c`'s `__wrap_` bodies
     pass `1`, `2` and `3`, and the set they pass is exactly that - not "the numbers are distinct" (two
     wrappers passing 1 and 2 with a third passing 1 would pass that) but the identity of each wrapper
     against the number `entry_timebase.c` documents for it. `STAGE90_TMR_SRC_MAX` must be *greater*
     than the largest source, because the note function's range guard is what silently drops an arming
     whose number it does not know.

  2. **the prototypes are transcriptions of Apple's, and `uint64_t` is where Apple puts it.** None of
     these files can include `osfmk/kern/timer_call.h`, so every prototype in `entry_trace.c` is a claim
     about the declaration in the header: the same argument *count*, and - the one that breaks a run
     rather than a compile - every argument Apple types `uint64_t` typed `uint64_t` here. On AAPCS a
     `uint64_t` takes an even-numbered register *pair*, so a prototype that spelled `deadline` as
     `uint32_t` would shift every argument after it by one, and the log would carry a `flags` word read
     out of the middle of a deadline. `__wrap_mdevadd`'s comment in `entry_trace.c` records the same
     hazard costing this project a run once already. The header's own `timer_call_param_t` is checked to
     still be `void *`, because the one-word spelling of it is what the prototypes depend on.

  3. **the wrapped names are defined in the image and each wrapper is a different address.** A `--wrap`
     whose name the generator had stubbed - rather than linked from XNU's own objects - would have
     `__wrap_X` calling the stand-in instead of the function, and the census would be a census of a
     stub; and a `__wrap_X` at the *same address* as `X` is the same-object case, where the wrapper is
     the function it wraps.

  4. **`thread_quantum_expire`'s wrapper is live, and that is a property of the link.** Its only
     reference in Apple's tree is the *address* handed to `timer_call_setup` (`processor.c:163`) - a
     function pointer, not a call - so whether `--wrap` reaches it cannot be argued from the source. The
     claim is read out of the linked image instead, and the *form* of the reference decides how: this
     image materialises that argument as a `movw`/`movt` pair four instructions before the `bl`, so the
     address exists as two half-word immediates and not as a word anywhere - this check's first version
     scanned the sections for an aligned 32-bit word, found neither address, and would have reported a
     live wrapper as dead. So the pairs are reconstructed from the disassembly *and* the data sections
     are scanned, and the wrapper's address must be in that union while the unwrapped function's must
     not be.

  5. **the census is bounded, and what bounds it is published rather than dropped.** The first
     `STAGE90_TMR_SETUP_SHOWN` `timer_call_setup` calls are published *unconditionally* - a sampled
     table is a table with holes in it, and an unresolvable `call` pointer says nothing - while the
     arming census publishes its first `STAGE90_TMR_ENTER_SHOWN` events and then counts, so eight
     million `getpid` calls cannot turn one instrument into a log nobody can read. Every path that drops
     a record increments a counter that is published, and `g_dec_distinct`'s table is bounded by
     `STAGE90_DEC_SHOWN` with the values past it counted separately.

  6. **the wraps are in the build's list, in the right list, and the wrapper calls the real function.**
     `build_entry.sh`'s `TRACE_LDFLAGS` names all five, `PASS1_LDFLAGS` does not (its two names are the
     ones whose `__wrap_` lives in a pre-pass-1 object, and these five live in `entry_trace.o`, which
     pass 1 does not link), and each wrapper has a `__real_` declaration to call rather than a second
     implementation of the function.

  7. **the family's fourth member is *not* wrapped, and it must stay uncalled.** `timer_call_enter1` is
     declared beside the other three in `osfmk/kern/timer_call.h` and this step does not wrap it, because
     this step's first build wrote the wrapper, the linker found no branch to it anywhere, and
     `build_entry.sh`'s reachability check refused the image. The reason is a fact about the build rather
     than about the file - `osfmk/kern/sfi.c` and `bsd/dev/dtrace/dtrace_glue.c` are its only callers in
     the tree, and neither reaches it here - so the claim is read out of the *object pool*: exactly one
     object among the hundreds the entry image draws from may reference the name at all, and it must be
     the one that defines it. A later step that makes SFI or dtrace live here turns this into a failure
     that names the wrapper to add, which is the event the omission would need reviewing at.

    ./tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest

What this check cannot say
--------------------------
Which source the millisecond actually comes from. That is the run's, and it is the run's *because* none
of the above can be read off the value: the claim that the census decides - "one number armed on every
call after the first, or more than one" - is `xnu_live_dec_same`/`xnu_live_dec_other` against
`xnu_live_dec_new_value`/`xnu_live_dec_new_call` in the log, and the identity of the arm-er is the join
of `xnu_live_tmr_enter_call` against the `xnu_live_tmr_setup_call`/`_func` pairs. Nor can this check say
that the instrument does not perturb what it measures: the wrappers are real calls on the arming paths,
and the run's own evidence that it did not is the console block and the block census staying
byte-identical to 483's.
"""

import argparse
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

BOOT_DIR = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot")
ENTRY_TRACE_C = os.path.join(BOOT_DIR, "entry_trace.c")
ENTRY_TIMEBASE_C = os.path.join(BOOT_DIR, "entry_timebase.c")
BUILD_ENTRY_SH = os.path.join(BOOT_DIR, "build_entry.sh")
TIMER_CALL_H = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/osfmk/kern/timer_call.h")
SCHED_H = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/osfmk/kern/sched.h")
# The pool the entry image links from, which is why claim 7 can read "what this build compiles" rather
# than "what this tree contains": the two answers differ, and the picture a log takes is of the former.
OBJECT_POOL = os.path.join(REPO_ROOT, "out/xnu_kernel_obj")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

# The arming entry points this step wraps and the source number each one must pass, in the order
# `entry_timebase.c` documents them. The numbers are part of the claim and not a convenience: they are
# what the log's `xnu_live_tmr_enter_src` is compared against by a reader, and a wrapper that passed the
# wrong one would attribute a millisecond to the wrong timer.
SOURCES = {
    "__wrap_timer_call_enter": 1,
    "__wrap_timer_call_enter_with_leeway": 2,
    "__wrap_timer_call_quantum_timer_enter": 3,
}
# Every wrapped name this step adds, wrapper -> the header that declares it.
WRAPPED = {
    "timer_call_enter": TIMER_CALL_H,
    "timer_call_enter_with_leeway": TIMER_CALL_H,
    "timer_call_quantum_timer_enter": TIMER_CALL_H,
    "timer_call_setup": TIMER_CALL_H,
    "thread_quantum_expire": SCHED_H,
}
# The family member this step deliberately does not wrap, and the one object in a pool of hundreds that
# may reference it - itself, since `osfmk/kern/timer_call.c` is where it is defined.
UNWRAPPED = {"timer_call_enter1": "osfmk_kern_timer_call.o"}


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    """Every test below runs on comment-stripped source: this step's rationale is long and lives in the
    same files as its code, and a claim satisfied by prose is not a claim."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def strip_shell_comments(text):
    """**Not `strip_comments`, and that is the whole point of this function.** `build_entry.sh` is a
    shell script, and a shell script's comments are `#`; running the C stripper over it deletes
    everything between a `/*` and the next `*/` *anywhere in the file*, and this script contains both -
    `osfmk/*/conf/files`, a glob in a comment - so the first version of this check silently truncated
    the file at byte 35308 of 2163983 and then reported that `PASS1_LDFLAGS` and every one of 484's six
    wraps were missing from a build that has them. Only full-line comments are removed: an inline `#`
    after code is not stripped, because the direction of that error is the safe one here (a
    commented-out `--wrap=` would be *seen*, and claim 6's `#` assertion below is what catches it)."""
    return re.sub(r"^[ \t]*#[^\n]*", "", text, flags=re.M)


def defines(text):
    """`#define NAME value`, with the `[ \\t]+` discipline 482 had to learn: a `\\s+` here can cross a
    newline and read the next `#define` as the value (that step's defect 216)."""
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


def signature(text, name):
    """`(return type, argument list)` for `name`, from a declaration or from a definition.

    Two forms because the six functions here are spelled two ways: `__real_X` is *declared* above the
    wrapper (there is no definition of it in this repository - the linker's `--wrap` supplies it), while
    `__wrap_X` is *defined*. Both are claims about Apple's header and both are checked. The parameter
    list is matched with `[^;{}]` rather than `[^;]`: the first version of this function matched a
    `__wrap_` body's inner call - `int r = __real_timer_call_enter(call, deadline, flags);` - as if it
    were a parameter list, which made `timer_call_enter` look like a five-argument function.
    """
    for tail in (r";", r"\{"):
        match = re.search(r"^([A-Za-z_][\w \t*]*?)\b%s\s*\(([^;{}]*?)\)\s*%s" % (re.escape(name), tail),
                          text, re.M | re.S)
        if not match:
            continue
        args, depth, current, out = match.group(2), 0, "", []
        for char in args:
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            if char == "," and depth == 0:
                out.append(current.strip())
                current = ""
            else:
                current += char
        if current.strip():
            out.append(current.strip())
        return match.group(1).strip(), out
    return None


def header_declaration(text, name):
    """`extern <ret> <name>(...)` out of a kernel header, split the same way."""
    match = re.search(r"extern\s+([A-Za-z_][\w \t*]*?)\s+%s\s*\(([^;]*?)\)\s*;" % re.escape(name),
                      text, re.M | re.S)
    if not match:
        return None, None
    args = match.group(2)
    out, depth, current = [], 0, ""
    for char in args:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if char == "," and depth == 0:
            out.append(current.strip())
            current = ""
        else:
            current += char
    if current.strip():
        out.append(current.strip())
    return match.group(1).strip(), out


def _array(text, name):
    """The *last* `NAME=(...)` assignment in the script, and the text inside the parentheses.

    Last and not first: `build_entry.sh` initialises both arrays empty before the `if` that fills them
    (`TRACE_LDFLAGS=()` at `ENTRY_TRACE`, `PASS1_LDFLAGS=()` in the link block), so a first-match
    extractor reads the empty initialiser and concludes that every wrap in the build is missing - which
    is exactly what this check's first run reported. The count of assignments is a note rather than a
    claim, so that a genuine second assignment (which would *shadow* the list, a real way for a wrap to
    vanish) is visible without this check inventing a rule about the script's shape."""
    matches = re.findall(r"^\s*%s=\((.*?)\)\s*$" % re.escape(name), text, re.M | re.S)
    if not matches:
        return None, 0
    return max(matches, key=len), len(matches)


def trace_wraps(text):
    body, _count = _array(strip_shell_comments(text), "TRACE_LDFLAGS")
    return None if body is None else re.findall(r"--wrap=([\w:]+)", body)


def pass1_wraps(text):
    body, _count = _array(strip_shell_comments(text), "PASS1_LDFLAGS")
    return None if body is None else re.findall(r"--wrap=([\w:]+)", body)


def elf_sections(path):
    """`[(name, bytes)]` for every allocated PROGBITS section, read out of the linked image.

    A minimal ELF32 reader rather than `objdump -s`'s text: claim 4 is about *four bytes at an aligned
    offset* inside the image, and a parser that turned hex back into bytes from a tool's pretty-printer
    would be a second transcription with its own chance of being wrong. Offsets are the ELF32 header's
    (`e_shoff` at 0x20, `e_shentsize` 0x2E, `e_shnum` 0x30, `e_shstrndx` 0x32) and each section header
    is 40 bytes with `sh_name`/`sh_type`/`sh_flags`/`sh_addr`/`sh_offset`/`sh_size` at 0, 4, 8, 12, 16 and
    20 - the same six fields `tools/check_saved_state_offsets.py` reads out of an object by hand.
    """
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        raise SystemExit("%s is not a 32-bit ELF" % path)
    shoff, shentsize, shnum, shstrndx = struct.unpack_from("<IHHH", data, 0x20)[0], \
        struct.unpack_from("<H", data, 0x2E)[0], struct.unpack_from("<H", data, 0x30)[0], \
        struct.unpack_from("<H", data, 0x32)[0]
    headers = []
    for index in range(shnum):
        base = shoff + index * shentsize
        name, stype, flags, _addr, offset, size = struct.unpack_from("<IIIIII", data, base)
        headers.append((name, stype, flags, offset, size))
    strtab = headers[shstrndx]
    strings = data[strtab[3]:strtab[3] + strtab[4]]
    out = []
    for name, stype, flags, offset, size in headers:
        if stype != 1 or not (flags & 0x2):
            continue
        end = strings.index(b"\0", name)
        out.append((strings[name:end].decode("ascii", "replace"), data[offset:offset + size]))
    return out


def aligned_words(sections):
    """Every 4-byte little-endian word at a 4-byte-aligned offset within its section, as a set. Aligned
    because the reference claim 4 makes is a compiler *literal*, and a word read across an alignment
    boundary is noise that could only produce a false positive."""
    words = set()
    for _name, blob in sections:
        for index in range(0, len(blob) - 3, 4):
            words.add(struct.unpack_from("<I", blob, index)[0])
    return words


def movw_movt_values(disassembly):
    """Every 32-bit constant this image materialises as a `movw`/`movt` pair, as a set.

    This is the form claim 4's reference actually takes and the reason the first version of this check
    was wrong: `timer_call_setup(&processor->quantum_timer, thread_quantum_expire, processor)` puts the
    address in an *argument*, so `arm-none-eabi-gcc -O2` emits `movw r1, #0x46c0` and `movt r1, #0x8047`
    - two half-word immediates four instructions before the `bl` - and neither the wrapper's address nor
    the original's exists as a word anywhere in the image. A pair is reconstructed by finding a `movt`
    and a `movw` to the *same register* within a short window on either side; the window is eight lines
    because the two are emitted adjacent, and a wider one would start pairing registers that were loaded
    for different reasons and invent addresses that are in no image."""
    values = set()
    pattern = re.compile(r"\b(movw|movt)\s+(r\d+),\s+#(\d+)")
    lines = []
    for line in disassembly.splitlines():
        match = pattern.search(line)
        lines.append((match.group(1), match.group(2), int(match.group(3)) & 0xFFFF) if match else None)
    for index, entry in enumerate(lines):
        if entry is None or entry[0] != "movt":
            continue
        high, register = entry[2], entry[1]
        for other in range(max(0, index - 8), min(len(lines), index + 9)):
            if other == index or lines[other] is None:
                continue
            kind, other_register, low = lines[other]
            if kind == "movw" and other_register == register:
                values.add((high << 16) | low)
    return values


def objects_referencing(pool, name):
    """`{object: [symbol kinds]}` for every object in `pool` that mentions `name` at all.

    One `nm` over the whole pool rather than one per file: the pool is seven hundred objects and a
    process per object turns a check that runs before every build into something a person waits for. The
    output is `path: symbol` per line for many files at once, which is why the file is taken from the
    line's prefix rather than from an argument."""
    if not os.path.isdir(pool):
        return None
    listing = sorted(f for f in os.listdir(pool) if f.endswith(".o"))
    if not listing:
        return None
    out = run([NM] + [os.path.join(pool, f) for f in listing])
    found = {}
    current = None
    for line in out.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            current = os.path.basename(line[:-1])
            continue
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            found.setdefault(current, []).append(parts[1])
        elif len(parts) == 2 and parts[1] == name:
            found.setdefault(current, []).append(parts[0])
    return found


# ------------------------------------------------------------------------------------------------
# Gather
# ------------------------------------------------------------------------------------------------

def gather(image):
    trace_text = read(ENTRY_TRACE_C)
    timebase_text = read(ENTRY_TIMEBASE_C)
    facts = {
        "trace_text": trace_text,
        "timebase_text": timebase_text,
        "build_text": read(BUILD_ENTRY_SH),
        "timer_call_h": read(TIMER_CALL_H),
        "sched_h": read(SCHED_H),
        "image": image,
    }
    facts["trace"] = strip_comments(trace_text)
    facts["timebase"] = strip_comments(timebase_text)
    facts["timebase_defines"] = defines(timebase_text)
    facts["symbols"] = nm(image) if image else {}
    facts["words"] = aligned_words(elf_sections(image)) if image else set()
    facts["pairs"] = movw_movt_values(run([OBJDUMP, "-d", image])) if image else set()
    facts["addresses"] = facts["words"] | facts["pairs"]
    facts["pool"] = objects_referencing(OBJECT_POOL, "timer_call_enter1")
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_sources(facts, failures, notes):
    """1. The four source numbers are the four wrappers, one each, and the range guard knows them."""
    passed = {}
    for wrapper, expected in SOURCES.items():
        body = function_body(facts["trace"], wrapper)
        if body is None:
            failures.append("entry_trace.c no longer defines %s, so the arming it was written to name "
                            "is unrecorded and the census would attribute nothing to source %d"
                            % (wrapper, expected))
            continue
        match = re.search(r"entry_timebase_note_timer_enter\(\s*(\d+)\s*u\s*,", body)
        if not match:
            failures.append("entry_trace.c's %s does not call entry_timebase_note_timer_enter with a "
                            "literal source number, so the arming it wraps is either unrecorded or "
                            "recorded under a number no reader can map to a timer" % wrapper)
            continue
        passed[wrapper] = int(match.group(1))

    if passed:
        wrong = {w: (n, SOURCES[w]) for w, n in passed.items() if n != SOURCES[w]}
        if wrong:
            detail = ", ".join("%s passes %d and not %d" % (w, n, e) for w, (n, e) in sorted(wrong.items()))
            failures.append("the source numbers no longer match the wrappers: %s. The numbers are what "
                            "`xnu_live_tmr_enter_src` means, so a swap turns the census into a claim "
                            "about the wrong timer" % detail)
        if len(set(passed.values())) != len(passed):
            failures.append("two of the four wrappers pass the same source number (%s), so the log "
                            "cannot separate the two armings" % sorted(passed.values()))

    largest = max(SOURCES.values())
    limit = facts["timebase_defines"].get("STAGE90_TMR_SRC_MAX")
    if limit is None:
        failures.append("entry_timebase.c no longer defines STAGE90_TMR_SRC_MAX, so the note function's "
                        "range guard has nothing to compare against and every source would be dropped")
    elif limit <= largest:
        failures.append("STAGE90_TMR_SRC_MAX is %d and the largest source is %d: the note function's "
                        "`source < STAGE90_TMR_SRC_MAX` guard would drop that arming's count silently, "
                        "which is exactly the record the step exists to produce" % (limit, largest))
    else:
        notes.append("STAGE90_TMR_SRC_MAX = %d, so all %d sources are inside the count array"
                     % (limit, len(SOURCES)))


def _types(arg):
    """The type words of an argument, with the parameter's own name dropped. Every prototype in this
    project is written `type name`, and the name is the last identifier-like token - which is why the
    caller of this passes the argument as written rather than a parse tree."""
    words = re.findall(r"[A-Za-z_]\w*", arg)
    return words[:-1] if len(words) > 1 else words


def claim_prototypes(facts, failures, notes):
    """2. Same argument count as Apple's declaration, and `uint64_t` exactly where Apple puts it."""
    param_typedef = re.search(r"typedef\s+[^;]*?\btimer_call_param_t\s*;", facts["timer_call_h"])
    if not param_typedef or "void" not in param_typedef.group(0) or "*" not in param_typedef.group(0):
        failures.append("timer_call_param_t is no longer a `void *` typedef in timer_call.h, and every "
                        "prototype in entry_trace.c spells it as one pointer word: a definition change "
                        "here would make those prototypes read the wrong registers without any file "
                        "failing to compile")
    else:
        notes.append("timer_call_param_t is %s" % " ".join(param_typedef.group(0).split()))

    for name, header in sorted(WRAPPED.items()):
        text = facts["timer_call_h"] if header == TIMER_CALL_H else facts["sched_h"]
        ret, args = header_declaration(text, name)
        if args is None:
            failures.append("%s no longer declares %s, so this check cannot say whether the wrapper "
                            "beside it matches" % (os.path.basename(header), name))
            continue
        # **Both spellings are claims about Apple's header, and the compiler only enforces one of
        # them.** The `__real_` declaration and the `__wrap_` definition are two unrelated names as far
        # as C is concerned - nothing makes `__wrap_X`'s prototype agree with `__real_X`'s - so a
        # wrapper whose parameter list drifted from the function it calls would compile, link and read
        # the wrong registers. Each is therefore compared against the header on its own.
        for prefix in ("__real_", "__wrap_"):
            ours = signature(facts["trace"], prefix + name)
            if ours is None:
                failures.append("entry_trace.c has no declaration or definition of %s%s, so what it "
                                "was compiled against is a default the compiler inferred"
                                % (prefix, name))
                continue
            our_ret, our_args = ours
            if len(our_args) != len(args):
                failures.append("%s%s takes %d argument(s) and %s declares %d: on AAPCS the extra or "
                                "missing one is a register pair, so every argument after it is read "
                                "from the wrong place"
                                % (prefix, name, len(our_args), os.path.basename(header), len(args)))
                continue

            for index, (mine, theirs) in enumerate(zip(our_args, args)):
                header_is_64 = "uint64_t" in theirs or "int64_t" in theirs
                ours_is_64 = "uint64_t" in mine or "int64_t" in mine
                if header_is_64 and not ours_is_64:
                    failures.append("%s%s argument %d is `%s` in %s and `%s` in the wrapper: a 64-bit "
                                    "argument occupies an even-numbered register pair on AAPCS, so a "
                                    "one-word transcription shifts every argument after it"
                                    % (prefix, name, index + 1, " ".join(_types(theirs)) or theirs,
                                       os.path.basename(header), " ".join(_types(mine)) or mine))
                if ours_is_64 and not header_is_64:
                    failures.append("%s%s argument %d is one word in %s and 64-bit in the wrapper, which "
                                    "consumes two registers for one argument"
                                    % (prefix, name, index + 1, os.path.basename(header)))
                if "timer_call_param_t" in theirs and "*" not in mine:
                    failures.append("%s%s argument %d is a `timer_call_param_t` - a pointer - in %s and "
                                    "`%s` in the wrapper"
                                    % (prefix, name, index + 1, os.path.basename(header), mine))

            # The return type, and it is a claim about *bits* rather than about spelling: `boolean_t` is
            # `unsigned int` here, so `int`, `uint32_t` and `boolean_t` are the same one register, while
            # a transcription that spelled the return `uint64_t` would have the wrapper read two words.
            if "boolean_t" in ret and "64" in our_ret:
                failures.append("%s%s returns `%s` in %s and a 64-bit type in the wrapper, so the value "
                                "the real function computed is read as two words"
                                % (prefix, name, ret, os.path.basename(header)))
    if not failures:
        notes.append("all %d wrapped declarations agree with the kernel headers on argument count and on "
                     "which arguments are 64-bit, in both the `__real_` declaration and the `__wrap_` "
                     "definition" % len(WRAPPED))


def claim_image(facts, failures, notes):
    """3. The wrapped names are in the image and no wrapper shares its address with what it wraps."""
    symbols = facts["symbols"]
    missing = []
    for name in sorted(WRAPPED):
        if name not in symbols:
            missing.append(name)
        elif "__wrap_" + name not in symbols:
            failures.append("the image defines %s and no __wrap_%s: this build made the --wrap but "
                            "linked no wrapper, so the arming is not recorded" % (name, name))
        elif symbols["__wrap_" + name] == symbols[name]:
            failures.append("__wrap_%s and %s are the same address in this image, so the wrapper *is* "
                            "the function it was written to wrap and the census would be a census of "
                            "itself" % (name, name))
    if missing:
        failures.append("the image defines no %s, so the wrapper for it calls the generator's stand-in: "
                        "a `--wrap` whose target is stubbed measures the stub, and the two names in "
                        "PASS1_LDFLAGS exist precisely because that failure is silent" % ", ".join(missing))
    if not failures and not missing:
        notes.append("all %d wrapped names and their wrappers are in the image at different addresses"
                     % len(WRAPPED))


def claim_quantum_expire_is_reachable(facts, failures, notes):
    """4. `thread_quantum_expire`'s rewrite reached the address, which only the image can say."""
    symbols = facts["symbols"]
    wrapper = symbols.get("__wrap_thread_quantum_expire")
    plain = symbols.get("thread_quantum_expire")
    if wrapper is None or plain is None:
        failures.append("the image does not define both thread_quantum_expire and its wrapper, so this "
                        "claim has nothing to read")
        return
    if wrapper not in facts["addresses"]:
        failures.append("no `movw`/`movt` pair and no aligned word in this image equals "
                        "__wrap_thread_quantum_expire's address (0x%08x). Its only reference in Apple's "
                        "tree is the address handed to `timer_call_setup` (processor.c:163), so if the "
                        "linker did not rewrite that reference then this wrapper never runs, "
                        "`xnu_live_tmr_qexp_*` is never written, and the one record that names *whose* "
                        "quantum expired is dead code that still costs a wrap in the accounting" % wrapper)
    if plain in facts["addresses"]:
        failures.append("a constant or word in this image equals thread_quantum_expire's own address "
                        "(0x%08x): the address taken at processor.c:163 was not rewritten, so the "
                        "function pointer the scheduler fires is the unwrapped function and the census "
                        "of quantum expiries is never produced" % plain)
    if wrapper in facts["addresses"] and plain not in facts["addresses"]:
        notes.append("__wrap_thread_quantum_expire's address (0x%08x) is materialised in the image - as "
                     "a `movw`/`movt` pair, which is the form this reference takes - and the unwrapped "
                     "address is not, so the rewrite reached the function pointer" % wrapper)


def claim_unwrapped_member(facts, failures, notes):
    """7. `timer_call_enter1` is not wrapped because nothing here calls it, read out of the pool."""
    pool = facts["pool"]
    if pool is None:
        failures.append("the object pool %s is not there or holds no objects, so this claim - that the "
                        "one member of the timer family this step does not wrap has no caller in this "
                        "build - has nothing to read. It is a claim about what was *compiled*, and the "
                        "tree's own callers of the name (sfi.c's seven, dtrace_glue.c's three) are "
                        "exactly the ones that do not reach it here" % OBJECT_POOL)
        return
    for name, definer in sorted(UNWRAPPED.items()):
        referencing = pool.get(definer)
        others = sorted(o for o in pool if o != definer)
        if not referencing:
            failures.append("%s does not reference %s at all, so the object this claim names as its "
                            "definer is not one: the check would then be satisfied by any pool in which "
                            "nothing references the name, including one where it was never compiled"
                            % (definer, name))
        if others:
            failures.append("these objects reference %s, and this step wraps only the family's other "
                            "three members, so the arming path they reach is unrecorded: %s. Add "
                            "`--wrap=%s` to TRACE_LDFLAGS in build_entry.sh and a wrapper beside the "
                            "others in entry_trace.c - the reachability check will refuse the image "
                            "until a branch to it exists, which is the state that would make the wrap "
                            "worth having" % (name, ", ".join(others), name))
        if name not in facts["trace_text"]:
            failures.append("entry_trace.c does not mention %s anywhere, so the argument for not "
                            "wrapping it has been deleted and the omission is silent" % name)
    if not failures:
        notes.append("exactly one of %d objects in the pool references timer_call_enter1 - %s, which "
                     "defines it - so the family member this step does not wrap has no caller here"
                     % (len(pool) if pool else 0,
                        UNWRAPPED["timer_call_enter1"]))


def claim_bounds(facts, failures, notes):
    """5. The census is bounded, the bounds are published, and the setup table is complete."""
    header = facts["timebase_defines"]
    body = function_body(facts["timebase"], "stage90_tbd_set_decrementer")
    if body is None:
        failures.append("entry_timebase.c no longer defines stage90_tbd_set_decrementer, so the census "
                        "of armed values does not exist")
        body = ""

    for macro in ("STAGE90_TMR_SETUP_SHOWN", "STAGE90_TMR_ENTER_SHOWN", "STAGE90_TMR_QEXPIRE_SHOWN",
                  "STAGE90_DEC_SHOWN"):
        if header.get(macro, 0) <= 0:
            failures.append("%s is %s, so the record it bounds is unrecorded or unbounded"
                            % (macro, header.get(macro)))

    setup = function_body(facts["timebase"], "entry_timebase_note_timer_setup")
    if setup is None:
        failures.append("entry_timebase.c no longer defines entry_timebase_note_timer_setup, so the "
                        "call pointers in the enter records have no table to be resolved against and "
                        "every name this step adds is unreachable")
    else:
        if "g_tmr_setup_n <= STAGE90_TMR_SETUP_SHOWN" not in setup:
            failures.append("entry_timebase_note_timer_setup no longer publishes its first "
                            "STAGE90_TMR_SETUP_SHOWN calls unconditionally: a table sampled on the "
                            "powers of two is a table with holes in it, and a `call` pointer missing "
                            "from it can never be resolved to a function")
        if re.search(r"g_tmr_setup_n\s*&\s*\(g_tmr_setup_n\s*-\s*1u\)", setup):
            failures.append("entry_timebase_note_timer_setup publishes on the powers of two, which is "
                            "the budget discipline for a counter and the wrong one for a table: the "
                            "records that are skipped are exactly the names the log cannot resolve")
        if "g_tmr_setup_over++" not in setup or "xnu_live_tmr_setup_over" not in setup:
            failures.append("entry_timebase_note_timer_setup drops the setups past its bound without "
                            "counting them, so a table that overflowed looks exactly like one that did not")

    enter = function_body(facts["timebase"], "entry_timebase_note_timer_enter")
    if enter is None:
        failures.append("entry_timebase.c no longer defines entry_timebase_note_timer_enter")
    else:
        for key in ("xnu_live_tmr_enter_src", "xnu_live_tmr_enter_call", "xnu_live_tmr_enter_delta"):
            if key not in enter:
                failures.append("entry_timebase_note_timer_enter no longer publishes %s, so the arming "
                                "cannot be joined against the setup table" % key)
        if "g_tmr_enter_n <= STAGE90_TMR_ENTER_SHOWN" not in enter:
            failures.append("entry_timebase_note_timer_enter no longer bounds its full record, so a "
                            "per-syscall timer would fill the log with one record per call")
        if "g_tmr_enter_over_all++" not in enter or "xnu_live_tmr_enter_over" not in enter:
            failures.append("entry_timebase_note_timer_enter drops the armings past its bound without "
                            "counting them, which is the defect this project's 459 found in the report "
                            "buffer: a full instrument and a silent one print the same log")

    if 'TB_LIVE("xnu_live_dec_min",' not in body:
        failures.append("stage90_tbd_set_decrementer no longer publishes the minimum, so 484's census "
                        "replaces 483's reading instead of completing it")
    if "g_dec_writes >= 2u" not in body:
        failures.append("the armed-value census no longer excludes call 1: its value is the saturated "
                        "DECREMENTER_MAX that deadline_to_decrementer produces from EndOfAllTime, so a "
                        "reference taken from it would make the first real deadline the only "
                        "'non-repeating' one in every run")
    if "xnu_live_dec_new_value" not in body or "xnu_live_dec_new_call" not in body:
        failures.append("stage90_tbd_set_decrementer no longer publishes a newly-armed value with the "
                        "call it first appeared on: that pair *is* 'the first non-repeating deadline' "
                        "and without it the run cannot say whether one value was armed or several")
    if "g_dec_distinct < STAGE90_DEC_SHOWN" not in body:
        failures.append("the distinct-value table is no longer bounded by STAGE90_DEC_SHOWN, so a "
                        "kernel that armed a different value on every call would put four thousand "
                        "records in the log instead of eight")
    if "g_dec_other_more++" not in body or 'TB_LIVE("xnu_live_dec_more",' not in body:
        failures.append("the values past the table's end are no longer counted separately, so 'eight "
                        "distinct values' and 'eight shown of four thousand' print the same census")
    if 'TB_LIVE("xnu_live_dec_ref",' not in body:
        failures.append("stage90_tbd_set_decrementer no longer publishes the reference value, so the "
                        "counts of repeats and non-repeats have no readable baseline")


def claim_build_list(facts, failures, notes):
    """6. The wraps are in TRACE_LDFLAGS, not in PASS1_LDFLAGS, and each wrapper calls a real."""
    trace = trace_wraps(facts["build_text"])
    pass1 = pass1_wraps(facts["build_text"])
    if trace is None:
        failures.append("build_entry.sh no longer has a TRACE_LDFLAGS array, so this check cannot say "
                        "whether this step's six wraps are in the traced build at all")
        trace = []
    if pass1 is None:
        failures.append("build_entry.sh's PASS1_LDFLAGS is unreadable, so this check cannot say whether "
                        "the two links were given the same wraps - which is the property that decides "
                        "whether a wrapper calls the function it names or the generator's stand-in")
        pass1 = []

    for name in sorted(WRAPPED):
        if name not in trace:
            failures.append("--wrap=%s is not in TRACE_LDFLAGS: the wrapper is defined and never asked "
                            "for, so the arming it records is unrecorded" % name)
        if name in pass1:
            failures.append("--wrap=%s is in PASS1_LDFLAGS. Its wrapper lives in entry_trace.o, which "
                            "pass 1 does not link, so pass 1 would report `__wrap_%s` undefined, the "
                            "generator would emit a definition of it, and that definition would collide "
                            "with the real wrapper in the final link" % (name, name))

    # **The array must not contain a `#`.** `strip_shell_comments` removes full-line comments only, so a
    # wrap commented out *after* code on the same line would still be read as present - and this check's
    # whole claim 6 is that the list is where the wraps are. The script's arrays have no inline
    # comments today, so the assertion costs nothing and closes the hole rather than documenting it.
    for array_name in ("TRACE_LDFLAGS", "PASS1_LDFLAGS"):
        body, count = _array(strip_shell_comments(facts["build_text"]), array_name)
        if body is None:
            failures.append("build_entry.sh no longer has a %s array at all" % array_name)
        elif "#" in body:
            failures.append("build_entry.sh's %s array contains a `#`: this check strips full-line "
                            "comments and reads the rest literally, so a commented-out wrap on a code "
                            "line would be counted as one this build makes" % array_name)
        else:
            notes.append("%s is assigned %d time(s) in build_entry.sh" % (array_name, count))
    for name in sorted(WRAPPED):
        if function_body(facts["trace"], "__wrap_" + name) is None:
            failures.append("entry_trace.c defines no __wrap_%s, so the --wrap in TRACE_LDFLAGS names a "
                            "wrapper that does not exist" % name)
        elif signature(facts["trace"], "__real_" + name) is None:
            failures.append("entry_trace.c's __wrap_%s has no declared __real_%s, so what it calls is "
                            "whatever the compiler inferred from the call" % (name, name))

    if not failures:
        notes.append("all %d wraps are in TRACE_LDFLAGS, none is in PASS1_LDFLAGS, and each has a "
                     "__wrap_ definition beside a __real_ declaration" % len(WRAPPED))


CLAIMS = (claim_sources, claim_prototypes, claim_image, claim_quantum_expire_is_reachable,
          claim_bounds, claim_build_list, claim_unwrapped_member)


def compare(facts):
    failures, notes = [], []
    for claim in CLAIMS:
        claim(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, old, new):
    if old not in text:
        raise SystemExit("the selftest cannot find what it mutates: %r" % old[:60])
    return text.replace(old, new, 1)


def mutate(facts, name):
    facts = dict(facts)
    trace = facts["trace_text"]
    timebase = facts["timebase_text"]
    build = facts["build_text"]

    def rederive_trace(text):
        facts["trace_text"] = text
        facts["trace"] = strip_comments(text)

    def rederive_timebase(text):
        facts["timebase_text"] = text
        facts["timebase"] = strip_comments(text)
        facts["timebase_defines"] = defines(text)

    def rederive_build(text):
        facts["build_text"] = text

    def rederive_addresses():
        facts["addresses"] = facts["words"] | facts["pairs"]

    if name == "source_number_swapped":
        rederive_trace(_bump(trace, "entry_timebase_note_timer_enter(2u,", "entry_timebase_note_timer_enter(1u,"))
    elif name == "source_number_is_a_constant":
        rederive_trace(_bump(trace, "entry_timebase_note_timer_enter(3u,",
                             "entry_timebase_note_timer_enter(SOURCE_QUANTUM,"))
    elif name == "source_number_two_wrappers_agree":
        rederive_trace(_bump(trace, "entry_timebase_note_timer_enter(3u,",
                             "entry_timebase_note_timer_enter(2u,"))
    elif name == "source_limit_excludes_the_last":
        rederive_timebase(_bump(timebase, "#define STAGE90_TMR_SRC_MAX       8u",
                                "#define STAGE90_TMR_SRC_MAX       3u"))
    elif name == "source_limit_removed":
        rederive_timebase(_bump(timebase, "#define STAGE90_TMR_SRC_MAX       8u", ""))
    elif name == "deadline_spelled_as_one_word":
        rederive_trace(_bump(trace, "int __real_timer_call_enter_with_leeway(void *call, void *param1, uint64_t deadline,",
                             "int __real_timer_call_enter_with_leeway(void *call, void *param1, uint32_t deadline,"))
    elif name == "leeway_spelled_as_one_word":
        rederive_trace(_bump(trace, "uint64_t leeway, uint32_t flags, uint32_t ratelimited);",
                             "uint32_t leeway, uint32_t flags, uint32_t ratelimited);"))
    elif name == "an_argument_removed":
        rederive_trace(_bump(trace, "int __real_timer_call_enter(void *call, uint64_t deadline, uint32_t flags);",
                             "int __real_timer_call_enter(void *call, uint64_t deadline);"))
    elif name == "an_argument_added":
        rederive_trace(_bump(trace, "int __real_timer_call_enter(void *call, uint64_t deadline, uint32_t flags);",
                             "int __real_timer_call_enter(void *call, uint64_t deadline, uint32_t flags,\n"
                             "                            uint32_t extra);"))
    elif name == "a_flag_becomes_64_bit":
        rederive_trace(_bump(trace, "int __real_timer_call_enter(void *call, uint64_t deadline, uint32_t flags);",
                             "int __real_timer_call_enter(void *call, uint64_t deadline, uint64_t flags);"))
    elif name == "param_becomes_an_integer":
        rederive_trace(_bump(trace, "int __real_timer_call_enter_with_leeway(void *call, void *param1, uint64_t deadline,",
                             "int __real_timer_call_enter_with_leeway(void *call, uint32_t param1, uint64_t deadline,"))
    elif name == "the_wrapper_and_the_real_disagree":
        rederive_trace(_bump(trace, "int __wrap_timer_call_enter(void *call, uint64_t deadline, uint32_t flags)",
                             "int __wrap_timer_call_enter(void *call, uint64_t deadline)"))
    elif name == "param_typedef_stops_being_a_pointer":
        facts["timer_call_h"] = _bump(facts["timer_call_h"],
                                      "typedef void\t\t*timer_call_param_t;",
                                      "typedef uint32_t\ttimer_call_param_t;")
    elif name == "a_header_declaration_removed":
        facts["timer_call_h"] = _bump(facts["timer_call_h"], "extern boolean_t\ttimer_call_enter_with_leeway(",
                                      "static boolean_t\tnot_the_name(")
    elif name == "a_wrapped_name_absent":
        facts["symbols"] = dict(facts["symbols"])
        facts["symbols"].pop("timer_call_enter_with_leeway", None)
    elif name == "a_wrapper_absent":
        facts["symbols"] = dict(facts["symbols"])
        facts["symbols"].pop("__wrap_timer_call_setup", None)
    elif name == "wrapper_is_the_function":
        facts["symbols"] = dict(facts["symbols"])
        facts["symbols"]["__wrap_timer_call_setup"] = facts["symbols"]["timer_call_setup"]
    elif name == "quantum_expire_address_not_rewritten":
        facts["pairs"] = set(facts["pairs"])
        facts["pairs"].discard(facts["symbols"].get("__wrap_thread_quantum_expire", 0))
        facts["pairs"].add(facts["symbols"].get("thread_quantum_expire", 0))
        rederive_addresses()
    elif name == "quantum_expire_reference_vanished":
        facts["pairs"] = set(facts["pairs"])
        facts["pairs"].discard(facts["symbols"].get("__wrap_thread_quantum_expire", 0))
        rederive_addresses()
    elif name == "the_pair_scanner_is_not_consulted":
        # The mutation that says the pair scanner is load-bearing: the image has no *word* equal to the
        # wrapper's address - the reference is two half-word immediates - so a claim that looked only at
        # the data sections would call this live wrapper dead. Dropping the pairs from the union must
        # therefore be refused.
        facts["addresses"] = set(facts["words"])
    elif name == "the_wrapped_member_gains_a_caller":
        facts["pool"] = dict(facts["pool"] or {})
        facts["pool"]["osfmk_kern_sfi.o"] = ["U"]
    elif name == "the_object_pool_is_missing":
        facts["pool"] = None
    elif name == "the_definer_does_not_define_it":
        facts["pool"] = {"osfmk_kern_sfi.o": ["U"]}
    elif name == "the_omission_loses_its_argument":
        facts["trace_text"] = trace.replace("timer_call_enter1", "the family's second member")
    elif name == "setup_table_is_sampled":
        rederive_timebase(_bump(timebase, "    if (g_tmr_setup_n <= STAGE90_TMR_SETUP_SHOWN) {",
                                "    if ((g_tmr_setup_n & (g_tmr_setup_n - 1u)) == 0u) {"))
    elif name == "setup_overflow_uncounted":
        rederive_timebase(_bump(timebase, "        g_tmr_setup_over++;\n"
                                          "        if (g_tmr_setup_over == 1u) {\n"
                                          "            TB_LIVE(\"xnu_live_tmr_setup_over\", g_tmr_setup_over);\n"
                                          "        }", "        ;"))
    elif name == "setup_bound_removed":
        rederive_timebase(_bump(timebase, "        TB_LIVE(\"xnu_live_tmr_setup_seq\", g_tmr_setup_n);",
                                "        (void)g_tmr_setup_n;"))
        rederive_timebase(_bump(timebase, "    if (g_tmr_setup_n <= STAGE90_TMR_SETUP_SHOWN) {", "    if (1) {"))
    elif name == "enter_record_unbounded":
        rederive_timebase(_bump(timebase, "    if (g_tmr_enter_n <= STAGE90_TMR_ENTER_SHOWN) {", "    if (1) {"))
    elif name == "enter_overflow_uncounted":
        rederive_timebase(_bump(timebase, "        g_tmr_enter_over_all++;", "        ;"))
    elif name == "enter_drops_the_source":
        rederive_timebase(_bump(timebase, "        TB_LIVE(\"xnu_live_tmr_enter_src\", source);",
                                "        (void)source;"))
    elif name == "enter_drops_the_call":
        rederive_timebase(_bump(timebase, "        TB_LIVE(\"xnu_live_tmr_enter_call\", call);",
                                "        (void)call;"))
    elif name == "enter_drops_the_delta":
        rederive_timebase(_bump(timebase, "        TB_LIVE(\"xnu_live_tmr_enter_delta\", (uint32_t)(deadline - (uint64_t)now));",
                                "        (void)deadline;"))
    elif name == "census_replaces_the_minimum":
        rederive_timebase(_bump(timebase, "        TB_LIVE(\"xnu_live_dec_min\", g_dec_min_value);",
                                "        ;"))
    elif name == "census_includes_call_one":
        rederive_timebase(_bump(timebase, "    if (g_dec_writes >= 2u) {", "    if (g_dec_writes >= 1u) {"))
    elif name == "census_drops_the_call_number":
        rederive_timebase(_bump(timebase, "                TB_LIVE(\"xnu_live_dec_new_call\", g_dec_writes);",
                                "                ;"))
    elif name == "census_table_unbounded":
        rederive_timebase(_bump(timebase, "            } else if (g_dec_distinct < STAGE90_DEC_SHOWN) {",
                                "            } else if (1) {"))
    elif name == "census_overflow_uncounted":
        rederive_timebase(_bump(timebase, "                g_dec_other_more++;", "                ;"))
    elif name == "census_drops_the_reference":
        rederive_timebase(_bump(timebase, "            TB_LIVE(\"xnu_live_dec_ref\", g_dec_ref_value);",
                                "            ;"))
    elif name == "wrap_removed_from_the_build":
        # The anchor is the wrap *pair* and not the line's last character: 485 appended its own wraps
        # after `thread_quantum_expire` and this mutation broke on the closing parenthesis the old
        # anchor carried, which is a selftest asserting a neighbour rather than the property.
        rederive_build(_bump(build, "--wrap=timer_call_setup --wrap=thread_quantum_expire",
                             "--wrap=thread_quantum_expire"))
    elif name == "wrap_moved_to_pass_one":
        rederive_build(_bump(build, "        PASS1_LDFLAGS=(--wrap=PE_init_platform --wrap=fiq_context_init)",
                             "        PASS1_LDFLAGS=(--wrap=PE_init_platform --wrap=fiq_context_init"
                             " --wrap=timer_call_setup)"))
    elif name == "wrapper_without_a_real":
        rederive_trace(_bump(trace, "void __real_timer_call_setup(void *call, void *func, void *param0);\n\n", ""))
    elif name == "wrapper_removed_from_the_source":
        rederive_trace(re.sub(r"^void\s+__wrap_timer_call_setup\s*\([^)]*\)\s*\{.*?^\}", "",
                              facts["trace_text"], count=1, flags=re.M | re.S))
    else:
        raise SystemExit("unknown mutation %s" % name)
    return facts


MUTATIONS = (
    "source_number_swapped", "source_number_is_a_constant", "source_number_two_wrappers_agree",
    "source_limit_excludes_the_last", "source_limit_removed",
    "deadline_spelled_as_one_word", "leeway_spelled_as_one_word", "an_argument_removed",
    "an_argument_added", "a_flag_becomes_64_bit", "param_becomes_an_integer",
    "the_wrapper_and_the_real_disagree", "param_typedef_stops_being_a_pointer",
    "a_header_declaration_removed",
    "a_wrapped_name_absent", "a_wrapper_absent", "wrapper_is_the_function",
    "quantum_expire_address_not_rewritten", "quantum_expire_reference_vanished",
    "the_pair_scanner_is_not_consulted", "the_wrapped_member_gains_a_caller",
    "the_object_pool_is_missing", "the_definer_does_not_define_it",
    "the_omission_loses_its_argument",
    "setup_table_is_sampled", "setup_overflow_uncounted", "setup_bound_removed",
    "enter_record_unbounded", "enter_overflow_uncounted", "enter_drops_the_source",
    "enter_drops_the_call", "enter_drops_the_delta", "census_replaces_the_minimum",
    "census_includes_call_one", "census_drops_the_call_number", "census_table_unbounded",
    "census_overflow_uncounted", "census_drops_the_reference", "wrap_removed_from_the_build",
    "wrap_moved_to_pass_one", "wrapper_without_a_real", "wrapper_removed_from_the_source",
)


def selftest(facts):
    # **The baseline first, and it is not a formality.** Every mutation below is "the check must still
    # report a failure after this edit", so on a baseline that *already* fails - an image built before
    # this step's wraps, say - every mutation is refused for the baseline's reason and the whole run says
    # nothing about the mutations. 483's selftest accepted four mutations before its own first good
    # build, for exactly this reason. So a failing baseline is reported as a failure of the selftest
    # rather than as a reason it passed.
    baseline, _notes = compare(facts)
    if baseline:
        print("FAIL: the selftest's own baseline fails %d claim(s), so 'every mutation was refused' "
              "would be true for the wrong reason. Fix these first:" % len(baseline), file=sys.stderr)
        for failure in baseline:
            print("      " + failure, file=sys.stderr)
        return 1
    accepted = []
    for name in MUTATIONS:
        failures, _notes = compare(mutate(facts, name))
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
        print("FAIL: --image is required: two of the six claims are about the linked image - the "
              "addresses the wrappers ended up at, and whether the rewrite reached the one reference "
              "that is a function pointer - and there is no default that can stand in for it",
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
        print("FAIL: the timer census this step builds is not the census this image has:", file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    say("  xnu_entry_484: the four arming entry points are wrapped and name themselves, the five "
        "prototypes beside them agree with Apple's declarations on argument count and on which arguments "
        "are 64-bit, the setup table is complete rather than sampled, and every bound this census "
        "imposes is published as a count rather than dropped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
