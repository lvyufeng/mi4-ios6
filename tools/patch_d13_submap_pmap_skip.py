#!/usr/bin/env python3
"""Skip `vm_map_pmap_enter` when the enter is a SUBMAP - never enter pmap through a vm_map (1001).

WHY. 1000's press (arm `armed-d13-bbdf41bc`, the adjudication in `records/1000-press-read.txt`) died
at XNU time 10.31 s, immediately after `attempting to start init of /sbin/launchd`:

  * BY VALUE: `panic(cpu 0 caller 0x80498058): hw_lock_lock(): PANIC: Lock 0xc0005b50 = 0xffb5be0b`.
    `hw_lock_lock` = 0x80498020 (the caller is +0x38 inside it); `vm_map_pmap_enter` = 0x8003d894;
    the "object" the call locked = 0xc0005b48 = a FREED vm_map-zone element (`w04 = 0xbaddecaf` =
    zalloc's ZP_NOT_POISONED free marker; 999's VMHINT991 guard records name the same two objects,
    0xc0005b48 and 0xc0005ab0, and 990 died on the same one at +0x18). 999 survived the same call
    because that element's +8 word was then benign; this boot's DRAM held 0xffb5be0b there.

WHY IT HAPPENS. `vm_map_enter`'s pmap-enter tail (this file, ~:2149) is conditioned
`object != VM_OBJECT_NULL && vm_map_pmap_enter_enable && !anywhere && !needs_copy && size < 128K`
- with NO `is_submap` guard - and on a SUBMAP enter the `object` parameter IS the submap itself
(`:2063 submap = (vm_map_t) object;`, a vm_map-zone element), not a vm_object. The call therefore
hands a vm_map to `vm_map_pmap_enter`, which locks `object + 8` (a struct vm_object's lock) and
reads `object + 0x18` (memq_hint). All four 2050-era trees in this workspace carry the call
verbatim; the SAME function guards the SAME parameter 30 lines later (`:2192 if (!is_submap && ...)`
for the pager block) - the missing guard is that idiom.

THE FIX. Inside the existing tail block, skip the pmap-enter work when `is_submap`, keeping
`pmap_empty = FALSE` (the submap's pages are pmap-nested by the submap block above, and the FALSE
keeps the same reading 999's surviving path produced for every other caller). This is a SEMANTIC
non-change for submaps - the call was only ever entering the SUBMAP's own vm_map as if it were a
vm_object - and it makes the rung independent of what freed DRAM happens to hold. A bounded (max 8)
kprintf beginning `SUBMAPSKIP:` records obj/start/end/size/lr in the skip branch, so the log NAMES
the submap callers and ranges by measurement even though the call is gone.

The edit is wrapped in `#if defined(STAGE90_XNU_SUBMAP_PMAP_SKIP) && STAGE90_XNU_SUBMAP_PMAP_SKIP
... #endif`, so switch OFF compiles the tree's original text byte for byte
(`stage_d13_vmhint_guard.py`'s rule; the `defined()`-first spelling is the wire/probe/geometry
gates' - `mi4-off-option-two-spellings`). The linked-image property is asserted BY VALUE by
`tools/check_submap_pmap_skip.py` (the `SUBMAPSKIP:` string referenced from `vm_map_enter`'s own
body, a kprintf call in the same body, and the still-present `vm_map_pmap_enter` call for the
non-submap path) - the checker refuses the 1000 image.

The tree is a re-provisionable, gitignored checkout; this script is the single source of truth for
the edit. Idempotent: a second run verifies the inserted form and is a no-op.
"""
import sys

# The marker that proves this patch ran.
MARK = "1001: SUBMAP_PMAP_SKIP"

# --- osfmk/vm/vm_map.c -----------------------------------------------------------------------------
# The anchor is the tree's EXACT text (TAB-indented - written as \t escapes so no editor can turn
# them into spaces; the trailing space on the `vm_map_pmap_enter(map, start, end, ` line and the
# TAB before `&&` on the `(!anywhere)` line are part of the tree's bytes and are reproduced
# verbatim). `new` contains `old` around the inserted `#if` blocks, so switch OFF is textually the
# original and compiles identically.
_TAIL_OLD = (
    "\t\tpmap_empty = FALSE; /* pmap won't be empty */\n"
    "\n"
    "\t\tif (override_nx(map, alias) && cur_protection)\n"
    "\t\t        cur_protection |= VM_PROT_EXECUTE;\n"
    "\n"
    "\t\tvm_map_pmap_enter(map, start, end, \n"
    "\t\t\t\t  object, offset, cur_protection);\n"
)
_TAIL_NEW = (
    "\t\tpmap_empty = FALSE; /* pmap won't be empty */\n"
    "\n"
    "#if defined(STAGE90_XNU_SUBMAP_PMAP_SKIP) && STAGE90_XNU_SUBMAP_PMAP_SKIP\n"
    "\t\t/* **1001: SUBMAP_PMAP_SKIP - a submap enter must not enter pmap AS a vm_object.**\n"
    "\t\t * On a SUBMAP enter `object` IS the submap (`~:2063 submap = (vm_map_t) object;`\n"
    "\t\t * - a vm_map-zone element), and this tail used to hand it to vm_map_pmap_enter\n"
    "\t\t * with no `is_submap` guard, which then locked and probed a vm_map as if it were\n"
    "\t\t * a vm_object. 1000's press died exactly there: hw_lock_lock on 0xc0005b48 + 8 (a\n"
    "\t\t * FREED vm_map-zone element, w04 = 0xbaddecaf), 10.31 s in, right after\n"
    "\t\t * `attempting to start init of /sbin/launchd`. 999 survived the same call only\n"
    "\t\t * because that element's +8 word was then benign; 990 died one field over\n"
    "\t\t * (+0x18), which 991's guard caught. The submap's own pages are pmap-nested by\n"
    "\t\t * the submap block above, so skipping is a semantic non-change for submaps;\n"
    "\t\t * pmap_empty stays FALSE (the same reading 999's survival path produced). The\n"
    "\t\t * probe is bounded (max 8) and fail-soft: it names the callers and the ranges\n"
    "\t\t * by measurement. Switch OFF => Apple's text, byte for byte.\n"
    "\t\t * See tools/patch_d13_submap_pmap_skip.py. */\n"
    "\t\tif (!is_submap) {\n"
    "#endif /* defined(STAGE90_XNU_SUBMAP_PMAP_SKIP) && STAGE90_XNU_SUBMAP_PMAP_SKIP */\n"
    "\t\tif (override_nx(map, alias) && cur_protection)\n"
    "\t\t        cur_protection |= VM_PROT_EXECUTE;\n"
    "\n"
    "\t\tvm_map_pmap_enter(map, start, end, \n"
    "\t\t\t\t  object, offset, cur_protection);\n"
    "#if defined(STAGE90_XNU_SUBMAP_PMAP_SKIP) && STAGE90_XNU_SUBMAP_PMAP_SKIP\n"
    "\t\t} else {\n"
    "\t\t\tstatic unsigned int submapskip_reports;\n"
    "\n"
    "\t\t\tif (submapskip_reports < 8u) {\n"
    "\t\t\t\tsubmapskip_reports++;\n"
    "\t\t\t\tkprintf(\"SUBMAPSKIP: obj=%p start=%p end=%p size=%p lr=%p\\n\",\n"
    "\t\t\t\t    (void *)(uintptr_t) object,\n"
    "\t\t\t\t    (void *)(uintptr_t) start,\n"
    "\t\t\t\t    (void *)(uintptr_t) end,\n"
    "\t\t\t\t    (void *)(uintptr_t) size,\n"
    "\t\t\t\t    __builtin_return_address(0));\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "#endif /* defined(STAGE90_XNU_SUBMAP_PMAP_SKIP) && STAGE90_XNU_SUBMAP_PMAP_SKIP */\n"
)

# (relative path, [(old, new), ...]) - one entry per file, all edits listed for that file.
EDITS = (
    ("osfmk/vm/vm_map.c", (
        (_TAIL_OLD, _TAIL_NEW),
    )),
)


def patch_text(text: str, pairs, path: str) -> tuple[str, bool]:
    """Return (new_text, changed). Idempotent, and a marker present is not taken on trust: every
    inserted form must be present exactly once, or this refuses rather than certifying a tree the
    switch does not actually carry (`patch_d13_kextb_login.py`'s rule)."""
    if MARK in text:
        for _old, new in pairs:
            if text.count(new) != 1:
                raise SystemExit(
                    f"patch_d13_submap_pmap_skip: the 1001 marker is present in {path} but an "
                    f"inserted form occurs {text.count(new)} times - moved by a hand edit, or "
                    "half-applied. Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_submap_pmap_skip: anchor in {path} occurs {n} times, not once - the "
                f"tree is not the expected revision, or it was edited by hand. Anchor begins: "
                f"{old[:60]!r}. Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_submap_pmap_skip: an inserted form did not land once in "
                             f"{path} - refusing to write")
    return text, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_submap_pmap_skip.py <XNU_TREE>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    for rel, pairs in EDITS:
        path = root + "/" + rel
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            print(f"patch_d13_submap_pmap_skip: no {path}", file=sys.stderr)
            return 2
        new, changed = patch_text(text, pairs, rel)
        if not changed:
            print(f"patch_d13_submap_pmap_skip: already applied ({path})")
            continue
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)
        print(f"patch_d13_submap_pmap_skip: {rel}'s vm_map_enter tail now skips vm_map_pmap_enter "
              f"when is_submap (pmap_empty stays FALSE; bounded SUBMAPSKIP probe) behind "
              f"STAGE90_XNU_SUBMAP_PMAP_SKIP ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())