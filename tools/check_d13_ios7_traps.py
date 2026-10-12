#!/usr/bin/env python3
"""Assert BY VALUE that the linked image wires iOS 7's `svc #0x80` r12 = -3 to the real clock.

Reads the disassembly (`arm-none-eabi-objdump -d`) and the symbol table (`arm-none-eabi-nm`) of a
linked image (default `out/stage90/xnu_arm_entry.elf`) and DECODES THE ARM ENCODINGS itself - the
`cmn`, the conditional branches and the call - rather than trusting any source comment or the
objdump mnemonic text (`mi4-a-claim-in-a-comment-is-not-a-check`).  The body it inspects is
`locore_fleh_swi`: the entry image's own `fleh_swi` is a record-and-stop stub (`entry_stubs.c:9405`),
and `entry_vectors.s:173` points slot 2 at the renamed copy of `traps_lo.s`'s `EnterARM(fleh_swi)`,
which is the body the -3 syscall actually enters.

WHAT IS BEING CHECKED, BY VALUE.  iOS 7 userspace's `mach_absolute_time` reads `_COMM_PAGE +
0x90`; it is 0 on D13, so userspace does `mvn ip, #2` (r12 = -3) then `svc #0x80`.  Three board
gates in `traps_lo.s` decide what the kernel does with it:

  SWITCH OFF (the pre-fix shape, `--expect off`): the gate at :116 is `#if
  !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)`, OPEN on CANCRO, so `locore_fleh_swi`'s first
  instructions are `cmn r12, #3` (`e37c0003`) / `beq <swi_trap_tb>` - the fast path, which returns
  through `swi_trap_tb`'s `movs pc, lr` WITHOUT writing r0:r1, i.e. garbage time.  The two positive
  gates at :162/:264 are closed, so there is NO call to `mach_absolute_time` anywhere in the body.

  SWITCH ON (`--expect on`, `-DSTAGE90_XNU_IOS7_TRAPS=1`): the :116 gate is closed - the `cmn r12,#3`
  fast path is GONE - and the :162/:264 gates are open, so the saved-state path carries
  `cmn r11, #3` (`e37b0003`) / `beq <ios7lab_timebase>` and `ios7lab_timebase` is
  `bl/blx mach_absolute_time` / `b swi_exit64`.  `swi_exit64` (`traps_lo.s:195`, `str r1, [r8, #4]`)
  plus `swi_exit`'s `str r0, [r8]` write BOTH halves of the 64-bit clock back into the saved state,
  so the value userspace receives is the real ns clock (D13's `mach_absolute_time()` is ns-scaled,
  `rtclock.c:188-193` -> `:82-93`, raw CNTVCT via `pe_msm8974.c:423`).

Checks (all on the `locore_fleh_swi` body, whose VA range runs from its symbol to the next GLOBAL text
symbol - `locore_fleh_prefabt`, the next handler, NOT the next local label, which would cut the body
off after 15 instructions and never reach the -3 route at all):

  OFF: (a) a `cmn r12, #3` whose FOLLOWING instruction is `beq` to `swi_trap_tb` is present
       (`mi4-linked-code-order-is-not-source-order`: the branch target, not the source order, is what
       binds the pair);  (b) NO `bl`/`blx` to `mach_absolute_time` exists in the body.
  ON:  (a) NO `cmn r12, #3` / `beq swi_trap_tb` fast path;  (b) a `cmn r11, #3` whose following
       instruction is `beq` to the block that STARTS at a `bl`/`blx mach_absolute_time`;  (c) that
       call's next instruction is an unconditional `b` to `swi_exit64` - so the result is written back
       to r0:r1.  (b)+(c) are what "on the `cmn r11,#3` branch path reaching `swi_exit64`" means: the
       `beq` target IS the call's address, and the `b swi_exit64` follows it.

Rehearsal: against the CURRENT image (pre-fix) `--expect off` must PASS and `--expect on` must FAIL;
against a rebuilt image (`-DSTAGE90_XNU_IOS7_TRAPS=1`) the reverse.  Exit 0 iff the asserted mode's
checks all pass.

KNOWN LOUD FAILURE MODE.  `ios7lab_timebase` reaches `mach_absolute_time` with a `blx`-immediate, so
the linker resolves it directly and the call is the instruction the check reads.  If a link ever puts
`mach_absolute_time` out of the ±32 MiB branch range the linker would insert a veneer, the call's
decoded target would be the veneer rather than the function, and the ON check would FAIL on
"NO call to mach_absolute_time" instead of passing on a call that no longer goes where it says.  That
is the intended direction (`mi4-silence-is-a-reading-only-if-success-is-silent`): the check reports the
call rather than assuming it, and the fix is to read the veneer by value.
"""
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"
TARGET = "locore_fleh_swi"
ABS_TIME = "mach_absolute_time"
EXIT64 = "swi_exit64"
TRAP_TB = "swi_trap_tb"

_LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
# `addr:\tENCODING\tmnemonic\toperands` (objdump pads the encoding column with a trailing space)
_INSN = re.compile(r"^([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)$")


def objdump_disasm(path):
    return subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True,
                          check=True).stdout


def symbols(path):
    """({name: va}, [va of every GLOBAL text symbol]) for the image.

    The second list bounds a handler's region: local labels inside it (`swi_trap_ret`, `swi_exit64`,
    `ios7lab_timebase`, ...) are not boundaries - the next GLOBAL text symbol is.
    """
    out, globals_ = {}, []
    for line in subprocess.run([NM, path], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("T", "t", "D", "d", "B", "b", "R", "r", "W", "w"):
            out[parts[2]] = int(parts[0], 16)
            if parts[1] == "T":
                globals_.append(int(parts[0], 16))
    return out, sorted(globals_)


def bodies(path):
    """{symbol: [(va, word, mnemonic, operands)]} in link order, from the disassembly."""
    funcs, cur = {}, None
    for line in objdump_disasm(path).splitlines():
        m = _LABEL.match(line)
        if m:
            cur = m.group(2)
            funcs.setdefault(cur, [])
            continue
        m = _INSN.match(line)
        if m and cur is not None:
            funcs[cur].append((int(m.group(1), 16), int(m.group(2), 16), m.group(3),
                               m.group(4).split(";")[0].strip()))
    return funcs


# ---- ARM encodings, decoded by value -------------------------------------------------------------
def is_cmn_imm(word, rn):
    """`cmn rN, #imm` - data-processing immediate (bits 27-26 == 00), opcode 1011 (CMN), S == 1."""
    if (word >> 26) & 0x3 != 0:
        return False
    opcode = (word >> 21) & 0xF
    s = (word >> 20) & 1
    return opcode == 0b1011 and s == 1 and ((word >> 16) & 0xF) == rn


def _sign_extend24(imm24):
    return imm24 - (1 << 24) if imm24 & (1 << 23) else imm24


def branch_target(word, va):
    """Absolute target of a B / BL / BLX-immediate, or None if the word is not one.

    B  = cond 1010 imm24 (bits 27-24 = 1010);  BL = cond 1011 imm24 (bits 27-24 = 1011, cond != 1111);
    BLX-immediate = 1111 101H imm24 (bits 31-25 = 1111101).  PC reads as insn_va + 8; the BLX halfword
    bit (H, bit 24) is bit 0 of the offset.  Returns (target, kind) with kind in {"b", "bl", "blx"}.
    """
    cond = (word >> 28) & 0xF
    top = (word >> 24) & 0xF
    if top == 0xA:                                   # B
        off = _sign_extend24(word & 0xFFFFFF) << 2
        return va + 8 + off, "b"
    if top == 0xB and cond != 0xF:                   # BL
        off = _sign_extend24(word & 0xFFFFFF) << 2
        return va + 8 + off, "bl"
    if (word >> 25) & 0x7F == 0b1111101:             # BLX immediate (1111 101H imm24)
        h = (word >> 24) & 1
        off = (_sign_extend24(word & 0xFFFFFF) << 2) | (h << 1)
        return va + 8 + off, "blx"
    return None, None


def find_pair(body, rn, target_vas):
    """VA of a `cmn rN,#imm` immediately followed by a conditional branch to one of `target_vas`.

    The `beq` is any conditional B (cond != 1111) whose target is in `target_vas`.  The pair is what
    binds the compare to the route: the source order of the two instructions is not the signal, their
    adjacency and the branch's absolute target are.
    """
    for i in range(len(body) - 1):
        va, word, _mn, _ops = body[i]
        if not is_cmn_imm(word, rn):
            continue
        tgt, kind = branch_target(body[i + 1][1], body[i + 1][0])
        if kind == "b" and tgt in target_vas:
            return va
    return None


def calls_to(body, target_vas):
    """[(VA, target_VA)] of every bl/blx in `body` whose decoded target is in `target_vas`."""
    out = []
    for va, word, _mn, _ops in body:
        tgt, kind = branch_target(word, va)
        if kind in ("bl", "blx") and tgt in target_vas:
            out.append((va, tgt))
    return out


def branches_to(body, target_vas, kind):
    """[(VA, target_VA)] of every branch of `kind` ("b" or "any") in `body` targeting `target_vas`."""
    out = []
    for va, word, _mn, _ops in body:
        tgt, k = branch_target(word, va)
        if tgt in target_vas and (kind == "any" or k == kind):
            out.append((va, tgt))
    return out


def region_body(funcs, start_va, globals_):
    """All instructions of the region beginning at `start_va`, up to the next GLOBAL text symbol.

    objdump emits a label for every local symbol, so `locore_fleh_swi` and its sub-labels
    (`swi_trap_ret`, `swi_exit64`, `swi_trap_tb`, `ios7lab_timebase`, ...) are separate `funcs`
    entries - ending the body at the next such label would cut the region off after 15 instructions
    and would never see the `-3` route at all.  The region the syscall enters runs to the next
    GLOBAL handler (`locore_fleh_prefabt`), so the pieces in between are concatenated and re-sorted
    (`mi4-linked-code-order-is-not-source-order`).
    """
    nxt = next((v for v in globals_ if v > start_va), None)
    out = []
    for b in funcs.values():
        if b and b[0][0] >= start_va and (nxt is None or b[0][0] < nxt):
            out.extend(b)
    out.sort()
    return out


def main() -> int:
    args, expect, i = [], "on", 1
    argv = sys.argv[1:]
    while i <= len(argv):
        a = argv[i - 1]
        if a == "--expect":
            if i >= len(argv):
                print("check_d13_ios7_traps: --expect needs a value (on|off)", file=sys.stderr)
                return 2
            expect = argv[i]
            i += 1
        elif a.startswith("--expect="):
            expect = a.split("=", 1)[1]
        elif not a.startswith("--"):
            args.append(a)
        i += 1
    if expect not in ("on", "off"):
        print(f"check_d13_ios7_traps: --expect must be on or off, not [{expect}]", file=sys.stderr)
        return 2
    path = args[0] if args else "out/stage90/xnu_arm_entry.elf"
    print(f"check_d13_ios7_traps: {path} (expect {expect})")
    try:
        syms, globals_ = symbols(path)
        funcs = bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP}/{NM} could not read it: {exc}")
        print("check_d13_ios7_traps: FAIL")
        return 2

    if TARGET not in syms:
        print(f"  FAIL {TARGET} is not in the image - wrong image, or the entry image's rename "
              f"step (build_entry.sh) did not run; the tree's traps_lo body is what the -3 syscall "
              f"enters and it is the ONLY body this check may read")
        print("check_d13_ios7_traps: FAIL")
        return 1
    body = region_body(funcs, syms[TARGET], globals_)
    if not body:
        print(f"  FAIL no disassembly for the region at {TARGET}={syms[TARGET]:#010x}")
        print("check_d13_ios7_traps: FAIL")
        return 1
    for name in (ABS_TIME, EXIT64, TRAP_TB):
        if name not in syms:
            print(f"  FAIL symbol {name} is not defined in the image - cannot bind the branch target")
            print("check_d13_ios7_traps: FAIL")
            return 1

    lo, hi = body[0][0], body[-1][0] + 4
    print(f"  body <{TARGET}> = [{lo:#010x}, {hi:#010x}), {len(body)} instructions; "
          f"{ABS_TIME}={syms[ABS_TIME]:#010x} {EXIT64}={syms[EXIT64]:#010x} {TRAP_TB}={syms[TRAP_TB]:#010x}")

    fast = find_pair(body, 12, {syms[TRAP_TB]})                # cmn r12,#3 -> beq swi_trap_tb
    # the route's beq target is ios7lab_timebase, a LOCAL label that labels the CALL INSTRUCTION, so it
    # is the call's own VA - NOT the callee's (`mi4-a-stand-in-can-be-the-right-size-and-the-wrong-value`:
    # the callee's VA is the right region and the wrong number).
    abs_calls = calls_to(body, {syms[ABS_TIME]})
    route = find_pair(body, 11, {va for va, _tgt in abs_calls}) if abs_calls else None
    exit_after = [va for va, _tgt in branches_to(body, {syms[EXIT64]}, "b")
                  if abs_calls and va == abs_calls[0][0] + 4]

    ok = True
    if expect == "off":
        if fast is None:
            print(f"  FAIL (off) no `cmn r12, #3` + `beq {TRAP_TB}` fast path in <{TARGET}> - the "
                  f"pre-fix image takes it at the top of the handler; its absence means the :116 gate "
                  f"was opened (the switch is on, or the edit is not the one being rehearsed)")
            ok = False
        else:
            print(f"  ok   (off) the `cmn r12, #3` / `beq {TRAP_TB}` fast path is present at "
                  f"{fast:#010x} - the pre-fix shape (garbage time for a -3 syscall)")
        if abs_calls:
            print(f"  FAIL (off) <{TARGET}> calls {ABS_TIME} at "
                  f"{', '.join(f'{v:#010x}' for v, _t in abs_calls)} - the pre-fix image has no -3 "
                  f"route to the real clock")
            ok = False
        else:
            print(f"  ok   (off) <{TARGET}> has NO call to {ABS_TIME} - the two positive gates are "
                  f"closed, as the pre-fix tree has them")
    else:  # on
        if fast is not None:
            print(f"  FAIL (on) the `cmn r12, #3` / `beq {TRAP_TB}` fast path is STILL in <{TARGET}> "
                  f"at {fast:#010x} - the :116 gate was not closed, so a -3 syscall returns through "
                  f"`movs pc, lr` without writing r0:r1 (garbage time)")
            ok = False
        else:
            print(f"  ok   (on) no `cmn r12, #3` / `beq {TRAP_TB}` fast path in <{TARGET}> - the :116 "
                  f"gate is closed and the saved-state path is taken")
        if not abs_calls:
            print(f"  FAIL (on) <{TARGET}> has NO call to {ABS_TIME} - the :162/:264 gates are not "
                  f"open, so the -3 fallback has no route to the real 64-bit clock")
            ok = False
        else:
            print(f"  ok   (on) <{TARGET}> calls {ABS_TIME} at "
                  f"{', '.join(f'{v:#010x}' for v, _t in abs_calls)}")
        if route is None:
            print(f"  FAIL (on) no `cmn r11, #3` + `beq <the {ABS_TIME} call>` in <{TARGET}> - the "
                  f":162 route does not reach the :264 call (r11 is the SAVED r12, `mov r11, r12`)")
            ok = False
        else:
            print(f"  ok   (on) `cmn r11, #3` at {route:#010x} branches to the {ABS_TIME} call at "
                  f"{abs_calls[0][0]:#010x} - the saved-state -3 route reaches it")
        if not exit_after:
            print(f"  FAIL (on) the {ABS_TIME} call is not immediately followed by `b {EXIT64}` - "
                  f"without it r1 (the high half) is never written back (`str r1, [r8, #4]`) and "
                  f"userspace gets a 32-bit clock")
            ok = False
        else:
            print(f"  ok   (on) the {ABS_TIME} call at {exit_after[0] - 4:#010x} is followed by "
                  f"`b {EXIT64}` at {exit_after[0]:#010x} - r0:r1 are written back")

    print(f"check_d13_ios7_traps: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
