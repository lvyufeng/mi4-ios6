#!/usr/bin/env python3
"""
Which symbol would the entry image stop on, if it started in a given function?

The frontier rule links one object per device run, and the prediction of what the
*next* run stops on has been made by hand from `objdump -d` - correctly eleven
times out of twelve. Experiment 226 is the failure, and it is worth naming
because it is the shape of failure this file exists to remove:

    `vm_mem_bootstrap` calls `vm_page_bootstrap` first, and `vm_page_bootstrap`
    was already real - 888 bytes, five calls, and every one of them real. So the
    prediction was the next call in the caller, `zone_bootstrap`. But
    `vm_page_bootstrap` calls `vm_page_init_lck_grp`, whose *last* instruction is
    a tail call to `vm_compressor_init_locks`, which is not in the image. The
    reading was one level deep and the path was two.

The hand method checks a function's own calls. This checks the transitive
closure of them, in call order, and reports the first stub it reaches - which is
the symbol `entry_stub_hit` will name.

    ./tools/xnu_entry_callwalk.py                       # from kernel_bootstrap
    ./tools/xnu_entry_callwalk.py --root arm_init
    ./tools/xnu_entry_callwalk.py --elf out/stage90/xnu_arm_entry.elf --json

What it does and does not know:

  - Stubs are recognised *structurally*, not by name: the generator emits
    `void f(void) { entry_stub_hit("f"); }`, so every stub's body ends in a
    branch to `entry_stub_hit`. Anything that is not one of those is treated as
    real code and walked into.
  - It follows direct `bl` and tail `b` to a named function. It **cannot follow
    an indirect call** (`blx r3`, `ldr pc, [...]`), and it says so rather than
    guessing: an indirect call reached before any stub is reported as a
    limitation, because the callee is only known at run time.
  - Calls are visited in address order within a function, which is call order
    for the straight-line initialisation code this image is made of. A call
    inside a conditional block may be followed even when the run would skip it,
    so the answer is "the first stub on the path as written", not "the first
    stub that will certainly execute". When the two differ, reading the branch
    is what decides - but the reading then starts from a named place instead of
    from the whole image.

Exit status is 0 when a stub was found, 1 when the walk completed with none, and
2 when it could not be completed (an indirect call with no stub before it).
"""

import argparse
import json
import re
import subprocess
import sys
from collections import OrderedDict

REPO_ROOT = __file__.rsplit("/tools/", 1)[0]
DEFAULT_ELF = REPO_ROOT + "/out/stage90/xnu_arm_entry.elf"
OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"

STUB_TARGET = "entry_stub_hit"

# `  20d67c:\teb00b68f \tbl\t23b0c0 <kernel_debug_string_early>`
INSN_RE = re.compile(
    r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{8}\s+)?"
    r"(?P<mnemonic>[a-z][a-z0-9.]*)\s*(?P<operands>.*?)\s*$"
)
SYM_RE = re.compile(r"^([0-9a-f]{8})\s+<([^>]+)>:\s*$")


class Image:
    """The entry ELF, as functions and the direct calls between them."""

    def __init__(self, path):
        self.path = path
        self.symbols = self._read_symbols()
        self.by_addr = {value: (name, size) for name, size, value in self.symbols}
        self.by_name = {name: value for name, _size, value in self.symbols}
        self.functions = self._read_functions()
        self.calls = {}
        self.stubs = set()
        self.indirect = {}
        for name in self.functions:
            self._analyse(name)

    def _read_symbols(self):
        out = subprocess.run(
            [NM, "-S", "-P", self.path], check=True, capture_output=True, text=True
        ).stdout
        symbols = []
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 4:
                continue
            name, kind, value, size = parts[0], parts[1], parts[2], parts[3]
            if kind not in ("T", "t") or value == "0" or size in ("", "0"):
                continue
            symbols.append((name, int(size, 16), int(value, 16)))
        return symbols

    def _read_functions(self):
        out = subprocess.run(
            [OBJDUMP, "-d", "--no-show-raw-insn", self.path],
            check=True, capture_output=True, text=True,
        ).stdout
        functions = OrderedDict()
        current = None
        for line in out.splitlines():
            sym = SYM_RE.match(line)
            if sym:
                # objdump labels every local branch target, as
                # `<lck_mtx_lock_contended+0x6c>`. Those are not functions: a
                # boundary created at one of them splits a function in two and
                # hides the conditional branches that guard its cold paths.
                name = sym.group(2).split("+", 1)[0]
                if name != current:
                    current = name
                    functions.setdefault(name, [])
                continue
            insn = INSN_RE.match(line)
            if insn and current is not None:
                functions[current].append(
                    (int(insn.group(1), 16), insn.group("mnemonic"), insn.group("operands"))
                )
        return functions

    def _analyse(self, fn):
        name = fn
        calls = []
        indirect = []
        cold, spans = self._guarded_addresses(name)
        for addr, mnemonic, operands in self.functions[name]:
            if mnemonic in ("bl", "blx"):
                target, _local = self._direct_target(operands, name)
                if target is None:
                    if mnemonic == "blx" or "pc" in operands:
                        indirect.append((addr, operands))
                    continue
                calls.append((addr, target, self._is_guarded(addr, cold, spans)))
                if target == STUB_TARGET:
                    self.stubs.add(name)
            elif mnemonic == "b":
                target, local = self._direct_target(operands, name)
                if target == STUB_TARGET:
                    # `void f(void) { entry_stub_hit("f"); }` compiles to a
                    # movw/movt of the name and a tail `b` into `entry_stub_hit`.
                    # That branch *is* the stub marker.
                    self.stubs.add(name)
                elif target is not None and not local:
                    calls.append((addr, target, self._is_guarded(addr, cold, spans)))
        self.calls[name] = calls
        self.indirect[name] = indirect

    def _guarded_addresses(self, fn):
        """Addresses in `fn` that no straight-line path from its entry reaches.

        Two ways a call ends up behind a condition, and both are needed:
        `vm_page_init_lck_grp`'s tail call to `vm_compressor_init_locks` is
        straight-line and must be walked into, while `lck_mtx_lock`'s `bl panic`
        - an argument assertion, six instructions in - is fall-through reachable
        and must not be.

          - unreachable-without-taking-a-branch: follow fall-through and local
            `b`, stop at `bx`/`pop {..pc}`/a tail call. The contended path of a
            mutex is entered this way.
          - inside a forward conditional branch's span: a `beq` that jumps over
            the call runs the call only when the branch is not taken. An
            assertion is entered this way.

        A call that is neither runs whenever its caller does, which is the
        assumption the prediction rests on.
        """
        insns = self.functions[fn]
        if not insns:
            return set(), []
        index = {addr: i for i, (addr, _m, _o) in enumerate(insns)}

        hot = set()
        pending = [insns[0][0]]
        while pending:
            addr = pending.pop()
            while addr in index and addr not in hot:
                hot.add(addr)
                i = index[addr]
                mnemonic, operands = insns[i][1], insns[i][2]
                if mnemonic == "b":
                    target, local = self._direct_target(operands, fn)
                    m = re.match(r"^([0-9a-f]+)\s", operands)
                    if local and m:
                        addr = int(m.group(1), 16)
                        continue
                    break
                if mnemonic in ("bx", "blx"):
                    break
                if (mnemonic.startswith("pop") or mnemonic.startswith("ldm")) and "pc" in operands:
                    break
                if i + 1 >= len(insns):
                    break
                addr = insns[i + 1][0]

        spans = []
        for addr, mnemonic, operands in insns:
            if mnemonic in ("b", "bl", "blx", "bic", "bfc", "bfi", "bkpt"):
                continue
            if not mnemonic.startswith("b"):
                continue
            _target, local = self._direct_target(operands, fn)
            if not local:
                continue
            m = re.match(r"^([0-9a-f]+)\s", operands)
            if m and int(m.group(1), 16) > addr:
                spans.append((addr, int(m.group(1), 16)))

        cold = {addr for addr in index if addr not in hot}
        return cold, spans

    @staticmethod
    def _is_guarded(addr, cold, spans):
        return addr in cold or any(frm < addr < to for frm, to in spans)

    def _direct_target(self, operands, current=None):
        """(callee name, is_local) for `bl`/`b` operands, or (None, True) if not direct.

        objdump writes a branch to a label inside the same function as
        `<f+0x6c>` and a tail call to another function as `<g>`. Both end in a
        symbol name, so the `+` offset and the name are what tell them apart -
        which matters, because a `bne <f+0x98>` that a cold call sits behind is
        the whole of the guarded-call detection.
        """
        m = re.search(r"<([^>]+)>\s*$", operands)
        if not m:
            return None, True
        inside = m.group(1)
        name, _, offset = inside.partition("+")
        is_local = bool(offset) and name == current
        if not offset and name == current:
            is_local = True
        return name, is_local

    def _is_local(self, target_name, addr):
        """A `b` to `name+0x18` is a loop, not a tail call into `name`."""
        return target_name in self.functions and self.by_name.get(target_name, 0) == 0

    def walk(self, root, guarded=False):
        """First stub on the path from `root`, in call order.

        A depth-first walk in call order, which is execution order for the
        straight-line initialisation code this image is made of. `seen` stops a
        cycle from being followed twice; a function is not revisited on a second
        path, which is not a loss here - the walk stops at the first stub either
        way, and the answer wanted is the earliest one.

        `guarded=False` skips calls inside a conditional block, which is the
        prediction: an initialisation path does not take its assertion branches.
        `guarded=True` follows them too, which is an upper bound - what the run
        would stop on if one of those branches were taken.
        """
        seen = set()

        def visit(name, path):
            if name in self.stubs:
                return path + [name]
            if name in seen:
                return None
            seen.add(name)
            for _addr, callee, is_guarded in self.calls.get(name, ()):
                if is_guarded and not guarded:
                    continue
                found = visit(callee, path + [name])
                if found:
                    return found
            return None

        path = visit(root, [])
        return (path[-1] if path else None), (path or [])

    def guarded_calls_before(self, root, stop):
        """The guarded call sites the unguarded walk stepped over, so they are not hidden."""
        seen = set()
        found = []

        def visit(name, path):
            if name == stop or name in seen:
                return
            seen.add(name)
            for addr, callee, is_guarded in self.calls.get(name, ()):
                if is_guarded:
                    found.append((name, addr, callee))
                else:
                    visit(callee, path + [name])

        visit(root, [])
        return found

    def report(self, root):
        found, path = self.walk(root)
        if found:
            print("walk from %s:" % root)
            for depth, name in enumerate(path):
                print("  %s%s%s" % ("  " * depth, name, "   STUB" if name == found else ""))
            print()
            print("first stub on the straight-line path: %s" % found)
            print("  the run should stop with stub_hit=%s" % found)
        else:
            print("walk from %s reached no stub on the straight-line path." % root)
            print("  either everything on it is real, or it continues through an")
            print("  indirect call, which this walk cannot follow.")
            self.report_indirect(root)

        skipped = self.guarded_calls_before(root, found)
        if skipped:
            print()
            print("  %d call(s) inside a conditional block were not followed;" % len(skipped))
            print("  if one of them is taken, the stop is somewhere else:")
            for name, addr, callee in skipped[:12]:
                print("    %s+0x%x -> %s" % (name, addr - self.by_name.get(name, addr), callee))
            if len(skipped) > 12:
                print("    ... and %d more" % (len(skipped) - 12))
        return 0 if found else 1

    def report_indirect(self, root):
        """Say where the walk was blind, so a 'no stub' answer is not read as 'all real'."""
        seen = set()
        blind = []

        def visit(name, depth):
            if name in seen or depth > 12:
                return
            seen.add(name)
            for addr, operands in self.indirect.get(name, ()):
                blind.append((depth, name, addr, operands))
            for _addr, callee, _is_guarded in self.calls.get(name, ()):
                visit(callee, depth + 1)

        visit(root, 0)
        if not blind:
            return
        print("  indirect calls the walk could not follow:")
        for depth, name, addr, operands in blind[:20]:
            print("    %s%s+0x%x: blx %s" % ("  " * depth, name,
                                             addr - self.by_name.get(name, addr), operands))
        if len(blind) > 20:
            print("    ... and %d more" % (len(blind) - 20))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", default=DEFAULT_ELF)
    ap.add_argument("--root", default="kernel_bootstrap")
    ap.add_argument("--list-stubs", action="store_true",
                    help="print every stub in the image and exit")
    args = ap.parse_args()

    image = Image(args.elf)
    if args.list_stubs:
        for name in sorted(image.stubs):
            print(name)
        print("%d stub(s) of %d function(s)" % (len(image.stubs), len(image.functions)))
        return 0

    if args.root not in image.functions:
        print("no function named %r in %s" % (args.root, args.elf), file=sys.stderr)
        return 2
    return image.report(args.root)


if __name__ == "__main__":
    sys.exit(main())
