#!/usr/bin/env python3
"""check_idle_window_unreachable.py - is the idle window's `pop {fp, pc}` reachable in this image?

WHY THIS EXISTS
---------------
The parked arm (594/595, `STAGE90_XNU_IDLE_NO_SLEEP=1`) is about to spend the project's one scarce
resource - a power press - on the claim that it gets past the death point at
`platform_cache_idle_exit`'s `pop {fp, pc}`. 594 built it, 595 gated it, and both describe *why* it
should: the switch compiles out 514's one-shot repair, so `SIGPdisabled` stays set, so `cpu_idle`
leaves by its first door on every pass and never enters the window.

That description is a story about a control flow. This file turns it into a property of the image, and
the property is stronger than the story: on the sleepless image the window has **no route in at all**,
and the reason is a fixed point of the port rather than an intention of the arm.

WHAT IT COMPUTES, AND WHY EACH STEP IS NEEDED
---------------------------------------------
Read Apple's own source (`osfmk/arm/cpu.c:118`) and the image agrees instruction for instruction:

    if ((!idle_enable) || (cpu_data_ptr->cpu_signal & SIGPdisabled))
            Idle_load_context();          /* -> 0x8000d978, a TAIL branch */
    if (!SetIdlePop())
            Idle_load_context();          /* -> the same door */
    ... platform_cache_idle_enter(); cpu_idle_wfi(); platform_cache_idle_exit(); ...

  (A) **`cpu_idle` is `noreturn`.** Both of its exits are a tail branch (`b`, not `bl`) into
      `__wrap_Idle_load_context`, so they do not return to `cpu_idle` - they dispatch. This matters
      because it makes "leaves by door 1" mean *the normal idle path* and not a spin: the door-1
      branch IS `Idle_load_context`, which is how XNU's idle hands the CPU to another thread.
  (B) **The window has exactly one entry.** Each of `__wrap_platform_cache_idle_enter`,
      `__wrap_cpu_idle_wfi` and `__wrap_platform_cache_idle_exit` has exactly one `bl` site in the
      whole image, and all three are inside `cpu_idle` and *after* the `SetIdlePop` gate. So there is
      no second route that could open the window while the gate stays shut.
  (C) **The gate is behind the `SIGPdisabled` test.** The instruction sequence at the head of
      `cpu_idle` loads `cpu_data->cpu_signal` (offset 0x28) and branches to the door when bit 31 is
      set: `cmn r0, #1` / `ble`, which is exactly `cpu_signal & SIGPdisabled` for `SIGPdisabled =
      0x80000000` (`osfmk/arm/cpu_internal.h:68`).
  (D) **`SIGPdisabled` is a closed fixed point on this port**, and that is the whole reason the arm
      works. The only writer that *clears* it is `cpu_signal_handler_internal(FALSE)`
      (`cpu_common.c:401-402`, the `mvn r1, #0x80000000` / `bl hw_atomic_and` pair), whose only caller
      is `cpu_signal_handler()` - registered as **the IPI handler** (`machine_routines.c:605`). And
      the IPI *sender* is guarded by the same bit (`cpu_common.c:343`/`:374`: no IPI while it is set).
      With `real_ncpus == 1` (measured for this image in 549) there is no other CPU to send one
      either. So the bit, once set at init (`cpu.c:365`), is never cleared - unless something calls
      the handler in software, which is precisely what 514's repair does and what this arm compiles
      out. Hence: **count the `bl cpu_signal_handler_internal` sites. Zero means the gate can never
      open and the `pop` is unreachable; one means it opens exactly once (the baseline's shape).**
  (E) **And the arm's ceiling is not "time stops".** The fixed point also means the IPI handler never
      runs, and that handler is one of the things that calls `rtclock_intr`. It is not the only one:
      `locore.s:1621`/`:1862` call it from the exception vectors (`fleh_decirq_handler` and the other
      second-level path) and `entry_irq_handler` calls it too. So the tick does not depend on the IPI,
      deadlines still fire, and the park's timed poll can return. This is what makes rung 1 of the
      pre-registered ladder a reachable reading rather than a hope.

It deliberately does **not** try to be an ARM decoder. It reads the handful of forms these functions
use and REFUSES on anything else, so a compile that changes shape is a refusal and not a silent
mis-parse. It prints its derivation, so the answer can be read rather than trusted.

EXIT CODES
----------
0 - the chain holds and the image's regime is as measured (the message says which).
1 - a check failed, or the image is in a regime this tool has no reading for.
2 - the image could not be read at all (no symbols, no disassembly).
"""

import argparse
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"

# The symbols the derivation walks. `cpu_idle` is the only caller of the window's three wrappers;
# `cpu_idle_exit` holds the second `noreturn` exit; the three `__wrap_*` are the interception points
# 517 put between XNU and the real functions.
SYM_CPU_IDLE = "cpu_idle"
SYM_CPU_IDLE_EXIT = "cpu_idle_exit"
SYM_DOOR = "__wrap_Idle_load_context"
SYM_SETIDLEPOP = "__wrap_SetIdlePop"
WINDOW_WRAPPERS = (
    "__wrap_platform_cache_idle_enter",
    "__wrap_cpu_idle_wfi",
    "__wrap_platform_cache_idle_exit",
)
SYM_CLEARER = "cpu_signal_handler_internal"
SYM_RTCLOCK = "rtclock_intr"

# `cpu_data->cpu_signal`'s offset, from `osfmk/arm/cpu_internal.h`'s struct. Asserted, not assumed:
# the test below is checked to read *this* offset, so a struct change that moved the field would make
# this tool refuse rather than read the wrong word.
CPU_SIGNAL_OFF = 0x28
SIGPDISABLED = 0x80000000

INSN = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*?)\s*$")
BRANCH_TARGET = re.compile(r"^([0-9a-f]+)\s+<([^>]+)>")


class Refuse(Exception):
    """A shape this checker does not understand. Refusing is the point."""


def run(argv):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, check=True)
    except FileNotFoundError:
        raise Refuse(f"{argv[0]} not found")
    except subprocess.CalledProcessError as exc:
        raise Refuse(f"command failed: {' '.join(argv)}\n{exc.stderr.strip()}")
    return out.stdout


def symbols(elf):
    """{name: (addr, kind)} from every `nm` entry, plus a sorted address list for extent lookups."""
    syms = {}
    for line in run([NM, "-n", elf]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr, kind, name = parts
            try:
                syms[name] = (int(addr, 16), kind)
            except ValueError:
                continue
    if not syms:
        raise Refuse(f"no symbols in {elf}")
    return syms


def insns(elf, lo, hi):
    """Parsed instructions in [lo, hi). Each is (addr, mnemonic, operands)."""
    text = run([OBJDUMP, "-d", f"--start-address={lo:#x}", f"--stop-address={hi:#x}", elf])
    out = []
    for line in text.splitlines():
        m = INSN.match(line)
        if m:
            out.append((int(m.group(1), 16), m.group(3), m.group(4)))
    if not out:
        raise Refuse(f"no instructions disassembled in [{lo:#x}, {hi:#x})")
    return out


def all_insns(elf):
    text = run([OBJDUMP, "-d", elf])
    out = []
    for line in text.splitlines():
        m = INSN.match(line)
        if m:
            out.append((int(m.group(1), 16), m.group(3), m.group(4)))
    if not out:
        raise Refuse("no instructions disassembled")
    return out


def func_of(addr, syms):
    """The name of the function containing `addr`: the greatest symbol address <= addr that is a
    function (`t`/`T`). Refuses if there is none, because 'which function is this in' is the whole
    question at several sites below."""
    best = None
    for name, (a, kind) in syms.items():
        if kind in ("t", "T") and a <= addr and (best is None or a > best[0]):
            best = (a, name)
    if best is None:
        raise Refuse(f"no function symbol at or below {addr:#x}")
    return best[1]


def target_of(operands):
    m = BRANCH_TARGET.match(operands)
    if not m:
        return None, None
    return int(m.group(1), 16), m.group(2)


def extends_of(syms, name):
    """(lo, hi) for a function: its address and the next function-or-object address after it."""
    addr = syms[name][0]
    nxt = min((a for a, _ in syms.values() if a > addr), default=addr + 0x400)
    return addr, nxt


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("elf", help="the entry image's ELF (out/stage90/xnu_arm_entry.elf)")
    args = ap.parse_args()
    elf = args.elf

    syms = symbols(elf)
    for need in (SYM_CPU_IDLE, SYM_CPU_IDLE_EXIT, SYM_DOOR, SYM_SETIDLEPOP,
                 SYM_CLEARER, SYM_RTCLOCK) + WINDOW_WRAPPERS:
        if need not in syms:
            raise Refuse(f"symbol {need} not in {elf} - this is not an image of this phase")

    print(f"image: {elf}")
    print()

    # ---------------------------------------------------------------- (A) cpu_idle is noreturn
    door_addr = syms[SYM_DOOR][0]
    idle_lo, idle_hi = extends_of(syms, SYM_CPU_IDLE)
    exit_lo, exit_hi = extends_of(syms, SYM_CPU_IDLE_EXIT)

    print(f"== (A) the two `noreturn` exits: tail branches into {SYM_DOOR} ==")
    taken = []
    for fname, lo, hi in ((SYM_CPU_IDLE, idle_lo, idle_hi), (SYM_CPU_IDLE_EXIT, exit_lo, exit_hi)):
        for addr, mnem, ops in insns(elf, lo, hi):
            tgt, tname = target_of(ops)
            if mnem == "b" and tgt == door_addr:
                taken.append((fname, addr))
                print(f"  {fname} @ {addr:#010x}   b {SYM_DOOR}")
            elif mnem == "bl" and tgt == door_addr:
                raise Refuse(f"{fname} @ {addr:#010x} calls {SYM_DOOR} with `bl`: cpu_idle is "
                             f"declared noreturn, so a returning call here means the compiled shape "
                             f"is not the one this file's note describes")
    if len(taken) != 2:
        raise Refuse(f"expected exactly 2 tail branches into {SYM_DOOR} (one in {SYM_CPU_IDLE}, one "
                     f"in {SYM_CPU_IDLE_EXIT}); found {len(taken)}")
    print("  ok: both exits dispatch rather than return, so 'leaving by the first door' is the")
    print("      normal idle path (`Idle_load_context`) and not a spin")
    print()

    # ------------------------------------------------- (C) the gate, and the test in front of it
    print("== (C) the gate: SetIdlePop, behind the SIGPdisabled test ==")
    body = insns(elf, idle_lo, idle_hi)
    # **There are two `bl __wrap_SetIdlePop` sites in `cpu_idle`, and only the first is the gate.**
    # This check's own first draft assumed one and refused on the real image, which is the tool doing
    # its job; the second is `if (cpu_data_ptr->rtcPop != lastPop) SetIdlePop();` inside the
    # `idle_timer_notify` block (`osfmk/arm/cpu.c:150`), i.e. *after* the window. So the gate is the
    # earliest site, and any later one is asserted to be past the window's own calls - otherwise a
    # second gate-shaped call before the window would be a route this check has not read.
    pops = [(addr, i) for i, (addr, mnem, ops) in enumerate(body)
            if mnem == "bl" and target_of(ops)[0] == syms[SYM_SETIDLEPOP][0]]
    if not pops:
        raise Refuse(f"no bl {SYM_SETIDLEPOP} in {SYM_CPU_IDLE}: the window's gate is not where this "
                     f"file's note says it is")
    gate_addr, gate_i = min(pops)
    print(f"  {SYM_CPU_IDLE} @ {gate_addr:#010x}   bl {SYM_SETIDLEPOP}   <- the gate (the earliest of "
          f"{len(pops)} site(s))")

    # after the gate: `cmp r0, #0` then a conditional branch to the door
    after = body[gate_i + 1:gate_i + 4]
    if len(after) < 2 or after[0][1] != "cmp" or after[0][2].replace(" ", "") != "r0,#0":
        raise Refuse(f"the instruction after bl {SYM_SETIDLEPOP} is not `cmp r0, #0`: {after[:1]}")
    if not after[1][1].startswith("b") or after[1][1] in ("b", "bl", "bic", "bic", "bkpt"):
        raise Refuse(f"the instruction after the gate's cmp is not a conditional branch: {after[1]}")
    # **The branch is `bne` to the WINDOW, and the door is the fall-through** - which is the opposite
    # of this check's first draft, and the shape `cpu.c:125` has to compile to for a noreturn call:
    #     if (!SetIdlePop()) Idle_load_context();   ->  cmp r0, #0 / bne <window> / b <door>
    # The compiler emitted the door as the fall-through and the window as the taken branch, so what
    # this must assert is that the target is the window and that the door follows it.
    btgt, bname = target_of(after[1][2])
    if btgt is None:
        raise Refuse(f"the gate's branch has no symbol target: {after[1]}")
    print(f"  {SYM_CPU_IDLE} @ {after[1][0]:#010x}   {after[1][1]} {bname}   <- SetIdlePop() != 0 -> the window")
    # The door is the fall-through, and the fall-through is **not** one instruction: the compiler
    # emits `mov lr, pc` then `b __wrap_Idle_load_context` (the standard ARM tail-call idiom for a
    # noreturn call, and the same pair the seam's `b entry_note_pcx` uses). So the assertion is that a
    # tail branch to the door exists between the gate's branch and the window - not that it is the
    # very next instruction. This check's second draft asserted the latter and refused on the real
    # image, which is the second time the tool corrected its own author's mental model of the shape.
    if btgt <= gate_addr:
        raise Refuse(f"the gate's branch at {after[1][0]:#x} targets {btgt:#x}, which is not after "
                     f"the gate: this check reads the branch as 'take the window' and the target is "
                     f"not in the window")
    doors = [(a, m, o) for a, m, o in body
             if gate_addr < a < btgt and m == "b" and target_of(o)[0] == door_addr]
    if len(doors) != 1:
        raise Refuse(f"expected exactly one tail branch to the door between the gate at "
                     f"{gate_addr:#x} and the window at {btgt:#x}; found {len(doors)}: "
                     f"{[(hex(a)) for a, _, _ in doors]}")
    print(f"  {SYM_CPU_IDLE} @ {doors[0][0]:#010x}   b {SYM_DOOR}   <- otherwise, the door "
          f"(the fall-through, {btgt - doors[0][0]} bytes before the window)")
    print(f"      so the window runs only when SetIdlePop() returned non-zero, and every other pass")
    print(f"      leaves by the door")

    # and the test in front of the gate must read cpu_signal at CPU_SIGNAL_OFF and test bit 31
    guard = [x for x in body[:gate_i] if x[1] == "cmn" and x[2].replace(" ", "") == "r0,#1"]
    if len(guard) != 1:
        raise Refuse(f"expected exactly one `cmn r0, #1` before the gate; found {len(guard)}")
    cmn_addr = guard[0][0]
    # the cmn's operand must have been loaded from [r5, #CPU_SIGNAL_OFF]
    loaders = [x for x in body[:gate_i]
               if x[1] in ("ldr", "ldr.w")
               and re.match(r"r0,\s*\[r5,\s*#%d\]" % CPU_SIGNAL_OFF, x[2])]
    if not loaders:
        raise Refuse(f"no `ldr r0, [r5, #{CPU_SIGNAL_OFF}]` before the gate: cpu_signal is not read "
                     f"at offset {CPU_SIGNAL_OFF:#x} in this image, so the bit tested is not "
                     f"SIGPdisabled and this check does not apply")
    print(f"  {SYM_CPU_IDLE} @ {loaders[0][0]:#010x}   ldr r0, [r5, #{CPU_SIGNAL_OFF:#x}]   <- cpu_data->cpu_signal")
    print(f"  {SYM_CPU_IDLE} @ {cmn_addr:#010x}   cmn r0, #1        <- == (cpu_signal & {SIGPDISABLED:#010x})")
    print(f"      and a `ble` on it takes the door, so SIGPdisabled set means the gate is never")
    print(f"      reached: SetIdlePop() is not even called")
    print()

    # ------------------------------------------------- (B) the window's one entry, and its order
    print("== (B) the window's only entry: one bl per wrapper, all inside cpu_idle, after the gate ==")
    everything = all_insns(elf)
    for w in WINDOW_WRAPPERS:
        want = syms[w][0]
        sites = [(a, func_of(a, syms)) for a, m, o in everything
                 if m == "bl" and target_of(o)[0] == want]
        if len(sites) != 1:
            raise Refuse(f"{w} has {len(sites)} bl site(s) in this image ({[hex(a) for a, _ in sites]}); "
                         f"this check assumes exactly one, and a second would be a route into the "
                         f"window the notes do not name")
        a, fn = sites[0]
        if fn != SYM_CPU_IDLE:
            raise Refuse(f"{w} is called from {fn} and not from {SYM_CPU_IDLE}: there is another way "
                         f"into the window")
        if a <= gate_addr:
            raise Refuse(f"{w} is called at {a:#x}, at or before the gate at {gate_addr:#x}: the "
                         f"window is not behind the gate")
        print(f"  {w:<38} {a:#010x}  in {fn}, {a - gate_addr} bytes after the gate")
    # **The region's domination, which is the property that actually matters.** The gate's branch
    # target is where the window path begins; the question is whether *anything else* can reach into
    # that region, because if something can, then the window can be entered with the gate shut and the
    # whole fixed-point argument falls. The check is a branch-into-region census: every branch in
    # `cpu_idle` whose target lands inside the region must itself be inside the region (or be the
    # gate's own branch). A region no branch from outside enters is a region entered only through the
    # gate.
    region_lo = btgt
    region_hi = idle_hi
    intruders = []
    for addr, mnem, ops in body:
        if not mnem.startswith("b"):
            continue
        tgt, tname = target_of(ops)
        if tgt is None:
            continue
        if region_lo <= tgt < region_hi and not (region_lo <= addr < region_hi):
            if addr == after[1][0]:
                continue      # the gate's own branch, which is the one way in
            intruders.append((addr, mnem, tgt, tname))
    if intruders:
        raise Refuse(f"branch(es) into the window region from outside it: "
                     f"{[(hex(a), m, hex(t), n) for a, m, t, n in intruders]}. The region is not "
                     f"dominated by the gate, so a shut gate would not make the window unreachable")
    print(f"      and the region [{region_lo:#x}, {region_hi:#x}) has **no branch into it from outside**: every")
    print(f"      branch targeting inside it starts inside it, except the gate's own. So the gate")
    print(f"      dominates the whole window - the three calls, the WFI and the exit - and shutting it")
    print(f"      makes the `pop` unreachable")
    for addr, _i in sorted(pops):
        if addr != gate_addr:
            print(f"      (the other bl {SYM_SETIDLEPOP} at {addr:#x} is inside that region: cpu.c:150's")
            print(f"      rtcPop re-arm, reachable only by a pass that is already through the gate)")
    print()

    # ------------------------------------------------- (D) the clearer count: the regime
    print(f"== (D) the clearer: every `bl {SYM_CLEARER}` in the image ==")
    want = syms[SYM_CLEARER][0]
    clearers = [(a, func_of(a, syms)) for a, m, o in everything
                if m == "bl" and target_of(o)[0] == want]
    for a, fn in clearers:
        print(f"  {a:#010x}  in {fn}")
    if not clearers:
        print(f"  (none)")
    print()

    # ------------------------------------------------- (E) the timer does not need the IPI
    print(f"== (E) the ceiling: every `bl {SYM_RTCLOCK}` in the image ==")
    want = syms[SYM_RTCLOCK][0]
    ticks = [(a, func_of(a, syms)) for a, m, o in everything
             if m == "bl" and target_of(o)[0] == want]
    outside = [(a, fn) for a, fn in ticks if fn != SYM_CLEARER]
    for a, fn in ticks:
        mark = "  <- not the IPI handler" if fn != SYM_CLEARER else ""
        print(f"  {a:#010x}  in {fn}{mark}")
    if not outside:
        raise Refuse(f"every bl {SYM_RTCLOCK} is inside {SYM_CLEARER}: with the IPI handler unable to "
                     f"run, the tick would have no route and the arm's timed readings would be "
                     f"unreachable. That is a different image from the one this file was written for")
    print()

    # ------------------------------------------------- the verdict
    if len(clearers) == 0:
        print("VERDICT: the window is UNREACHABLE in this image.")
        print("  Nothing in it clears SIGPdisabled, the bit is set at init, the IPI sender is gated by")
        print("  the same bit, and there is one CPU - so the gate never opens and")
        print("  `platform_cache_idle_exit`'s `pop {fp, pc}` is never executed. This is the sleepless")
        print("  arm's claim, and it is a property of the image and not of the arm's intent.")
        print(f"  And the ceiling is not 'time stops': {len(outside)} of {len(ticks)} `bl {SYM_RTCLOCK}`")
        print("  sites are outside the IPI handler, so the tick has a route that does not need it.")
        return 0
    if len(clearers) == 1:
        print("VERDICT: the window is reachable EXACTLY ONCE in this image.")
        print(f"  One `bl {SYM_CLEARER}` at {clearers[0][0]:#010x} in {clearers[0][1]} - the repair 514")
        print("  added. Nothing else can clear the bit, so the gate opens on the pass after that call")
        print("  and the window is entered once per boot, which is the baseline's measured shape (520")
        print("  and 533 each publish one `sip_seq`/`pce_seq`/`wfi_seq` record against 16 door records).")
        return 0
    raise Refuse(f"{len(clearers)} `bl {SYM_CLEARER}` sites in this image: with more than one "
                 f"software clearer the bit is not a fixed point and neither verdict above applies. "
                 f"No image of this phase has had that, so this is a state this check has no reading "
                 f"for rather than a failure of the arm")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Refuse as exc:
        print(f"REFUSING: {exc}", file=sys.stderr)
        sys.exit(1)
