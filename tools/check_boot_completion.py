#!/usr/bin/env python3
"""
Check the six things 485's reading of the kernel's *own* completion depends on, before the device is
asked.

484 read the log of a run and named the clock's owner; this step wraps five calls in the kernel's boot
thread so that the log says **how far the kernel's own main thread got**, and walks the device tree the
driver layer is handed so that "which basic drivers are running" is a table rather than an inference
from the absence of console text. Each of those has a way of being wrong that produces a run rather
than an error, so each is a claim here:

  1. **the five are positions in Apple's tail, and the order is Apple's.** `entry_trace.c` publishes one
     index per wrapper, and the index is what `tail_seen[]` counts. The order it is a transcription of is
     read out of `osfmk/kern/startup.c`'s `kernel_bootstrap_thread` in this check rather than taken from
     the wrapper's own comment, so a reordered tail cannot relabel the log silently. The two calls this
     step deliberately does *not* wrap - `bsd_init` and `thread_bind` - are claimed where they sit:
     before the five, and between the last two.

  2. **each of the five is called from exactly one place in this kernel.** That is what makes a record
     of one of them a *position* rather than a count of everything. It is read out of the object pool:
     one defining object and one referencing object per name, the same referencing object for all five,
     and that object is the one that defines `kernel_bootstrap_thread`. The exclusion of `thread_bind` is
     claimed the other way round - it must be referenced by more than one object, because *that* is the
     argument for leaving it alone - so an argument that stops being true fails the build instead of
     staying a comment.

  3. **each wrapper records before the call it wraps, and publishes the function it calls.** `vm_pageout`
     never returns, so a record written after its call would never be written; the note has to precede
     the call in all five, and the census inside the fifth has to run between the note and the call,
     because after the call it never runs and before the note it is outside the position it belongs to.
     The site a wrapper publishes must be `__real_<name>`'s address - a wrapper that published its own
     would put all five in one place - and nothing may run after the fifth call.

  4. **the bounds fit the things they bound.** The tail count is one `#define` that both the counter
     array and its guard read (two literals would be this project's oldest defect class, and the failure
     it would produce here is a store past the end of a `.bss` array in the kernel's boot thread); the
     census's cap fits inside the table the notes store into; and the offset the census reads `__state[1]`
     at is compared against the offset `build_entry.sh`'s layout check pins `IOService::getState`'s own
     load at.

  5. **the census counts with Apple's own bits, over every child, and publishes every count.** The
     `Matched`/`Registered` bits are read from `IOService.h`'s enum rather than spelled, each tally is
     guarded by the test that names it, `shown` counts every child the note is handed, and the four
     tables are indexed by the caller's index. The walk starts from the root the OS's own walk starts
     from - `getRegistryRoot()` and its child in `gIODTPlane` - and publishes 461's recorded root beside
     it, with whether they agree, because this step's first run read 0 children of the recorded root
     while the registry's own child had 21 and the log could not say which object either number was
     about. Every counter has a key in the report, every key is written by a writer the report path
     calls, and every record - including the reason a reading was not taken - is in the live channel as
     well, because the run that matters is the one where the kernel reaches `vm_pageout`, never returns,
     and the payload never gets its report path back (459's defect was a report buffer that filled up;
     this is the same lesson one step further on).

  6. **the instrument is in the linked image, at the tail's own call sites.** The five wrappers and their
     reals are separate symbols; `kernel_bootstrap_thread`'s own text transfers to each wrapper, in
     Apple's order and to no other wrapper; and each wrapper materialises its site as a `movw`/`movt`
     pair equal to the real function's address. A `--wrap` is a property of a *reference*, and whether
     the reference at the tail was rewritten is a fact about the link that no source can state.

    ./tools/check_boot_completion.py --image out/stage90/xnu_arm_entry.elf
    ./tools/check_boot_completion.py --image out/stage90/xnu_arm_entry.elf --selftest

What this check cannot say
--------------------------
Whether the tail is ever *reached*. `tail_seen[0..3]` are the control - if they are zero the wrappers
were never called and the run says nothing about the fourth or fifth - and `tail_seen[4] != 0` is the
reading itself: `vm_pageout` entered, which cannot happen unless `bsd_init` returned. Which children the
tree has, what they are named, and which of them carry `Registered` or `Matched` are the run's as well;
this check only says that the table those facts are read out of is the census it claims to be.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

BOOT_DIR = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot")
ENTRY_TRACE_C = os.path.join(BOOT_DIR, "entry_trace.c")
ENTRY_STUBS_C = os.path.join(BOOT_DIR, "entry_stubs.c")
BUILD_ENTRY_SH = os.path.join(BOOT_DIR, "build_entry.sh")
STARTUP_C = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/osfmk/kern/startup.c")
IOSERVICE_H = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/iokit/IOKit/IOService.h")
OBJECT_POOL = os.path.join(REPO_ROOT, "out/xnu_kernel_obj")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

KERNEL_THREAD = "kernel_bootstrap_thread"

# The five, **in the order Apple's `kernel_bootstrap_thread` calls them**. The order is a transcription
# of `startup.c` and claim 1 compares it against `startup.c` itself: the tuple is what this check
# believes, the source is what it measures, and a step that reordered Apple's tail fails here rather
# than re-transcribing the numbers into the wrappers.
WRAPPED = (
    "OSKextRemoveKextBootstrap",
    "kdebug_free_early_buf",
    "serial_keyboard_init",
    "vm_page_init_local_q",
    "vm_pageout",
)
# The two in the same tail that are deliberately *not* wrapped, each for a reason claim 1 and claim 2
# check rather than restate.
EXCLUDED = ("bsd_init", "thread_bind")

DEFINING = "TtDdBbRrSsGgVv"

# Every count this step takes, and the key it is published under. The list is here rather than read out
# of the writer because it is the *claim* - `calls` against the five `seen` counters says which of
# Apple's calls ran, `count` against `shown` says whether the census is complete, and the three tallies
# say how many children are published and matched - and a key that stopped being written would make the
# group read as a tree with no children rather than as a record that was not taken.
TAIL_KEYS = (
    "xnu_entry_tail_calls", "xnu_entry_tail_last_site",
    "xnu_entry_tail_seen0", "xnu_entry_tail_seen1", "xnu_entry_tail_seen2", "xnu_entry_tail_seen3",
    "xnu_entry_tail_seen4",
)
DTK_KEYS = (
    "xnu_entry_dtk_calls", "xnu_entry_dtk_count", "xnu_entry_dtk_shown", "xnu_entry_dtk_ok",
    "xnu_entry_dtk_dt_root", "xnu_entry_dtk_plane", "xnu_entry_dtk_recorded", "xnu_entry_dtk_root",
    "xnu_entry_dtk_same", "xnu_entry_dtk_set", "xnu_entry_dtk_kids", "xnu_entry_dtk_none",
    "xnu_entry_dtk_name00", "xnu_entry_dtk_name01", "xnu_entry_dtk_name10", "xnu_entry_dtk_name11",
    "xnu_entry_dtk_name20", "xnu_entry_dtk_name21",
    "xnu_entry_dtk_state00", "xnu_entry_dtk_state01", "xnu_entry_dtk_state10", "xnu_entry_dtk_state11",
    "xnu_entry_dtk_state20", "xnu_entry_dtk_state21",
    "xnu_entry_dtk_named", "xnu_entry_dtk_matched", "xnu_entry_dtk_registered",
)


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    """A claim satisfied by a comment is not a claim. This step's own prose names all five tail calls and
    both exclusions, so every test below runs on comment-stripped source - the same discipline 482's
    defect 217 forced on this project."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def strip_shell_comments(text):
    """Full-line `#` comments only, so the array extractor reads the same script the shell does."""
    return re.sub(r"^[ \t]*#[^\n]*$", "", text, flags=re.M)


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


def _matching(text, start, opener, closer):
    """The index of the `closer` that balances the `opener` at `start`."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opener:
            depth += 1
        elif text[index] == closer:
            depth -= 1
            if depth == 0:
                return index
    return None


def function_body(text, name):
    """The braces-balanced body of `name`'s *definition*.

    Matched from the name's own `(` rather than by a regular expression over the signature, because a
    return type on a line of its own - `static void\\nkernel_bootstrap_thread(void)` in
    `osfmk/kern/startup.c` - is a definition a one-line pattern cannot see, and the failure it produces
    is silent: no body, every claim about it vacuously refused or vacuously true. A *declaration*
    (`... ;`) is skipped because the character after the closing parenthesis is not `{`, and a call is
    skipped for the same reason. Balanced throughout, because a one-line body puts its closing brace on
    the same line and an extractor that returned nothing would make every claim about it vacuous (482's
    defect 217 found that the hard way)."""
    for match in re.finditer(r"(?<![\w])%s\s*\(" % re.escape(name), text):
        close = _matching(text, match.end() - 1, "(", ")")
        if close is None:
            continue
        start = close + 1
        while start < len(text) and text[start] in " \t\r\n":
            start += 1
        if start >= len(text) or text[start] != "{":
            continue
        end = _matching(text, start, "{", "}")
        return text[start:end + 1] if end is not None else None
    return None


def _array(text, name):
    """The *last* `NAME=(...)` assignment in the script and the text inside the parentheses. Last and
    not first, for the reason `tools/check_timer_sources.py` documents: `build_entry.sh` initialises
    both wrap arrays empty before the `if` that fills them, so a first-match extractor reads the empty
    initialiser and concludes that every wrap in the build is missing."""
    matches = re.findall(r"^\s*%s=\((.*?)\)\s*$" % re.escape(name), text, re.M | re.S)
    if not matches:
        return None
    return max(matches, key=len)


def trace_wraps(text):
    body = _array(strip_shell_comments(text), "TRACE_LDFLAGS")
    return None if body is None else re.findall(r"--wrap=([\w:]+)", body)


def pass1_wraps(text):
    body = _array(strip_shell_comments(text), "PASS1_LDFLAGS")
    return None if body is None else re.findall(r"--wrap=([\w:]+)", body)


def layout_state0_offset(text):
    """455's reading, out of the layout check that fails the build on it: the offset
    `IOService::getState`'s own `ldr r0, [r0, #N]` loads `__state[0]` at."""
    match = re.search(r"\[\[\s*\$off\s*==\s*(\d+)\s*\]\]", text)
    return int(match.group(1)) if match else None


def apple_tail(startup):
    """`{"body":…, "hits": {name: [positions]}}` for `kernel_bootstrap_thread`, or None.

    Positions rather than a first match, because "called once" is part of the claim and a `re.search`
    can only answer "called at least once" - which is the reading that would let a second call site into
    the tail unnoticed."""
    body = function_body(startup, KERNEL_THREAD)
    if body is None:
        return None
    hits = {}
    for name in WRAPPED + EXCLUDED:
        hits[name] = [m.start() for m in re.finditer(r"(?<![\w])%s\s*\(" % re.escape(name), body)]
    return {"body": body, "hits": hits}


def disassembly_functions(text):
    """`{function: [lines]}` from `objdump -d`. Only a line at the left margin is a function label, so a
    `bl <name+0x10>` - a branch into the middle of a function - cannot start one."""
    functions = {}
    current = None
    for line in text.splitlines():
        match = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
        if match:
            current = match.group(1)
            functions.setdefault(current, [])
            continue
        if current is not None:
            functions[current].append(line)
    return functions


def transfers(lines):
    """Every transfer of control to a *named* symbol in these lines, as `[(mnemonic, target)]`.

    Both a `bl` and a tail `b` count, and that is the reason this helper exists rather than a search for
    `bl`: `vm_pageout` is declared `noreturn`, so `arm-none-eabi-gcc -O2` emits the last call of the tail
    as `b __wrap_vm_pageout` after popping `{r4, lr}`. A check that looked only for `bl` would report
    that the fifth wrapper is never reached - the one reading this step exists to take."""
    out = []
    for line in lines:
        match = re.search(r"\b(bl|blx|b)\s+[0-9a-f]+\s+<([^>]+)>", line)
        if match:
            out.append((match.group(1), match.group(2)))
    return out


def movw_movt_values(lines):
    """Every 32-bit constant these lines materialise as a `movw`/`movt` pair, as a set.

    The same reconstruction `tools/check_timer_sources.py` documents, and for the same reason: the site
    a wrapper publishes goes into an *argument* (`entry_note_boot_tail(0u, (uint32_t)(uintptr_t)
    __real_X)`), so the compiler emits `movw r1, #…` and `movt r1, #…` four instructions before the
    `bl` and the address exists as no word in the image. A pair is found by looking for a `movt` and a
    `movw` to the same register within eight lines of each other."""
    values = set()
    pattern = re.compile(r"\b(movw|movt)\s+(r\d+),\s+#(\d+)")
    entries = []
    for line in lines:
        match = pattern.search(line)
        entries.append((match.group(1), match.group(2), int(match.group(3)) & 0xFFFF)
                       if match else None)
    for index, entry in enumerate(entries):
        if entry is None or entry[0] != "movt":
            continue
        for other in range(max(0, index - 8), min(len(entries), index + 9)):
            if other == index or entries[other] is None:
                continue
            kind, register, low = entries[other]
            if kind == "movw" and register == entry[1]:
                values.add((entry[2] << 16) | low)
    return values


def pool_references(pool, names):
    """`{name: {object: [symbol kinds]}}` for every name in `names`, in one `nm` over the pool.

    One process for seven hundred objects rather than one per object, and the file is taken from the
    line's own prefix (`nm` prints `path:` before the symbols of each file) - the shape
    `tools/check_timer_sources.py` established. A defining kind and a `U` are separated by the caller,
    because "one object defines it and one object calls it" is the claim and those are two answers."""
    if not os.path.isdir(pool):
        return None
    listing = sorted(f for f in os.listdir(pool) if f.endswith(".o"))
    if not listing:
        return None
    out = run([NM] + [os.path.join(pool, f) for f in listing])
    found = {name: {} for name in names}
    current = None
    for line in out.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            current = os.path.basename(line[:-1])
            continue
        parts = line.split()
        if len(parts) == 3 and parts[2] in found:
            found[parts[2]].setdefault(current, []).append(parts[1])
        elif len(parts) == 2 and parts[1] in found:
            found[parts[1]].setdefault(current, []).append(parts[0])
    return found


def wrapper_records(facts):
    """`({name: index}, {name: body})` for the wrappers `entry_trace.c` defines. A name with no body is
    absent from both, and the claims say so rather than treating it as an index of zero."""
    records = {}
    bodies = {}
    for name in WRAPPED:
        body = function_body(facts["trace"], "__wrap_" + name)
        bodies[name] = body
        if body is None:
            continue
        match = re.search(r"entry_note_boot_tail\(\s*(\d+)\s*u?\s*,", body)
        if match:
            records[name] = int(match.group(1))
    return records, bodies


# ------------------------------------------------------------------------------------------------
# Gather
# ------------------------------------------------------------------------------------------------

def gather(image):
    startup_text = read(STARTUP_C)
    startup = strip_comments(startup_text)
    trace_text = read(ENTRY_TRACE_C)
    stubs_text = read(ENTRY_STUBS_C)
    build_text = read(BUILD_ENTRY_SH)
    ioservice = read(IOSERVICE_H)
    facts = {
        "trace_text": trace_text,
        "stubs_text": stubs_text,
        "build_text": build_text,
        "ioservice": ioservice,
        "image": image,
        "startup_text": startup_text,
        "startup": startup,
        "apple": apple_tail(startup),
        "trace_defines": defines(trace_text),
        "stubs_defines": defines(stubs_text),
        "layout_state0_offset": layout_state0_offset(build_text),
        "symbols": nm(image) if image else {},
        "functions": disassembly_functions(run([OBJDUMP, "-d", image])) if image else {},
        "pool": pool_references(OBJECT_POOL, WRAPPED + EXCLUDED + (KERNEL_THREAD,)),
    }
    facts["trace"] = strip_comments(trace_text)
    facts["stubs"] = strip_comments(stubs_text)
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_apple_order(facts, failures, notes):
    """1. The five are one call each in Apple's tail, in the order the wrappers number, and the two
    exclusions sit where the argument for excluding them says they sit."""
    apple = facts["apple"]
    if apple is None:
        failures.append("osfmk/kern/startup.c no longer defines kernel_bootstrap_thread, so the tail "
                        "these five wrappers are positions in does not exist and the index each of them "
                        "publishes is a numbering of nothing")
        return

    wrong = sorted(n for n in WRAPPED + ("bsd_init",) if len(apple["hits"][n]) != 1)
    for name in wrong:
        failures.append("Apple's kernel_bootstrap_thread calls %s %d time(s), not once: a wrapper's "
                        "record is a *position* in this tail - `tail_seen[i]` says which of Apple's "
                        "calls ran - and a call site that is absent or repeated makes that counting mean "
                        "something else" % (name, len(apple["hits"][name])))
    if wrong:
        return

    measured = tuple(sorted(WRAPPED, key=lambda n: apple["hits"][n][0]))
    if measured != WRAPPED:
        failures.append("Apple's tail calls %s, and this check's table says %s: the indices the wrappers "
                        "publish are a transcription of that order, so a reordered tail would relabel "
                        "every record in the log while every file still compiled"
                        % (" then ".join(measured), " then ".join(WRAPPED)))
        return

    first = apple["hits"][WRAPPED[0]][0]
    penultimate = apple["hits"][WRAPPED[-2]][0]
    last = apple["hits"][WRAPPED[-1]][0]
    if apple["hits"]["bsd_init"][0] > first:
        failures.append("Apple's tail no longer calls `bsd_init` before the first of the five, which is "
                        "the reason this step does not wrap it: the boot's own console text is what "
                        "*it* leaves behind, and the five exist to say what happened after it returned")

    # `thread_bind` is the one name in this body that is not called once, and the exception is the
    # argument: Apple's thread binds itself to `processor` early on and to `PROCESSOR_NULL` at the end,
    # so a wrapper on the name would record a call that is nowhere near the tail (and 461's log already
    # holds a `thread_bind` whose caller is not this function at all). The check therefore claims the
    # *tail* call's position and requires every other call of the name to be outside the five.
    bind = apple["hits"]["thread_bind"]
    if not bind:
        failures.append("Apple's kernel_bootstrap_thread no longer calls `thread_bind`, so the call this "
                        "step deliberately leaves unwrapped is not in the tail and the reasoning in "
                        "entry_trace.c is about a function that has moved")
    else:
        if not penultimate < bind[-1] < last:
            failures.append("`thread_bind`'s last call in Apple's body is no longer between %s and %s: "
                            "that position is why omitting it needs an argument at all, and the argument "
                            "- that a call of it is not this tail's position - is checked in claim 2"
                            % (WRAPPED[-2], WRAPPED[-1]))
        earlier = [p for p in bind[:-1] if p > first]
        if earlier:
            failures.append("Apple's body calls `thread_bind` %d more time(s) inside or after the five "
                            "(position(s) %s): the reason this step does not wrap it is that a record of "
                            "it would fire away from the tail, and a call inside the tail would make the "
                            "omission a decision to revisit rather than a comment"
                            % (len(earlier), ", ".join(str(p) for p in earlier)))
        if len(bind) > 1 and not failures:
            notes.append("`thread_bind` is called %d times in this body - the earlier one to `processor` - "
                         "so a wrapper on the name would record a call that is not the tail" % len(bind))

    trace = trace_wraps(facts["build_text"])
    pass1 = pass1_wraps(facts["build_text"])
    if trace is None:
        failures.append("build_entry.sh no longer has a TRACE_LDFLAGS array, so whether these five are "
                        "wrapped in the traced build cannot be read out of it")
        trace = []
    if pass1 is None:
        failures.append("build_entry.sh's PASS1_LDFLAGS is unreadable, so whether a wrapper of one of "
                        "the five was asked for in the pass that does not link entry_trace.o cannot be "
                        "read out of it")
        pass1 = []
    for name in WRAPPED:
        if name not in trace:
            failures.append("--wrap=%s is not in TRACE_LDFLAGS: the wrapper is defined and never asked "
                            "for, so that position in the tail is unrecorded" % name)
        if name in pass1:
            failures.append("--wrap=%s is in PASS1_LDFLAGS. Its wrapper lives in entry_trace.o, which "
                            "pass 1 does not link, so pass 1 would report `__wrap_%s` undefined, the "
                            "generator would emit a definition of it, and that definition would collide "
                            "with the real wrapper in the final link - or, worse, the stand-in would win "
                            "in the pass that decides the addresses" % (name, name))
    for name in EXCLUDED:
        if name in trace:
            failures.append("--wrap=%s is in TRACE_LDFLAGS, and this step's design says it must not be: "
                            "%s" % (name, "its completion is already the console text the run reads, and "
                                          "a second record of it would be a second definition of the "
                                          "same thing" if name == "bsd_init" else
                                          "it is called from all over the kernel, so a record of it would "
                                          "name no position in this tail"))
    if not failures:
        notes.append("Apple's kernel_bootstrap_thread calls %s in that order, and TRACE_LDFLAGS wraps "
                     "exactly those five and neither of the two beside them" % ", ".join(WRAPPED))


def claim_exclusive(facts, failures, notes):
    """2. Each of the five is called from exactly one place in this kernel - the boot thread's own
    object - and the exclusion of `thread_bind` still has its reason."""
    pool = facts["pool"]
    if pool is None:
        failures.append("the object pool (out/xnu_kernel_obj) is not there, so the one claim that makes "
                        "a record of one of the five a *position* cannot be made: that this kernel calls "
                        "each of them from exactly one place")
        return

    referrers = {}
    for name in WRAPPED + EXCLUDED:
        entry = pool.get(name) or {}
        defining = sorted(o for o, kinds in entry.items() if any(k in DEFINING for k in kinds))
        refs = sorted(o for o, kinds in entry.items() if "U" in kinds)
        if not entry:
            failures.append("no object in this pool mentions %s at all, so the call this step wraps is "
                            "not in the kernel being linked" % name)
            continue
        if len(defining) != 1:
            failures.append("%s is defined by %d objects (%s): the wrapper calls `__real_%s`, and a "
                            "second definition is a second function behind one record"
                            % (name, len(defining), ", ".join(defining) or "none", name))
        referrers[name] = refs

    singles = []
    for name in WRAPPED:
        refs = referrers.get(name)
        if refs is None:
            continue
        if len(refs) != 1:
            failures.append("%s is referenced by %d objects (%s), not one: a wrapper's record would then "
                            "count every caller of %s, which is not a position in the boot thread's tail"
                            % (name, len(refs), ", ".join(refs), name))
        else:
            singles.append(refs[0])

    if len(singles) == len(WRAPPED):
        if len(set(singles)) != 1:
            failures.append("the five are referenced by %d different objects (%s): they would not be "
                            "five calls in one thread's tail, and the index each wrapper publishes would "
                            "not order anything" % (len(set(singles)), ", ".join(sorted(set(singles)))))
        else:
            startup_object = singles[0]
            defines_thread = pool.get(KERNEL_THREAD, {}).get(startup_object, [])
            if not any(k in DEFINING for k in defines_thread):
                failures.append("the five are all referenced by %s, which does not define %s: the "
                                "wrappers' positions are positions in *that* thread's tail, and the "
                                "object that makes all five calls is what says so"
                                % (startup_object, KERNEL_THREAD))
            else:
                notes.append("all five are referenced by one object (%s), and it is the object that "
                             "defines %s" % (startup_object, KERNEL_THREAD))

    bind = referrers.get("thread_bind")
    if bind is not None and len(bind) < 2:
        failures.append("`thread_bind` is referenced by exactly one object (%s), and this step's stated "
                        "reason for not wrapping it is that it is called from all over the kernel and so "
                        "names no position. A reason that has stopped being true is a decision to remake, "
                        "not a comment to leave - and with one caller the wrapper would be a position and "
                        "the omission would be the arbitrary choice" % ", ".join(bind))
    elif bind is not None and not failures:
        notes.append("`thread_bind` is referenced by %d objects, so the argument for leaving it "
                     "unwrapped is still the tree's own shape" % len(bind))


def claim_wrappers(facts, failures, notes):
    """3. Each wrapper records before the call it wraps and publishes the function it calls; the census
    runs inside the fifth wrapper between the two; nothing runs after the call that never returns."""
    records, bodies = wrapper_records(facts)
    expected = {}
    apple = facts["apple"]
    # `thread_bind` is called twice in Apple's body and is not one of the five, so the guard is over
    # WRAPPED and not over every name the tail census found: testing all of `hits` made `expected` empty
    # in every run, and an empty expectation is a claim about indices that never runs at all.
    if apple is not None and all(len(apple["hits"][name]) == 1 for name in WRAPPED):
        order = sorted(WRAPPED, key=lambda n: apple["hits"][n][0])
        expected = {name: order.index(name) for name in WRAPPED}

    for name in WRAPPED:
        body = bodies.get(name)
        if body is None:
            failures.append("entry_trace.c no longer defines __wrap_%s, so that call of Apple's tail "
                            "goes through unwrapped and the run cannot say whether it ran" % name)
            continue
        if body.count("entry_note_boot_tail(") != 1:
            failures.append("entry_trace.c's __wrap_%s calls entry_note_boot_tail %d times: a record is "
                            "one position, and a second write of it is a second definition"
                            % (name, body.count("entry_note_boot_tail(")))
        if not re.search(r"entry_note_boot_tail\(\s*\d+\s*u?\s*,\s*"
                         r"\(uint32_t\)\(uintptr_t\)__real_%s\s*\)" % re.escape(name), body):
            failures.append("entry_trace.c's __wrap_%s does not publish `(uint32_t)(uintptr_t)"
                            "__real_%s` as its site: the address in the log is then either this wrapper's "
                            "own - which would put all five in one place, since they are the same kind "
                            "of function - or a number no symbol is behind" % (name, name))

        note = body.find("entry_note_boot_tail(")
        real = body.find("__real_%s(" % name)
        if real < 0:
            failures.append("entry_trace.c's __wrap_%s never calls __real_%s: the wrapper records a "
                            "position and does not perform the call it names" % (name, name))
        elif note >= 0 and note > real:
            failures.append("entry_trace.c's __wrap_%s records *after* the call it wraps. For the fifth "
                            "of these that can only be a record of a return that cannot happen (%s is "
                            "followed by Apple's own NOTREACHED), and for the four the same ordering is "
                            "what makes the last record written the position the boot reached"
                            % (name, name))
        if name in expected and name in records and records[name] != expected[name]:
            failures.append("entry_trace.c's __wrap_%s publishes index %d and Apple's tail has that call "
                            "at position %d: the index is what tail_seen[] counts, so a wrong one "
                            "attributes a call that ran to a call that did not"
                            % (name, records[name], expected[name]))

    if records and len(set(records.values())) != len(records):
        failures.append("two of the wrappers publish the same index (%s), so the five counters in "
                        "tail_seen[] cannot separate them" % sorted(records.values()))

    last = WRAPPED[-1]
    body = bodies.get(last) or ""
    if body:
        note = body.find("entry_note_boot_tail(")
        real = body.find("__real_%s(" % last)
        probe = body.find("entry_probe_dt_children()")
        if probe < 0:
            failures.append("entry_trace.c's __wrap_%s does not call entry_probe_dt_children, so the "
                            "device tree census - this step's second reading - is not taken" % last)
        elif not note < probe < real:
            failures.append("entry_trace.c's __wrap_%s does not run the census between its own record and "
                            "the call: after the call it would never run (%s never returns), and before "
                            "the record it would be a read taken outside the position it belongs to"
                            % (last, last))
        if real >= 0:
            rest = body[real:]
            end = rest.find(";")
            after = rest[end + 1:-1] if end >= 0 else rest
            if ";" in after:
                failures.append("entry_trace.c's __wrap_%s runs a statement after __real_%s returns, and "
                                "this step's whole reading is that the call is the end of the kernel's "
                                "boot: a record placed there is either dead code or - worse - the reason "
                                "the run would keep going" % (last, last))
    if not failures:
        notes.append("each of the five records its position before performing the call, the indices are "
                     "Apple's order, and the census runs inside the fifth between them")


def claim_bounds(facts, failures, notes):
    """4. The tail count is one value read twice, the census's cap fits the table it writes into, and the
    state offset agrees with the layout check that pins it."""
    stubs = facts["stubs_defines"]
    trace_defines = facts["trace_defines"]

    count = stubs.get("ENTRY_TAIL_CALLS")
    if count is None:
        failures.append("entry_stubs.c no longer defines ENTRY_TAIL_CALLS, so the array that counts the "
                        "five calls and the guard that keeps a ninth index out of it are two literals "
                        "again - this project's oldest defect class, and here its failure would be a "
                        "store past the end of a `.bss` array in the kernel's own boot thread")
    elif count != len(WRAPPED):
        failures.append("ENTRY_TAIL_CALLS is %d and entry_trace.c defines %d wrappers: the counter array "
                        "and the wrappers disagree about how many positions Apple's tail has"
                        % (count, len(WRAPPED)))
    declaration = re.search(r"uint32_t\s+g_boot_tail_seen\s*\[\s*([^\]]+?)\s*\]", facts["stubs"])
    if declaration is None:
        failures.append("entry_stubs.c no longer declares g_boot_tail_seen[] as a `uint32_t` array, so "
                        "the five counters the tail group publishes have no declared type or size")
    elif declaration.group(1).strip() != "ENTRY_TAIL_CALLS":
        failures.append("entry_stubs.c sizes g_boot_tail_seen[] with `%s` instead of ENTRY_TAIL_CALLS: "
                        "one value, two definitions, neither compared - the wrapper count is the other "
                        "one, and a size that is too small is a store past the end of a `.bss` array in "
                        "the kernel's boot thread" % declaration.group(1).strip())
    if not re.search(r"index\s*<\s*ENTRY_TAIL_CALLS", facts["stubs"]):
        failures.append("entry_stubs.c's entry_note_boot_tail no longer guards its store with "
                        "`index < ENTRY_TAIL_CALLS`, so an index outside the array stops being refused "
                        "at the one place that can see it")

    note = function_body(facts["stubs"], "entry_note_boot_tail")
    if note is None:
        failures.append("entry_stubs.c no longer defines entry_note_boot_tail, so the wrappers' records "
                        "have nowhere to go")
    else:
        for key in ("xnu_live_tail_seq", "xnu_live_tail_idx", "xnu_live_tail_site"):
            if key not in note:
                failures.append("entry_note_boot_tail no longer publishes %s: the live channel is the "
                                "only copy of the tail's position when the report path is not reached, "
                                "and 459 measured a report buffer that was full while the run went on"
                                % key)
        if not re.search(r"if\s*\(index\s*<\s*ENTRY_TAIL_CALLS\)\s*\n\s*g_boot_tail_seen\[index\]\+\+",
                         note):
            failures.append("entry_note_boot_tail no longer counts into g_boot_tail_seen[index] behind "
                            "its guard: the count per call is what separates 'the fifth call ran' from "
                            "'the fifth call never happened', which a last-value slot cannot say")

    shown = stubs.get("ENTRY_DTK_SHOWN")
    cap = trace_defines.get("STAGE90_DTK_MAX")
    if shown is None:
        failures.append("entry_stubs.c no longer defines ENTRY_DTK_SHOWN, so the four tables the census "
                        "stores a child into have no declared size")
    elif shown <= 0:
        failures.append("ENTRY_DTK_SHOWN is %d, so the census stores no child at all" % shown)
    if cap is None:
        failures.append("entry_trace.c no longer defines STAGE90_DTK_MAX, so the walk has no cap of its "
                        "own and would record a child per node the tree happens to have")
    elif shown is not None and cap > shown:
        failures.append("entry_trace.c's STAGE90_DTK_MAX is %d and entry_stubs.c's ENTRY_DTK_SHOWN is "
                        "%d: entry_note_dtchild indexes g_dtk_name0[seq] and its three siblings with the "
                        "caller's index, so the cap has to fit inside the table" % (cap, shown))
    if not re.search(r"i\s*<\s*n\s*&&\s*i\s*<\s*STAGE90_DTK_MAX", facts["trace"]):
        failures.append("entry_probe_dt_children's loop no longer stops at STAGE90_DTK_MAX as well as at "
                        "the tree's own count, so the cap is not what bounds the tables")

    if not re.search(r"IOOptionBits\s+__state\s*\[\s*2\s*\]", facts["ioservice"]):
        failures.append("IOService.h no longer declares `__state` as a two-word array, and this census "
                        "reads the second word as the first plus one word")
    state0 = trace_defines.get("STAGE90_DTK_STATE0_OFF")
    pinned = facts["layout_state0_offset"]
    if state0 is None:
        failures.append("entry_trace.c no longer defines STAGE90_DTK_STATE0_OFF, so `__state[0]` is "
                        "spelled as a number at the read and nothing compares it against the layout "
                        "check that pins it")
    elif pinned is None:
        failures.append("build_entry.sh no longer pins IOService::getState's `ldr r0, [r0, #N]`, so the "
                        "offset entry_trace.c reads __state[0] at has no second source to agree with")
    elif state0 != pinned:
        failures.append("entry_trace.c reads __state[0] at +%d and build_entry.sh's layout check pins "
                        "getState's own load at +%d: the census would publish the word beside the one "
                        "the match's own test reads" % (state0, pinned))
    if not re.search(r"STAGE90_DTK_STATE0_OFF\s*\+\s*4u", facts["trace"]):
        failures.append("entry_trace.c no longer reads the second state word at the pinned offset plus "
                        "the one word IOService.h's `__state[2]` declares")
    if not failures:
        notes.append("ENTRY_TAIL_CALLS = %s is read by both the array and the guard, the census's cap "
                     "%s fits inside ENTRY_DTK_SHOWN = %s, and the state offset agrees with 455's layout "
                     "reading" % (count, cap, shown))


def claim_census(facts, failures, notes):
    """5. The census counts with Apple's bits, over every child, and publishes every count."""
    ioservice = facts["ioservice"]
    stubs = facts["stubs"]
    trace = facts["trace"]

    body = function_body(stubs, "entry_note_dtchild")
    if body is None:
        failures.append("entry_stubs.c no longer defines entry_note_dtchild, so the device tree census "
                        "has nowhere to record a child")
        return

    masks = {}
    for member in ("kIOServiceRegisteredState", "kIOServiceMatchedState"):
        match = re.search(r"\b%s\s*=\s*(0x[0-9a-fA-F]+|\d+)" % member, ioservice)
        masks[member] = int(match.group(1), 0) if match else None
    for member, tally in (("kIOServiceRegisteredState", "g_dtk_registered"),
                          ("kIOServiceMatchedState", "g_dtk_matched")):
        value = masks[member]
        if value is None:
            failures.append("IOService.h no longer declares %s, so the bit this census counts has no "
                            "source but the literal in entry_stubs.c - and a table of state words with "
                            "an uncompared bit in it is a table nobody can read without the header"
                            % member)
            continue
        if not re.search(r"\(state0\s*&\s*0x%xu\)" % value, body):
            failures.append("entry_note_dtchild no longer counts %s as `state0 & 0x%xu` - Apple's own "
                            "%s - so the number published under that key is a claim about a word with "
                            "nothing to compare it against" % (tally, value, member))
        if "%s++" % tally not in body:
            failures.append("entry_note_dtchild never increments %s, so the count published under its "
                            "key is zero whatever the tree holds" % tally)

    if not re.search(r"if\s*\(name0\s*!=\s*0u\s*\|\|\s*name1\s*!=\s*0u\)\s*\n\s*g_dtk_named\+\+", body):
        failures.append("entry_note_dtchild's g_dtk_named is no longer guarded by the test that its name "
                        "is non-zero: the key says how many children *had a name*, and an unguarded count "
                        "would answer 'how many children' under the same key")
    if not re.search(r"g_dtk_shown\+\+", body) or re.search(r"if\s*\([^)]*\)\s*\n\s*g_dtk_shown\+\+", body):
        failures.append("entry_note_dtchild's g_dtk_shown is no longer the count of every child it was "
                        "handed: `count` and `shown` together are what say whether the table is complete "
                        "- 21 children and 24 shown is a census, and 3 shown out of 21 that reads as a "
                        "complete table is not")
    for seq_store in ("g_dtk_name0[seq] = name0;", "g_dtk_name1[seq] = name1;",
                      "g_dtk_state0[seq] = state0;", "g_dtk_state1[seq] = state1;"):
        if seq_store not in body:
            failures.append("entry_note_dtchild no longer stores `%s`: a table indexed by the caller's "
                            "index is the only thing that lets the report's name00/state00 keys be read "
                            "as the same child" % seq_store)
    for key in ("xnu_live_dtk_seq", "xnu_live_dtk_child", "xnu_live_dtk_name0", "xnu_live_dtk_name1",
                "xnu_live_dtk_state0", "xnu_live_dtk_state1"):
        if key not in body:
            failures.append("entry_note_dtchild no longer publishes %s: the live channel is what a run "
                            "reads when the report path never gets there, and 459 measured a report "
                            "buffer that was full while the run went on" % key)

    writer = function_body(stubs, "entry_write_485_kv")
    if writer is None:
        failures.append("entry_stubs.c no longer defines entry_write_485_kv, so nothing writes the "
                        "boot thread's position or the device tree census into the report, and the run's "
                        "only copy is the live channel")
    else:
        for key in TAIL_KEYS + DTK_KEYS:
            if key not in writer:
                failures.append("entry_write_485_kv no longer publishes %s: a key that is not written "
                                "reads in the report as a count of zero - 'the tail was never reached' "
                                "and 'the reader was not asked' have to be different logs" % key)
    calls = len(re.findall(r"entry_write_485_kv\(\)", stubs))
    if calls != 1:
        failures.append("entry_stubs.c calls entry_write_485_kv() %d times and not once: the writer's "
                        "keys exist in the report only if the report path calls it" % calls)

    probe = function_body(trace, "entry_probe_dt_children")
    if probe is None:
        failures.append("entry_trace.c no longer defines entry_probe_dt_children, so the census has no "
                        "walk and no reader of the tree")
        return
    for call, why in (("entry_xnu_child_entry(entry_xnu_registry_root(), plane)",
                       "the root the OS's own walk starts from - `getRegistryRoot()` and then its child "
                       "in gIODTPlane, which is what `fromPath` itself does. 485's first run walked "
                       "461's recorded root instead and read no children of it while the registry's own "
                       "child had 21, so a comment naming the root is not a claim about the root"),
                      ("entry_xnu_child_count(root, plane)",
                       "`IORegistryEntry::getChildCount`, the entry's own answer beside the array's"),
                      ("entry_xnu_child_set(root, plane)",
                       "the OS's own `IORegistryEntry::getChildSetReference` - the accessor the "
                       "match's own walk uses"),
                      ("entry_xnu_array_count(set)",
                       "`OSArray::getCount`, so `count` is the array's own answer"),
                      ("entry_xnu_array_object(set, i)",
                       "`OSArray::getObject`, which does not retain - the census must not change the "
                       "reference counts of the tree it reads"),
                      ("entry_xnu_entry_name(child, plane)",
                       "`IORegistryEntry::getName(plane)`, the name the tree itself answers with"),
                      ("entry_xnu_entry_state(child)",
                       "`IOService::getState`, the one implementation `build_entry.sh` counts")):
        if call not in probe:
            failures.append("entry_probe_dt_children no longer calls %s, so it is not reading %s"
                            % (call, why))
    # The two names for the root are published *as a comparison*, not one of them chosen: the recorded
    # one is 461's reading and the live one is the registry's, and a census whose log cannot say which
    # object it was about is a census of nothing in particular.
    if not re.search(r"entry_note_dtbegin\(\s*\(uint32_t\)\(uintptr_t\)plane\s*,\s*"
                     r"\(uint32_t\)\(uintptr_t\)recorded\s*,", probe):
        failures.append("entry_probe_dt_children no longer publishes both roots to entry_note_dtbegin: "
                        "the recorded root (461's reading of IODeviceTreeAlloc's return) and the "
                        "registry's are different objects in this boot, and which one the count is about "
                        "is the whole reading")
    if not re.search(r"\(root\s*==\s*recorded\)\s*\?\s*1u\s*:\s*0u", probe):
        failures.append("entry_probe_dt_children no longer compares the two roots, so 'the census read "
                        "the tree' and 'the census read something else that happens to have children' "
                        "print the same record")
    for why, meaning in ((0, "there is no IODT plane"),
                         (1, "the registry root has no child in it - i.e. the tree is detached, which is "
                             "a different fact from an empty tree")):
        if "entry_note_dtnone(%du)" % why not in probe:
            failures.append("entry_probe_dt_children no longer reports the reason it took no reading at "
                            "all when %s (%d): a census that returns silently and a census that found no "
                            "children have to be different records, and the first run of this step is "
                            "what a silent return costs" % (meaning, why))
    if not re.search(r"entry_note_dtchild\(\s*i\s*,", probe):
        failures.append("entry_probe_dt_children's loop no longer records each child, so the names and "
                        "state words the census exists for are not written")
    if "entry_str8(name, &name0, &name1)" not in probe:
        failures.append("entry_probe_dt_children no longer copies the name's first eight bytes into the "
                        "record with entry_str8: a pointer names nothing in a log written after the tree "
                        "has moved on, which is 484's reason for the same choice")
    for mangled in ("_ZN15IORegistryEntry15getRegistryRootEv",
                    "_ZNK15IORegistryEntry13getChildEntryEPK15IORegistryPlane",
                    "_ZNK15IORegistryEntry13getChildCountEPK15IORegistryPlane",
                    "_ZNK15IORegistryEntry20getChildSetReferenceEPK15IORegistryPlane",
                    "_ZNK7OSArray8getCountEv", "_ZNK7OSArray9getObjectEj",
                    "_ZNK15IORegistryEntry7getNameEPK15IORegistryPlane",
                    "_ZNK9IOService8getStateEv"):
        if mangled not in trace:
            failures.append("entry_trace.c no longer names %s: a C translation unit calls a C++ member "
                            "by a spelling, and a spelling that is one character wrong links - to a "
                            "generated stand-in (455's defect)" % mangled)

    # The notes' own live records, per function, so a reading that only exists in the report group -
    # which is exactly what this step's first run lost - fails the build instead of the run.
    for function, keys in (("entry_note_dtbegin",
                            ("xnu_live_dtk_calls", "xnu_live_dtk_plane", "xnu_live_dtk_recorded",
                             "xnu_live_dtk_root", "xnu_live_dtk_same")),
                           ("entry_note_dtset", ("xnu_live_dtk_set", "xnu_live_dtk_kids")),
                           ("entry_note_dtcount", ("xnu_live_dtk_count",)),
                           ("entry_note_dtnone", ("xnu_live_dtk_none",))):
        note = function_body(stubs, function)
        if note is None:
            failures.append("entry_stubs.c no longer defines %s, so the census's records have nowhere "
                            "to go" % function)
            continue
        for key in keys:
            if key not in note:
                failures.append("%s no longer publishes %s: the live channel is the only copy of the "
                                "census in the run that matters - the one where the kernel reaches "
                                "`vm_pageout` and never gives the payload its report path back" %
                                (function, key))
    if not re.search(r"g_dtk_calls\+\+", function_body(stubs, "entry_note_dtbegin") or ""):
        failures.append("entry_note_dtbegin no longer counts its own calls, so 'the census ran once' is "
                        "a claim about a record that may be the only one written")
    if not failures:
        notes.append("the census reads the tree through the OS's own accessors from the root the OS's "
                     "own walk uses, publishes both candidate roots and the two child counts beside each "
                     "other, and puts every one of those records in the live channel as well as the "
                     "report")


def claim_image(facts, failures, notes):
    """6. The instrument is in the image, at the tail's own call sites, in Apple's order."""
    symbols = facts["symbols"]
    functions = facts["functions"]

    for name in WRAPPED:
        plain = symbols.get(name)
        wrapper = symbols.get("__wrap_" + name)
        if plain is None:
            failures.append("the image defines no %s, so the wrapper for it calls the generator's "
                            "stand-in: a `--wrap` whose target is stubbed measures the stub" % name)
        elif wrapper is None:
            failures.append("the image defines %s and no __wrap_%s: this build asked for the wrap and "
                            "linked no wrapper, so that position in the tail is unrecorded"
                            % (name, name))
        elif plain == wrapper:
            failures.append("__wrap_%s and %s are the same address in this image, so the wrapper *is* "
                            "the call it was written to wrap" % (name, name))
    for symbol in ("entry_note_boot_tail", "entry_probe_dt_children", "entry_note_dtchild"):
        if symbol not in symbols:
            failures.append("the image does not define %s: the instrument this step writes its two "
                            "readings with is not in the build" % symbol)

    lines = functions.get(KERNEL_THREAD)
    if lines is None:
        failures.append("the image has no kernel_bootstrap_thread, so the tail this step wraps is not "
                        "in it and the four `bl`s the run would take do not exist")
    else:
        wrapped_targets = [t for _kind, t in transfers(lines) if t.startswith("__wrap_")]
        measured = tuple(sorted((t[len("__wrap_"):] for t in wrapped_targets),
                                key=lambda n: wrapped_targets.index("__wrap_" + n)))
        for name in WRAPPED:
            if "__wrap_" + name not in wrapped_targets:
                failures.append("kernel_bootstrap_thread's own text in this image never transfers to "
                                "__wrap_%s: the wrap is in the build and the reference at this call site "
                                "was not rewritten, so the wrapper never runs and that position is "
                                "unrecorded - a `--wrap` is a property of a reference and only the "
                                "linked image can say whether it took" % name)
        extra = sorted(set(measured) - set(WRAPPED))
        if extra:
            failures.append("kernel_bootstrap_thread transfers to %s, which this step does not wrap: an "
                            "unaccounted wrapper in the tail is a fifth reading nobody declared"
                            % ", ".join("__wrap_" + n for n in extra))
        # The order the transfers appear in *is* the order they are executed in, so a tail that runs its
        # five calls in another order than Apple's source has them would write indices that disagree with
        # the sequence the log holds - and the pair `tail_seq`/`tail_idx` is exactly that comparison.
        if set(measured) == set(WRAPPED) and measured != WRAPPED:
            failures.append("kernel_bootstrap_thread transfers to its wrappers in the order %s and not "
                            "Apple's %s: each wrapper publishes its index from Apple's source, so the "
                            "order the calls actually run in would disagree with the number beside it in "
                            "the log" % (", ".join(measured), ", ".join(WRAPPED)))
        if measured == WRAPPED:
            notes.append("kernel_bootstrap_thread transfers to the five wrappers in Apple's order, the "
                         "last of them as a tail branch because vm_pageout is `noreturn`")

    for name in WRAPPED:
        wrapper_lines = functions.get("__wrap_" + name)
        plain = symbols.get(name)
        if wrapper_lines is None or plain is None:
            continue
        if plain not in movw_movt_values(wrapper_lines):
            failures.append("__wrap_%s's own code does not materialise this image's %s address (%#x) as "
                            "a constant: the site it publishes in the log is then not the function it "
                            "calls" % (name, name, plain))
    if not failures:
        notes.append("all five wrappers are linked at addresses of their own and each publishes its real "
                     "function's address")


CLAIMS = (claim_apple_order, claim_exclusive, claim_wrappers, claim_bounds, claim_census, claim_image)


def compare(facts, mutate=None):
    if mutate is not None:
        facts = mutate_facts(facts, mutate)
    failures, notes = [], []
    for claim in CLAIMS:
        claim(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["trace_defines"] = dict(facts["trace_defines"])
    facts["stubs_defines"] = dict(facts["stubs_defines"])
    facts["symbols"] = dict(facts["symbols"])
    facts["functions"] = {name: list(lines) for name, lines in facts["functions"].items()}
    facts["pool"] = (None if facts["pool"] is None
                     else {name: {obj: list(kinds) for obj, kinds in entry.items()}
                           for name, entry in facts["pool"].items()})

    def rederive_trace(text):
        facts["trace_text"] = text
        facts["trace"] = strip_comments(text)
        facts["trace_defines"] = defines(text)

    def rederive_stubs(text):
        facts["stubs_text"] = text
        facts["stubs"] = strip_comments(text)
        facts["stubs_defines"] = defines(text)

    def rederive_startup(text):
        facts["startup"] = strip_comments(text)
        facts["apple"] = apple_tail(facts["startup"])

    def rederive_build(text):
        facts["build_text"] = text
        facts["layout_state0_offset"] = layout_state0_offset(text)

    if mutate == "apple_reorders_the_tail":
        rederive_startup(_bump(facts["startup_text"],
                               "\tserial_keyboard_init();\t\t/* Start serial keyboard if wanted */\n",
                               "\tvm_page_init_local_q();\n"))
        rederive_startup(_bump(facts["startup_text"], "\tvm_page_init_local_q();\n\n\tthread_bind",
                               "\tserial_keyboard_init();\t\t/* Start serial keyboard if wanted */\n"
                               "\n\tthread_bind"))
    elif mutate == "apple_drops_a_tail_call":
        rederive_startup(_bump(facts["startup_text"], "\tkdebug_free_early_buf();\n", ""))
    elif mutate == "apple_calls_one_twice":
        rederive_startup(_bump(facts["startup_text"], "\tvm_page_init_local_q();\n",
                               "\tvm_page_init_local_q();\n\tvm_page_init_local_q();\n"))
    elif mutate == "apple_moves_thread_bind_out_of_the_tail":
        rederive_startup(_bump(facts["startup_text"], "\tthread_bind(PROCESSOR_NULL);\n", ""))
    elif mutate == "apple_moves_bsd_init_after_the_five":
        rederive_startup(_bump(facts["startup_text"], "\tbsd_init();\n", ""))
        rederive_startup(_bump(facts["startup_text"], "\tvm_page_init_local_q();\n",
                               "\tbsd_init();\n\tvm_page_init_local_q();\n"))
    elif mutate == "a_wrap_is_dropped_from_the_build":
        rederive_build(_bump(facts["build_text"], " --wrap=vm_pageout)", ")"))
    elif mutate == "the_excluded_one_is_wrapped":
        rederive_build(_bump(facts["build_text"], " --wrap=vm_pageout)",
                             " --wrap=vm_pageout --wrap=thread_bind)"))
    elif mutate == "a_wrap_moves_to_pass_one":
        rederive_build(_bump(facts["build_text"],
                             "PASS1_LDFLAGS=(--wrap=PE_init_platform --wrap=fiq_context_init)",
                             "PASS1_LDFLAGS=(--wrap=vm_pageout)"))
    elif mutate == "wrapper_index_swapped":
        # Replaced with the call each one wraps in the pattern, because a bare `entry_note_boot_tail(0u,`
        # is not unique once the first swap has run and the second `_bump` would then undo the first -
        # a mutation that mutates nothing, which is what this selftest's first run reported.
        rederive_trace(_bump(facts["trace_text"],
                             "entry_note_boot_tail(0u, (uint32_t)(uintptr_t)__real_"
                             "OSKextRemoveKextBootstrap)",
                             "entry_note_boot_tail(1u, (uint32_t)(uintptr_t)__real_"
                             "OSKextRemoveKextBootstrap)"))
        rederive_trace(_bump(facts["trace_text"],
                             "entry_note_boot_tail(1u, (uint32_t)(uintptr_t)__real_"
                             "kdebug_free_early_buf)",
                             "entry_note_boot_tail(0u, (uint32_t)(uintptr_t)__real_"
                             "kdebug_free_early_buf)"))
    elif mutate == "wrapper_index_is_a_name":
        rederive_trace(_bump(facts["trace_text"], "entry_note_boot_tail(1u,",
                             "entry_note_boot_tail(TAIL_SECOND,"))
    elif mutate == "the_site_is_the_wrapper":
        rederive_trace(_bump(facts["trace_text"],
                             "entry_note_boot_tail(0u, (uint32_t)(uintptr_t)__real_"
                             "OSKextRemoveKextBootstrap)",
                             "entry_note_boot_tail(0u, (uint32_t)(uintptr_t)__wrap_"
                             "OSKextRemoveKextBootstrap)"))
    elif mutate == "the_record_follows_the_call":
        rederive_trace(_bump(facts["trace_text"],
                             "    entry_note_boot_tail(2u, (uint32_t)(uintptr_t)__real_serial_keyboard_init);\n"
                             "    __real_serial_keyboard_init();",
                             "    __real_serial_keyboard_init();\n"
                             "    entry_note_boot_tail(2u, (uint32_t)(uintptr_t)__real_serial_keyboard_init);"))
    elif mutate == "a_wrapper_loses_its_record":
        rederive_trace(_bump(facts["trace_text"],
                             "    entry_note_boot_tail(3u, (uint32_t)(uintptr_t)__real_vm_page_init_local_q);\n",
                             ""))
    elif mutate == "the_census_moves_after_the_call":
        rederive_trace(_bump(facts["trace_text"],
                             "    entry_probe_dt_children();\n    __real_vm_pageout();",
                             "    __real_vm_pageout();\n    entry_probe_dt_children();"))
    elif mutate == "the_census_is_dropped":
        rederive_trace(_bump(facts["trace_text"],
                             "    entry_probe_dt_children();\n    __real_vm_pageout();",
                             "    __real_vm_pageout();"))
    elif mutate == "something_runs_after_the_last_call":
        rederive_trace(_bump(facts["trace_text"], "    __real_vm_pageout();\n",
                             "    __real_vm_pageout();\n    g_dtk_calls++;\n"))
    elif mutate == "the_last_wrapper_calls_nothing":
        rederive_trace(_bump(facts["trace_text"], "    __real_vm_pageout();\n", ""))
    elif mutate == "the_census_cap_is_lifted":
        rederive_trace(_bump(facts["trace_text"], "i < n && i < STAGE90_DTK_MAX", "i < n"))
    elif mutate == "the_census_walks_the_recorded_root":
        rederive_trace(_bump(facts["trace_text"],
                             "root = entry_xnu_child_entry(entry_xnu_registry_root(), plane);",
                             "root = (void *)(uintptr_t)g_dtplane_root;"))
    elif mutate == "the_two_roots_are_not_compared":
        rederive_trace(_bump(facts["trace_text"], "(root == recorded) ? 1u : 0u", "1u"))
    elif mutate == "the_detached_tree_is_silent":
        rederive_trace(_bump(facts["trace_text"], "        entry_note_dtnone(1u);\n", ""))
    elif mutate == "the_entry_child_count_is_dropped":
        rederive_trace(_bump(facts["trace_text"], "    kids = entry_xnu_child_count(root, plane);\n",
                             "    kids = 0u;\n"))
    elif mutate == "a_live_census_record_is_dropped":
        rederive_stubs(_bump(facts["stubs_text"],
                             '    entry_live_write("xnu_live_dtk_root", root);\n', ""))
    elif mutate == "the_two_reasons_become_one":
        rederive_trace(_bump(facts["trace_text"], "        entry_note_dtnone(1u);",
                             "        entry_note_dtnone(0u);"))
    elif mutate == "a_census_key_is_dropped":
        rederive_stubs(_bump(facts["stubs_text"],
                             '    entry_write_kv("xnu_entry_dtk_same", g_dtk_same);\n', ""))
    elif mutate == "the_first_state_word_is_read_raw":
        rederive_trace(_bump(facts["trace_text"], "state0 = entry_xnu_entry_state(child);",
                             "state0 = *(volatile uint32_t *)((const char *)child + "
                             "STAGE90_DTK_STATE0_OFF);"))
    elif mutate == "the_second_state_word_moves_a_word_back":
        rederive_trace(_bump(facts["trace_text"], "STAGE90_DTK_STATE0_OFF + 4u",
                             "STAGE90_DTK_STATE0_OFF"))
    elif mutate == "the_mangled_accessor_is_dropped":
        rederive_trace(_bump(facts["trace_text"],
                             '__asm__("_ZNK7OSArray8getCountEv")', '__asm__("_ZNK7OSArray8getCountE")'))
    elif mutate == "the_tail_count_is_spelled_again":
        rederive_stubs(_bump(facts["stubs_text"], "g_boot_tail_seen[ENTRY_TAIL_CALLS]",
                             "g_boot_tail_seen[5]"))
    elif mutate == "the_guard_uses_a_literal":
        rederive_stubs(_bump(facts["stubs_text"], "index < ENTRY_TAIL_CALLS", "index < 5u"))
    elif mutate == "the_tail_count_shrinks":
        rederive_stubs(_bump(facts["stubs_text"], "#define ENTRY_TAIL_CALLS 5u",
                             "#define ENTRY_TAIL_CALLS 4u"))
    elif mutate == "the_table_shrinks_below_the_cap":
        rederive_stubs(_bump(facts["stubs_text"], "#define ENTRY_DTK_SHOWN 24u",
                             "#define ENTRY_DTK_SHOWN 4u"))
    elif mutate == "the_matched_bit_moves":
        rederive_stubs(_bump(facts["stubs_text"], "(state0 & 0x4u)", "(state0 & 0x1u)"))
    elif mutate == "the_registered_tally_is_dropped":
        rederive_stubs(_bump(facts["stubs_text"], "        g_dtk_registered++;\n", ""))
    elif mutate == "the_shown_count_becomes_conditional":
        rederive_stubs(_bump(facts["stubs_text"], "    g_dtk_shown++;\n",
                             "    if (0)\n        g_dtk_shown++;\n"))
    elif mutate == "the_named_tally_loses_its_guard":
        rederive_stubs(_bump(facts["stubs_text"],
                             "    if (name0 != 0u || name1 != 0u)\n        g_dtk_named++;\n",
                             "    g_dtk_named++;\n"))
    elif mutate == "the_array_store_uses_a_literal":
        rederive_stubs(_bump(facts["stubs_text"], "g_dtk_name0[seq] = name0;", "g_dtk_name0[0] = name0;"))
    elif mutate == "a_live_record_is_dropped":
        rederive_stubs(_bump(facts["stubs_text"],
                             '    entry_live_write("xnu_live_tail_seq", g_boot_tail_calls);\n', ""))
    elif mutate == "a_key_is_dropped":
        rederive_stubs(_bump(facts["stubs_text"],
                             '    entry_write_kv("xnu_entry_dtk_matched", g_dtk_matched);\n', ""))
    elif mutate == "the_kv_writer_is_not_called":
        rederive_stubs(_bump(facts["stubs_text"], "    entry_write_485_kv();\n", ""))
    elif mutate == "the_five_gain_a_second_caller":
        facts["pool"]["serial_keyboard_init"]["some_other_object.o"] = ["U"]
    elif mutate == "the_five_are_called_from_two_objects":
        facts["pool"]["vm_page_init_local_q"] = {"elsewhere.o": ["U"]}
    elif mutate == "the_startup_object_does_not_define_the_thread":
        facts["pool"][KERNEL_THREAD] = {}
    elif mutate == "thread_bind_is_called_from_one_place":
        facts["pool"]["thread_bind"] = {"osfmk_kern_startup.o": ["U"]}
    elif mutate == "the_object_pool_is_missing":
        facts["pool"] = None
    elif mutate == "a_wrapper_is_absent_from_the_image":
        facts["symbols"].pop("__wrap_kdebug_free_early_buf", None)
    elif mutate == "a_wrapper_is_the_function":
        facts["symbols"]["__wrap_vm_pageout"] = facts["symbols"]["vm_pageout"]
    elif mutate == "the_tail_call_is_not_rewritten":
        facts["functions"][KERNEL_THREAD] = [
            line for line in facts["functions"][KERNEL_THREAD]
            if "__wrap_serial_keyboard_init" not in line]
    elif mutate == "the_tail_calls_are_out_of_order":
        lines = facts["functions"][KERNEL_THREAD]
        first = next(l for l in lines if "__wrap_OSKextRemoveKextBootstrap" in l)
        second = next(l for l in lines if "__wrap_kdebug_free_early_buf" in l)
        swapped = [second if l is first else first if l is second else l for l in lines]
        facts["functions"][KERNEL_THREAD] = swapped
    elif mutate == "the_last_transfer_goes_to_the_function":
        facts["functions"][KERNEL_THREAD] = [
            line.replace("<__wrap_vm_pageout>", "<vm_pageout>")
            for line in facts["functions"][KERNEL_THREAD]]
    elif mutate == "a_tail_transfer_moves_out_of_the_thread":
        moved = next(l for l in facts["functions"][KERNEL_THREAD] if "__wrap_vm_page_init_local_q" in l)
        facts["functions"][KERNEL_THREAD] = [
            l for l in facts["functions"][KERNEL_THREAD] if "__wrap_vm_page_init_local_q" not in l]
        facts["functions"].setdefault("load_context", []).append(moved)
    elif mutate == "the_site_word_is_not_the_function":
        facts["functions"]["__wrap_vm_pageout"] = [
            line.replace("movw\tr1, #7924", "movw\tr1, #7920")
            for line in facts["functions"]["__wrap_vm_pageout"]]
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "apple_reorders_the_tail", "apple_drops_a_tail_call", "apple_calls_one_twice",
    "apple_moves_thread_bind_out_of_the_tail", "apple_moves_bsd_init_after_the_five",
    "a_wrap_is_dropped_from_the_build", "the_excluded_one_is_wrapped", "a_wrap_moves_to_pass_one",
    "wrapper_index_swapped", "wrapper_index_is_a_name", "the_site_is_the_wrapper",
    "the_record_follows_the_call", "a_wrapper_loses_its_record", "the_census_moves_after_the_call",
    "the_census_is_dropped", "something_runs_after_the_last_call", "the_last_wrapper_calls_nothing",
    "the_census_cap_is_lifted", "the_census_walks_the_recorded_root", "the_two_roots_are_not_compared",
    "the_detached_tree_is_silent", "the_entry_child_count_is_dropped",
    "a_live_census_record_is_dropped", "the_two_reasons_become_one", "a_census_key_is_dropped",
    "the_first_state_word_is_read_raw",
    "the_second_state_word_moves_a_word_back", "the_mangled_accessor_is_dropped",
    "the_tail_count_is_spelled_again", "the_guard_uses_a_literal", "the_tail_count_shrinks",
    "the_table_shrinks_below_the_cap", "the_matched_bit_moves", "the_registered_tally_is_dropped",
    "the_shown_count_becomes_conditional", "the_named_tally_loses_its_guard",
    "the_array_store_uses_a_literal", "a_live_record_is_dropped", "a_key_is_dropped",
    "the_kv_writer_is_not_called", "the_five_gain_a_second_caller",
    "the_five_are_called_from_two_objects", "the_startup_object_does_not_define_the_thread",
    "thread_bind_is_called_from_one_place", "the_object_pool_is_missing",
    "a_wrapper_is_absent_from_the_image", "a_wrapper_is_the_function",
    "the_tail_call_is_not_rewritten", "the_tail_calls_are_out_of_order",
    "the_last_transfer_goes_to_the_function", "a_tail_transfer_moves_out_of_the_thread",
    "the_site_word_is_not_the_function",
)


def selftest(facts):
    # **The baseline first, and it is not a formality.** Every mutation below is "the check must still
    # report a failure after this edit", so on a baseline that already fails - an image built before this
    # step's wrappers, say - every mutation is refused for the baseline's reason and the whole run says
    # nothing about the mutations. 484's selftest is where this project learned that.
    baseline, _notes = compare(facts)
    if baseline:
        print("FAIL: the selftest's own baseline fails %d claim(s), so 'every mutation was refused' "
              "would be true for the wrong reason. Fix these first:" % len(baseline), file=sys.stderr)
        for failure in baseline:
            print("      " + failure, file=sys.stderr)
        return 1
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
        print("FAIL: --image is required: three of the six claims are about the linked image - the "
              "wrappers' addresses, the tail's own call sites and the constant each wrapper publishes as "
              "its site - and there is no default that can stand in for it", file=sys.stderr)
        return 1
    facts = gather(args.image)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the boot thread's tail is not the tail this image would record:", file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    say("  xnu_entry_485: Apple's own tail is wrapped in Apple's order, each of the five is called from "
        "one place in this kernel - the boot thread's object - the fifth records its position before a "
        "call that never returns, and the device tree census walks from the root the OS's own walk uses, "
        "publishing both candidate roots and both child counts, with every record in the live channel as "
        "well as the report")
    return 0


if __name__ == "__main__":
    sys.exit(main())
