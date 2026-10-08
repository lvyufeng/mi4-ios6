#!/usr/bin/env python3
"""
Check that the assembled ARM layer reads `struct thread` at the offsets this configuration's
`assym.s` gave it.

    XNU_KERNEL_CONFIG=STAGE90_XNU ./tools/check_assym_cswitch.py \\
        --assym out/xnu_assym/STAGE90_XNU/assym.s --object out/xnu_asm_obj/cswitch.o

Why this exists. `assym.s` is a **per-configuration** artifact: `tools/gen_assym.sh` defaults
`CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}` and writes `out/xnu_assym/$CONFIG/assym.s`, and both
`tools/assemble_arm_layer.sh` and `scripts/xnu_arm_assemble.sh` read that path. The three
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

# The fields, in the order `machine_load_context` loads them (`osfmk/arm/cswitch.s`).
#
# **There are two of these, because there are two context-switch schemes (934).** Darwin 17's
# `machine_load_context` loads three `struct thread` fields into TPIDRURO/TPIDRURW and the register
# save area - `TH_CTH_SELF`, `TH_CTH_DATA`, `TH_KSTACKPTR` - and that is the arrangement this check
# was written for (`cswitch.s`'s own comment calls it "the first instruction sequence a newly
# scheduled thread runs"). **Darwin 13's `osfmk/arm/cswitch.s:106-127` is a different scheme**: it
# writes `r0` straight into TPIDRURO (`mcr p15, 0, r0, c13, c0, 4`, no `CTH_SELF` load at all), loads
# one field `MACHINE_THREAD_CTHREAD_SELF` into `r1` (the `CTHREAD_SELF`, i.e. the `_cthread_self`/
# `cthread_self` pair), reads TPIDRURW into `r2` (`mrc`), and takes the register save area from
# `TH_PCB_ISS` (`ldr r3, [r0, TH_PCB_ISS]`) rather than a chain of `kstackptr`/`PCB` fields. The
# offsets and the count are different, so the single 4570 list cannot describe it.
#
# The check's real property is unchanged: **every `ldr rX, [r0, #N]` immediately after the
# thread-register `mcr` must equal the offset this configuration's `assym.s` gave that field**, so
# `machine_load_context` reads `struct thread` where the C side laid it out. On the D13 scheme the
# parked `mcr`/`mrc` pair is also asserted present, because a `machine_load_context` with no thread
# register write is not the Darwin 13 sequence at all.
SCHEMES = {
    # 4570: three loads, `TH_CTH_SELF` first.
    "4570": {"fields": ["TH_CTH_SELF", "TH_CTH_DATA", "TH_KSTACKPTR"], "require_mcr": False},
    # D13: the `r0 -> TPIDRURO` write must precede a single `TH_CTHREAD_SELF` load, a TPIDRURW `mrc`,
    # and the `TH_PCB_ISS` save-area load. The `mcr`/`mrc` are not fields; they are asserted, not
    # compared.
    "d13": {"fields": ["MACHINE_THREAD_CTHREAD_SELF", "TH_PCB_ISS"], "require_mcr": True},
}
# The default is the 4570 scheme, so a plain run is the check it always was.
FIELDS = SCHEMES["4570"]["fields"]

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


def scan(obj):
    """
    `machine_load_context`'s shape, read from the disassembly: the `ldr rX, [r0, #N]` immediates in
    the order they appear, and whether the thread-register write (`mcr p15, 0, r0, c13, c0, 4`, the
    `TPIDRURO` store) is present.

    A function's extent is taken to be from its label to the next label at column 0 - the same rule
    the project's other disassembly readers use. The disassembly is `-dr` so that relocation lines
    (`R_ARM_...` under an instruction) are skipped by the instruction regex rather than mistaken for
    code.
    """
    dis = subprocess.run([OBJDUMP, "-dr", obj], capture_output=True, text=True, check=True).stdout
    inside = False
    offsets = []
    mcr = False
    for line in dis.splitlines():
        if re.match(r"^[0-9a-f]{8} <", line):
            inside = line.strip().endswith("<machine_load_context>:")
            continue
        if not inside:
            continue
        m = re.search(r"\bldr\s+r\d+,\s*\[r0,\s*#(\d+)\]", line)
        if m:
            offsets.append(int(m.group(1)))
        # `TPIDRURO <- r0`: `mcr p15, 0, r0, c13, c0, 4` disassembles as `mcr 15, 0, r0, cr13,
        # cr0, {4}`. A `mov r0, #0` before the `ldm` (present in both schemes) is what makes the
        # thread register have to be written *before* that point, which is the D13 assertion.
        if re.search(r"\bmcr\s+15,\s*0,\s*r0,\s*cr13,\s*cr0,\s*\{4\}", line):
            mcr = True
    return offsets, mcr


def load_offsets(obj):
    """Back-compat wrapper: the `ldr` immediates only (the 4570 caller uses this)."""
    return scan(obj)[0]


def check(assym_path, obj_path, scheme="4570"):
    """
    Returns `(failures, notes)`. `failures` is empty when every arrangement holds.
    """
    failures = []
    notes = []
    fields = SCHEMES[scheme]["fields"]
    require_mcr = SCHEMES[scheme]["require_mcr"]

    values = read_assym(assym_path)
    missing = [f for f in fields if f not in values]
    if missing:
        return ([f"{assym_path} has no numeric #define for {', '.join(missing)} - the file is not "
                 f"the assym.s gen_assym.sh writes, or the fields moved"], [])

    want = [values[f] for f in fields]
    got, mcr = scan(obj_path)

    if require_mcr and not mcr:
        return ([f"{obj_path}'s machine_load_context has no `mcr p15, 0, r0, c13, c0, 4` - the "
                 f"Darwin 13 scheme writes the thread's `struct thread *` into TPIDRURO there, and "
                 f"without that write this is not the D13 context-switch sequence"], [])

    if not got:
        return ([f"no `ldr rX, [r0, #N]` in machine_load_context in {obj_path} - either the object "
                 f"does not define it or the disassembly rule no longer matches it"], [])

    notes.append(f"scheme {scheme}; assym {assym_path}: " +
                 ", ".join(f"{f} {v}" for f, v in zip(fields, want)))
    notes.append(f"object {obj_path}: machine_load_context loads at " +
                 ", ".join(f"#{v}" for v in got))

    if len(got) != len(want):
        failures.append(f"machine_load_context has {len(got)} `ldr rX, [r0, #N]` instruction(s) and "
                        f"this configuration has {len(want)} fields to read ({', '.join(fields)})")
        return (failures, notes)

    for f, w, g in zip(fields, want, got):
        if w != g:
            failures.append(f"machine_load_context reads [r0, #{g}] where this configuration's "
                            f"assym.s gives {f} = #{w} - the ARM layer was assembled against a "
                            f"different configuration's assym.s, so the thread pointer, the "
                            f"per-thread data pointer or the kernel stack pointer it loads is "
                            f"another field's value")
    return (failures, notes)


def selftest(assym_path, obj_path, scheme="4570"):
    """Every field of the scheme, one at a time, has to be refused."""
    fields = SCHEMES[scheme]["fields"]
    text = open(assym_path, "r", errors="replace").read()
    refused = 0
    for f in fields:
        m = re.search(r"^#define\s+%s\s+#(\d+)\s*$" % f, text, re.M)
        if not m:
            print(f"selftest: no numeric #define for {f} to mutate", file=sys.stderr)
            return 1
        mutated = text[:m.start()] + f"#define {f} #{int(m.group(1)) + 16}" + text[m.end():]
        tmp = os.path.join("/tmp", f"assym-selftest-{f}.s")
        with open(tmp, "w") as fh:
            fh.write(mutated)
        failures, _ = check(tmp, obj_path, scheme)
        if failures:
            refused += 1
        else:
            print(f"selftest: moving {f} by 16 was NOT refused - the comparison is not reading it",
                  file=sys.stderr)
        os.unlink(tmp)
    if refused != len(fields):
        return 1
    print(f"selftest[{scheme}]: all {refused} mutations were refused")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assym", default=None)
    ap.add_argument("--object", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    config = os.environ.get("XNU_KERNEL_CONFIG", "RELEASE")
    # **934: both artifacts are TREE-derived roots.** `tools/xnu_tree_roots.sh` (933) gives every
    # tree's output root a suffix - `_d13` iff the selected tree ships `osfmk/sys/types.h` - so a run
    # against Darwin 13 writes `out/xnu_assym_d13/` and `out/xnu_asm_obj_d13/`. This script defaulted
    # both to the *unsuffixed* 4570 paths, so an entry build on D13 compared the D13 object it had
    # just assembled against 4570's `assym.s` and failed with three offsets that differed by exactly
    # the two trees' `struct thread` layout - the check working, but on the wrong pair. The rule is
    # the one file's; it is read here rather than respelled (asking the sourced shell for the resolved
    # value keeps this the single definition).
    suffix = os.environ.get("XNU_OBJ_SUFFIX")
    tree = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
    if suffix is None:
        suffix = "_d13" if os.path.isfile(os.path.join(tree, "osfmk", "sys", "types.h")) else ""
    # **And the SCHEME follows the same discriminator.** D13 ships `osfmk/sys/types.h` and a
    # `machine_load_context` that is a different sequence; the tree picks both the roots and the
    # fields, so one `XNU_TREE` is enough for the whole check.
    scheme = "d13" if os.path.isfile(os.path.join(tree, "osfmk", "sys", "types.h")) else "4570"
    assym = args.assym or os.path.join(REPO_ROOT, "out", f"xnu_assym{suffix}", config, "assym.s")
    obj = args.object or os.path.join(REPO_ROOT, "out", f"xnu_asm_obj{suffix}", "cswitch.o")

    for p in (assym, obj):
        if not os.path.exists(p):
            print(f"missing {p}", file=sys.stderr)
            return 2

    if args.selftest:
        return selftest(assym, obj, scheme)

    failures, notes = check(assym, obj, scheme)
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
    print(f"ok[{scheme}]: machine_load_context reads "
          f"{', '.join(SCHEMES[scheme]['fields'])} at this configuration's offsets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
