#!/usr/bin/env python3
"""Make Darwin-13's `arm_vm_init` carry the boot table into the system table (experiment 971).

WHY.  970g restored the boot path so the entry's console mapping is installed into the boot table at
`topOfKernelData` (`armed-d13-321e3332`).  That makes the console survive the `_start`->`arm_init`
window: `xnu_live_console` and the first wrapped probes publish under it.  But past `arm_vm_init` the
console is lost AGAIN, because D13 builds the system table WRONG:

    D13  osfmk/arm/arm_vm_init.c:352-353
        cpu_ttb = gTopOfKernel + L1_SIZE;
        bzero((void*)phys_to_virt(cpu_ttb), L1_SIZE);     <-- a FRESH table, nothing copied

    4570 osfmk/arm/arm_vm_init.c:385-391
        boot_ttep = args->topOfKernelData;                 <-- the boot table 970g now fills
        boot_tte  = (tt_entry_t *) phystokv(boot_ttep);
        cpu_ttep  = boot_ttep + ARM_PGBYTES * 4;           <-- the system table
        cpu_tte   = (tt_entry_t *) phystokv(cpu_ttep);     <-- NOTE: gTopOfKernel + L1_SIZE, the SAME
        bcopy(boot_tte, cpu_tte, ARM_PGBYTES * 4);         <-- address D13 bzeroes; it COPIES

The entry's own source states this dependency (`src/entry/entry_stubs.c:2210-2214`): the console's
descriptors survive the table switch *because* `arm_vm_init` copies the boot table into the table the
MMU walks afterwards - "the copy is what carries the console's descriptors into the table the MMU walks
afterwards" - and the console's own latch "stays valid *because* its install happened first".  D13 does
no such copy, and D13's managed map is `[MANAGED_BASE 0xC0000000, 0xC0000000 + gMemSize)`, which does
NOT contain the console VA `0xde500000` (RAM_CONSOLE_BASE).  So after `set_mmu_ttb(cpu_ttb)` the boot
table's console sections are gone, the slot fault has no translation, and every record written after
`arm_vm_init` is dropped - exactly the way 970/968 were silent at `_start`, one rung later.

WHY THIS IS THE WHOLE FIX, AND WHY IT NEEDS NO SECOND EDIT.  4570 follows the copy with a loop that
writes `ARM_TTE_TYPE_FAULT` over the copied V==P slots `[ttenum(gPhysBase), ttenum(gVirtBase))` - the
boot table's own identity sections, which must not survive into the system table.  D13 does not need
that loop for two reasons, both read by value:

  * it maps the identity region FRESH.  `l2_cache_to_range(identityCachePA, identityBaseVA, ...)` and
    `l2_map_linear_range(identityCachePA, gPhysBase - sectionOffset, gPhysBase + gMemSize)` run AFTER
    this point and write `L1_TYPE_PTE` over the L1 slots for `[gVirtBase, gVirtBase + gMemSize)` - the
    very slots any copied identity section would occupy - so the copied V==P sections are overwritten,
    not inherited;
  * on the shipped arm `gPhysBase == gVirtBase == 0x80000000`, so 4570's clear range
    `[gPhysBase, gVirtBase)` is EMPTY anyway.

What the copy therefore carries into the system table is exactly what must survive: the identity
sections (overwritten next), the entry console at `0xde500000`, its megabyte twin `0xde600000` and the
alias `0xee500000`, and any device sections the early probes installed.  This is 4570's table, by
4570's own mechanism, and it is the change that makes the keys after `arm_vm_init` observable.

SCOPE.  One statement in D13's `osfmk/arm/arm_vm_init.c` (the 4570 tree is untouched - its bcopy is
already there and correct, and this file does not exist as a D13/D13-distinct path).  The change is a
strict superset of the old behaviour: a zeroed table gains the boot table's descriptors.

Idempotent and re-appliable: the replacement's token is its "already patched" marker, and the anchor is
matched whole (exact spacing) so a source update that moves it refuses loudly rather than patching the
wrong bytes.  `tools/stage_d13_vm_init.sh` applies it; the proof is by VALUE - a rebuilt
`out/xnu_asm_obj_d13.../arm_vm_init.o` must call `_bcopy` where it used to call `_bzero`.
"""
import sys

FILE = "osfmk/arm/arm_vm_init.c"

# Matched whole, exact spacing (leading four spaces, no space in `(void*)`).  A re-spaced source makes
# the count != 1 and we refuse rather than patch the wrong bytes.
ANCHOR = (
    "    cpu_ttb = gTopOfKernel + L1_SIZE;\n"
    "    bzero((void*)phys_to_virt(cpu_ttb), L1_SIZE);\n"
)
REPLACEMENT = (
    "    cpu_ttb = gTopOfKernel + L1_SIZE;\n"
    "    /* 971: copy the boot table instead of building a fresh one (tools/patch_d13_vm_init.py).\n"
    "     *\n"
    "     * 4570's arm_vm_init (start.s's successor) does `bcopy(boot_tte, cpu_tte, ARM_PGBYTES*4)`\n"
    "     * here, and that copy is what carries the entry's console descriptors from the boot table\n"
    "     * at topOfKernelData into the table the MMU walks after `set_mmu_ttb(cpu_ttb)` below.\n"
    "     * `src/entry/entry_stubs.c:2210-2214` depends on it by name.  D13 bzeroed a FRESH table,\n"
    "     * so the console (VA 0xde500000, outside D13's managed map [0xC0000000, 0xC0000000+gMemSize))\n"
    "     * was lost the moment the table switched - every key after arm_vm_init was dropped.\n"
    "     *\n"
    "     * The copied identity sections are harmless: the l2_cache_to_range/l2_map_linear_range\n"
    "     * calls below overwrite the L1 slots for [gVirtBase, gVirtBase+gMemSize) with L1_TYPE_PTE,\n"
    "     * and gPhysBase == gVirtBase on this arm, so 4570's V==P clear range is empty. */\n"
    "    bcopy((void*)phys_to_virt(gTopOfKernel), (void*)phys_to_virt(cpu_ttb), L1_SIZE);\n"
)
MARKER_GONE = "bzero((void*)phys_to_virt(cpu_ttb), L1_SIZE);"
MARKER_PRESENT = "bcopy((void*)phys_to_virt(gTopOfKernel), (void*)phys_to_virt(cpu_ttb), L1_SIZE);"
TOKEN = "971: copy the boot table"


def patch_once(src, anchor, replacement, patched_token, name):
    """Apply one replace edit.  Idempotence is keyed on `patched_token`, which appears ONLY in the
    replacement: its presence means the edit already ran (left alone), its absence means `anchor` must
    be found exactly once (else refuse - Apple's source moved and we will not patch the wrong bytes)."""
    if patched_token in src:
        print(f"patch_d13_vm_init: {name}: already applied (idempotent, left alone)")
        return src, False
    n = src.count(anchor)
    if n != 1:
        raise SystemExit(f"patch_d13_vm_init: {name}: {n} matches for the anchor; the source has moved"
                         f" or is ambiguous. Refusing rather than patching the wrong bytes.")
    out = src.replace(anchor, replacement, 1)
    if out == src or patched_token not in out:
        raise SystemExit(f"patch_d13_vm_init: {name}: substitution did not insert the patched token;"
                         f" refusing")
    print(f"patch_d13_vm_init: {name}: applied")
    return out, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_vm_init.py <external/xnu-hd2-darwin13/xnu>", file=sys.stderr)
        return 2
    path = sys.argv[1].rstrip("/") + "/" + FILE
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
        src = fh.read()

    src, _ = patch_once(src, ANCHOR, REPLACEMENT, TOKEN, "arm_vm_init table copy")

    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(src)

    final = open(path, "r", encoding="utf-8", errors="surrogateescape").read()
    assert MARKER_GONE not in final, "the bzero is still present - edit did not take"
    assert MARKER_PRESENT in final, "the bcopy is not present - edit did not take"
    print(f"patch_d13_vm_init: wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())