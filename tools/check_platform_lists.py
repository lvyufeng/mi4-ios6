#!/usr/bin/env python3
"""
Refuse a platform expert whose `deleteList`/`excludeList` answers NULL.

    ./tools/check_platform_lists.py                       # the stage's platform sources
    ./tools/check_platform_lists.py --file a.cpp b.cpp    # what build_xnu_arm_kernel.sh passes

Why this exists. `IODTPlatformExpert` leaves two virtuals abstract (`IOPlatformExpert.h:229-230`) and
`IODTPlatformExpert::processTopLevel` asks the subclass for both (`IOPlatformExpert.cpp:1322`, `:1355`).
The delete one is the infanticide:

    kids = IODTFindMatchingEntries( rootEntry, 0, deleteList() );
    while( (next = kids->getNextObject())) next->detachAll( gIODTPlane);

and `IODTFindMatchingEntries` (`IODeviceTreeSupport.cpp:886`) does **not** read a NULL key list as
"match nothing":

    if( keys) { cmp = IODTMatchNubWithKeys( next, keys ); ... } else result->setObject( next);

it takes the `else` branch and collects *every* entry - so a NULL `deleteList()` detaches every child
the root entry has in the IODT plane, `chosen` among them, which is the node `IOFindBSDRoot` roots
this machine from (`iokit/bsddev/IOKitBSDInit.cpp:404`, `:440`). That is experiment 462's defect, and
it stood in `MSM8974PlatformExpert.cpp` from experiment 363 to 461 as `return( (const char *)0 );`,
under a comment that called it "the empty one" - a claim in a comment, which is not a check.

Apple's own subclasses answer with a list of the few nodes that deserve deleting, as quoted names in
the OSUnserialize list grammar:

    AppleMacIO.cpp:108            "('sd', 'st', 'disk', 'tape', 'pram', 'rtc', 'mouse')"
    ApplePlatformExpert.cpp:86    "('packages', 'psuedo-usb', 'psuedo-hid', 'multiboot', 'rtas')"

So the property this checks is not "the answer is empty" but "the answer is a string in that
grammar": `(` ... `)` around zero or more quoted names, which `OSUnserialize` parses to an `OSArray`
(`OSUnserialize.y:168` is `array: '(' ')' { $$ = NULL; }`, so the empty list is legal and non-NULL) and
which `IODTMatchNubWithKeys` can then compare names against. NULL, the empty *C string* `""`, and any
expression that is not a string literal all fail here.

**What it will not do, stated rather than implied:** it reads the two functions as text - a definition
line, then the first `return` in the body, then one level of `static const char name[] = "..."` for the
returned name if it is one. So it can miss a definition written in a way those patterns do not match.
Both counts are printed on every run, including the runs where nothing fails, because a scan that finds
nothing and passes is this project's oldest defect: the definition count (2 expected - one per method)
and the number of returns it could read. A missing definition is not a harmless gap either: the stub
generator synthesises a stand-in for anything undefined, so a deleted `deleteList` would link and run.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

DEFAULT_FILES = [os.path.join(REPO_ROOT, "stages", "stage90", "xnu_platform",
                              "MSM8974PlatformExpert.cpp"),
                 os.path.join(REPO_ROOT, "stages", "stage90", "xnu_platform",
                              "MSM8974RootResource.cpp")]

METHODS = ("deleteList", "excludeList")

# A definition line: `Class::method( void )`, with the braces on the following line - the shape both
# of this project's platform experts use. The body ends at the first `}` in column 0.
DEF_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*::(%s)\s*\(\s*void\s*\)\s*$" % "|".join(METHODS))
RET_RE = re.compile(r"\breturn\s*[\(]?\s*([A-Za-z_][A-Za-z0-9_]*|\"[^\"]*\")")
# `static const char name[] = "...";` - the one binding form the two files use.
BIND_RE = re.compile(r'\bstatic\s+const\s+char\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*\])?\s*=\s*"([^"]*)"')
# The OSUnserialize list grammar, as a name list: `()` or `('a', 'b')`.
LIST_RE = re.compile(r"^\(\s*(?:'[^']*'\s*(?:,\s*'[^']*'\s*)*)?\)$")


def say(*a):
    print(*a)


def fail(lines):
    for ln in lines:
        print(ln, file=sys.stderr)
    sys.exit(1)


def bodies(path):
    """Every `deleteList`/`excludeList` body in `path`, as (method, lineno, text)."""
    try:
        src = open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        fail(["platform_lists: cannot read %s: %s" % (path, e)])

    out = []
    lines = src.splitlines()
    i = 0
    while i < len(lines):
        m = DEF_RE.match(lines[i])
        if not m:
            i += 1
            continue
        start = i
        i += 1
        while i < len(lines) and lines[i] != "}":
            i += 1
        out.append((m.group(1), start + 1, "\n".join(lines[start:i])))
        i += 1
    return out


def bindings(src):
    """Every `static const char name[] = "..."` in the source, with the *set* of values per name.

    A name with two values is its own defect - "one value, two definitions" - so it is reported
    rather than silently resolved to whichever the scan saw last, which is what a dict built with
    `dict(...findall(...))` does and what the first draft of this check did.
    """
    out = {}
    for name, value in BIND_RE.findall(src):
        out.setdefault(name, set()).add(value)
    return out


def answer(body, binds, where):
    """The string a body's first `return` answers with, or None if there is no readable one."""
    m = RET_RE.search(body)
    if not m:
        return None
    tok = m.group(1)
    if tok.startswith('"'):
        return tok[1:-1]

    # The body's own binding wins over the file's: a local `static const char kEmptyList[]` inside
    # the function is the one that `return( kEmptyList )` reads.
    local = BIND_RE.search(body)
    if local and local.group(1) == tok:
        return local.group(2)
    values = binds.get(tok)
    if not values:
        return None
    if len(values) != 1:
        fail(["platform_lists: %s answers with `%s`, and this file binds that name to %d different "
              "values (%s)." % (where, tok, len(values), ", ".join(repr(v) for v in sorted(values))),
              "  One name, two values: whichever this function reads is not a property of the text a "
              "reader can check by eye."])
    return next(iter(values))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--file", nargs="*", default=DEFAULT_FILES,
                    help="C++ sources to scan (default: the stage's platform experts)")
    args = ap.parse_args()

    found = 0
    for path in args.file:
        if not os.path.exists(path):
            fail(["platform_lists: %s does not exist - a check that cannot read its input is not a "
                  "pass" % path])
        src = open(path, encoding="utf-8", errors="replace").read()
        binds = bindings(src)
        for method, lineno, body in bodies(path):
            found += 1
            where = "%s:%d %s" % (path, lineno, method)
            value = answer(body, binds, where)
            if value is None:
                fail(["platform_lists: %s answers with something this check cannot read as a string "
                      "literal." % where,
                      "  A NULL (or a name bound to a NULL, or any non-literal expression) is the "
                      "defect:",
                      "  `IODTFindMatchingEntries(from, 0, NULL)` takes the *else* branch and collects "
                      "every entry, so `processTopLevel` detaches the whole IODT top level - `chosen` "
                      "included - and `IOFindBSDRoot` can no longer root this machine.",
                      "  Answer with an OSUnserialize list: `\"()\"` for the empty one."])
            if not LIST_RE.match(value):
                fail(["platform_lists: %s answers with %r, which is not an OSUnserialize name list."
                      % (where, value),
                      "  The grammar is `()` or `('name', 'name')` (`libkern/c++/OSUnserialize.y:168`); "
                      "`IODTMatchNubWithKeys` hands the string to `OSUnserialize` and compares the "
                      "entry's name against what comes back, so a string outside that grammar is a "
                      "NULL at run time even when it is not a NULL here.",
                      "  Apple's own answers: "
                      "`\"('sd', 'st', 'disk', 'tape', 'pram', 'rtc', 'mouse')\"` (AppleMacIO.cpp:108) "
                      "and `\"('packages', 'psuedo-usb', 'psuedo-hid', 'multiboot', 'rtas')\"` "
                      "(ApplePlatformExpert.cpp:86)."])
            say("  platform_lists: %s answers %r" % (where, value))

    if found != len(METHODS):
        fail(["platform_lists: %d of the %d platform-expert list methods were found in %s."
              % (found, len(METHODS), ", ".join(os.path.basename(f) for f in args.file)),
              "  Both are pure virtuals of `IODTPlatformExpert`, and a missing definition is not a "
              "gap in this check: the stub generator synthesises a stand-in for anything undefined, "
              "so the machine would run a generated body instead of a platform expert's."])
    return 0


if __name__ == "__main__":
    sys.exit(main())
