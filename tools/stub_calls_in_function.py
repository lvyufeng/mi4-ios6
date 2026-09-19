#!/usr/bin/env python3
"""Which stubs does one function of the entry image call, and in what order?

    ./tools/stub_calls_in_function.py thread_invoke
    ./tools/stub_calls_in_function.py thread_block_reason thread_select thread_invoke
    ./tools/stub_calls_in_function.py --all | grep -v "no stub calls"

`xnu_entry_callwalk.py` answers the frontier question *transitively*: from a root, what is the
first name this image lacks? This answers it *per function*: the previous run stopped on a call,
the object defining that callee is about to be linked, and the question is what is left inside the
function the stop was in — and inside the functions it hands on to. Ten experiments running, the
next stop has been in that neighbourhood, and reading it off a disassembly by hand is where the
errors come from.

The predicate is the point of this file. Stub-ness is decided **by name, against the generated
stub object** (`nm --defined-only` on `xnu_arm_entry_realstubs.o`, type `T`), never by address.
The address rule looks safe and is not: the linker places the stub object's `.text` and then keeps
placing *real* code after it, so in the 306 image the stub text runs 0x80101690..0x80105410 and
`__aeabi_ldivmod` — real code out of libgcc — sits at 0x80105410, above the boundary, where a
`>= 0x80101690` rule calls it a stub. Preparing experiment 307, a hand scan of three functions was
made with exactly that rule; the three functions it was pointed at happened to contain no real
code above the boundary, so nothing was wrong in the answer, but the rule was wrong and would have
been silently wrong the first time a libgcc call appeared on a path.

Two limits, stated because the file cannot see past them:

  * **An indirect call is invisible.** `SCHED(f)` is `sched_multiq_dispatch.f`, so a scheduler
    callback is `blx r3` and the walk stops being able to name a callee. Every function's report
    says how many indirect calls it contains, so the blind spot is counted rather than assumed
    absent — `thread_select` has 56 of them and no direct stub call at all, which is a different
    statement from "thread_select cannot stop".
  * **Source order is not execution order.** Calls are listed by address, which is the order the
    compiler emitted the basic blocks, not the order the device runs them. A guarded call printed
    first may still never run (experiment 234), and a call printed last may be the next one.

Exit status is 0 when at least one of the named functions has a stub call, 1 when none does, and 2
when a named function is not in the image.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
OBJDUMP = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")
NM = os.environ.get("NM", "arm-none-eabi-nm")
DEFAULT_ELF = REPO_ROOT + "/out/stage90/xnu_arm_entry.elf"
DEFAULT_STUBS = REPO_ROOT + "/out/stage90/xnu_arm_entry_realstubs.o"

FUNC_RE = re.compile(r"^([0-9a-f]{8}) <(\S+)>:$")
# `800a0c10:	eb018a06 	bl	80103430 <mt_sched_update>` - leading offset is 4 hex digits in a
# relocated object and 8 in a linked image, so neither is assumed.
CALL_RE = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{8}\s+)?b(?:l)?\s+[0-9a-f]+ <(\S+)>")
INDIRECT_RE = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{8}\s+)?(?:blx\s|bx\s|ldr\s+pc|ldr\s+r\d+,\s*\[pc)")


def stub_names(path):
    """The names the generator stands in for, by the stub object's own symbol table."""
    out = subprocess.run([NM, "--defined-only", path], capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] == "T":
            names.add(parts[2])
    if not names:
        sys.exit("stub_calls_in_function: %s defines no function stubs" % path)
    return names


def functions(path):
    """{name: [(offset_address, callee), ...]} for every function, plus indirect counts."""
    out = subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True).stdout
    calls, indirect, fn = {}, {}, None
    for line in out.splitlines():
        m = FUNC_RE.match(line.strip())
        if m:
            fn = m.group(2)
            # A local label inside a function (`foo+0x10`) is a branch, not a function.
            if "+" not in fn:
                calls.setdefault(fn, [])
                indirect.setdefault(fn, 0)
            continue
        if fn is None:
            continue
        if INDIRECT_RE.match(line):
            indirect[fn] = indirect.get(fn, 0) + 1
        m = CALL_RE.match(line)
        if m and "+" not in m.group(1) and not m.group(1).startswith("."):
            addr = int(line.split(":", 1)[0].strip(), 16)
            calls.setdefault(fn, []).append((addr, m.group(1)))
    return calls, indirect


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("function", nargs="*", help="function(s) to report")
    ap.add_argument("--elf", default=DEFAULT_ELF)
    ap.add_argument("--stubs", default=DEFAULT_STUBS)
    ap.add_argument("--all", action="store_true", help="report every function in the image")
    args = ap.parse_args()

    if not os.path.exists(args.elf):
        sys.exit("stub_calls_in_function: no such image: %s" % args.elf)
    stubs = stub_names(args.stubs)
    calls, indirect = functions(args.elf)

    if args.all:
        wanted = sorted(calls, key=lambda f: min((a for a, _ in calls[f]), default=0))
    elif args.function:
        wanted = args.function
    else:
        ap.error("name a function, or pass --all")

    print("%d stub name(s); %d function(s) in %s" % (len(stubs), len(calls), args.elf))
    found = 0
    for name in wanted:
        if name not in calls and name not in indirect:
            print("%-28s not in this image" % name)
            return 2 if not args.all else 0
        hits = [(a, c) for a, c in sorted(calls.get(name, [])) if c in stubs]
        print("%-28s %d indirect call(s)%s" %
              (name, indirect.get(name, 0), "" if hits else "   no stub calls"))
        for addr, callee in hits:
            # `caller` is the address the callee returns to, which is what xnu_entry_stub_caller
            # prints: the `bl` is at caller-4.
            print("    %08x  caller %08x  ->  %s" % (addr, addr + 4, callee))
        found += len(hits)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
