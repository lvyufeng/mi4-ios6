#!/usr/bin/env python3
"""check_idle_frame_slot.py - the address the idle exit's `pop` loads `pc` from, and who else writes it.

WHY THIS EXISTS
---------------
The first XNU boot dies at `platform_cache_idle_exit`'s `pop {fp, pc}`. Two facts about that death had
been established separately and never joined, and joining them is what this check does mechanically:

  (1) **The word the `pop` loads as `pc` is a value this project's own instrumentation spilled.** The
      two idle wrappers save `r4` in their prologues, and `r4` at that point is `[cpu_data + 0xe0]` - a
      deadline word - which `entry_slot_rtc_note` publishes as `xnu_live_slot_rtcpre_pop`. Both
      archived runs' fatal `pc` equals exactly that value `& ~1` (520: `0x04b79075` -> `0x04b79074`;
      533: `0x33f1c1b5` -> `0x33f1c1b4`). Registers are not addresses, so the value alone proves
      nothing about *memory* - it proves only where the value came from.

  (2) So the question is whether the wrappers' spill and the exit's saved `lr` are **the same memory
      address**. If they are, the only way the `pop` can see the spill's value is a cache line older
      than the exit's `push` - because the `push` stores `lr` to that same address with the cache off,
      i.e. straight to DRAM. If they are not, the whole account is different and the cache story is
      not needed. This file is the second half, and it is a claim about instruction addresses, so it
      can be *computed* rather than argued.

WHAT IT COMPUTES
----------------
For each of the four functions on the path it walks the prologue/epilogue as a running `sp` offset
relative to one base - `cpu_idle`'s `sp` at the three `bl`s - and records every store's absolute
offset and every `pop`/`ldm sp!` that loads `pc`. Then it asserts the equality above.

It deliberately does **not** try to be an ARM decoder. It understands the handful of instruction forms
these four functions actually use (`push`/`pop`, `sub`/`add sp`, `str`/`strd` on `sp`, `bl`, `b`, and
the `!` writeback forms) and refuses loudly on anything else, so a compile that changes shape is a
REFUSAL and not a silent mis-parse. The offset arithmetic is signed and printed with its derivation, so
the answer can be read rather than trusted.
"""

import argparse
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"

# The five symbols the derivation needs. `cpu_idle` is where the three wrappers are called from; the
# two real functions are the bodies the wrappers enclose.
SYMS = (
    "cpu_idle",
    "__wrap_platform_cache_idle_enter",
    "__wrap_cpu_idle_wfi",
    "__wrap_platform_cache_idle_exit",
    "platform_cache_idle_exit",
)

INSN = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)$")
BRANCH_TARGET = re.compile(r"^([0-9a-f]+)\s+<([^>]+)>")


class Refuse(Exception):
    """A shape this checker does not understand. Refusing is the point: a silent mis-parse here would
    be a check that agrees with the bug it exists to find."""


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode != 0:
        raise Refuse("command failed: %s\n%s" % (" ".join(cmd), p.stderr.strip()))
    return p.stdout


def symbols(elf):
    out = {}
    for line in run([NM, "-n", elf]).splitlines():
        f = line.split()
        if len(f) == 3 and f[1] in ("T", "t"):
            out.setdefault(f[2], int(f[0], 16))
    return out


def disasm(elf, start, stop):
    """[(addr, mnemonic, operands)] for [start, stop)."""
    out = run([OBJDUMP, "-d", "--start-address=0x%x" % start, "--stop-address=0x%x" % stop, elf])
    insns = []
    for line in out.splitlines():
        m = INSN.match(line)
        if m:
            insns.append((int(m.group(1), 16), m.group(2), m.group(3), m.group(4).split(";")[0].strip()))
    if not insns:
        raise Refuse("nothing disassembled in [0x%x, 0x%x) - wrong ELF or wrong address" % (start, stop))
    return insns


def end_of(elf, syms, name):
    """The exclusive end of a symbol: the next symbol above it, else start + 0x400."""
    if name not in syms:
        raise Refuse("%s is not in this ELF" % name)
    start = syms[name]
    above = sorted(a for a in syms.values() if a > start)
    return start, (above[0] if above else start + 0x400)


def sp_delta(operands):
    """`sub sp, sp, #N` / `add sp, sp, #N` -> +-N, or None if this is not one of those."""
    m = re.match(r"^sp,\s*sp,\s*#(-?\d+)$", operands)
    if not m:
        m = re.match(r"^sp,\s*#(-?\d+)$", operands)
    return int(m.group(1)) if m else None


def reg_list(operands):
    return [r.strip() for r in operands.strip("{}").split(",") if r.strip()]


def walk(elf, syms, name, base_note):
    """Walk one function and report what it writes to the stack and what it pops into `pc`.

    Returns (stores, pc_sources, calls) where
      stores     = [(absolute offset from the walk's base, register, mnemonic, pretty)] for every store
      pc_sources = [(absolute offset, detail)] for every `pop`/`ldm` that loads `pc`
      calls      = [(addr, callee)] for every `bl`
    The walk starts at offset 0 == the function's entry `sp`.
    """
    start, stop = end_of(elf, syms, name)
    insns = disasm(elf, start, stop)
    stores, pc_sources, calls = [], [], []
    sp = 0  # offset of `sp` from the function's entry sp

    def refuse(what, addr, mn, ops):
        raise Refuse("%s: %s at 0x%x: %s %s (refusing rather than guessing)"
                     % (name, what, addr, mn, ops))

    for addr, enc, mn, ops in insns:
        # Conditional variants (`ldrne`, `strbeq`, ...) share their base mnemonic; the condition does
        # not change the address arithmetic, so strip it and remember that the instruction may or may
        # not execute. A conditional `sp` write would be a shape this checker cannot model, and it
        # refuses those below rather than averaging over the two paths.
        cond = ""
        for c in ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt",
                  "gt", "le", "al"):
            if len(mn) > 2 and mn.endswith(c) and mn[:-2] in ("str", "strd", "ldr", "ldrd", "ldm",
                                                              "stm", "add", "sub", "mov", "bic",
                                                              "orr", "and", "eor", "tst"):
                cond, mn = c, mn[:-2]
                break
        if mn in ("push", "stmdb") or (mn == "stm" and "!" in ops and "sp" in ops):
            regs = reg_list(ops.split("[")[0] if "[" in ops else ops)
            if cond:
                refuse("a conditional push", addr, mn, ops)
            sp -= 4 * len(regs)
            for i, r in enumerate(regs):
                stores.append((sp + 4 * i, r, mn, "%s %s" % (mn, ops)))
        elif mn == "pop":
            regs = reg_list(ops)
            for i, r in enumerate(regs):
                if r == "pc":
                    pc_sources.append((sp + 4 * i, "pop %s" % ops))
            sp += 4 * len(regs)
        elif mn == "strd":
            m = re.match(r"^(\w+),\s*\[sp,\s*#(-?\d+)\]!?$", ops)
            if not m:
                refuse("strd form not understood", addr, mn, ops)
            off, rt = int(m.group(2)), m.group(1)
            if cond:
                refuse("a conditional strd", addr, mn, ops)
            sp += off
            stores.append((sp, rt, mn, "%s %s" % (mn, ops)))
            stores.append((sp + 4, "r%d" % (int(rt[1:]) + 1), mn, "%s %s" % (mn, ops)))
        elif mn == "str":
            m = re.match(r"^(\w+),\s*\[sp(?:,\s*#(-?\d+))?\]!?$", ops)
            if not m:
                if "[sp" in ops or re.search(r"\bsp\b", ops):
                    refuse("str with sp not understood", addr, mn, ops)
                continue
            if cond and ops.rstrip().endswith("!"):
                refuse("a conditional str with writeback", addr, mn, ops)
            off = int(m.group(2) or 0)
            at = sp + off
            if ops.rstrip().endswith("!"):
                sp += off
            stores.append((at, m.group(1), mn, "%s %s" % (mn, ops)))
        elif mn == "ldm":
            if not ops.rstrip().endswith("!"):
                continue
            if not ops.startswith("sp"):
                refuse("ldm with writeback on a non-sp base", addr, mn, ops)
            regs = reg_list(ops.split(",", 1)[1])
            if cond:
                refuse("a conditional ldm", addr, mn, ops)
            for i, r in enumerate(regs):
                if r == "pc":
                    pc_sources.append((sp + 4 * i, "ldm %s" % ops))
            sp += 4 * len(regs)
        elif mn in ("add", "sub"):
            if sp_delta(ops) is not None:
                if cond:
                    refuse("a conditional sp adjustment", addr, mn, ops)
                sp += sp_delta(ops)
            elif re.search(r"\bsp\b", ops):
                refuse("add/sub touching sp in a form not understood", addr, mn, ops)
        elif mn == "bl":
            t = BRANCH_TARGET.match(ops)
            calls.append((addr, t.group(2) if t else ops))
        else:
            # Everything else is only interesting if it touches `sp`. Three forms matter: a load with
            # writeback on sp, a write to sp, and a *read* of sp into a register (`mov r3, sp`, which
            # the exit wrapper uses to pass its own frame to the note). The read is harmless and
            # allowed; the other two are refused rather than ignored.
            if re.match(r"^sp\s*,", ops):
                refuse("an instruction writing sp", addr, mn, ops)
            if ops.rstrip().endswith("!") and re.search(r"\[sp", ops):
                refuse("a load with writeback on sp", addr, mn, ops)
            if re.search(r"\bsp\b", ops) and not re.search(r"\[sp", ops) \
               and not re.match(r"^\w+,\s*sp$", ops):
                refuse("an instruction naming sp in a form not understood", addr, mn, ops)
    return stores, pc_sources, calls, sp


def csselr_census(a, syms, say):
    """The second half: the level a set/way sweep acts on is CSSELR's, so *who writes CSSELR* decides
    whether the enter wrapper's clean could have cleaned the L1.

    The claim this checks is one the seam's own comment makes and the tree does not support: that the
    enter wrapper's `CleanPoC_Dcache` "cleaned the whole L1 earlier in this same window", so a clean at
    the seam "has nothing to write back". `CleanPoC_Dcache` is `DCCSW` by **set/way** (`cr7, cr10, {2}`)
    with **no CSSELR write of its own**, so it acts on whatever level CSSELR selects - and the only
    CSSELR writers are named here. If a cache routine ever grows one, or a third writer appears inside
    the idle path, this refuses.
    """
    start, stop = end_of(a.elf, syms, "cpu_idle")
    # every mcr/mcrr in .text that selects a cache level: `mcr 15, 2, ...` is CSSELR
    out = run([OBJDUMP, "-d", a.elf])
    ordered = sorted(syms.items(), key=lambda kv: kv[1])
    # The routines whose *operation* the seam's argument turns on. Matching is by the name's family
    # rather than by an exact list, because the disassembly labels these routines' inner loops with
    # their own symbols (`clean_dcacheway`, `cudr_loop`, ...) and an exact list would miss them - which
    # is how the first draft of this census let a doctored `CleanPoC_Dcache` through on the count test
    # alone instead of on the property.
    guard_prefixes = ("Clean", "Flush", "clean_", "flush_", "Invalidate", "invalidate_",
                      "platform_cache_idle", "clean_dcache", "clean_l2dcache")

    def enclosing(addr):
        name = None
        for n, v in ordered:
            if v <= addr:
                name = n
            else:
                break
        return name

    writers, cache_routine_writers = [], []
    for line in out.splitlines():
        m = INSN.match(line)
        if not m:
            continue
        addr, mn, ops = int(m.group(1), 16), m.group(3), m.group(4)
        if mn.startswith("mcr") and re.match(r"^15,\s*2,", ops):
            who = enclosing(addr) or ""
            writers.append((addr, who))
            if who.startswith(guard_prefixes):
                cache_routine_writers.append((addr, who))

    say("\n== every CSSELR writer in this image (mcr 15, 2, ...) ==")
    for addr, who in writers:
        say("  0x%08x  in %s" % (addr, who))

    bad = []
    if not writers:
        bad.append("no CSSELR writer at all - this image does not match the tree this check describes")
    for addr, who in cache_routine_writers:
        bad.append("a cache routine writes CSSELR at 0x%08x: %s - the level a sweep acts on would then "
                   "be decided by the routine itself and the seam's argument changes direction" % (addr, who))
    if len(writers) != 2:
        bad.append("expected exactly 2 CSSELR writers (XNU's `machine_write_csselr` and the entry "
                   "fixture's `entry_epilogue`), found %d: %s"
                   % (len(writers), ", ".join("0x%08x in %s" % w for w in writers)))
    if bad:
        say("")
        for b in bad:
            print("REFUSING: %s" % b, file=sys.stderr)
        return 1
    say("  -> neither is a cache routine, so no set/way sweep in this image selects its own level:")
    say("     each acts on whatever CSSELR holds, and XNU's `do_cacheid` leaves it at the L2.")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    try:
        syms = symbols(a.elf)
        base = syms.get("cpu_idle")
        if base is None:
            raise Refuse("cpu_idle is not in this ELF")

        def say(s):
            if not a.quiet:
                print(s)

        # ---- 1. the three calls out of cpu_idle, and that nothing moves `sp` between them ---------
        cstart, cstop = end_of(a.elf, syms, "cpu_idle")
        insns = disasm(a.elf, cstart, cstop)
        sp = 0
        sites = []          # (addr, callee, sp offset at the call)
        for addr, enc, mn, ops in insns:
            if mn in ("sub", "subs") and sp_delta(ops) is not None:
                sp += sp_delta(ops)
            elif mn in ("add", "adds") and sp_delta(ops) is not None:
                sp += sp_delta(ops)
            elif mn == "bl":
                t = BRANCH_TARGET.match(ops)
                sites.append((addr, t.group(2) if t else ops, sp))
        # `cpu_idle` writes its own frame words (`str r1, [sp]` / `[sp, #4]`) and that is fine - the
        # one-base argument needs only that `sp` does not MOVE between the three calls, which is what
        # the offsets recorded above say. Nothing else here changes `sp`; if a future compile moves it,
        # the three offsets differ and step 1 refuses below.
        wanted = ["__wrap_platform_cache_idle_enter", "__wrap_cpu_idle_wfi",
                  "__wrap_platform_cache_idle_exit"]
        got = {c: s for _, c, s in sites if c in wanted}
        say("== the three idle calls out of cpu_idle ==")
        for c in wanted:
            if c not in got:
                raise Refuse("cpu_idle does not call %s" % c)
            say("  %-38s sp offset from cpu_idle's entry: %+d" % (c, got[c]))
        if len({got[c] for c in wanted}) != 1:
            raise Refuse("the three wrappers are called with different sp offsets (%s) - the one-base "
                         "argument this check rests on does not hold: %s"
                         % (sorted(got.values()), got))

        # ---- 2. each wrapper's r4 spill, in the one base ------------------------------------------
        # The base is **cpu_idle's sp at the three bls**, by definition - not cpu_idle's entry sp. Step
        # 1 has just asserted that all three calls carry the same offset from cpu_idle's entry, so a
        # wrapper's own entry sp *is* the base and every offset below is measured from it. (The first
        # draft added `got[...]` here as well, counting the same `sub sp, sp, #8` twice and printing
        # -4 for an address that is at -12. The print of step 1's offsets is what it should be, and
        # this is the correction: the base needs no adjustment.)
        anchor = got[wanted[0]]

        def dump(name, tag):
            st, pc, cl, endsp = walk(a.elf, syms, name, tag)
            return st, pc, cl, endsp

        spill = []
        for w in ("__wrap_platform_cache_idle_enter", "__wrap_cpu_idle_wfi"):
            st, _, _, _ = dump(w, w)
            cand = [(o, r, p) for (o, r, _, p) in st if r == "r4"]
            if not cand:
                raise Refuse("%s does not store r4 on the stack - the spill this check is about is "
                             "not there, so the ELF is not the shape this check describes" % w)
            for o, r, p in cand:
                spill.append((w, o, p))
            say("\n== %s ==" % w)
            for o, r, _, p in st:
                say("  sp%+d  %s   (%s)" % (o, r, p))

        # ---- 3. the real exit: the `push {fp, lr}` and the `pop {fp, pc}` ------------------------
        # `platform_cache_idle_exit` is entered from `__wrap_platform_cache_idle_exit`, whose own sp at
        # that call is the wrapper's entry sp plus whatever its prologue did. Walk the wrapper to find
        # that offset, then walk the real function from there.
        wst, _, wcl, wend = dump("__wrap_platform_cache_idle_exit", None)
        exit_off = None
        for addr, callee in wcl:
            if callee == "platform_cache_idle_exit":
                # recompute the wrapper's sp at that call, then the real entry sp
                sp = 0
                insns = disasm(a.elf, *end_of(a.elf, syms, "__wrap_platform_cache_idle_exit"))
                for a2, e2, m2, o2 in insns:
                    if a2 == addr:
                        exit_off = sp
                        break
                    if m2 == "str" and o2.rstrip().endswith("!"):
                        sp += int(re.search(r"#(-?\d+)", o2).group(1))
                    elif m2 == "strd" and o2.rstrip().endswith("!"):
                        sp += int(re.search(r"#(-?\d+)", o2).group(1))
                    elif m2 in ("sub", "subs") and sp_delta(o2) is not None:
                        sp += sp_delta(o2)
                    elif m2 in ("add", "adds") and sp_delta(o2) is not None:
                        sp += sp_delta(o2)
                break
        if exit_off is None:
            raise Refuse("__wrap_platform_cache_idle_exit does not call platform_cache_idle_exit")
        say("\n== __wrap_platform_cache_idle_exit ==")
        for o, r, _, p in wst:
            say("  sp%+d  %s   (%s)" % (o, r, p))
        say("  -> it calls platform_cache_idle_exit with sp at %+d (from the one base)" % exit_off)

        rst, rpc, _, _ = dump("platform_cache_idle_exit", None)
        say("\n== platform_cache_idle_exit ==")
        for o, r, _, p in rst:
            say("  sp%+d  %s   (%s)" % (o + exit_off, r, p))
        if not rpc:
            raise Refuse("platform_cache_idle_exit has no `pop ... pc` - the death this check is about "
                         "is not in this function")
        say("  -> the pop takes pc from:")
        for o, detail in rpc:
            say("       sp%+d  (%s)" % (o + exit_off, detail))

        # ---- 4. the assertion, which is the whole point ------------------------------------------
        lr_addr = None
        for o, r, _, p in rst:
            if r == "lr" and "push" in p:
                lr_addr = o + exit_off
        if lr_addr is None:
            raise Refuse("platform_cache_idle_exit's push does not save lr")
        pc_addr = rpc[-1][0] + exit_off

        print("\n== the derivation, one base ==")
        print("  base = cpu_idle's sp at its three bls (offset 0)")
        for w, o, p in spill:
            print("  %-38s stores r4 at  %+d" % (w, o))
        print("  %-38s saves lr at   %+d   (%s)" % ("platform_cache_idle_exit", lr_addr, "push {fp, lr}"))
        print("  %-38s pops  pc from %+d   (%s)" % ("platform_cache_idle_exit", pc_addr, rpc[-1][1]))
        print("  and the base is cpu_idle's sp at its three bls, %+d from its own entry" % anchor)

        bad = []
        for w, o, p in spill:
            if o != pc_addr:
                bad.append("%s spills r4 at %+d, the pop reads pc from %+d" % (w, o, pc_addr))
        if pc_addr != lr_addr:
            bad.append("the pop reads pc from %+d while the push saves lr at %+d (that would mean no sp "
                       "change between them is not what this image does)" % (pc_addr, lr_addr))
        if bad:
            print("\nREFUSING: the wrappers' spill and the pop's source are NOT one address:")
            for b in bad:
                print("  " + b)
            return 1
        print("\nok: the two idle wrappers' r4 spill and this exit's saved lr are the SAME address")
        print("    (sp%+d from cpu_idle's own sp). So the word the pop loads as pc is the word the" % pc_addr)
        print("    push stored there, and the only way the pop can see the wrapper's value instead is")
        print("    a cache line older than the push - the push stores with SCTLR.C clear, i.e. to DRAM.")

        return csselr_census(a, syms, say)
    except Refuse as e:
        print("REFUSING: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
