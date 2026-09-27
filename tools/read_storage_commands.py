#!/usr/bin/env python3
"""Print the storage ladder's per-command table out of a capture, with the CMD LINE bit decoded.

WHY THIS EXISTS. Every rung's record ends in a hand-built table of what each command on this
block's bus did, and until now that table was the *only* form the fact took: a reader rebuilt it
from `grep` output, by hand, per press. Two things went wrong that way, and both are in the same
column of the same table.

  1. `inhibit_last` is published as the WHOLE `PRESENT_STATE 0x24` word, and every table written
     from it decoded **bit 0 only** - `CMD_INHIBIT`. Bit 24 is the CMD line's own signal level,
     and it is a second producer of the row's meaning, sitting unused in a number the record had
     already printed.

  2. `inhibit_seen = 0` was read as one outcome - "the block declined the command" - through
     746 section 4b, 747 section 7 and rung 21's whole branch table. **It has two producers**:
     the CMD2 shape, where the line never left its idle level and the response register never
     moved, and the CMD1 shape, where the line went LOW inside the window and `RESPONSE` gained a
     well-formed word. Same `inhibit_seen`, same window, ONE BIT apart. (749 records the finding;
     `m760` records the class.)

So this tool exists for the reason the project keeps giving itself: **a claim in a comment is not a
check.** 749's repair was prose in a document - a reader has to remember it at press time. This
prints it instead, from the capture, with absent cells named as UNREAD rather than guessed.

WHAT IT DELIBERATELY DOES NOT DO. It does not print a verdict for a `inhibit_seen = 0` row. 749
showed the archive holds **two candidate readings of CMD1** - 733/740's *the card answered* and
746b's *declined* - and that they disagree, so neither may be used as a premise. The tool prints
the SHAPE (which bits moved) and names which shape it is; it refuses to collapse a shape into an
outcome, because that collapse is exactly the defect it was written to stop.

AND THE COMPARABILITY RULE, which is also easy to get wrong by hand: `inhibit_last` is ONE sample.
On a row whose poll exited on `INT_STATUS` that sample is the moment of the **completion**; on a row
that ran the whole bound it is inside a still-open transmission. Only rows with the same exit can be
compared. The table prints which exit each row took so the comparison is made on like rows.

Usage:  tools/read_storage_commands.py <capture> [<capture> ...]
Exit 0 if every capture was read; 1 if one could not be.
"""
import os
import re
import sys

# The ladder's command bodies, in the order the driver's own attach sequence runs them, with the
# `core.h` flag value each was sent with. `rca` and `nrsp` are the two rungs that drive CMD3 -
# rung 19's R1 and rung 20's no-response - and `nidx` is rung 21's `R1` minus the INDEX bit. They
# are three separate families because they are three separate bodies, and the record names them
# that way; a tool that merged them would lose the comparison the rungs exist to make.
FAMILIES = [
    ("cmd0", 0x00, "CMD0  GO_IDLE_STATE   ", "rung 11"),
    ("cmd1", 0x01, "CMD1  SEND_OP_COND    ", "rung 15"),
    ("cid",  0x02, "CMD2  ALL_SEND_CID    ", "rung 17"),
    ("rb",   0x00, "the pre-command read  ", "rung 18"),
    ("rca",  0x03, "CMD3  SET_REL_ADDR R1 ", "rung 19"),
    ("nrsp", 0x03, "CMD3  SET_REL_ADDR NONE", "rung 20"),
    ("nidx", 0x03, "CMD3  SET_REL_ADDR R1-no-INDEX", "rung 21"),
]

CMD_LINE_BIT = 1 << 24        # PRESENT_STATE bit 24 - the SDHCI spec's CMD Line Signal Level
CMD_INHIBIT_BIT = 1 << 0      # PRESENT_STATE bit 0 - the driver's own `sdhci.c:1096` gate

CELL = re.compile(r"^xnu_live_storage_([a-z0-9]+)_([a-z0-9_]+)=0x([0-9a-fA-F]+)\s*$")


def read_cells(path):
    """Every `xnu_live_storage_<family>_<cell>=0x...` line, as {family: {cell: int}}."""
    fams = {}
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = CELL.match(line.strip())
            if m:
                fam, cell, val = m.group(1), m.group(2), int(m.group(3), 16)
                fams.setdefault(fam, {})[cell] = val
    return fams


def cells_of(fam, *names):
    """The values, or None per name. An absent cell is UNREAD and never defaulted to zero -
    an absent key has more than one producer (m720), and a tool that read one as 0 would be
    inventing the reading it claims to report."""
    return [fam.get(n) for n in names]


def hexof(v):
    return "UNREAD" if v is None else "0x%08x" % v


def bit24(v, label):
    if v is None:
        return "UNREAD"
    return "%s " % label if (v & CMD_LINE_BIT) else "LOW  "


def shape(row):
    """The row's SHAPE, from the measured columns only. See the module docstring: this returns a
    shape and never a verdict, because for `inhibit_seen = 0` the archive holds two readings."""
    seen, last, pb, pa, comp, any_ = (row["inhibit_seen"], row["inhibit_last"],
                                      row["ps_before"], row["ps_after"],
                                      row["complete"], row["status_any"])
    if row["sent"] is None:
        return ("not a command body", "this prefix publishes no `sent` cell, so it drives nothing "
                "on the bus; its row is shown for the reads it does own")
    if row["sent"] == 0:
        return "NOT ISSUED", "the body's own gate refused before the store"
    if seen is None:
        return "UNKNOWN", "`inhibit_seen` is not published by this body"
    if seen > 0:
        if last is None:
            return "UNKNOWN", "`inhibit_seen` > 0 but `inhibit_last` is not published"
        if last & CMD_INHIBIT_BIT:
            return "TAKEN -> STUCK AT THE BOUND", "the inhibit bit was SET at the last sample"
        if comp == 1:
            return "TAKEN -> COMPLETED", "the inhibit bit CLEAR at the last sample and the completion latched"
        return "TAKEN -> RELEASED, NO COMPLETION", "the inhibit bit CLEAR at the last sample and no completion"
    # seen == 0: THE AMBIGUOUS ROW, and the reason this tool exists.
    if last is None:
        return "UNKNOWN", "`inhibit_seen` = 0 and `inhibit_last` is not published, so the CMD line was never sampled"
    moved_in = (last & CMD_LINE_BIT) == 0
    moved_out = any(v is not None and (v & CMD_LINE_BIT) == 0 for v in (pb, pa))
    if moved_in or moved_out:
        return ("LINE MOVED INSIDE THE WINDOW  (the CMD1 shape)",
                "the CMD line is LOW in at least one reading; 749 records that the archive holds "
                "TWO candidate readings of this shape and that they disagree")
    if pb is None and pa is None:
        return ("LINE NEVER MOVED, UNCONFIRMED",
                "the CMD line is HIGH at the only sample and no second moment was published")
    return ("LINE NEVER MOVED  (the CMD2 shape)",
            "the CMD line is HIGH at every published moment and the word was in the register")


def table(path):
    fams = read_cells(path)
    if not fams:
        print("  no xnu_live_storage_* cell in this capture - is it a storage rung's log?")
        return False
    print("=" * 108)
    print(path)
    print("=" * 108)
    hdr = ("%-8s %-6s %-6s %-9s %-6s %-9s %-6s %-7s %-7s %-10s %-8s %-6s"
           % ("family", "op", "word", "sent", "inhib", "inhib", "bit24", "ps_bef", "ps_after",
              "polls", "status", "compl"))
    print(hdr)
    print("%-8s %-6s %-6s %-9s %-6s %-9s %-6s %-7s %-7s %-10s %-8s %-6s"
          % ("", "", "", "", "after", "seen", "last", "bit24", "bit24", "", "any", ""))
    print("-" * 108)
    rows = []
    for key, op, label, whence in FAMILIES:
        if key not in fams:
            continue
        f = fams[key]
        r = {
            "family": key, "whence": whence, "label": label,
            "op": f.get("op"),
            "word": f.get("word"),
            "sent": f.get("sent"),
            "inhibit_after": f.get("inhibit_after"),
            "inhibit_seen": f.get("inhibit_seen"),
            "inhibit_last": f.get("inhibit_last"),
            "ps_before": f.get("ps_before"),
            "ps_after": f.get("ps_after"),
            "polls": f.get("polls"),
            "status_any": f.get("status_any"),
            "complete": f.get("complete"),
            "ticks": f.get("ticks"),
            "resp_moved": f.get("resp_moved"),
        }
        rows.append(r)
        print("%-8s %-6s %-6s %-9s %-6s %-9s %-6s %-7s %-7s %-10s %-8s %-6s"
              % (key,
                 "-" if r["op"] is None else "%d" % r["op"],
                 "UNREAD" if r["word"] is None else "0x%04x" % r["word"],
                 hexof(r["sent"]),
                 hexof(r["inhibit_after"]),
                 hexof(r["inhibit_seen"]),
                 bit24(r["inhibit_last"], "HIGH"),
                 bit24(r["ps_before"], "HIGH"),
                 bit24(r["ps_after"], "HIGH"),
                 hexof(r["polls"]),
                 hexof(r["status_any"]),
                 hexof(r["complete"])))
    print()
    a = "%-34s %-31s %s"
    print(a % ("family", "SHAPE (from the measured columns only)", "why, and what it is NOT"))
    print("-" * 108)
    for r in rows:
        sh, why = shape(r)
        print(a % (r["family"] + " (" + r["whence"] + ")", sh, why))
    print()
    print("  exit kind, which decides which rows may be compared with each other (inhibit_last is")
    print("  ONE sample: on a row that exited on INT_STATUS it is the moment of the COMPLETION, on a")
    print("  row that ran the bound it is inside a still-open transmission):")
    for r in rows:
        if r["polls"] is None:
            continue
        if r["status_any"]:
            kind = "exited on INT_STATUS   <- NOT comparable with a bounded row"
        elif r["complete"] == 1:
            kind = "exited on INT_STATUS   <- NOT comparable with a bounded row"
        else:
            kind = "ran the whole bound    <- comparable with other bounded rows"
        print("    %-8s polls=%-10s %s" % (r["family"], hexof(r["polls"]), kind))
    print()
    print("  RESPONSE movement, the second witness of the same window - and for the rows that carry")
    print("  it, the four raw words are in the capture beside this reading (749 section 3):")
    for r in rows:
        if r["family"] in fams:
            f = fams[r["family"]]
            keys = [k for k in ("resp_pre", "resp_post", "resp_moved", "resp_is_arg",
                                "resp_read", "resp", "resp0", "resp_short") if k in f]
            if keys:
                print("    %-8s %s" % (r["family"],
                                       "  ".join("%s=%s" % (k, hexof(f[k])) for k in keys)))
    return True


# ================================================================================================
# 752: THE BRANCH TABLE AS A CHECK. 747 section 5 states three branches and a verdict for each, and
# the verdicts are what the next rung is chosen FROM. Two of the three conditions turn out to be
# satisfied by more than one SHAPE in the archive - the same defect 749 found in branch 1 and the tool
# above already discriminates - so this mode reports, per branch, HOW MANY distinct shapes satisfy it,
# and refuses to print a verdict for a branch whose condition has more than one.
#
# It is a check and not a paragraph for 751's reason: 749's correction was prose a reader had to
# remember at press time, and this mode is that correction made mechanical for all three branches.
# ================================================================================================

# 747 section 5's three branches, in the order the table lists them (a reader takes the FIRST match).
BRANCHES = [
    ("B1", "the block DECLINED the command - the rule is CONFIRMED",
     lambda seen, comp, last: seen == 0 and comp is not None),
    ("B2", "the block TOOK it and it never finished - the rule's INDEX half is KILLED",
     lambda seen, comp, last: seen is not None and comp is not None and seen > 0 and comp == 0),
    ("B3", "the word STARTED and COMPLETED - the rule is killed outright",
     lambda seen, comp, last: comp == 1 and seen is not None),
]


def producer_shape(seen, comp, last, pb, pa):
    """The SHAPE a row is a producer of. Two rows with the same shape are ONE piece of evidence about
    the branch condition; two different shapes under one condition mean the condition's name is doing
    the work of two facts, which is the defect both 749 and this mode exist for."""
    if seen is None:
        return "no `inhibit_seen` published (the body predates the cell)"
    if seen == 0:
        if last is None:
            return "seen=0, CMD line never sampled"
        moved = ((last & CMD_LINE_BIT) == 0
                 or any(v is not None and (v & CMD_LINE_BIT) == 0 for v in (pb, pa)))
        return "seen=0, CMD line MOVED (the CMD1 shape)" if moved \
            else "seen=0, CMD line NEVER MOVED (the CMD2 shape)"
    if comp == 1:
        return "seen>0, COMPLETED (inhibit bit clear, completion latched)"
    if last is None:
        return "seen>0, complete=0, `inhibit_last` not published"
    return ("seen>0, complete=0, STILL STUCK (inhibit bit SET at the last sample)"
            if (last & CMD_INHIBIT_BIT)
            else "seen>0, complete=0, RELEASED with nothing latched (inhibit bit CLEAR)")


def branch_audit(paths):
    """Per 747 section 5 branch: the distinct SHAPES in the archive that satisfy it."""
    shapes = {}          # shape -> [(capture, family)]
    matched = {}         # branch key -> set of shapes
    for p in paths:
        fams = read_cells(p)
        for key, f in fams.items():
            if "sent" not in f:
                continue          # not a command body
            sh = producer_shape(f.get("inhibit_seen"), f.get("complete"), f.get("inhibit_last"),
                                f.get("ps_before"), f.get("ps_after"))
            if f.get("inhibit_seen") is None and f.get("complete") is None:
                continue          # a read-only body: no branch condition applies to it
            shapes.setdefault(sh, []).append("%s:%s" % (os.path.basename(p)[:26], key))
            for k, _v, test in BRANCHES:
                if test(f.get("inhibit_seen"), f.get("complete"), f.get("inhibit_last")):
                    matched.setdefault(k, set()).add(sh)
                    break         # FIRST match wins, as the table is read
    print("=" * 108)
    print("747 section 5's branch table, audited against %d capture(s)" % len(paths))
    print("=" * 108)
    ok = True
    for k, verdict, _test in BRANCHES:
        got = sorted(matched.get(k, ()))
        print()
        print("  %s  condition: %s" % (k, verdict))
        if not got:
            print("      a condition no archived row satisfies - NOT EXERCISED")
            continue
        if len(got) == 1:
            print("      ONE shape satisfies it: %s" % got[0])
        else:
            ok = False
            print("      *** %d DIFFERENT SHAPES SATISFY IT, AND THE TABLE GIVES THEM ONE VERDICT ***"
                  % len(got))
            for s in got:
                print("        - %s" % s)
                for w in shapes[s][:3]:
                    print("            %s" % w)
            print("      A press whose log lands in this branch cannot be read by this branch's")
            print("      sentence alone. Name the shape first (749 did this for B1).")
    print()
    print("  every distinct shape in the archive, and which branch it falls in:")
    for sh in sorted(shapes):
        br = "-"
        s0 = shapes[sh][0]
        print("    %-62s %-4s %d row(s), e.g. %s" % (sh[:62], br, len(shapes[sh]), s0))
    print()
    print("  RESULT: %s" % ("every branch's condition has exactly one producer shape"
                            if ok else
                            "AT LEAST ONE BRANCH CONDITION HAS MORE THAN ONE PRODUCER - see above"))
    return ok


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s <capture> [<capture> ...]" % argv[0])
        print("       %s --branch-audit <capture> [<capture> ...]" % argv[0])
        return 1
    if argv[1] == "--branch-audit":
        paths = argv[2:]
        if not paths:
            print("--branch-audit needs at least one capture")
            return 1
        try:
            return 0 if branch_audit(paths) else 1
        except OSError as e:
            print("could not read a capture: %s" % e)
            return 1
    ok = True
    for p in argv[1:]:
        try:
            ok = table(p) and ok
        except OSError as e:
            print("could not read %s: %s" % (p, e))
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
