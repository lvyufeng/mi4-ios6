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
    ./tools/xnu_entry_callwalk.py --elf out/stage90/xnu_arm_entry.elf --list-stubs

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
    for the straight-line initialisation code this image is made of.
  - **A run can stop earlier than this says, on a conditional branch that is
    taken, and no static walk can know that.** So the report is two lists: the
    straight-line answer, and the guarded call sites on that path in execution
    order, which is what to read when the device disagrees. Experiment 228 is
    that case - the answer was `zone_bootstrap`, the device printed
    `OSCompareAndSwap16`, and the guard that was taken is
    `pmap_steal_memory`'s on `pmap_enter`, which appears in the second list.
  - It can also stop **later** than this says, and that is experiment 234: the
    walk named `zinit`'s `kmem_alloc_kobject`, and the device printed `snprintf`,
    the *next* stub in the same function, because
    `if (kmem_alloc_ready) { ... kmem_alloc_kobject ... }` was false at the time.
    `kmem_alloc_ready` is set by `vm_mem_bootstrap` at `+0x148`, which is *after*
    its `vm_object_bootstrap` call at `+0x3c`, so when `zinit` runs the flag is
    still 0 and the whole block is skipped. No walk can evaluate that flag; what
    a walk can do is call the call guarded, which is what the guard rule now
    does, and this file was wrong about it until then - `by_branch` was being
    propagated along the entire straight-line segment that begins at an
    unconditional `b`, so the `beq` guarding the allocation was invisible. It now
    stops propagating at the first conditional branch crossed, which leaves
    experiment 228's loop body alone (its head-to-back-edge run is straight) and
    makes `--root vm_object_bootstrap` say `snprintf` on the exp-234 image.
  - Each entry in the second list is annotated with `guards_to_stub(callee)`:
    **None** means a taken branch there reaches no symbol this image lacks, so
    it cannot be the stop, and a number means the stop is that many conditional
    branches further, which names it. The annotation is per-entry on purpose.
    Sorting the list by it was tried and is worse: the cheapest entries are
    guards in functions that never execute on this path at all - the whole of
    `thread_deallocate`'s teardown, at zero or one branch from a `kfree` stub -
    and ranking by graph distance put them first and the stop that happened
    73rd of 90. Which guard is taken is a question about data, and the graph
    does not carry it.

**How to predict with it.** The answer that matters is usually not the walk
from `kernel_bootstrap` - that walk enters every large `-O2` function on the
path and then cannot leave its entry block. Four experiments running, the stop
has been *inside* a function the previous step made real:

    exp-229  linked OSAtomicOperations, stopped in memorystatus_pages_update
    exp-230  linked kern_memorystatus,  stopped in vm_pressure_response
    exp-228  linked vm_map,             stopped in OSCompareAndSwap16
    exp-234  linked vm_object,          stopped in snprintf

and the last two are named correctly by the simplest possible question, which is
not a walk at all:

    python3 tools/xnu_entry_callwalk.py --root <the symbol the last run named>

The previous frontier is where the run stopped, so that function is where it
*would* have gone next, and asking what *it* calls is one level down instead of
one level up. `--root memorystatus_pages_update` names `vm_pressure_response`,
which is what the device printed. When the frontier is a leaf - `vm_pressure_
response` may be one, `OSCompareAndSwap16` was - the answer is in the caller
after the return instead, and that is the case `--assume-taken` exists for.

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

    def __init__(self, path, forced=()):
        self.path = path
        # Call sites to walk into even though a conditional branch guards them.
        # A static walk cannot know which conditions hold; a *source reading* can
        # ("vm_page_bucket_count == 0 on the first boot"), and this is where that
        # reading is supplied so the rest of the walk stays mechanical.
        self.forced = set(forced)
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
        cold, by_branch, spans = self._guarded_addresses(name)
        for addr, mnemonic, operands in self.functions[name]:
            if mnemonic in ("bl", "blx"):
                target, _local = self._direct_target(operands, name)
                if target is None:
                    if mnemonic == "blx" or "pc" in operands:
                        indirect.append((addr, operands))
                    continue
                calls.append((addr, target,
                              self._is_guarded(addr, cold, by_branch, spans)))
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
                    calls.append((addr, target,
                                  self._is_guarded(addr, cold, by_branch, spans)))
        self.calls[name] = calls
        self.indirect[name] = indirect

    def _guarded_addresses(self, fn):
        """(cold, spans) for `fn`: what no straight-line path from its entry reaches.

        Two ways a call ends up behind a condition:

          - unreachable without taking a branch. Follow fall-through and local
            `b`, stop at `bx`/`pop {..pc}`/a tail call. The contended path of a
            mutex is entered this way.
          - inside a forward conditional branch's span *and* only reachable by
            falling into that span. `lck_mtx_lock`'s argument assertion is
            entered this way: `beq` at +0x10 jumps the `bl panic` at +0x20.

        The second rule is where this tool was wrong for experiment 228, and the
        correction is one word. A loop's exit test is also a forward conditional
        branch, and the loop *body* sits inside its span while being entered by
        an explicit `b` back to the loop head:

            bcs 21952c        <- the exit test, spanning the whole body
              ...
            b   2194cc        <- an unambiguous branch to the head
        2194cc:
            bl  pmap_next_page_hi     <- inside the span, and always executed

        Calling that call guarded is what made the walk answer `zone_bootstrap`
        where the device answered `OSCompareAndSwap16`, three calls further on.
        An address reached by an actual branch is not conditional, whatever
        span it happens to sit in, so the traversal now carries that fact.

        That fix was then wrong in the other direction, and experiment 234 is
        how: the flag was propagated along the *whole* run from an unconditional
        `b` to the next one, and a long run crosses conditional branches. In
        `zinit` the run from `b zinit+0x254` to `b zinit+0x638` covers the
        `beq zinit+0x63c` that guards `kmem_alloc_kobject`, so the guard was
        invisible, the walk named that call, and the device printed `snprintf`
        instead. The propagation now stops at the first conditional branch
        crossed - after one, the span rule is the right question again. A loop
        body is unaffected because its head-to-back-edge run is straight.

        **A third case is known and is deliberately not modelled here: code that
        follows a loop.** Experiment 425 is it - the walk answered `nwk_wq_init`
        and the device printed `dqinit`. In `vfsinit` the `for (i = 0;
        i < maxvfsslots; i++, vfsp++)` loop's exit tests are conditional forward
        branches to `+0x488`, so `+0x488` and the block after it are reached only
        by *taking* a branch, and the fall-through traversal never enters them:

            3b94: b    3be4          <- into the loop head
            3be0: beq  3d08          <- the exit test
            3bec: beq  3d08          <- and the second one
            ...
            3d08: ...  bl vnode_authorize_init   <- the post-loop block
            3d1c:      bl dqinit                 <- the stop the device printed

        Unlike the two cases above, knowing that a branch is a loop's exit test
        does not make its target *unconditional*: `zfree` reaches its zone-check
        blocks the same way (`beq 80070694` at `+0x530`, from inside the
        element-scan loop) and those calls do not run in this configuration. So
        the cold set keeps calling all of them guarded, the walk keeps skipping
        them, and the answer stays one level up - which is why `report()` prints
        the **stub-callee sites** separately: the guarded calls whose callee is
        itself a stub are the only ones that can be the stop at their own site,
        and there were 33 of them behind `bsd_init`'s answer and 175 in the full
        guarded list. Reading that short list is how this case is caught.
        """
        insns = self.functions[fn]
        if not insns:
            return set(), set(), []
        index = {addr: i for i, (addr, _m, _o) in enumerate(insns)}

        hot = set()
        by_branch = set()
        pending = [(insns[0][0], False)]
        while pending:
            addr, branched = pending.pop()
            while addr in index:
                if addr in hot and (not branched or addr in by_branch):
                    break
                hot.add(addr)
                if branched:
                    by_branch.add(addr)
                i = index[addr]
                mnemonic, operands = insns[i][1], insns[i][2]
                if mnemonic == "b":
                    target, local = self._direct_target(operands, fn)
                    m = re.match(r"^([0-9a-f]+)\s", operands)
                    if local and m:
                        pending.append((int(m.group(1), 16), True))
                    break
                if mnemonic in ("bx", "blx"):
                    break
                if (mnemonic.startswith("pop") or mnemonic.startswith("ldm")) and "pc" in operands:
                    break
                if (mnemonic.startswith("b")
                        and mnemonic not in ("b", "bl", "bx", "blx", "bic", "bfc", "bfi", "bkpt")):
                    # A conditional branch. What follows it is reached by falling
                    # past it, which is the span rule's case - so the branch that
                    # got us here stops justifying the addresses after this one.
                    # zinit's `kmem_alloc_kobject` is why this line exists: the
                    # straight-line segment from `b zinit+0x254` to `b zinit+0x638`
                    # was all marked `by_branch`, which made the `beq zinit+0x63c`
                    # guarding the allocation invisible, and the walk named a call
                    # the device skipped.
                    branched = False
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
        return cold, by_branch, spans

    @staticmethod
    def _is_guarded(addr, cold, by_branch, spans):
        """A call a straight-line path does not reach, or reaches only by falling in."""
        if addr in cold:
            return True
        return addr not in by_branch and any(frm < addr < to for frm, to in spans)

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

    def walk(self, root, guarded=False, visited=None):
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

        `visited` is an optional list the caller passes in; every function the
        walk enters is appended to it in visit order, whichever way the walk
        ends. `report()` needs it because the functions the walk *left* are not
        in the returned path, and those are where a skipped guarded call can hide
        the real stop (experiment 425, see `_guarded_addresses`).
        """
        seen = set()
        if visited is None:
            visited = []

        def visit(name, path):
            if name in self.stubs:
                return path + [name]
            if name in seen:
                return None
            seen.add(name)
            if not path or visited[-1:] != [name]:
                visited.append(name)
            for addr, callee, is_guarded in self.calls.get(name, ()):
                if is_guarded and not guarded and (name, addr) not in self.forced:
                    continue
                found = visit(callee, path + [name])
                if found:
                    return found
            return None

        path = visit(root, [])
        return (path[-1] if path else None), (path or [])

    @staticmethod
    def parse_site(text):
        """`pmap_steal_memory+0x130` -> (name, absolute address), using by_name."""
        name, _, offset = text.partition("+")
        return name, int(offset, 16) if offset else 0

    def first_stub_beyond(self, name):
        """First stub the *straight-line* walk from `name` reaches, or None.

        Deliberately sound rather than eager. A looser walk - following every
        conditional call - answers `PEHaltRestart` for almost everything, because
        `panic` is reachable from nearly every function in this image through an
        assertion. A sound `None` says "the guard was taken and what is behind it
        is not a straight-line path", which is a smaller and more honest claim
        than a sorted guess: it tells the reader to follow that one callee by
        hand, and it says exactly which one.
        """
        return self.walk(name)[0]

    def guards_to_stub(self, name):
        """Fewest further conditional branches to take from `name` to reach a stub, or None.

        This is the number that answers "which guard matters". Not every
        conditional call is equally far from a stop, and a static walk cannot
        know which condition holds - but it *can* know that one guard's callee
        stands one branch away from an unprovided symbol while another's stands
        behind seven.

        Experiment 228 is why this exists. The walk answered `zone_bootstrap`;
        the device printed `OSCompareAndSwap16`, reached through
        `pmap_steal_memory`'s guard on `pmap_enter`, and every entry in the
        guarded list had nothing to say beside it. A cost of 0 marks the entry
        that leads somewhere; `None` marks the entry that leads nowhere this
        image can name, and those cannot be the stop.

        Uniform-cost search over the call graph: an unguarded edge costs 0, a
        guarded edge costs 1, and the answer is the cheapest route to any stub.
        """
        if name in self.stubs:
            return 0
        index = {}
        best = None
        frontier = [(0, name)]
        while frontier:
            frontier.sort(key=lambda item: item[0])
            cost, here = frontier.pop(0)
            if here in index and index[here] <= cost:
                continue
            index[here] = cost
            for _addr, callee, is_guarded in self.calls.get(here, ()):
                step = cost + (1 if is_guarded else 0)
                if callee in self.stubs:
                    if best is None or step < best:
                        best = step
                    continue
                if step < index.get(callee, 1 << 30):
                    frontier.append((step, callee))
        return best

    def guarded_expansions(self, root):
        """Guarded call sites on the straight-line path, in execution order, with what is behind them.

        This is the answer to "the run stopped earlier than the walk said":
        a conditional branch that is taken. The list is ordered by where each
        site sits on the path, so it is read top-down in the order the kernel
        would reach them, and the first entry whose condition holds is the stop.
        Experiment 228's stop, `OSCompareAndSwap16`, is behind
        `pmap_steal_memory`'s guard on `pmap_enter`, and it appears here.
        """
        seen = set()
        found = []

        def visit(name):
            if name in seen or name in self.stubs:
                return
            seen.add(name)
            for addr, callee, is_guarded in self.calls.get(name, ()):
                if is_guarded:
                    found.append((name, addr, callee,
                                  callee if callee in self.stubs else self.first_stub_beyond(callee)))
                else:
                    visit(callee)

        visit(root)
        return found

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
        visited = []
        found, path = self.walk(root, visited=visited)
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

        self.report_guarded_stub_sites(visited, found, path)

        expansions = self.guarded_expansions(root)
        annotated = [(self.guards_to_stub(callee), name, addr, callee, beyond)
                     for name, addr, callee, beyond in expansions]
        if annotated:
            print()
            print("  if a conditional branch on that path is taken instead, the stop is")
            print("  the first of these whose condition holds, in execution order:")
            for cost, name, addr, callee, beyond in annotated[:16]:
                print("    %s+0x%x -> %s%s" % (name, addr - self.by_name.get(name, addr),
                                               callee, self._beyond(cost, callee, beyond)))
            if len(annotated) > 16:
                print("    ... and %d more" % (len(annotated) - 16))

    def report_guarded_stub_sites(self, visited, found, path=(), limit=40):
        """The guarded sites the walk skipped whose callee is itself a stub.

        The full guarded list runs to hundreds of entries and is truncated at 16,
        which buries the entries that matter most: a guarded call whose callee is
        a stub needs no further walking to be decisive - if that branch is taken,
        the stop is *there*, and the walk's answer - reached by leaving that
        function - is one level too high. Experiment 425's `vfsinit+0x49c ->
        dqinit` was 5th of these behind `bsd_init` but ~400th line of the full
        list, and it was the stop.

        Only sites the run reaches before the answer are listed, and that is
        decided rather than assumed:

          - a function the walk entered and left without descending towards the
            answer was fully traversed, so *every* site in it is earlier;
          - a function on the path towards the answer was left at one particular
            call - the one whose callee is the next name on the path - and only
            the sites before that call count.

        Listed in visit order: the functions the walk entered, in the order it
        entered them, and within each one the sites in address order, which is
        execution order for the straight-line code this image is made of.
        """
        descent = {}
        for i, name in enumerate(path[:-1]):
            for addr, callee, is_guarded in self.calls.get(name, ()):
                if callee == path[i + 1] and (not is_guarded or (name, addr) in self.forced):
                    descent[name] = addr
                    break
        sites = []
        for name in visited:
            for addr, callee, is_guarded in self.calls.get(name, ()):
                if not is_guarded or callee not in self.stubs:
                    continue
                if (name, addr) in self.forced:
                    continue
                if name in descent and addr > descent[name]:
                    continue
                sites.append((name, addr, callee))
        if not sites:
            return
        print()
        print("  and these guarded call sites have a stub for a callee, so if one of")
        print("  their branches is taken the stop is there, not at %s - in execution" % (found or "the answer"))
        print("  order, and each one is reached before that answer:")
        for name, addr, callee in sites[:limit]:
            print("    %s+0x%x -> %s   STUB" % (name, addr - self.by_name.get(name, addr), callee))
        if len(sites) > limit:
            print("    ... and %d more" % (len(sites) - limit))

        return 0 if found else 1

    def _beyond(self, cost, callee, beyond):
        """The tail of a guard entry: the symbol reached, and how many branches away."""
        if beyond is None:
            if cost is None:
                return "   (leads nowhere this image can name)"
            return "   (a stub %d guard%s further)" % (cost, "" if cost == 1 else "s")
        if beyond == callee:
            return "   STUB"
        return " -> %s" % beyond

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
    ap.add_argument("--assume-taken", action="append", default=[],
                    metavar="CALLER+0xNNN",
                    help="walk into this guarded call site as if its branch were "
                         "taken; repeatable, and the offsets are the ones the "
                         "guard list prints")
    args = ap.parse_args()

    image = Image(args.elf)
    for site in args.assume_taken:
        name, offset = Image.parse_site(site)
        if name not in image.functions:
            print("no function named %r in %s" % (name, args.elf), file=sys.stderr)
            return 2
        image.forced.add((name, image.by_name.get(name, 0) + offset))
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
