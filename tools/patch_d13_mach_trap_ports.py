#!/usr/bin/env python3
"""Stage the mach trap-24 install (`mach_trap_table[24] = ios7lab_mach_port_construct_trap`) into the
D13 tree -- rung 1023, switch `STAGE90_XNU_IOS7_TRAPS`.

THE DEFECT, BY VALUE (docs/1023-port-construct-trap24-design.md sections 2.1-2.3; the chain was
re-read out of the shared cache and the tree this session).

libxpc's port-allocation wrapper (`0x3824dfc8`, `__xpc_mach_port_allocate`-family) does
`3824e00e: blx 0x38260b60`, a dyld lazy-bind veneer.  The veneer's slot at VA `0x39f45174` (the THIRD
cache mapping) holds `0x381c66ed` -> the shim at `0x381c66ec`.  The shim's `381c6702: blx 0x381c59dc`
targets an ARM mach-trap fast-trap stub: `mov r12,sp; push {r4,r5}; ldr r4,[r12]; mvn r12,#0x17;
svc #0x80`.  `mvn r12,#0x17` is `r12 = -24`, and a mach trap's index encoding is `imm + 1` (anchor: the
trap-15 stub is `mvn r12,#0x0E` -> -15, `records/1021d-mmap-failure.txt`), so the trap is **24**, not
16.  The shim then compares the return to `0x10000003` (`MACH_SEND_INVALID_DEST`,
`osfmk/mach/message.h:692`) and takes its slow leg ONLY for that value; anything else is returned to
the wrapper, whose `cbz r0` / `cmp r0,#3` / `cmpne r0,#6` chain falls through to `udf #254` for every
other `kern_return_t`.

`osfmk/kern/syscall_sw.c:139` is `#if IOS7_COMPAT_PLATFORM`, wrapping row 24's
`MACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL),`; `IOS7_COMPAT_PLATFORM` is
`BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` (`osfmk/arm/ios7_compat.h:5`) and is a compile-time
0 on CANCRO, so the row compiles as the `#else` `MACH_TRAP(kern_invalid, 0, NULL, NULL),` and the
wrapper receives `KERN_INVALID_ARGUMENT` = 4.  That is the frontier 1022 left (PART 5 of
`records/1022-press-read.txt`: `udf #254` at file VA `0x3824e022`).

THE FIX (shape (B), the narrow switch -- docs/1022 section 2, docs/1023 section 3).  NO new switch:
`STAGE90_XNU_IOS7_TRAPS` is already record key 19, already in the pool `DEFINES`
(`tools/build_xnu_arm_kernel.sh:902`), already in `EXTRA` (`src/entry/build_entry.sh:2109`) and already
in `ENTRY_ARM_KEYS` (`:2312`).  Two edits, and the SECOND is the load-bearing one:

  1. `osfmk/kern/syscall_sw.c` -- row 24's OWN gate (`:139`) is widened.  Row 25's gate is a SEPARATE
     `#if` (`:144`) and rows 41/42 share a third (`:164`), so this rung leaves them bare: the deferral
     for 25/41/42 stays honest and `tools/check_d13_ios7_vmwire.py` keeps checking it.

  2. `osfmk/ipc/mach_kernelrpc.c` -- the runtime selector INSIDE `ios7lab_mach_port_construct_trap`
     (`:74`) stops reading `ios7_compat_vmabi_enabled()` (a compile-time 0 on CANCRO,
     `ios7_compat.h:11-21`) and reads `ios7_traps_enabled()` instead, with the tree's ORIGINAL line
     verbatim in the `#else`.  WITHOUT this, an installed row returns `MACH_SEND_INVALID_DEST` -- the
     shim's OWN sentinel -- and the shim branches AWAY from the slow leg it needs, i.e. strictly worse
     than today (the T24-SENTINEL cell, docs/1023 section 0.2, the exact shape 1022 hit for trap 15).

  3. the same function gains a switch-gated register-frame dump (`IOS7LAB trap24 frame ...`), so the
     press DECIDES the trap-24 user-side wire empirically.  docs/1023 section 4 states the two inputs
     it must not assume: `regs[1]` is read as a USER POINTER to 24 option bytes (the fault frame has
     `r1 = 0xe0f33713`, which no static argument shows is a user pointer), and `regs[4]` is read as the
     output-name pointer.  The body's own `done:` printf already carries the verdict of both -- 0 for
     success, 4 for the options guard, `0x10000004` for a `copyin` refusal, `0x10000003` for the
     sentinel -- so the dump plus that line name which path was taken, with no extra probe.

NO NAME-TABLE EDIT.  `mach_syscall_name_table[24]` (`:291`) also reads `"kern_invalid"`, and 1022 left
row 15's name entry alone too (`:282`) WITHOUT failing its by-value clause -- that array is read by
`osfmk/arm/model_dep.c:713`'s panic decode and dtrace's provider names, not by the trap's dispatch,
and `mach_trap_table`'s own `mach_trap_name` word comes from the `MACH_TRAP(name, ...)` macro's `#name`
(`syscall_sw.h:96-99`), which this patch moves with the row automatically.  docs/1023 section 6.1
records it as SUNKNOWN; changing it would move an unmeasured consumer for nothing.

OFF IS BYTE-IDENTICAL BY MECHANISM, and the proof runs with the REAL PREPROCESSOR over the WHOLE file
before a byte is written.  Every new `#if` is written `defined(...) && ...` -- `defined()` is evaluated
FIRST, so an unset switch never evaluates the identifier (`mi4-off-option-two-spellings`).  Edit 1 is a
pure gate widen (its OFF arm is the tree's own bare `#if`), edit 3 is a pure insertion inside the `#if`
arm, and edit 2's new line sits in the `#if` arm with the tree's ORIGINAL line verbatim in the `#else`.
The proof machinery is `tools/patch_d13_mach_trap15.py`'s (`verify_off`), imported rather than copied:
that patcher owns the OFF argument for its own edits, and this one chains them so `head` means "no
rung's edit present".

CLI: `patch_d13_mach_trap_ports.py <xnu-tree-root>` plans (dry-run, prints the edits); `--apply` writes;
`--verify-off` proves OFF byte-identity on the CURRENT tree and writes nothing.
"""
import sys
from pathlib import Path

XNU_SYSCALL_SW = "osfmk/kern/syscall_sw.c"
XNU_RPC = "osfmk/ipc/mach_kernelrpc.c"

# The spelling is exactly the one tools/patch_d13_ios7_vmwire.py (`:66-68`) and
# tools/patch_d13_mach_trap15.py (`:66-67`) use.
GATE_OLD = "#if IOS7_COMPAT_PLATFORM"
GATE_NEW = ("#if IOS7_COMPAT_PLATFORM"
            " || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)")

# ---- syscall_sw.c: the row-24 gate (:139) ---------------------------------------------------
# The anchor is the GATE LINE + THE ROW LINE, so it is unique: the bare gate alone occurs three times
# (rows 24/25/41), and the row line alone would leave the edit blind to which gate it widened.

SSW_ROW24_OLD = GATE_OLD + """
/* 24 */\tMACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL),
"""
SSW_ROW24_NEW = GATE_NEW + """
/* 24 */\tMACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL),
"""

# ---- syscall_sw.c: the rows this rung still defers (asserted, never edited) ------------------
# Two-line anchors (gate + row) so they cannot match a WIDENED neighbour.

SSW_ROW25_DEFERRED = GATE_OLD + """
/* 25 */\tMACH_TRAP(ios7lab_mach_port_destruct_trap, 5, NULL, NULL),
"""
SSW_ROWS4142_DEFERRED = GATE_OLD + """
/* 41 */\tMACH_TRAP(ios7lab_mach_port_guard_trap, 5, NULL, NULL),
"""

# ---- syscall_sw.c: the externs the row needs, already declared by 1022 ---------------------

SSW_EXTERN_NEEDED = "extern int ios7lab_mach_port_construct_trap(void *saved_registers);"

# ---- mach_kernelrpc.c: the construct body's prologue + selector (:64-:76) ------------------
# One region replace: the selector swap (edit 2) and the frame dump (edit 3) are anchored together so
# neither can be half-applied.  The anchor is the whole prologue-through-early-out, which is unique to
# `ios7lab_mach_port_construct_trap` (`uint32_t options[6] = {0};` appears in no other body, and the
# other three lab adapters declare their frames in a different order).

RPC_CTOR_OLD = """    uint32_t *regs = (uint32_t *)saved_registers;
    uint32_t options[6] = {0};
    /* Runtime ABI selector is shared. */
    int rv = MACH_SEND_INVALID_DEST;
    task_t task = TASK_NULL;
    mach_port_name_t name = MACH_PORT_NULL;
    ipc_port_t send_port = IP_NULL;
    static unsigned int logged = 0;
    if (!ios7_compat_vmabi_enabled())
        return rv;
"""

RPC_CTOR_NEW = """    uint32_t *regs = (uint32_t *)saved_registers;
    uint32_t options[6] = {0};
    /* Runtime ABI selector is shared. */
    int rv = MACH_SEND_INVALID_DEST;
    task_t task = TASK_NULL;
    mach_port_name_t name = MACH_PORT_NULL;
    ipc_port_t send_port = IP_NULL;
    static unsigned int logged = 0;
#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    /* 1023: on CANCRO `ios7_compat_vmabi_enabled()` is a compile-time 0 (ios7_compat.h:11-21), so an
     * installed trap-24 row would return `MACH_SEND_INVALID_DEST` -- the shim's OWN sentinel
     * (osfmk/mach/message.h:692, 0x10000003; the shim at 0x381c66ec compares against exactly that
     * word and takes its slow leg ONLY on a match) -- and the row would be strictly worse than
     * absent.  Read the SAME selector the trap-15 map trap and the traps -10/-12/-14 adapters read.
     * OFF compiles the tree's own `ios7_compat_vmabi_enabled()` line, verbatim, in the #else below. */
    if (!ios7_traps_enabled())
#else
    if (!ios7_compat_vmabi_enabled())
#endif
        return rv;
#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    /* 1023 DISPUTE-RESOLVER (docs/1023-port-construct-trap24-design.md section 4): publish the
     * register frame the adapter ACTUALLY received.  Two inputs are UNMEASURED, not assumed-away:
     * `regs[1]` is read below as a POINTER to 24 option bytes (the fault frame's r1 = 0xe0f33713 is
     * not a plausible user text pointer), and `regs[4]` is read as the output name pointer.  This
     * line names every slot the handler reads, and the `done:` printf below already carries the
     * verdict: 0 = success, 4 = the options guard, 0x10000004 = the copyin refused, 0x10000003 = the
     * sentinel (a selector regression).  Its own counter (not `logged`, which the `done:` printf
     * shares) keeps the two dumps independent. */
    {
        static unsigned int frame_logged = 0;
        if (frame_logged++ < 12)
            printf("IOS7LAB trap24 frame r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r8=%08x\\n",
                   regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[8]);
    }
#endif
"""

# ---- mach_kernelrpc.c: the inner lab-adapter gate 1022 already widened (asserted) -----------

RPC_INNER_GATE_WIDE = (GATE_NEW + " /* port/vm_map lab adapters: ARMPBA8/LEO only */")


class Refusal(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


def import_trap15():
    """`tools/patch_d13_mach_trap15.py` -- for its OFF byte-identity proof, imported not copied.

    It is a sibling tool in this repo (trusted, same author, same lane discipline): its `verify_off`
    is the whole-file real-preprocessor proof, and its `unstaged_text` reverses ITS OWN edits, so
    chaining the two reversals makes `head` mean "no rung's edit present".
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import patch_d13_mach_trap15  # noqa: E402
    return patch_d13_mach_trap15


def plan_region(text, what, old, new):
    """Replace the region `old` with `new`, asserting it occurs EXACTLY ONCE.

    Idempotence is keyed on `new`: once staged, `old` occurs 0 times and `new` once.  Any other state
    (a partial edit, a double edit, a hand-edited region) is REFUSED, never re-guessed.
    """
    n_old = text.count(old)
    n_new = text.count(new)
    if n_old == 0 and n_new == 1:
        return text, False, f"{what}: already staged"
    if n_old != 1 or n_new != 0:
        raise Refusal(4, f"{what}: found {n_old} original region(s) and {n_new} staged copy/copies, "
                         f"expected exactly one original and zero-or-one staged - the region was "
                         f"edited by hand; not repaired in place")
    return text.replace(old, new, 1), True, f"{what}: staged"


def unstaged_text(text):
    """Reverse THIS patch's two edits, then 1022's, to recover the tree's ORIGINAL text."""
    for old, new in ((SSW_ROW24_OLD, SSW_ROW24_NEW), (RPC_CTOR_OLD, RPC_CTOR_NEW)):
        if text.count(new) == 1:
            text = text.replace(new, old, 1)
    return import_trap15().unstaged_text(text)


def run_verify_off(ssw_staged, rpc_staged):
    """Both files, per-file, through 1022's `verify_off` (it owns the real-preprocessor proof)."""
    import tempfile
    p15 = import_trap15()
    with tempfile.TemporaryDirectory() as td:
        for rel, staged in ((XNU_SYSCALL_SW, ssw_staged), (XNU_RPC, rpc_staged)):
            ok, msg = p15.verify_off(unstaged_text(staged), staged, rel, td)
            if not ok:
                return False, msg
    return True, ""


def main() -> int:
    argv = sys.argv[1:]
    apply_ = "--apply" in argv
    verify_only = "--verify-off" in argv
    argv = [a for a in argv if a not in ("--apply", "--verify-off")]
    if len(argv) != 1:
        print("usage: patch_d13_mach_trap_ports.py <xnu-tree-root> [--apply|--verify-off]",
              file=sys.stderr)
        return 2
    root = Path(argv[0])
    ssw, rpc = root / XNU_SYSCALL_SW, root / XNU_RPC
    for path in (ssw, rpc):
        if not path.is_file():
            print(f"patch_d13_mach_trap_ports: no {path} (D13 tree not provisioned?)", file=sys.stderr)
            return 2

    # Plan every edit first: a refusal anywhere writes NOTHING (no partial application).
    try:
        ssw_text, ch_a, note_a = plan_region(
            ssw.read_text(), f"{XNU_SYSCALL_SW}: row-24 gate (:139)", SSW_ROW24_OLD, SSW_ROW24_NEW)
        rpc_text, ch_b, note_b = plan_region(
            rpc.read_text(), f"{XNU_RPC}: trap-24 selector + dispute-resolver frame dump (:74)",
            RPC_CTOR_OLD, RPC_CTOR_NEW)
    except Refusal as e:
        print(f"patch_d13_mach_trap_ports: REFUSING: {e}", file=sys.stderr)
        return e.code

    # ---- the cross-file structural assertions, BEFORE anything is written --------------------
    # (a) 1022 must have run: the row's symbol has to be DECLARED on CANCRO (its extern-block gate)
    #     and the body has to be COMPILED on CANCRO (the inner lab-adapter gate at :59).  Without
    #     either, this rung's row would name an undefined symbol - a link error, not a silent pass.
    if ssw_text.count(SSW_EXTERN_NEEDED) != 1:
        print(f"patch_d13_mach_trap_ports: REFUSING: {XNU_SYSCALL_SW} carries "
              f"{ssw_text.count(SSW_EXTERN_NEEDED)} declaration(s) of the row's handler, expected 1 - "
              f"run tools/patch_d13_mach_trap15.py first (it widens the extern-decl gate)",
              file=sys.stderr)
        return 5
    if rpc_text.count(RPC_INNER_GATE_WIDE) != 1:
        print(f"patch_d13_mach_trap_ports: REFUSING: {XNU_RPC} carries "
              f"{rpc_text.count(RPC_INNER_GATE_WIDE)} widened inner lab-adapter gate(s), expected 1 - "
              f"the body would not compile on CANCRO; run tools/patch_d13_mach_trap15.py first",
              file=sys.stderr)
        return 5

    # (b) the deferral this rung declares: rows 25/41/42 keep their BARE two-line anchors, so a
    #     widened neighbour cannot masquerade as deferred.  This is the whole-file, structure-level
    #     form of the tuple that tools/check_d13_ios7_vmwire.py reads off the linked image (the lesson
    #     of records/1022-patcher-composition-defect.txt: a shared tree edited by several tools needs
    #     a clause neither tool's own predicate can supply).
    for what, anchor in (("row 25", SSW_ROW25_DEFERRED), ("rows 41/42", SSW_ROWS4142_DEFERRED)):
        if ssw_text.count(anchor) != 1:
            print(f"patch_d13_mach_trap_ports: REFUSING: {XNU_SYSCALL_SW}'s {what} deferral anchor "
                  f"occurs {ssw_text.count(anchor)} time(s), expected 1 - this rung opens row 24 "
                  f"ONLY (docs/1023 section 2.3)", file=sys.stderr)
            return 5

    # (c) the whole-file preprocessor stack stays balanced (an `#if` without its `#endif` compiles
    #     nothing and is invisible to every region-local predicate).
    for what, text in ((XNU_SYSCALL_SW, ssw_text), (XNU_RPC, rpc_text)):
        n_if = sum(1 for ln in text.split("\n") if ln.lstrip().startswith("#if"))
        n_end = sum(1 for ln in text.split("\n") if ln.lstrip().startswith("#endif"))
        if n_if != n_end:
            print(f"patch_d13_mach_trap_ports: REFUSING: {what} would carry {n_if} `#if` and "
                  f"{n_end} `#endif` - an unbalanced preprocessor stack (this is how 1022's "
                  f"composition defect showed, records/1022-patcher-composition-defect.txt)",
                  file=sys.stderr)
            return 5

    for note in (note_a, note_b):
        print(f"  {note}")
    staged = []
    if ch_a:
        staged.append(XNU_SYSCALL_SW)
    if ch_b:
        staged.append(XNU_RPC)

    if verify_only:
        ok, msg = run_verify_off(ssw_text, rpc_text)
        print(f"patch_d13_mach_trap_ports: OFF byte-identity proof {'PASS' if ok else 'FAIL'}")
        if not ok:
            print(f"patch_d13_mach_trap_ports: {msg}", file=sys.stderr)
            return 1
        return 0

    if not apply_:
        print(f"patch_d13_mach_trap_ports: PLAN only (no --apply); would stage "
              f"{', '.join(staged) if staged else 'nothing'}")
        return 0

    ok, msg = run_verify_off(ssw_text, rpc_text)
    print(f"patch_d13_mach_trap_ports: OFF byte-identity proof {'PASS' if ok else 'FAIL'}")
    if not ok:
        print(f"patch_d13_mach_trap_ports: REFUSING to write - {msg} (the tree is untouched)",
              file=sys.stderr)
        return 1

    if ch_a:
        ssw.write_text(ssw_text)
    if ch_b:
        rpc.write_text(rpc_text)
    print(f"patch_d13_mach_trap_ports: {'staged ' + ', '.join(staged) if staged else 'nothing to do'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())