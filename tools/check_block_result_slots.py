#!/usr/bin/env python3
"""
Check that this image's `wait_result_t` -> histogram-slot mapping is Apple's own set of values, and
that the slot names the report writes exist once.

Why this exists
---------------
Experiment 478 captures what `thread_block` returns. That value is `self->wait_result`, and it is the
argument of the panic 476's and 477's runs both stop on: `ipc_mqueue_receive`'s tail is
`ipc_mqueue_receive_results(wresult)`, and that function's `default:` arm panics for anything outside
the four values its switch accepts (`osfmk/ipc/ipc_mqueue.c:871`). So the number the instrument
records *is* the reading, and it is read by comparing it with `osfmk/kern/kern_types.h`.

That comparison happens in two places, and both of them are transcriptions:

  1. `entry_block_result_slot` in `stages/stage90/xnu_arm_boot/entry_stubs.c` writes each member's
     value as a `case` label and the member's name in a comment beside it. **The name is the claim**,
     and until this check nothing compared it with the header: a case labelled `/* THREAD_RESTART */`
     whose value is 10 would have published a slot the enum does not have, and the log would have read
     exactly as convincingly. Six transcribed numbers and six transcribed names is the shape this
     project has paid for twenty-four times (`mi4-one-value-two-definitions`).
  2. The report writes one key per slot - `xnu_entry_block_results_k0 .. k_N` - and a reader holds
     that histogram beside the enum, so the two lists have to name the same things.

Three further properties are checked because the *reading* depends on them rather than on the values,
and none of them is visible in a diff:

  - **The four members `ipc_mqueue_receive_results` accepts must have slot == value.** The epilogue
    reports slots and the live channel reports values; the two are only the same statement if
    `THREAD_AWAKENED` is slot 0. With any other mapping `_results_k3` would hold a member that is not
    `THREAD_RESTART`, and nothing in the log would say so.
  - **Every member of the header is a case and nothing else is.** A member added to the header and
    missed here falls into the `default` slot and is reported as "not one of them" - the one reading
    that turns a wiring gap into a false frontier.
  - **The names live in one place.** `entry_block_result_key` is the only function allowed to hold a
    slot's name, because the epilogue and the live channel both write the histogram and two spellings
    of one key is how a report loses a column.

What this check cannot see, and what does
----------------------------------------
It compares definitions, not the running kernel. Whether `thread_block`'s return value really is
`self->wait_result`, and whether this image's `int` is that ABI, are read out of XNU's own source and
its disassembly elsewhere (`entry_trace.c`'s 478 block, which cites `sched_prim.c`). What this check
replaces is a reader comparing two lists by eye.

    ./tools/check_block_result_slots.py
    ./tools/check_block_result_slots.py --verbose
    ./tools/check_block_result_slots.py --selftest     # break every claim, require the refusal
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

KERN_TYPES = os.path.join(REPO_ROOT, "external/xnu-4570.1.46/osfmk/kern/kern_types.h")
STUBS = os.path.join(REPO_ROOT, "stages/stage90/xnu_arm_boot/entry_stubs.c")

# The block that declares the wait results, delimited by Apple's own text rather than by line numbers:
# the comment above it and the `thread_continue_t` typedef below it.
ENUM_START_RE = re.compile(r"Possible wait_result_t values")
ENUM_END_RE = re.compile(r"typedef\s+void\s*\(\s*\*\s*thread_continue_t\s*\)")

# `#define THREAD_WAITING		-1		/* thread is waiting */`
WAIT_DEFINE_RE = re.compile(r"^#define\s+(THREAD_[A-Z_]+)\s+(-?\d+)\b", re.M)

SLOTS_DEFINE_RE = re.compile(r"^#define\s+ENTRY_BLOCK_RESULT_SLOTS\s+(\d+)[uUlL]*\s*$", re.M)

# `    case 0xFFFFFFFFu: return 5u;          /* THREAD_WAITING      */`
CASE_RE = re.compile(r"^\s*case\s+(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*:\s*return\s+(\d+)[uUlL]*\s*;"
                     r"\s*/\*\s*([^*]*?)\s*\*/\s*$", re.M)
DEFAULT_RE = re.compile(r"^\s*default\s*:\s*return\s+(\d+)[uUlL]*\s*;\s*/\*\s*([^*]*?)\s*\*/\s*$",
                        re.M)

KEY_LITERAL_RE = re.compile(r'"(xnu_entry_block_results_k\d+)"')

# The two places that write the histogram, and the one number they share.
HIST_DECL_RE = re.compile(r"uint32_t\s+g_block_results\s*\[\s*ENTRY_BLOCK_RESULT_SLOTS\s*\]\s*;")
HIST_DECL_FIXED_RE = re.compile(r"uint32_t\s+g_block_results\s*\[\s*(\d+)\s*\]\s*;")
HIST_LOOP_RE = re.compile(r"for\s*\(\s*unsigned\s+i\s*=\s*0\s*;\s*i\s*<\s*ENTRY_BLOCK_RESULT_SLOTS"
                          r"\s*;\s*i\+\+\s*\)\s*\n\s*entry_write_kv\s*\(\s*entry_block_result_key"
                          r"\s*\(\s*i\s*\)\s*,\s*g_block_results\s*\[\s*i\s*\]\s*\)\s*;")


def say(message):
    print(message)


def fail(messages):
    if isinstance(messages, str):
        messages = [messages]
    for message in messages:
        print("FAIL: %s" % message, file=sys.stderr)
    sys.exit(1)


def read(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as handle:
        return handle.read()


def strip_comments(text):
    """Block comments first, then line comments - the source uses both."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def brace_body(text, start):
    """The body of the definition whose opening brace is at or after `start`, by brace depth, so a
    nested block cannot end it early."""
    depth = 0
    body_start = None
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
            if depth == 1:
                body_start = index + 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return body_start, text[body_start:index]
    return None, None


def function_body(text, name):
    """`name`'s C definition body, or None - the signature may wrap onto more than one line, so the
    search is for the name followed by `(` and then the first `{` after the argument list."""
    match = re.search(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(name), text)
    if not match:
        return None
    return brace_body(text, match.end() - 1)[1]


# ---------------------------------------------------------------------------- the two sources

def parse_header_members(text):
    """The `THREAD_*` wait results, in declaration order, as (name, signed value)."""
    start = ENUM_START_RE.search(text)
    if not start:
        fail("osfmk/kern/kern_types.h has no `Possible wait_result_t values` comment any more, so "
             "this check has nothing to compare against - the header moved and the mapping has to be "
             "re-read rather than assumed")
    end = ENUM_END_RE.search(text, start.end())
    if not end:
        fail("the `wait_result_t` block in osfmk/kern/kern_types.h no longer ends at a "
             "`thread_continue_t` typedef, so the block's extent is unknown - see the previous "
             "message")

    members = WAIT_DEFINE_RE.findall(text[start.end():end.start()])
    if not members:
        fail("no `THREAD_*` value definitions were found between the `wait_result_t` comment and the "
             "`thread_continue_t` typedef, so the block was read and held nothing")
    return [(name, int(value)) for name, value in members]


def parse_cases(stubs_text):
    """The switch's rows as (case value, slot, member name), and the `default:` slot."""
    match = re.search(r"\bentry_block_result_slot\s*\([^;{]*\)\s*\{", stubs_text)
    if not match:
        fail("`entry_block_result_slot` is not in %s - it is what maps a `wait_result_t` to a slot, "
             "and the table below is checked against it" % os.path.relpath(STUBS, REPO_ROOT))
    body_start, body = brace_body(stubs_text, match.end() - 1)
    if body is None:
        fail("`entry_block_result_slot`'s body could not be matched - its braces are unbalanced")

    cases = [(int(value, 0), int(slot), name.strip())
             for value, slot, name in CASE_RE.findall(body)]
    defaults = DEFAULT_RE.findall(body)
    if not cases:
        fail("`entry_block_result_slot` holds no `case <value>: return <slot>;  /* NAME */` rows, so "
             "this check matched nothing. Every row must carry the member's name in a comment, "
             "because that name is the claim being checked")
    if len(defaults) != 1:
        fail("`entry_block_result_slot` has %d `default:` rows with a comment, expected exactly 1 - "
             "the slot for a value the enum does not have" % len(defaults))
    return cases, int(defaults[0][0])


def parse_slots_define(stubs_text):
    match = SLOTS_DEFINE_RE.search(stubs_text)
    if not match:
        fail("`#define ENTRY_BLOCK_RESULT_SLOTS <n>` is not in %s" % os.path.relpath(STUBS, REPO_ROOT))
    return int(match.group(1))


def parse_keys(stubs_text):
    body = function_body(stubs_text, "entry_block_result_key")
    if body is None:
        fail("`entry_block_result_key` is not in %s, or its body is not a brace block - it is the "
             "one place a slot's name may be written" % os.path.relpath(STUBS, REPO_ROOT))
    return KEY_LITERAL_RE.findall(body)


# ---------------------------------------------------------------------------- the check

def check(header_text, stubs_text, verbose):
    members = parse_header_members(header_text)
    cases, default_slot = parse_cases(stubs_text)
    slots = parse_slots_define(stubs_text)
    keys = parse_keys(stubs_text)
    problems = []

    by_name = {name: value for name, value in members}
    if len(by_name) != len(members):
        problems.append("osfmk/kern/kern_types.h declares the same wait result twice")

    # (1) each case's value is the value the header gives the member its comment names, and the
    #     comments name each member once.
    name_of = {}
    slot_of = {}
    for value, slot, name in cases:
        if name not in by_name:
            problems.append("`entry_block_result_slot` has a case commented `%s`, which is not one of "
                            "the %d wait results in osfmk/kern/kern_types.h (%s)"
                            % (name, len(members), ", ".join(n for n, _ in members)))
        else:
            if name in name_of:
                problems.append("`%s` has two cases in `entry_block_result_slot` (values %d and %d), "
                                "so the mapping no longer says which slot holds it"
                                % (name, name_of[name], value))
            name_of[name] = value
            if value != (by_name[name] & 0xFFFFFFFF):
                problems.append("`entry_block_result_slot` gives `%s` the value %d and "
                                "osfmk/kern/kern_types.h gives it %d - the slot the report writes is "
                                "not the one the header's value names" % (name, value, by_name[name]))
        if slot in slot_of:
            problems.append("slots %d and %d both `return %d`, so `_results_k%d` would hold two "
                            "different members" % (slot_of[slot], value, slot, slot))
        slot_of[slot] = value

    # (2) every member of the header is a case, or it lands in the default slot.
    missing = [name for name, _ in members if name not in name_of]
    if missing:
        problems.append("%s %s declared in osfmk/kern/kern_types.h and has no case in "
                        "`entry_block_result_slot`, so a return of its value would be counted in the "
                        "`default` slot - the one for a value the enum does not have"
                        % (", ".join("`%s`" % name for name in missing),
                           "is" if len(missing) == 1 else "are"))

    # (3) the slots are exactly 0..n-1 over the members, the default is n, and one number says so.
    if sorted(slot_of) != list(range(len(members))):
        problems.append("the case slots are %s and the %d members want %s - a slot that is unused, "
                        "repeated or out of step with the enum is a report whose columns do not line "
                        "up" % (sorted(slot_of), len(members), list(range(len(members)))))
    if default_slot != len(members):
        problems.append("the `default:` row returns %d and the %d members occupy slots 0..%d, so the "
                        "default slot must be %d - and `entry_block_result_key` reads its array with "
                        "the same count" % (default_slot, len(members), len(members) - 1,
                                            len(members)))
    if slots != len(members) + 1:
        problems.append("ENTRY_BLOCK_RESULT_SLOTS is %d: %d members plus one default slot want %d - "
                        "as a name array index a value that is too large reads past the names"
                        % (slots, len(members), len(members) + 1))

    # (4) the four values the panicking switch accepts are numbered the same in slots.
    for value, slot, name in cases:
        if value <= 3 and slot != value:
            problems.append("`%s` is %d and slot %d - the four values `ipc_mqueue_receive_results` "
                            "accepts are 0..3, and the epilogue's slots have to number them the same "
                            "way or `_results_k%d` holds a member a reader will not expect"
                            % (name, value, slot, slot))

    # (5) the names: one per slot, in order, written once.
    wanted_keys = ["xnu_entry_block_results_k%d" % slot for slot in range(len(members) + 1)]
    if keys != wanted_keys:
        problems.append("`entry_block_result_key`'s array holds %s and the %d slots want %s"
                        % (keys, len(members) + 1, wanted_keys))
    if len(set(keys)) != len(keys):
        problems.append("`entry_block_result_key`'s array repeats a name, so two slots publish to one "
                        "key and a histogram read from the log silently keeps only the later")
    written = KEY_LITERAL_RE.findall(stubs_text)
    if len(written) != len(keys):
        problems.append("a slot name is written %d times in %s and the array holds %d - the names "
                        "live in `entry_block_result_key` only, because the epilogue's histogram and "
                        "the live one are two spellings of one report"
                        % (len(written), os.path.relpath(STUBS, REPO_ROOT), len(keys)))

    # (6) the histogram's declaration and its two write loops, which is what makes the count and the
    #     names one number rather than three.
    if not HIST_DECL_RE.search(stubs_text):
        fixed = HIST_DECL_FIXED_RE.search(stubs_text)
        problems.append("`g_block_results` is not declared as "
                        "`uint32_t g_block_results[ENTRY_BLOCK_RESULT_SLOTS]`%s - the array's size "
                        "and the number of names must be one number"
                        % (" (it is `[%s]`)" % fixed.group(1) if fixed else ""))
    code = strip_comments(stubs_text)
    for holder in ("entry_write_478_kv", "entry_note_block_return"):
        body = function_body(code, holder)
        if body is None:
            problems.append("`%s` is not in %s, so the histogram's report in it cannot be checked"
                            % (holder, os.path.relpath(STUBS, REPO_ROOT)))
        elif not HIST_LOOP_RE.search(body):
            problems.append("`%s` no longer writes the histogram with "
                            "`entry_write_kv(entry_block_result_key(i), g_block_results[i])` in a loop "
                            "over `i < ENTRY_BLOCK_RESULT_SLOTS` - seven hand-written calls is the "
                            "second definition this check exists to refuse" % holder)

    if problems:
        fail(problems)

    if verbose:
        rows = sorted(cases, key=lambda row: row[1])
        say("ok: %d wait results, %d slots" % (len(members), slots))
        for value, slot, name in rows:
            say("    slot %d = %d = %s" % (slot, value, name))
        say("    slot %d = the default, for a return that is none of them" % default_slot)
        say("    keys %s" % ", ".join(keys))


# ---------------------------------------------------------------------------- the mutations

def sub_or_raise(pattern, repl, text, count=1):
    """A mutation that matches nothing is a control that has never been green
    (`mi4-measurement-defects`, 200), so every substitution asserts that it fired."""
    mutated, fired = re.subn(pattern, repl, text, count=count)
    if fired != count:
        raise AssertionError("mutation %r fired %d times, wanted %d" % (pattern, fired, count))
    return mutated


def mutations():
    """Every claim the check makes, broken one at a time. The pattern is a regex because the source's
    alignment is not part of the claim."""
    return [
        ("a case whose value is not the header's member",
         lambda t: sub_or_raise(r"case 0u:(\s*)return 0u;", r"case 5u:\1return 0u;", t)),

        ("a member name in a comment that is not a wait result",
         lambda t: sub_or_raise(r"/\*\s*THREAD_RESTART\s*\*/", "/* THREAD_CONTINUED */", t)),

        ("two comments naming the same member",
         lambda t: sub_or_raise(r"/\*\s*THREAD_RESTART\s*\*/", "/* THREAD_TIMED_OUT */", t)),

        ("a member the header declares and the table does not",
         lambda t: sub_or_raise(r"\n\s*case 3u:.*?/\* THREAD_RESTART\s*\*/", "", t)),

        ("a slot out of order",
         lambda t: sub_or_raise(r"case 3u:(\s*)return 3u;", r"case 3u:\1return 4u;", t)),

        ("a default slot that is not the last",
         lambda t: sub_or_raise(r"default:(\s*)return 6u;", r"default:\1return 5u;", t)),

        ("ENTRY_BLOCK_RESULT_SLOTS one too large",
         lambda t: sub_or_raise(r"ENTRY_BLOCK_RESULT_SLOTS 7u", "ENTRY_BLOCK_RESULT_SLOTS 8u", t)),

        ("ENTRY_BLOCK_RESULT_SLOTS one too small",
         lambda t: sub_or_raise(r"ENTRY_BLOCK_RESULT_SLOTS 7u", "ENTRY_BLOCK_RESULT_SLOTS 6u", t)),

        ("a slot name renamed",
         lambda t: t.replace('"xnu_entry_block_results_k6"', '"xnu_entry_block_results_k7"', 1)),

        ("two slots sharing one name",
         lambda t: t.replace('"xnu_entry_block_results_k6"', '"xnu_entry_block_results_k5"', 1)),

        ("a slot name written a second time, outside the array",
         lambda t: sub_or_raise(r'entry_write_kv\("xnu_entry_block_result",',
                                'entry_write_kv("xnu_entry_block_results_k0",', t)),

        ("the histogram declared with its own number",
         lambda t: sub_or_raise(r"g_block_results\[ENTRY_BLOCK_RESULT_SLOTS\]",
                                "g_block_results[6]", t)),

        ("the live report listing the names by hand",
         lambda t: sub_or_raise(r"for \(unsigned i = 0; i < ENTRY_BLOCK_RESULT_SLOTS; i\+\+\)\s*\n"
                                r"\s*entry_write_kv\(entry_block_result_key\(i\), "
                                r"g_block_results\[i\]\);",
                                'entry_write_kv("xnu_entry_block_results_k0", '
                                "g_block_results[0]);", t)),
    ]


def selftest():
    header_text = read(KERN_TYPES)
    stubs_text = read(STUBS)

    try:
        check(header_text, stubs_text, verbose=False)
    except SystemExit:
        fail("--selftest: the unmutated sources were refused, so refusals below would prove nothing")

    count = 0
    for name, mutate in mutations():
        try:
            mutated = mutate(stubs_text)
        except AssertionError as error:
            fail("--selftest: the mutation %r did not apply: %s" % (name, error))
        if mutated == stubs_text:
            fail("--selftest: the mutation %r changed nothing" % name)
        try:
            check(header_text, mutated, verbose=False)
        except SystemExit:
            count += 1
            continue
        fail("--selftest: the mutation %r was accepted" % name)

    # And a mutated *header* has to be refused, or the table is being compared with itself.
    mutated_header = sub_or_raise(r"#define THREAD_RESTART(\s+)3\b", r"#define THREAD_RESTART\g<1>4",
                                  header_text)
    try:
        check(mutated_header, stubs_text, verbose=False)
        fail("--selftest: a header whose `THREAD_RESTART` moved was accepted, so the values are not "
             "being read out of it")
    except SystemExit:
        count += 1

    say("ok: --selftest refused all %d mutations" % count)


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().split("\n")[0])
    parser.add_argument("--selftest", action="store_true",
                        help="break every claim in turn and require the refusal")
    parser.add_argument("--verbose", action="store_true", help="print what was compared")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return

    check(read(KERN_TYPES), read(STUBS), args.verbose)


if __name__ == "__main__":
    main()
