#!/usr/bin/env python3
"""Check a tree's G1 (iOS 7 shared-region geometry + stack) staging state - draft v2.

Verifies, by value, that the three files named in patch_d13_shreg_geom.py are all in the pre-image
shape or all in the staged shape (and nothing in between), and that the shape's load-bearing pieces
are present: the helper beside `ios7_traps_enabled()`, the 994 gate spelling in both .c files, the
two call sites, and the geometry/stack constants the patch must never touch.  The patcher module is
imported so the strings have ONE definition.

Usage:
  check_d13_shreg_geom.py --tree <xnu-tree-root> [--expect pre|staged]

Exit 0 = the requested state holds; 1 = it does not (with per-file counts); 2 = usage/IO.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import patch_d13_shreg_geom as P  # noqa: E402  (the constants live there, once)

GEOMETRY_KEYS = {
    # file -> (literal, expected count) - the ABI constants, present in BOTH states
    P.XNU_VM_SHARED_REGION: [("base_address = 0x2C000000U;\n", 1),
                             ("size = 0x14000000U;\n", 1),
                             ("IOS7LAB shared region 0x2c000000..0x40000000; "
                              "stack below 0x2be00000\\n", 1)],
    P.XNU_ARM_STATUS: [("0x2BE00000U", 1), ("#define USRSTACK    0x2FE00000", 1)],
}

STAGED_KEYS = {
    P.XNU_ARM_IOS7_COMPAT: [(P.HELPER, 1),
                            ("#if defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM\n"
                             "    return 1;\n#else\n    return ios7_compat_vmabi_enabled();\n"
                             "#endif\n", 1)],
    P.XNU_VM_SHARED_REGION: [(P.VMSR_CALL_S, 1), (P.VMSR_CALL_C, 0),
                             (P.GATE_IF, 1), (P.BARE_IF, 0)],
    P.XNU_ARM_STATUS: [(P.STATUS_CALL_S, 1), (P.STATUS_CALL_C, 0),
                       (P.GATE_IF, 1), (P.BARE_IF, 0)],
}

# v1's inline `#if` wrap must never appear in a .c file (the measured line-shift defect)
V1_SHAPE = "#if defined(STAGE90_XNU_SHREG_GEOM) && STAGE90_XNU_SHREG_GEOM\n"


def main() -> int:
    argv = sys.argv[1:]
    expect = None
    if "--expect" in argv:
        i = argv.index("--expect")
        try:
            expect = argv[i + 1]
        except IndexError:
            print("check_d13_shreg_geom: --expect needs pre|staged", file=sys.stderr)
            return 2
        del argv[i:i + 2]
    if len(argv) != 2 or argv[0] != "--tree":
        print("usage: check_d13_shreg_geom.py --tree <xnu-tree-root> [--expect pre|staged]",
              file=sys.stderr)
        return 2
    if expect not in (None, "pre", "staged"):
        print("check_d13_shreg_geom: --expect must be pre or staged", file=sys.stderr)
        return 2
    root = Path(argv[1])

    fails, verdicts = [], {}
    texts = {}
    for rel in P.FILES:
        path = root / rel
        if not path.is_file():
            print(f"check_d13_shreg_geom: FAIL - {path} does not exist", file=sys.stderr)
            return 2
        texts[rel] = path.read_text()
        state, counts = P.state_of(texts[rel], P.KIND[rel])
        verdicts[rel] = state
        print(f"  {rel}: {state} [{counts}]")

    states = set(verdicts.values())
    if states == {"pre"}:
        tree_state = "pre"
    elif states == {"staged"}:
        tree_state = "staged"
    else:
        tree_state = "mixed/foreign"
    print(f"check_d13_shreg_geom: tree state = {tree_state}")
    if expect and tree_state != expect:
        fails.append(f"tree state {tree_state} != requested {expect}")
    if tree_state == "mixed/foreign":
        fails.append("the three files disagree - a partial or foreign edit")

    for rel, keys in GEOMETRY_KEYS.items():
        for lit, want in keys:
            got = texts[rel].count(lit)
            if got != want:
                fails.append(f"{rel}: constant {lit!r} count {got} (want {want})")

    if tree_state == "staged":
        for rel, keys in STAGED_KEYS.items():
            for lit, want in keys:
                got = texts[rel].count(lit)
                if got != want:
                    fails.append(f"{rel}: staged-shape {lit!r} count {got} (want {want})")
        calls = sum(texts[rel].count(P.HELPER) for rel in P.FILES if rel != P.XNU_ARM_IOS7_COMPAT)
        defs = texts[P.XNU_ARM_IOS7_COMPAT].count("static inline int " + P.HELPER)
        if calls != 2:
            fails.append(f"helper {P.HELPER} called {calls}x in the .c files (want 2)")
        if defs != 1:
            fails.append(f"helper {P.HELPER} defined {defs}x in the header (want 1)")
    elif tree_state == "pre":
        for rel in (P.XNU_VM_SHARED_REGION, P.XNU_ARM_STATUS):
            if P.HELPER in texts[rel]:
                fails.append(f"{rel}: helper name present in the pre-image")
    else:
        fails = fails  # mixed: the per-file counts above are the evidence

    for rel in (P.XNU_VM_SHARED_REGION, P.XNU_ARM_STATUS):
        if V1_SHAPE in texts[rel]:
            fails.append(f"{rel}: v1's inline `#if` wrap is present (the measured line-shift defect)")

    if fails:
        for f in fails:
            print(f"check_d13_shreg_geom: FAIL - {f}", file=sys.stderr)
        return 1
    print(f"check_d13_shreg_geom: PASS - {tree_state} state holds, constants intact, "
          f"no v1 shape")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())