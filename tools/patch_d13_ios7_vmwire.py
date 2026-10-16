#!/usr/bin/env python3
"""Stage the iOS 7 mach-trap WIRE-WIDTH fix for traps -10/-12/-14 into the D13 tree (idempotent).

THE DEFECT, by value.  A mach trap's arguments are the SAVED-REGISTER FRAME, and iOS 7 ARMv7
userspace (the real dyld in the shared cache) passes every 64-bit `mach_vm_address_t`/`mach_vm_size_t`
as a PAIR of 32-bit words:

  trap -10 mach_vm_allocate    { target, addr, size_lo, size_hi, flags }                       5 words
  trap -12 mach_vm_deallocate  { target, addr_lo, addr_hi, size_lo, size_hi }                  5 words
  trap -14 mach_vm_protect     { .., size_lo, size_hi, set_maximum, new_protection }           7 words

dyld's own stubs prove the shapes: `__kernelrpc_mach_vm_allocate_trap` (0x2be1b410) is
`mov r12,sp; push {r4,r5}; ldr r4,[r12]` - it lifts the 5th word off the stack into r4, so the frame
is 5 words with `flags` last; `__kernelrpc_mach_vm_deallocate_trap` (0x2be1b42c) lifts the 5th word
the same way; `__kernelrpc_mach_vm_protect_trap` (0x2be1b448) is `push {r4,r5,r6,r8}; ldm r12,
{r4,r5,r6}` - 7 words.  `_vm_allocate` (0x2be1bb70) stores flags with `str r3,[sp]` and then
`movs r3,#0` (size_hi), exactly the trap-10 shape.

This kernel's native argument structs are ONE WORD SHORTER each, because on this 32-bit build
`mach_vm_address_t`/`mach_vm_size_t` are `uint32_t` (`osfmk/mach/arm/vm_types.h:107-109`, under
`#else /* !__LP64__ */`, "We target iOS 4.3 for compatibility") and `PAD_ARG_` pads nothing for a
32-bit type on `__arm__` (`osfmk/mach/mach_traps.h`).  So the native bodies read the wire one field
early: trap -10's `flags` is really `size_hi` (0 - the allocation runs with no VM_FLAGS_ANYWHERE),
trap -12's `size` is really `addr_hi` (0 - a deallocate that frees nothing), trap -14's
`set_maximum`/`new_protection` are really `size_lo`/`size_hi` (garbage protection).  The linked
pre-fix image shows it: `_kernelrpc_mach_vm_allocate_trap` loads `ldr r3,[r5,#12]` as the flags word
while the wire's flags sits at #16.

THE FIX.  A new narrow switch, `STAGE90_XNU_IOS7_TRAPS`, opens three board-neutral adapters that
parse the register frame at its REAL width and call the same `mach_vm_*` primitives
(`ios7lab_vm_allocate` already exists for ARMPBA8; `ios7lab_vm_deallocate` and `ios7lab_vm_protect`
are added here in the same style, each with a `typedef char ..._size[...==N?1:-1]` wire-size assert
and a bounded runtime `printf`, `IOS7LAB vm_deallocate64` / `IOS7LAB vm_protect64`).  Every gate
appends `|| (defined(...) && STAGE90_XNU_IOS7_TRAPS)` so with the switch OFF the tree's own bytes are
compiled (`defined()` first: the identifier is never evaluated undefined -
`mi4-off-option-two-spellings`).

Edits (all in two files; exact-match, no partial application - every anchor is asserted to occur
exactly once BEFORE anything is written, and a second run says "already staged" and writes nothing):

  osfmk/arm/ios7_compat.h
    1. add `ios7_traps_enabled()` after `ios7_compat_vmabi_enabled()` - the one selector the three
       call sites read (switch ON -> 1; OFF -> the board's vmabi answer, so ARMPBA8/LEO behavior is
       bit-for-bit what it was).

  osfmk/ipc/mach_kernelrpc.c
    2. the file-wide gate `#if IOS7_COMPAT_PLATFORM` (:39) also opens under the switch, so the
       ios7lab wire structs and `ios7lab_vm_allocate` become reachable on CANCRO.
    3. an inner `#if IOS7_COMPAT_PLATFORM` around the port/vm_map lab adapters (:62-:238) keeps a
       CANCRO switch-ON build to the VM wire only - the port traps are not part of this fix.
    4. the two new wire structs + two new adapters, gated on the switch alone, at the end of the
       file-wide block - so ARMPBA8/LEO images are unchanged even with the switch set.
    5. the three trap bodies route through their adapter when `ios7_traps_enabled()`.

NOT staged here (spec-only, owned by other lanes - see docs/reference/d13-ios7-vm-wire-defect.md):
`osfmk/kern/syscall_sw.c` needs no change (traps 10/12/14 already carry arg counts 5/5/7 and, on
`__arm__`, the mungers are ignored), and `osfmk/mach/mach_traps.h` needs no change (its structs stay
the control ABI; the switch-ON path never dereferences them).
"""
import sys
from pathlib import Path

XNU_COMPAT = "osfmk/arm/ios7_compat.h"
XNU_RPC = "osfmk/ipc/mach_kernelrpc.c"

GATE_OLD = "#if IOS7_COMPAT_PLATFORM"
GATE_NEW = ("#if IOS7_COMPAT_PLATFORM"
            " || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)")

# ---- osfmk/arm/ios7_compat.h: the selector ------------------------------------------------

COMPAT_ANCHOR = """#else
    return 0;
#endif
}
"""
COMPAT_BLOCK = """

/* iOS 7 mach-trap wire width (2026-10-12).  The trap wire is the SAVED-REGISTER FRAME, not a C ABI:
 * trap -10 (mach_vm_allocate) is 5 words {target, addr, size_lo, size_hi, flags}, trap -12
 * (mach_vm_deallocate) 5 words {target, addr_lo, addr_hi, size_lo, size_hi}, trap -14
 * (mach_vm_protect) 7 words {.., size_lo, size_hi, set_maximum, new_protection}.  This kernel's
 * mach_vm_address_t/mach_vm_size_t are 32-bit (osfmk/mach/arm/vm_types.h:107-109, "We target iOS 4.3
 * for compatibility") and PAD_ARG_ pads nothing for a 32-bit type on __arm__, so its native structs
 * are one word short each and the native bodies read the wire's NEXT field.  OFF: this header is the
 * tree's line for line and no call site compiles (mi4-off-option-two-spellings: `defined()` first,
 * so the identifier is never evaluated undefined). */
static inline int ios7_traps_enabled(void)
{
#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    return 1;
#else
    return ios7_compat_vmabi_enabled();
#endif
}
"""

# ---- osfmk/ipc/mach_kernelrpc.c: the inner guard around the port/vm_map lab adapters --------

INNER_OPEN_ANCHOR = """typedef char ios7lab_vm_allocate_wire_size[
    sizeof(struct ios7lab_vm_allocate_wire) == 20 ? 1 : -1];
"""
INNER_OPEN_BLOCK = """
/* 2026-10-12: the file-wide gate above also opens for CANCRO when STAGE90_XNU_IOS7_TRAPS is set
 * (the traps -10/-12/-14 wire fix lives in this file), but the port and vm_map lab adapters stay on
 * the boards that already run them - a CANCRO switch-ON build compiles the VM wire only. */
#if IOS7_COMPAT_PLATFORM /* port/vm_map lab adapters: ARMPBA8/LEO only */
"""

# **1022 COMPOSITION FIX (records/1022-patcher-composition-defect.txt).**  1022's
# `patch_d13_mach_trap15.py` WIDENS this block's last line (it must - the trap-15 body
# `ios7lab_mach_vm_map_trap` is inside this guard and is gated out on CANCRO otherwise).  That edit
# rewrites THIS insert's idempotence key (`anchor + block`), so a rebuild that re-runs vmwire FIRST
# would no longer match its own block and would insert a SECOND copy (a bare gate above the widened
# one) - MEASURED as a `#if`/`#endif` imbalance (15 vs 14).  This is the WIDENED spelling of the same
# block; plan_insert is handed it as an ALTERNATE staged form, so a trap15-widened tree reports
# "already staged" instead of re-inserting.  A fresh tree (and a bare-only tree) still takes the
# original BARE path unchanged, so on every tree state that previously succeeded no staged byte moves.
INNER_OPEN_BLOCK_WIDE = """
/* 2026-10-12: the file-wide gate above also opens for CANCRO when STAGE90_XNU_IOS7_TRAPS is set
 * (the traps -10/-12/-14 wire fix lives in this file), but the port and vm_map lab adapters stay on
 * the boards that already run them - a CANCRO switch-ON build compiles the VM wire only. */
#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS) /* port/vm_map lab adapters: ARMPBA8/LEO only */
"""

INNER_CLOSE_ANCHOR = """    if (logged++ < 12)
        printf("IOS7LAB vm_map64 size=%08x:%08x mask=%08x:%08x flags=%x prot=%x result=%d address=%08x\\n",
               regs[3], regs[2], regs[5], regs[4], regs[6], regs[8], rv, address32);
    if (task)
        task_deallocate(task);
    return rv;
}
"""
INNER_CLOSE_BLOCK = "#endif /* port/vm_map lab adapters */\n"

# ---- osfmk/ipc/mach_kernelrpc.c: traps -12 and -14, wires + adapters -------------------------

ALLOC_TAIL_ANCHOR = """done:
    if (logged++ < 8)
        printf("IOS7LAB vm_allocate64 size=%08x:%08x flags=%x result=%d address=%08x\\n",
               wire->size_high, wire->size_low, wire->flags, rv, address32);
    if (task)
        task_deallocate(task);
    return rv;
}
"""
NEW_ADAPTERS_BLOCK = """
#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
/* Traps -12 and -14 carry the SAME wire-width defect as trap -10 above, with the shapes that
 * comment gives: { target, address_low, address_high, size_low, size_high } (5 words) and
 * { .., size_low, size_high, set_maximum, new_protection } (7 words).  The native bodies read them
 * one field early, so trap -12's `size` is really address_high and trap -14's
 * `set_maximum`/`new_protection` are really size_low/size_high.  A 32-bit process has high words of
 * 0, so the native -12 frees nothing and the native -14 protects with a protection word that is
 * really a size.  Guard as in the allocate adapter: a nonzero high word cannot name anything in a
 * 32-bit address space, so it is KERN_INVALID_ARGUMENT rather than a silent truncation. */
struct ios7lab_vm_deallocate_wire {
    uint32_t target, address_low, address_high, size_low, size_high;
};
typedef char ios7lab_vm_deallocate_wire_size[
    sizeof(struct ios7lab_vm_deallocate_wire) == 20 ? 1 : -1];

struct ios7lab_vm_protect_trap_wire {
    uint32_t target, address_low, address_high, size_low, size_high;
    uint32_t set_maximum, new_protection;
};
typedef char ios7lab_vm_protect_trap_wire_size[
    sizeof(struct ios7lab_vm_protect_trap_wire) == 28 ? 1 : -1];

static int
ios7lab_vm_deallocate(struct ios7lab_vm_deallocate_wire *wire)
{
    task_t task = port_name_to_task(wire->target);
    int rv = MACH_SEND_INVALID_DEST;
    static unsigned int logged = 0;

    if (task != current_task())
        goto done;
    if (wire->address_high || wire->size_high) {
        rv = KERN_INVALID_ARGUMENT;
        goto done;
    }
    rv = mach_vm_deallocate(task->map, (mach_vm_address_t)wire->address_low,
                            (mach_vm_size_t)wire->size_low);
done:
    if (logged++ < 12)
        printf("IOS7LAB vm_deallocate64 address=%08x:%08x size=%08x:%08x result=%d\\n",
               wire->address_high, wire->address_low, wire->size_high, wire->size_low, rv);
    if (task)
        task_deallocate(task);
    return rv;
}

static int
ios7lab_vm_protect(struct ios7lab_vm_protect_trap_wire *wire)
{
    task_t task = port_name_to_task(wire->target);
    int rv = MACH_SEND_INVALID_DEST;
    static unsigned int logged = 0;

    if (task != current_task())
        goto done;
    if (wire->address_high || wire->size_high) {
        rv = KERN_INVALID_ARGUMENT;
        goto done;
    }
    rv = mach_vm_protect(task->map, (mach_vm_address_t)wire->address_low,
                         (mach_vm_size_t)wire->size_low, wire->set_maximum,
                         wire->new_protection);
done:
    if (logged++ < 12)
        printf("IOS7LAB vm_protect64 address=%08x:%08x size=%08x:%08x set_max=%x new_prot=%x result=%d\\n",
               wire->address_high, wire->address_low, wire->size_high, wire->size_low,
               wire->set_maximum, wire->new_protection, rv);
    if (task)
        task_deallocate(task);
    return rv;
}
#endif
"""

# ---- osfmk/ipc/mach_kernelrpc.c: the three call sites ---------------------------------------

CALLS = (
    ("trap -10 (mach_vm_allocate)", """#if BOARD_CONFIG_ARMPBA8
    int lab_abi = 0;
    if (PE_parse_boot_argn("ios7lab_vmabi", &lab_abi, sizeof(lab_abi)) && lab_abi == 1)
        return ios7lab_vm_allocate((struct ios7lab_vm_allocate_wire *)args);
#endif
""", """#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    if (ios7_traps_enabled())
        return ios7lab_vm_allocate((struct ios7lab_vm_allocate_wire *)args);
#endif
"""),
    ("trap -12 (mach_vm_deallocate)", """int
_kernelrpc_mach_vm_deallocate_trap(struct _kernelrpc_mach_vm_deallocate_args *args)
{
#if BOARD_CONFIG_QSD8250_LEO
    return ios7leo_vm_deallocate64((struct ios7lab_vm_range_wire *)args);
#endif
""", """#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    if (ios7_traps_enabled())
        return ios7lab_vm_deallocate((struct ios7lab_vm_deallocate_wire *)args);
#endif
"""),
    ("trap -14 (mach_vm_protect)", """int
_kernelrpc_mach_vm_protect_trap(struct _kernelrpc_mach_vm_protect_args *args)
{
#if BOARD_CONFIG_QSD8250_LEO
    return ios7leo_vm_protect64((struct ios7lab_vm_protect_wire *)args);
#endif
""", """#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
    if (ios7_traps_enabled())
        return ios7lab_vm_protect((struct ios7lab_vm_protect_trap_wire *)args);
#endif
"""),
)


class Refusal(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


def plan_insert(text, what, anchor, block, block_alt=None):
    """Insert `block` immediately after `anchor`.  (new_text, changed, note) or Refusal.

    Idempotence is keyed on `anchor + block` - the anchor stays in the file BELOW the insertion, so
    it can never be the key (`mi4-an-edit-on-a-prefix-deletes-the-suffix`).  Any other state is
    refused, never re-guessed.

    `block_alt`, if given, is an ALTERNATE spelling of the same block that a LATER patcher may have
    produced (e.g. `patch_d13_mach_trap15.py` widening this block's gate line - records/1022-patcher-
    composition-defect.txt).  When `anchor + block_alt` occurs exactly once, the block is treated as
    ALREADY STAGED and nothing is written (a second insertion would double it).  This only widens the
    "already staged" predicate; a tree where neither spelling is present stages `block` exactly as
    before, so no tree state that previously succeeded changes bytes.
    """
    n_anchor = text.count(anchor)
    n_staged = text.count(anchor + block)
    n_alt = text.count(anchor + block_alt) if block_alt is not None else 0
    if n_anchor == 1 and n_staged == 1:
        return text, False, f"{what}: already staged"
    if n_anchor == 1 and n_alt == 1:
        return text, False, f"{what}: already staged (alternate spelling staged by a later patcher)"
    if n_staged != 0 or n_alt != 0 or n_anchor != 1:
        raise Refusal(6, f"{what}: found {n_anchor} anchor(s), {n_staged} staged copy/copies and "
                         f"{n_alt} alternate-spelling copy/copies, expected exactly one anchor and "
                         f"zero-or-one staged copy - the region was edited by hand; not repaired in "
                         f"place")
    at = text.index(anchor) + len(anchor)
    return text[:at] + block + text[at:], True, f"{what}: staged"


def plan_gate(text, what, old, new, expect):
    """Whole-line replace of `old` with `new`, asserting the exact total count."""
    lines = text.split("\n")
    at_old = [i for i, ln in enumerate(lines) if ln == old]
    at_new = [i for i, ln in enumerate(lines) if ln == new]
    if not at_old and len(at_new) == expect:
        return text, False, f"{what}: already staged ({len(at_new)} line(s))"
    if len(at_old) + len(at_new) != expect:
        raise Refusal(4, f"{what}: {len(at_old)} original + {len(at_new)} staged line(s), expected "
                         f"{expect} total - a partial or double edit is not repaired in place")
    for i in at_old:
        lines[i] = new
    return "\n".join(lines), True, f"{what}: {len(at_old)} line(s) opened at " \
                                   f"{', '.join(str(i + 1) for i in at_old)}"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_ios7_vmwire.py <xnu-tree-root>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    compat, rpc = root / XNU_COMPAT, root / XNU_RPC
    for path in (compat, rpc):
        if not path.is_file():
            print(f"patch_d13_ios7_vmwire: no {path} (D13 tree not provisioned?)", file=sys.stderr)
            return 2

    # Plan every edit first: a refusal anywhere writes NOTHING (no partial application).
    try:
        new_compat, ch_compat, note_compat = plan_insert(
            compat.read_text(), "ios7_compat.h: ios7_traps_enabled()", COMPAT_ANCHOR, COMPAT_BLOCK)

        rpc_text = rpc.read_text()
        notes, changed = [], False
        rpc_text, ch, note = plan_gate(rpc_text, "mach_kernelrpc.c: file-wide gate (:39)",
                                       GATE_OLD, GATE_NEW, 1)
        notes.append(note); changed |= ch
        for what, anchor, block in (("mach_kernelrpc.c: inner port/vm_map guard",
                                     INNER_OPEN_ANCHOR, INNER_OPEN_BLOCK),
                                    ("mach_kernelrpc.c: inner guard close",
                                     INNER_CLOSE_ANCHOR, INNER_CLOSE_BLOCK),
                                    ("mach_kernelrpc.c: traps -12/-14 wires + adapters",
                                     ALLOC_TAIL_ANCHOR, NEW_ADAPTERS_BLOCK)):
            # The inner guard's block may already be present in the WIDENED spelling, if 1022's
            # patch_d13_mach_trap15.py has run (records/1022-patcher-composition-defect.txt).
            alt = INNER_OPEN_BLOCK_WIDE if anchor is INNER_OPEN_ANCHOR else None
            rpc_text, ch, note = plan_insert(rpc_text, what, anchor, block, block_alt=alt)
            notes.append(note); changed |= ch
        for what, anchor, block in CALLS:
            rpc_text, ch, note = plan_insert(rpc_text, f"mach_kernelrpc.c: route {what}",
                                             anchor, block)
            notes.append(note); changed |= ch
    except Refusal as e:
        print(f"patch_d13_ios7_vmwire: REFUSING: {e}", file=sys.stderr)
        return e.code

    for note in [note_compat] + notes:
        print(f"  {note}")
    staged = []
    if ch_compat:
        compat.write_text(new_compat)
        staged.append(XNU_COMPAT)
    if changed:
        rpc.write_text(rpc_text)
        staged.append(XNU_RPC)
    print(f"patch_d13_ios7_vmwire: {'staged ' + ', '.join(staged) if staged else 'nothing to do'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())