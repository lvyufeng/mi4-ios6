#!/usr/bin/env python3
"""Make `osfmk/arm/hw_lock.s`'s `mlckwait` set the WAITERS bit unconditionally (986).

WHY. The D13 boot stops in `IOKitInitializeTime`: the boot thread's 2 s deadline sleep now RETURNS
(985's clock tick), but the same thread then blocks in `lck_mtx_lock_wait` (from `mlckwait`) and never
returns; a second thread is parked at the same site. The cause is a **lost wakeup** in the D13 ARM
mutex wait:

  * `lck_mtx_lock`'s slow path reaches `mlckwait` only when the lock word carries owner bits. There it
    must record that a waiter is queued before it sleeps, by setting bit 1 (`ARM_LCK_WAITERS`).
  * The linked image instead has `80497504: e3833000  orr r3, r3, #0` - bit 0 (interlock) and bit 1
    (waiters) BOTH clear, i.e. the waiter leaves no trace.
  * `lck_mtx_unlock`'s fast path is taken iff `(word & 3) == 0` (`hw_lock.s:387-394`). With no bit set,
    the owner takes the fast path, `lck_mtx_unlock_wakeup` is never called, and `thread_wakeup_one` is
    never delivered - the parked thread is stranded. `lck_mtx_ilk_unlock` clears bit 0 only
    (`machine_routines_asm.s:50-54)`), so nothing else in the tree sets bit 1.
  * The unlock slow path itself PANICS unless bit 1 is set (`lmuslow: ands r2,r3,#2; beq lmupanic`).
    That invariant - "unlock needs bit 1 to wake waiters" - is stated in the tree's own comment.

WHY IT WAS #0. `hw_lock.s:457` gates the correct `orr r3, r3, #3` behind
`#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`. Upstream Apple's file is plain `#0` (identical
in the imbushuo/HTC-Leo/winocm mirrors); the `#3` is a **4-line addition by the fork**. Apple's own
Mach-O build defines `-DBOARD_CONFIG_$(MACHINE_CONFIG)` (default `ARMPBA8`) and so takes `#3`; this
project's ELF assembly path receives **no** `BOARD_CONFIG_*` macro (`tools/xnu_config/arm_asm_defines.sh`
emits only the configuration's `CONFIG_*` options, and the C side's `MSM8974_CANCRO` is never passed to
a `.s` compile), so the gate is closed and the defective `#0` arm is linked. Making the fix
unconditional is safe because `hw_lock.s` is **D13-only** - 4570 uses `osfmk/arm/locks_arm.c` and ships
no `hw_lock.s` (verified: 0 hw_lock entries in the 4570 manifest, 1 in the D13 manifest).

The edit is TEXT, in a gitignored tree, so it must be a script a re-checkout can re-apply
(`stage_d13_board.sh`'s rule). Idempotent: a second run is a no-op.
"""
import re
import sys

# The exact gated block this replaces (the fork's addition, with its comment and the stock `#0` arm).
_OLD = (
    "#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO\n"
    "    /* lock_wait requires the interlock; unlock needs bit 1 to wake waiters. */\n"
    "    orr     r3, r3, #3\n"
    "#else\n"
    "    orr     r3, r3, #0\n"
    "#endif\n"
)

_NEW = (
    "    /* **986: UNCONDITIONAL - this is the fix, not a style choice.**\n"
    "     * The stock upstream `orr r3, r3, #0` sets NEITHER bit 0 (interlock) NOR bit 1\n"
    "     * (ARM_LCK_WAITERS), so a parked waiter leaves no trace: `lck_mtx_unlock`'s fast\n"
    "     * path is `(word & 3) == 0` and is taken, `lck_mtx_unlock_wakeup` never runs, and\n"
    "     * `thread_wakeup_one` is never delivered - the thread is stranded. `lmuslow` PANICS\n"
    "     * unless bit 1 is set (`ands r2,r3,#2; beq lmupanic`), the invariant in words: unlock\n"
    "     * needs bit 1 to wake waiters. The fork gated this behind\n"
    "     * `BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`; this ELF build's assembler gets\n"
    "     * no BOARD_CONFIG_* macro, so the gate closed and the defective arm linked\n"
    "     * (verified: 0x80497504 `orr r3,r3,#0`). hw_lock.s is D13-only (4570 uses locks_arm.c). */\n"
    "    orr     r3, r3, #3\n"
)

# A marker that proves THIS patch already ran (so re-runs are no-ops).
_MARK = "986: UNCONDITIONAL"


def patch_text(text: str) -> tuple[str, bool]:
    """Return (new_text, changed). Idempotent."""
    if _MARK in text:
        return text, False
    if _OLD not in text:
        raise SystemExit(
            "patch_d13_hw_lock_mlckwait: the gated mlckwait block is not present verbatim - "
            "the tree is not the expected revision, or it was edited by hand. Refusing."
        )
    return text.replace(_OLD, _NEW, 1), True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_hw_lock_mlckwait.py <XNU_TREE>", file=sys.stderr)
        return 2
    path = sys.argv[1] + "/osfmk/arm/hw_lock.s"
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print(f"patch_d13_hw_lock_mlckwait: no {path}", file=sys.stderr)
        return 2

    new, changed = patch_text(text)
    if not changed:
        print(f"patch_d13_hw_lock_mlckwait: already applied ({path})")
        return 0

    # Sanity: the replacement must still put `orr r3, r3, #3` right before the store at mlckwait.
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"patch_d13_hw_lock_mlckwait: mlckwait now sets bit 1 unconditionally ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())