#!/usr/bin/env python3
"""Re-derive the staged D13 `__LINKEDIT` kext-bootstrap guard (experiment 988).

WHY THIS EXISTS. 987's press ran through the whole HFS mount and ALL of BSD init and then panicked at
the first statement after `bsd_init()` returns - `startup.c:408` `OSKextRemoveKextBootstrap()` - because
`OSKext::removeKextBootstrap()` dereferences `seg_to_remove` (`OSKext.cpp:859`) inside a LIVE
`#if CONFIG_KXLD` block, and `seg_to_remove` is NULL: our synthetic Mach-O declares ncmds=3 with
segments only `__TEXT`/`__DATA`/`__PRELINK_TEXT`, so `getsegbyname("__LINKEDIT")` finds nothing.

`tools/stage_d13_kextb_login.sh` wraps that block in `#ifndef STAGE90_XNU_KEXTB_LOGIN`, and
`build_entry.sh`'s 988 arm compiles the pool with `-DSTAGE90_XNU_KEXTB_LOGIN=1`. The arm is only what
its record says if the tree actually carries the guard, and `external/` is a re-provisionable,
gitignored checkout: a re-checkout restores Apple's unguarded text, the define then does nothing, and
the boot panics again with no other symptom. So the edit's SOURCE half is checked here, and this
refuses drift.

THE PROPERTY, NOT THE PRESENCE.  A file containing the string `#ifndef STAGE90_XNU_KEXTB_LOGIN` is not
enough: a guard moved away from the `#if CONFIG_KXLD` block, or a `#endif` that closes somewhere else,
still reads as "present" while the teardown compiles in - the class a presence-only check cannot see
([[mi4-a-claim-in-a-comment-is-not-a-check]]). So this re-derives, BY LINE NUMBER:

  1. `#ifndef STAGE90_XNU_KEXTB_LOGIN` is IMMEDIATELY before `#if CONFIG_KXLD` (if_l + 1 == kxld_if_l);
  2. the guard's `#endif` is IMMEDIATELY after `#endif /* CONFIG_KXLD */` (kxld_end_l + 1 == end_l);
  3. the faulting dereference (`seg_data = (void *) seg_to_remove->vmaddr;`, the one statement that
     loads offset 28 through the NULL base) lies BETWEEN the two, i.e. the block that dereferences is
     the block that is wrapped - matched on the WHOLE statement, because the guarded `__KLD` sibling
     above carries `seg_to_remove->vmaddr` too inside `ml_static_mfree` and must not be mistaken for
     the faulting one;
and it refuses a second copy of any of the four anchors, so a duplicate cannot make 1-3 read true at
the wrong site.

The 4570 half is the CONTROL: this guard is D13-only, 4570 never passes the define, and 4570's
OSKext.cpp must NOT be expected to carry it - the same D13 discriminator (`osfmk/sys/types.h`) the
build uses, so the check and the build cannot disagree about which tree is which.

Usage:
    check_d13_kextb_login_staged.py                 # $XNU_TREE, else the repo's D13 tree
    check_d13_kextb_login_staged.py --tree <TREE>
    check_d13_kextb_login_staged.py --selftest
Exit 0 when the guard wraps the block (or the tree is not D13 / not provisioned), 1 when it does not.
"""
import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(REPO, "external", "xnu-hd2-darwin13", "xnu")

GUARD_IF = "#ifndef STAGE90_XNU_KEXTB_LOGIN"
GUARD_END = "#endif /* STAGE90_XNU_KEXTB_LOGIN */"
KXLD_IF = "#if CONFIG_KXLD"
KXLD_END = "#endif /* CONFIG_KXLD */"
# The WHOLE faulting statement, not `seg_to_remove->vmaddr`: the guarded `__KLD` sibling above carries
# that substring too (`ml_static_mfree(seg_to_remove->vmaddr, ...)`), and it is not the load that faults.
DEREF = "seg_data = (void *) seg_to_remove->vmaddr;"


def is_d13(tree):
    """The same discriminator the build uses - one value, one definition (`osfmk/sys/types.h` is
    D13's legacy private header; 4570 ships none)."""
    return os.path.isfile(os.path.join(tree, "osfmk", "sys", "types.h"))


def _facts(text):
    """Return "" when the guard wraps the `#if CONFIG_KXLD` block, else the refusal's reason.

    Pure over the file's text, so `--selftest` can feed a mutated tree and assert each property is
    caught when it moves.
    """
    lines = text.split("\n")

    def hits(anchor):
        return [i + 1 for i, line in enumerate(lines) if line == anchor]

    if_l = hits(GUARD_IF)
    end_l = hits(GUARD_END)
    kxld_if_l = hits(KXLD_IF)
    kxld_end_l = hits(KXLD_END)

    for name, anchor, found in (("guard", GUARD_IF, if_l), ("guard #endif", GUARD_END, end_l),
                                ("block open", KXLD_IF, kxld_if_l), ("block close", KXLD_END, kxld_end_l)):
        if len(found) != 1:
            return ("OSKext.cpp carries %d `%s` line(s) (%s), wanted exactly 1 - the guard cannot be "
                    "placed by line number" % (len(found), anchor, name))

    if if_l[0] + 1 != kxld_if_l[0]:
        return ("`%s` (line %d) is not IMMEDIATELY before `%s` (line %d) - a guard that does not open "
                "on the block does not compile the teardown out (want ifndef_line + 1 == kxld_line)"
                % (GUARD_IF, if_l[0], KXLD_IF, kxld_if_l[0]))

    if kxld_end_l[0] + 1 != end_l[0]:
        return ("`%s` (line %d) is not IMMEDIATELY after `%s` (line %d) - the guard closes somewhere "
                "else, so the block is not the block that is wrapped (want kxld_end_line + 1 == "
                "endif_line)" % (GUARD_END, end_l[0], KXLD_END, kxld_end_l[0]))

    deref_l = [i + 1 for i, line in enumerate(lines) if DEREF in line]
    if len(deref_l) != 1:
        return ("OSKext.cpp carries %d `%s` line(s), wanted exactly 1 - the faulting statement cannot be "
                "placed by line number (0 means the 988 defect is gone from the source; >1 means a "
                "duplicate could sit outside the guard while this check read the inside one)"
                % (len(deref_l), DEREF))
    if not (if_l[0] < deref_l[0] < end_l[0]):
        return ("the faulting dereference `%s` (line %d) is NOT between the guard's `#ifndef` (line %d) "
                "and `#endif` (line %d) - the guarded region does not contain the dereference, so the "
                "switch would not compile it out" % (DEREF, deref_l[0], if_l[0], end_l[0]))

    return ""


def selftest():
    """Feed the checker texts where each premise is moved, and assert each is caught."""
    good = ("    seg_to_remove = (kernel_segment_command_t *)getsegbyname(\"__LINKEDIT\");\n"
            "#ifndef STAGE90_XNU_KEXTB_LOGIN\n"
            "#if CONFIG_KXLD\n"
            "        seg_data = (void *) seg_to_remove->vmaddr;\n"
            "#else /* we are not CONFIG_KXLD */\n"
            "#error CONFIG_KXLD is expected for this arch\n"
            "#endif /* CONFIG_KXLD */\n"
            "#endif /* STAGE90_XNU_KEXTB_LOGIN */\n"
            "    seg_to_remove = NULL;\n")
    moved_if = good.replace("#ifndef STAGE90_XNU_KEXTB_LOGIN\n#if CONFIG_KXLD\n",
                            "#ifndef STAGE90_XNU_KEXTB_LOGIN\n/* a comment in between */\n#if CONFIG_KXLD\n")
    moved_end = good.replace("#endif /* CONFIG_KXLD */\n#endif /* STAGE90_XNU_KEXTB_LOGIN */\n",
                             "#endif /* CONFIG_KXLD */\n/* a comment in between */\n"
                             "#endif /* STAGE90_XNU_KEXTB_LOGIN */\n")
    deref_out = good.replace("        seg_data = (void *) seg_to_remove->vmaddr;\n", "")
    deref_above = good.replace("        seg_data = (void *) seg_to_remove->vmaddr;\n", "") \
                      .replace("#ifndef STAGE90_XNU_KEXTB_LOGIN\n",
                               "        seg_data = (void *) seg_to_remove->vmaddr;\n"
                               "#ifndef STAGE90_XNU_KEXTB_LOGIN\n")
    no_guard = good.replace("#ifndef STAGE90_XNU_KEXTB_LOGIN\n", "") \
                    .replace("#endif /* STAGE90_XNU_KEXTB_LOGIN */\n", "")
    no_guard_end = good.replace("#endif /* STAGE90_XNU_KEXTB_LOGIN */\n", "")
    two_guards = good + "#ifndef STAGE90_XNU_KEXTB_LOGIN\n#endif /* STAGE90_XNU_KEXTB_LOGIN */\n"
    cases = [
        ("the staged tree as it is", good, True),
        ("the `#ifndef` no longer immediately before `#if CONFIG_KXLD`", moved_if, False),
        ("the guard's `#endif` no longer immediately after the block's", moved_end, False),
        ("the guarded region no longer contains the dereference", deref_out, False),
        ("the dereference moved above the guard", deref_above, False),
        ("no guard at all (a re-provisioned tree)", no_guard, False),
        ("the guard's `#endif` removed", no_guard_end, False),
        ("a second copy of the guard", two_guards, False),
    ]
    bad = 0
    for label, text, want_ok in cases:
        got_ok = _facts(text) == ""
        if got_ok != want_ok:
            print("check_d13_kextb_login_staged --selftest: %s -> ok=%s wanted %s"
                  % (label, got_ok, want_ok), file=sys.stderr)
            bad += 1
    if bad:
        print("check_d13_kextb_login_staged --selftest: FAIL (%d)" % bad, file=sys.stderr)
        return 1
    print("check_d13_kextb_login_staged --selftest: ok - every way the guard can fail to wrap the "
          "`#if CONFIG_KXLD` block is caught")
    return 0


def check_tree(tree):
    """Check one tree; returns 0/1."""
    path = os.path.join(tree, "libkern", "c++", "OSKext.cpp")
    if not os.path.isfile(path):
        print("check_d13_kextb_login_staged: no %s - D13 tree not provisioned, skipping" % path)
        return 0
    if not is_d13(tree):
        print("check_d13_kextb_login_staged: %s is 4570 (no osfmk/sys/types.h) - the "
              "STAGE90_XNU_KEXTB_LOGIN guard is D13-only and 4570 never passes it, so this is "
              "SKIPPED on 4570" % tree)
        return 0
    msg = _facts(open(path, encoding="utf-8", errors="replace").read())
    if msg:
        print("check_d13_kextb_login_staged: FAIL - %s" % msg, file=sys.stderr)
        print("check_d13_kextb_login_staged: run tools/stage_d13_kextb_login.sh to re-apply the edit",
              file=sys.stderr)
        return 1
    print("check_d13_kextb_login_staged: ok - %s wraps its `#if CONFIG_KXLD` block in "
          "`#ifndef STAGE90_XNU_KEXTB_LOGIN` (guard immediately before, `#endif` immediately after, "
          "the `seg_to_remove->vmaddr` dereference inside), so -DSTAGE90_XNU_KEXTB_LOGIN=1 compiles "
          "the 988 teardown out" % path)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", help="XNU tree root (default: $XNU_TREE, else the repo's D13 tree)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    tree = args.tree or os.environ.get("XNU_TREE") or DEFAULT_TREE
    return check_tree(tree)


if __name__ == "__main__":
    sys.exit(main())