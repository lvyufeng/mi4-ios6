#!/usr/bin/env python3
"""
Check that the assembled ARM layer reads `struct thread` at the offsets this configuration's
`assym.s` gave it.

    XNU_KERNEL_CONFIG=STAGE90_XNU ./tools/check_assym_cswitch.py \\
        --assym out/xnu_assym/STAGE90_XNU/assym.s --object out/xnu_asm_obj/cswitch.o

Why this exists. `assym.s` is a **per-configuration** artifact: `tools/gen_assym.sh` defaults
`CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}` and writes `out/xnu_assym/$CONFIG/assym.s`, and both
`tools/assemble_arm_layer.sh` and `stages/stage90/xnu_arm_assemble.sh` read that path. The three
generator steps - the option headers, `gen_assym.sh`, `assemble_arm_layer.sh` - were hand-run
prerequisites, so a configuration change could leave the ARM layer assembled against *RELEASE*
offsets while every C object was compiled with the new configuration's.

That is not a subtle mismatch. `osfmk/arm/cswitch.s`'s `machine_load_context` is the first
instruction sequence a newly scheduled thread runs, and it is three literal offsets into the
`thread_t` it was handed:

    ldr r1, [r0, #TH_CTH_SELF]      ; -> TPIDRURO
    ldr r1, [r0, #TH_CTH_DATA]      ; -> TPIDRURW
    ldr r3, [r0, #TH_KSTACKPTR]     ; the register save area, then `ldm r3!, {r4-r14}`

With RELEASE's offsets against a `DEVELOPMENT`-sized `struct thread`, the first load reads
`TH_KSTACKPTR`'s field and writes it to TPIDRURO, the third reads whatever the new layout put
there, and the `ldm` walks off the end of the stack into unmapped memory: a data abort on **every**
thread switch, in the kernel's own scheduler, which is experiment 468's device run - five
`sleh_abort` panics with `pc = machine_load_context+0x30` (the `ldm`), `lr = slave_main`, and an OS
console that stops twelve lines in.

So the check is over the *linked artifact* and not over the build's command line, and it compares
the two definitions of one number: the `#define`s the assembler was given, and the immediates that
came out the other side. It is the "one value, two definitions" rule with both sides measured.

The offsets are read in **source order** rather than as a set, because a set cannot see two fields
swapped - and a swap is exactly what a wrong `assym.s` produces when two neighbouring fields are the
same size. That is not a hypothetical: 468's mismatch read `[r0,#1464]` where the configuration said
`TH_CTH_SELF 1496`, i.e. the RELEASE offset of `TH_KSTACKPTR`, and a set comparison of the three
numbers would have accepted a permutation of them.

`--selftest` mutates each of the three values in the `assym` text in turn and requires the check to
refuse all three, so that "the check passes" is a statement about the comparison and not about the
parsing having found nothing.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

# The three fields, in the order `machine_load_context` loads them (`osfmk/arm/cswitch.s`).
FIELDS = ["TH_CTH_SELF", "TH_CTH_DATA", "TH_KSTACKPTR"]

OBJDUMP = os.environ.get("ARM_OBJDUMP", "arm-none-eabi-objdump")

# `#define NAME #123` - `gen_assym.sh` writes the `#` because Apple's `genassym.s` is fed to `sed`
# and the marker is what distinguishes a numeric define from a text one.
DEFINE_RE = re.compile(r"^#define\s+(\w+)\s+#(\d+)\s*$")


def read_assym(path):
    """The `#define`s, as `{name: int}` over the numeric ones only."""
    values = {}
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m = DEFINE_RE.match(line.rstrip("\n"))
            if m:
                values[m.group(1)] = int(m.group(2))
    return values


def load_offsets(obj):
    """
    `machine_load_context`'s `ldr rX, [r0, #N]` immediates, in the order they appear in the
    disassembly.

    A function's extent is taken to be from its label to the next label at column 0 - the same rule
    the project's other disassembly readers use. The disassembly is `-dr` so that relocation lines
    (`R_ARM_...` under an instruction) are skipped by the instruction regex rather than mistaken for
    code.
    """
    dis = subprocess.run([OBJDUMP, "-dr", obj], capture_output=True, text=True, check=True).stdout
    inside = False
    out = []
    for line in dis.splitlines():
        if re.match(r"^[0-9a-f]{8} <", line):
            inside = line.strip().endswith("<machine_load_context>:")
            continue
        if not inside:
            continue
        m = re.search(r"\bldr\s+r\d+,\s*\[r0,\s*#(\d+)\]", line)
        if m:
            out.append(int(m.group(1)))
    return out


def check(assym_path, obj_path):
    """
    Returns `(failures, notes)`. `failures` is empty when every arrangement holds.
    """
    failures = []
    notes = []

    values = read_assym(assym_path)
    missing = [f for f in FIELDS if f not in values]
    if missing:
        return ([f"{assym_path} has no numeric #define for {', '.join(missing)} - the file is not "
                 f"the assym.s gen_assym.sh writes, or the fields moved"], [])

    want = [values[f] for f in FIELDS]
    got = load_offsets(obj_path)

    if not got:
        return ([f"no `ldr rX, [r0, #N]` in machine_load_context in {obj_path} - either the object "
                 f"does not define it or the disassembly rule no longer matches it"], [])

    notes.append(f"assym {assym_path}: " + ", ".join(f"{f} {v}" for f, v in zip(FIELDS, want)))
    notes.append(f"object {obj_path}: machine_load_context loads at " +
                 ", ".join(f"#{v}" for v in got))

    if len(got) != len(want):
        failures.append(f"machine_load_context has {len(got)} `ldr rX, [r0, #N]` instruction(s) and "
                        f"this configuration has {len(want)} fields to read ({', '.join(FIELDS)})")
        return (failures, notes)

    for f, w, g in zip(FIELDS, want, got):
        if w != g:
            failures.append(f"machine_load_context reads [r0, #{g}] where this configuration's "
                            f"assym.s gives {f} = #{w} - the ARM layer was assembled against a "
                            f"different configuration's assym.s, so the thread pointer, the "
                            f"per-thread data pointer or the kernel stack pointer it loads is "
                            f"another field's value")
    return (failures, notes)


def selftest(assym_path, obj_path):
    """Every one of the three values, one at a time, has to be refused."""
    text = open(assym_path, "r", errors="replace").read()
    refused = 0
    for f in FIELDS:
        m = re.search(r"^#define\s+%s\s+#(\d+)\s*$" % f, text, re.M)
        if not m:
            print(f"selftest: no numeric #define for {f} to mutate", file=sys.stderr)
            return 1
        mutated = text[:m.start()] + f"#define {f} #{int(m.group(1)) + 16}" + text[m.end():]
        tmp = os.path.join("/tmp", f"assym-selftest-{f}.s")
        with open(tmp, "w") as fh:
            fh.write(mutated)
        failures, _ = check(tmp, obj_path)
        if failures:
            refused += 1
        else:
            print(f"selftest: moving {f} by 16 was NOT refused - the comparison is not reading it",
                  file=sys.stderr)
        os.unlink(tmp)
    if refused != len(FIELDS):
        return 1
    print(f"selftest: all {refused} mutations were refused")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assym", default=None)
    ap.add_argument("--object", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    config = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
    assym = args.assym or os.path.join(REPO_ROOT, "out", "xnu_assym", config, "assym.s")
    obj = args.object or os.path.join(REPO_ROOT, "out", "xnu_asm_obj", "cswitch.o")

    for p in (assym, obj):
        if not os.path.exists(p):
            print(f"missing {p}", file=sys.stderr)
            return 2

    if args.selftest:
        return selftest(assym, obj)

    failures, notes = check(assym, obj)
    for n in notes:
        print(f"    {n}")
    if failures:
        print("FAIL: the ARM layer and this configuration's assym.s disagree:", file=sys.stderr)
        for f in failures:
            print(f"      {f}", file=sys.stderr)
        print("      Re-run, in this order and with the same XNU_KERNEL_CONFIG and XNU_MASTER_LOCAL:",
              file=sys.stderr)
        print(f"        XNU_KERNEL_CONFIG={config} ./tools/gen_assym.sh", file=sys.stderr)
        print(f"        XNU_KERNEL_CONFIG={config} ./tools/assemble_arm_layer.sh", file=sys.stderr)
        return 1
    print(f"ok: machine_load_context reads {', '.join(FIELDS)} at this configuration's offsets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
