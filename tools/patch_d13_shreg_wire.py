#!/usr/bin/env python3
"""Open the tree's OWN iOS 7 shared-region wire on CANCRO - 997's G3, the 32-byte mapping struct.

WHAT IS WRONG (measured; docs/reference/d13-shreg-frontier.md 2).  iOS 7's dyld builds the
`struct shared_file_mapping_np` array for syscall 438 with a **32-byte stride** (dyld 0x2be057ba is
`add.w r0, r4, r3, lsl #5`; the copy loop is `add.w r8, r8, #32`; initProt is read at `[r8,#28]`),
while this kernel's `shared_file_mapping_np` is **20 bytes** - `mach_vm_address_t/_offset_t/_size_t` are
`uint32_t` here (`osfmk/mach/arm/vm_types.h:107-113`) and `vm_prot_t` is `int`, so it is `{4,4,4,4,4}`
(`osfmk/mach/shared_region.h:108-114`).  `shared_region_copyin_mappings` therefore computes
`count * 5 << 2` (measured in the image at 0x8041fdf4/0x8041fdf8) and copies 3x20 = 60 of the 96 bytes
dyld wrote - no EFAULT, it simply MISREADS: m[0].size becomes 0, `vm_map_round_page(0)` reaches
`vm_map_enter_mem_object`'s `initial_size == 0` check (`osfmk/vm/vm_map.c:2344`) and 438 returns
EINVAL(22) on the first mapping, before any slide work.  Same defect family as 994's trap wire.

THE FIX IS ALREADY IN THE TREE - TWO GATES DEEP.  `bsd/vm/vm_unix.c` carries the 32-byte branch
(`vm_unix.c:1190-1220`, with the per-mapping `IOS7LAB cache mapping ...` printf at :1214) but it is
inside `#if IOS7_COMPAT_PLATFORM`, which `osfmk/arm/ios7_compat.h:5` defines as
`(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)` = **0 on CANCRO**; and even with that region
compiled, the runtime selector `ios7lab_cache_abi()` -> `ios7_compat_vmabi_enabled()` is
`#else return 0;` on CANCRO (`ios7_compat.h:18-20`), so the branch would stay dead and clang would
delete it.  **Both layers must open**, which is why this patcher makes TWO edits per the 994 pattern:

  (A) each of the three `#if IOS7_COMPAT_PLATFORM` LINES in `vm_unix.c` gains
      `|| (defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE)` - the exact spelling measured
      in `osfmk/ipc/mach_kernelrpc.c:39` for STAGE90_XNU_IOS7_TRAPS.  The MACRO itself is NOT touched:
      13 files reference `IOS7_COMPAT_PLATFORM` and widening it would wake exec/fork/ipc behaviour
      wholesale (the 994 "do not widen the macro" lesson).
  (B) the local helper's body is gated to `return 1;` under the same switch, with the original
      `return ios7_compat_vmabi_enabled();` as the `#else` half (`ios7_compat.h:14` returns `1` the
      same way).

`vm_unix.c`'s whole `IOS7_COMPAT_PLATFORM` footprint is exactly those three regions (measured: three
`#if` sites, the helper, and its two call sites - nothing else in the file), so (A) opens exactly:
:101 the helper; :1141-1146 294's 8-byte copyout; :1190-1220 the 32 B branch.  That is deliberately the
WHOLE shared-cache wire family, not just 438's half: on the reference platform
(`BOARD_CONFIG_QSD8250_LEO`) `ios7_compat_vmabi_enabled()` returns 1 unconditionally, so BOTH halves
were live upstream, and 994 fixed the whole trap family at once for the same reason.  The 294 half is
measured-safe (doc 3): dyld allocates 8 bytes and zeroes both words, reads only the low word, and the
8-byte value's high word is 0 (`uint64_t address64 = start_address;`, a 32-bit value zero-extended),
so the caller-visible bytes and the return code are identical either way.

OFF-IDENTITY.  `STAGE90_XNU_SHREG_WIRE` undefined: `#if IOS7_COMPAT_PLATFORM || (0 && ...)` is the
preprocessor's original condition and the helper's body is the original `return
ios7_compat_vmabi_enabled();`, so the compiled translation unit is unchanged; `vm_unix.c` has no
`__LINE__`/`__FILE__`/`ASSERT`/`VERIFY(` use (measured), so the +3 gate lines cannot surface as a
line-number constant.  The FILE differs only by the gate text, which is why `--verify-off` proves the
strip round trip rather than a byte comparison.

THE 997 PRE-IMAGE is the 996 probe-staged file (sha256 516df2327cd113c3d916ef0346ef3d31dd5310f194d0c5
3319bf8fd9839c14f6).  This patcher's anchors do not intersect the probe's seven regions (the probe
touches check_np's entry/exit and 438/_shared_region_map; this one touches the helper and the two
`#if` lines), so it applies cleanly on top, and `--verify-off` proves it is the same file minus its
two edits.  Marker-based and idempotent: a second run says "nothing to do", a partial or foreign edit
is REFUSED (exit 1) rather than repaired.

NO tree edits happen here: the patcher edits the `<xnu-tree-root>/bsd/vm/vm_unix.c` it is given, which
for the rehearsal is a sandbox copy.

Usage:
  patch_d13_shreg_wire.py <xnu-tree-root>              stage (or report nothing to do)
  patch_d13_shreg_wire.py <xnu-tree-root> --dry-run    plan + unified diff, write nothing
  patch_d13_shreg_wire.py <xnu-tree-root> --verify-off round-trip proof against the pre-image
"""
import difflib
import hashlib
import sys
from pathlib import Path

XNU_VM_UNIX = "bsd/vm/vm_unix.c"

# --- edit (A): the three gate LINES, 994's exact spelling (mach_kernelrpc.c:39) --------------------
BARE_IF = "#if IOS7_COMPAT_PLATFORM\n"
GATE_IF = ("#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_SHREG_WIRE) "
           "&& STAGE90_XNU_SHREG_WIRE)\n")
SITES_A = 3

# --- edit (B): the local selector's body (vm_unix.c:102-106) ----------------------------------------
HELPER_RET = "    return ios7_compat_vmabi_enabled();\n"
BODY_OPEN = ("#if defined(STAGE90_XNU_SHREG_WIRE) && STAGE90_XNU_SHREG_WIRE\n"
             "    return 1;\n"
             "#else\n")
BODY_CLOSE = "#endif\n"
STAGED_BODY = BODY_OPEN + HELPER_RET + BODY_CLOSE
SITES_B = 1

SWITCH = "STAGE90_XNU_SHREG_WIRE"


class Refusal(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code
        self.msg = msg


def sha256(text):
    return hashlib.sha256(text.encode()).hexdigest()


def plan(text):
    """(staged_text, actions) for the unstaged state, or Refusal naming the exact counts.

    The THREE states the file can be in: unstaged (3 bare `#if`s, 0 gates, the helper's original
    return), staged (3 gated `#if`s, 1 body gate), anything else = a partial or foreign edit.
    """
    n_bare, n_gate = text.count(BARE_IF), text.count(GATE_IF)
    n_ret, n_body = text.count(HELPER_RET), text.count(BODY_OPEN)
    if n_bare == SITES_A and n_gate == 0 and n_ret == SITES_B and n_body == 0:
        out = text.replace(BARE_IF, GATE_IF)
        # The helper's return is unique (measured: 1 occurrence in the file); the replace() would
        # silently do nothing if it were not, so the count is checked before and after.
        out = out.replace(HELPER_RET, STAGED_BODY)
        actions = [f"{XNU_VM_UNIX}: {SITES_A} gate line(s) -> the 994 spelling",
                   f"{XNU_VM_UNIX}: the ios7lab_cache_abi() body gated to return 1"]
        if out.count(GATE_IF) != SITES_A or out.count(BODY_OPEN) != SITES_B \
                or out.count(BARE_IF) != 0 or out.count(STAGED_BODY) != SITES_B:
            raise Refusal(6, "the plan did not land cleanly: "
                             f"gate={out.count(GATE_IF)} body={out.count(BODY_OPEN)} "
                             f"bare={out.count(BARE_IF)} - refusing rather than writing it")
        return out, actions
    if n_bare == 0 and n_gate == SITES_A and n_ret == SITES_B and n_body == SITES_B:
        raise Refusal(0, "already staged")           # code 0: the nothing-to-do signal
    raise Refusal(6, "the file is neither the pre-image nor this patcher's output: "
                     f"{n_bare} bare `#if IOS7_COMPAT_PLATFORM` (want {SITES_A}), "
                     f"{n_gate} gated (want 0 or {SITES_A}), "
                     f"{n_ret} helper return(s) (want {SITES_B}), "
                     f"{n_body} body gate(s) (want 0 or {SITES_B}) - a partial or foreign edit, "
                     f"refusing rather than repairing it")


def strip_edits(staged):
    """The exact inverse of `plan` - used to prove the round trip, never written by default."""
    if staged.count(GATE_IF) != SITES_A or staged.count(STAGED_BODY) != SITES_B:
        raise Refusal(6, "strip: the file is not this patcher's output "
                         f"(gate={staged.count(GATE_IF)}, body={staged.count(STAGED_BODY)})")
    return staged.replace(STAGED_BODY, HELPER_RET).replace(GATE_IF, BARE_IF)


def verify_off(pre, staged):
    """The OFF/round-trip proof: strip(staged) == pre AND re-plan(pre) == staged."""
    try:
        back = strip_edits(staged)
    except Refusal as exc:
        return False, exc.msg
    if back != pre:
        return False, "removing the edits does not reproduce the pre-image byte for byte"
    again, _acts = plan(pre)
    if again != staged:
        return False, "re-planning the pre-image does not reproduce the staged text"
    return True, (f"round trip holds: strip(staged) == pre-image (sha256 {sha256(pre)}), "
                  f"re-plan(pre) == staged (sha256 {sha256(staged)})")


def diff_text(pre, staged):
    return "".join(difflib.unified_diff(pre.splitlines(keepends=True),
                                        staged.splitlines(keepends=True),
                                        fromfile="a/" + XNU_VM_UNIX, tofile="b/" + XNU_VM_UNIX,
                                        n=3))


def main() -> int:
    argv = sys.argv[1:]
    dry_run = "--dry-run" in argv
    verify = "--verify-off" in argv
    argv = [a for a in argv if not a.startswith("--")]
    if len(argv) != 1:
        print("usage: patch_d13_shreg_wire.py <xnu-tree-root> [--dry-run] [--verify-off]",
              file=sys.stderr)
        return 2
    root = Path(argv[0])
    path = root / XNU_VM_UNIX
    if not path.is_file():
        print(f"patch_d13_shreg_wire: {path} does not exist (is {root} an xnu tree root?)",
              file=sys.stderr)
        return 2
    pre = path.read_text()
    try:
        staged, actions = plan(pre)
        already = False
    except Refusal as exc:
        if exc.code != 0:
            print(f"patch_d13_shreg_wire: REFUSING: {exc.msg}", file=sys.stderr)
            return 1
        staged, actions, already = pre, [], True

    if verify:
        # --verify-off must work in BOTH states: in the already-staged state the input IS the staged
        # text, so the pre-image is reconstructed by the strip and re-planning it must reproduce the
        # staged text.  (996's tool had to be taught the same thing; a verify that only runs before
        # the first stage proves nothing about the file the build will actually read.)
        if already:
            try:
                pre_image = strip_edits(staged)
            except Refusal as exc:
                print(f"patch_d13_shreg_wire: --verify-off FAIL - {exc.msg}", file=sys.stderr)
                return 1
        else:
            pre_image = pre
        ok, why = verify_off(pre_image, staged)
        print(f"patch_d13_shreg_wire: --verify-off {'PASS' if ok else 'FAIL'} "
              f"({'already-staged' if already else 'unstaged'} input) - {why}")
        return 0 if ok else 1

    if already:
        print(f"patch_d13_shreg_wire: nothing to do {XNU_VM_UNIX} - already staged")
        print(f"  {SITES_A} gate lines and {SITES_B} body gate are already {SWITCH}'s shape")
        return 0
    if dry_run:
        for a in actions:
            print(f"  {a}")
        print(f"  {SITES_A} gate regions, {SITES_B} body gate")
        sys.stdout.write(diff_text(pre, staged))
        return 0

    for a in actions:
        print(f"  {a}")
    path.write_text(staged)
    print(f"patch_d13_shreg_wire: staged {XNU_VM_UNIX} "
          f"(sha256 {sha256(staged)})")
    print(f"  {staged.count(GATE_IF)} gate region(s), {staged.count(BODY_OPEN)} body gate; "
          f"pre-image sha256 {sha256(pre)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())