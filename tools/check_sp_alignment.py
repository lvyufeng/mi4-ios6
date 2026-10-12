#!/usr/bin/env python3
"""Assert BY VALUE that the linked image hands C 8-byte-aligned stacks at the AST gates (990).

Reads `arm-none-eabi-objdump -d` of a linked image (default `out/stage90/xnu_arm_entry.elf`) and checks
the three properties 989's death named, from the instructions and not from any source comment
(`mi4-a-claim-in-a-comment-is-not-a-check`):

  1. Every `bl`/`blx` to `ast_taken` happens with the cumulative stack delta - counted over `push`/
     `stmfd sp!`, `pop`/`ldmfd sp!`, `vpush`/`vpop`, `sub sp`/`add sp`/`bic sp,sp,#7` - equal to
     0 (mod 8) from its function's label. 989: `ml_set_interrupts_enabled`'s `astloop` saved 9 words
     (`stmfd sp!,{r0-r7,lr}` = 36 = 4 mod 8) before the call, so `ast_taken` ran at 4 and the whole AST
     subtree (bsd_ast -> bsdinit_task -> load_init_program -> execve -> parse_machfile) ran at 4 until
     `parse_machfile+0x60c`'s `vld1.32 {d18}, [r1 :64]` faulted at sp = 0xcfdbba7c. A function that
     makes sp indeterminate (an sp move/load this walker does not model) is a REFUSAL, not a pass.
  2. `machine_stack_attach` and `machine_stack_handoff` each carry at least one 8-byte rounding of the
     saved-state value (`bic rX, rX, #7` or an equivalent mask). That mask is the compiled footprint of
     `& ~7u`: `iss = stack_top - 68` is 4 (mod 8), and `Call_continuation` loads the continuation's sp
     straight from it.
  3. `Call_continuation` still takes sp from the thread's saved-state POINTER (`ldr sp, [rX, #576]`,
     `TH_PCB_ISS` = 576 on this tree). Together with 2 that is the full chain: the field 2 aligns is
     the field 3 loads.

Rehearsal: run against the 989 image (`armed-d13-2e0eebf5`) it must FAIL on the astloop site
(delta -36 = 4 mod 8); against the 990 image it must pass all three. Exit 0 iff all pass.
"""
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
AST = "ast_taken"
STACK_FNS = ("machine_stack_attach", "machine_stack_handoff")

_LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^[0-9a-f]+:\s+[0-9a-f ]+\t([a-z0-9.]+)\s*(.*)$")


def reg_bytes(regs: str) -> tuple[int, bool]:
    """Bytes pushed by `{...}`. Returns (bytes, ok); ok=False for a form this walker does not model
    (a `^`-writeback continuation load, a double-register range in single-register syntax, ...).
    Named ARM registers (`lr`, `pc`, `ip`, ...) have no digit and are all one word."""
    NAMED = {"fp": 11, "ip": 12, "sp": 13, "lr": 14, "pc": 15}
    total = 0
    body = regs.strip().strip("{}^").strip()
    if not body:
        return 0, True
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.fullmatch(r"([a-z]+)(\d+)-([a-z]+)(\d+)", part)
        if m:
            p1, n1, p2, n2 = m.group(1), int(m.group(2)), m.group(3), int(m.group(4))
            if p1 != p2:
                return 0, False
            total += (abs(n2 - n1) + 1) * (8 if p1 == "d" else 4)
            continue
        m = re.fullmatch(r"([a-z]+)(\d+)", part)
        if m:
            total += 8 if m.group(1) == "d" else 4
            continue
        if part in NAMED or part == "sl" or part == "sb":
            total += 4
            continue
        return 0, False
    return total, True


def scan(path: str):
    """Yield (func, addr, mnemonic, operands) for every instruction, tracking the function label."""
    out = subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True, check=True).stdout
    func = None
    for line in out.splitlines():
        m = _LABEL.match(line)
        if m:
            # `.LBB…`-style local labels sit INSIDE a function: they must not reset the walk (a reset
            # at the wrong label is a false pass). Only real (non-dot) labels start a function.
            if not m.group(2).startswith("."):
                func = m.group(2)
            continue
        m = _INSN.match(line)
        if m and func is not None:
            yield func, line.split(":")[0], m.group(1), m.group(2)
        elif line and not line.startswith(" ") and not line.startswith("\t") and ":" not in line:
            func = None  # disassembly header / section banner
    return


_COND = re.compile(r"^(push|pop|stmfd|ldmfd|vpush|vpop)([a-z]{2})?$")


def ast_sites(path: str) -> tuple[list[str], bool]:
    """Walk each function linearly and report the cumulative stack delta at every ast_taken call.
    Returns (report_lines, all_ok)."""
    delta = 0
    unresolved = False
    prev_func = None
    lines = []
    ok = True
    for func, addr, mn, ops in scan(path):
        if func != prev_func:  # each function's walk starts from its own label: delta = 0 at entry
            prev_func = func
            delta = 0
            unresolved = False
        if mn in ("bl", "blx") and f"<{AST}>" in ops:
            if unresolved:
                lines.append(f"  FAIL {func}+{addr}: sp is indeterminate before the {AST} call")
                ok = False
            elif delta % 8 != 0:
                lines.append(f"  FAIL {func}+{addr}: cumulative stack delta {delta} = {delta % 8} "
                             f"(mod 8) at bl {AST} - the C call starts at a misaligned sp")
                ok = False
            else:
                lines.append(f"  ok   {func}+{addr}: delta {delta} = 0 (mod 8) at bl {AST}")
            continue
        m = _COND.match(mn)
        if m:
            if m.group(2):  # conditional form - path-dependent, not modelled
                unresolved = True
                continue
            body = ops
            if mn in ("stmfd", "ldmfd", "vpush", "vpop") and body.lower().startswith("sp!"):
                body = body.split(",", 1)[1] if "," in body else ""
            n, good = reg_bytes(body)
            if not good:
                unresolved = True
                continue
            delta += n if mn in ("pop", "ldmfd", "vpop") else -n
            continue
        if mn.startswith(("sub", "add")) and ops.startswith("sp, sp,"):
            imm = re.search(r"#(\d+)", ops)
            if not imm:
                unresolved = True
                continue
            delta += int(imm.group(1)) if mn.startswith("add") else -int(imm.group(1))
            continue
        if mn.startswith("bic") and ops.startswith("sp, sp,") and "#7" in ops:
            delta = 0  # an explicit realignment: from here sp is 8-aligned
            continue
        if re.match(r"^(mov|ldr|ldm|popx)[a-z]*\s+sp", ops) or \
           re.match(r"(add|sub|bic|and|orr)[a-z]*\s+sp,", ops):
            unresolved = True
            continue
    return lines, ok


def function_body(path: str, name: str) -> list[str]:
    body, grab = [], False
    out = subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        m = _LABEL.match(line)
        if m:
            grab = m.group(2) == name
            continue
        if grab:
            body.append(line)
    return body


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "out/stage90/xnu_arm_entry.elf"
    ok = True

    print(f"check_sp_alignment: {path}")
    sites, sites_ok = ast_sites(path)
    if not sites:
        print("  FAIL no ast_taken call found at all - wrong image?")
        ok = False
    for line in sites:
        print(line)
    ok &= sites_ok

    for fn in STACK_FNS:
        body = function_body(path, fn)
        if not body:
            print(f"  FAIL {fn} not in the image")
            ok = False
            continue
        masks = [ln for ln in body
                 if re.search(r"\tbic[a-z]*\s+r\d+, r\d+, #7\b", ln)
                 or re.search(r"\tand[a-z]*\s+r\d+, r\d+, #(4294967288|0xfffffff8)\b", ln)]
        if masks:
            print(f"  ok   {fn}: {len(masks)} 8-byte rounding(s) of the saved-state value")
        else:
            print(f"  FAIL {fn}: no `bic rX, rX, #7` - the & ~7u rounding this switch adds is not "
                  f"compiled in (stale object, or the switch did not reach the pool compile)")
            ok = False

    body = function_body(path, "Call_continuation")
    if not body:
        print("  FAIL Call_continuation not in the image")
        ok = False
    elif any(re.search(r"\tldr\s+sp, \[(?:r\d+|ip), #(576|0x240)\]", ln) for ln in body):
        print("  ok   Call_continuation: sp := [thread + #576] (TH_PCB_ISS - the field "
              "machine_stack_attach itself aligns)")
    else:
        print("  FAIL Call_continuation does not load sp from TH_PCB_ISS (#576) - the continuation "
              "sp no longer comes from the field the stack edit aligns, so the edit cannot reach it")
        ok = False

    print(f"check_sp_alignment: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())