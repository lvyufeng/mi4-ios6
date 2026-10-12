#!/usr/bin/env python3
"""Compile the `__LINKEDIT` kext-bootstrap teardown OUT of D13 (988).

WHY. 987's press (arm `armed-d13-2df6333a`, commit `cb2dd66`) is a DECISIVE SUCCESS: the boot went
through the whole HFS mount and ALL of BSD init (`Added memory device md0/rmd0` -> `BSD root: md0` ->
`bsd_init: done`), and then panicked at the FIRST statement AFTER `bsd_init()` returns -
`osfmk/kern/startup.c:408`, `OSKextRemoveKextBootstrap()`. The fault is inside
`OSKext::removeKextBootstrap()` (`libkern/c++/OSKext.cpp:746`):

  * BY VALUE: `pc=0x800b2c90` = `removeKextBootstrap()+0x198`, instruction `e590501c  ldr r5,[r0,#28]`,
    `r0=0`, `far=0x1c`, `fsr=0x5` - a load of offset 28 through a NULL base.
  * The NULL source is `seg_to_remove` dereferenced at `OSKext.cpp:859`,
    `seg_data = (void *) seg_to_remove->vmaddr;` - `vmaddr` is the field at offset 28 of
    `kernel_segment_command_t`. `seg_to_remove` came from `getsegbyname("__LINKEDIT")` at `:843`.
  * Our kernel is a synthetic ELF/Mach-O (`_mh_execute_header` = 0x804e54e4) whose header declares
    **ncmds=3** with segments only `__TEXT`/`__DATA`/`__PRELINK_TEXT` - there is NO `__LINKEDIT`, so
    the lookup returns NULL. The whole `#if CONFIG_KXLD` block is LIVE (CONFIG_KXLD=1) and unguarded;
    Apple and the reference repo carry no guard here. The siblings at `:805` (`__KLD`) and `:838`
    (`__symtab`) ARE guarded (`if (seg_to_remove)`), which is why they do not fault.

THE FIX, BY EDIT. Wrap the whole `#if CONFIG_KXLD ... #else ... #endif` block in
`#ifndef STAGE90_XNU_KEXTB_LOGIN ... #endif`. With `-DSTAGE90_XNU_KEXTB_LOGIN=1` the teardown AND the
`#else`/`#error` alternative are both removed, so nothing dereferences `seg_to_remove` and the
`seg_to_remove = NULL;` below the block stands. With the switch OFF the preprocessor sees Apple's
text and emits Apple's object - the two directives and the marker comment carry no code, so the
4570 build (which never passes the define) is untouched.

The edit is TEXT, in a gitignored tree, so it must be a script a re-checkout can re-apply
(`stage_d13_board.sh`'s rule). Idempotent: a second run is a no-op.

PLACEMENT. The marker comment sits on the line ABOVE the `#ifndef`, never between it and
`#if CONFIG_KXLD`: `tools/check_d13_kextb_login_staged.py` re-derives the property "the `#ifndef`
line is IMMEDIATELY before `#if CONFIG_KXLD`, and the guard's `#endif` IMMEDIATELY after
`#endif /* CONFIG_KXLD */`" - a comment in between would make that property false while the guard
still looked present ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
"""
import sys

# The guard's two lines, and the marker comment that proves this patch already ran. One definition
# each: the insert text, the already-applied re-check and the post-write sanity check all read these,
# so they cannot disagree about what the guard is.
MARK = "988: KEXTB_LOGIN"
GUARD_LINE = "#ifndef STAGE90_XNU_KEXTB_LOGIN"
GUARD_END_LINE = "#endif /* STAGE90_XNU_KEXTB_LOGIN */"

# The two anchors, each verified UNIQUE in the tree this patches (counted once each in the D13
# OSKext.cpp; `tools/check_d13_kextb_login_staged.py` re-derives the resulting order/adjacency, it
# does not re-count these). The `#if CONFIG_KXLD` line exists in 4570's OSKext.cpp too, so the patcher
# never guesses which tree it is in: it is only ever run at the D13 tree by `stage_d13_kextb_login.sh`.
_OPEN_ANCHOR = "#if CONFIG_KXLD\n"
_CLOSE_ANCHOR = "#endif /* CONFIG_KXLD */\n"

# The inserted opening. The marker comment is ABOVE the guard line so the guard stays IMMEDIATELY
# before `#if CONFIG_KXLD` (the adjacency the checker re-derives). The `#else`/`#error` alternative is
# inside the wrapped region and so disappears with the switch on.
_OPEN_NEW = (
    "    /* **988: KEXTB_LOGIN - the __LINKEDIT kext-bootstrap teardown is compiled OUT on D13.**\n"
    "     * The block below dereferences `seg_to_remove` unchecked (`seg_data = (void *)\n"
    "     * seg_to_remove->vmaddr;`), and `seg_to_remove` came from getsegbyname(\"__LINKEDIT\") - a\n"
    "     * segment our synthetic Mach-O (ncmds=3: __TEXT/__DATA/__PRELINK_TEXT) does not carry, so\n"
    "     * the lookup is NULL and the boot panics at the first statement after `bsd_init()`\n"
    "     * (`startup.c:408` OSKextRemoveKextBootstrap). Apple and the reference repo carry no guard\n"
    "     * here; the __KLD and __symtab siblings ARE guarded. With -DSTAGE90_XNU_KEXTB_LOGIN=1 the\n"
    "     * whole `#if CONFIG_KXLD ... #else ... #endif` below (teardown AND `#error` alternative) is\n"
    "     * removed, so nothing touches `seg_to_remove` and its `= NULL` assignment stands. Switch\n"
    "     * OFF => Apple's object, byte for byte. */\n"
    + GUARD_LINE + "\n"
    + _OPEN_ANCHOR
)
_CLOSE_NEW = _CLOSE_ANCHOR + GUARD_END_LINE + "\n"


def _guard_is_present_and_wraps(lines) -> bool:
    """True iff GUARD_LINE is IMMEDIATELY before `#if CONFIG_KXLD` and GUARD_END_LINE IMMEDIATELY
    after `#endif /* CONFIG_KXLD */` - the property, not the presence. Shared by the already-applied
    re-check and the post-write sanity check so they cannot drift apart."""
    def at(anchor):
        return lines.index(anchor) if lines.count(anchor) == 1 else -1

    ifn = at(GUARD_LINE)
    eok = at("#endif /* CONFIG_KXLD */")
    if ifn < 0 or eok < 0:
        return False
    return lines[ifn + 1] == "#if CONFIG_KXLD" and lines[eok + 1] == GUARD_END_LINE


def patch_text(text: str) -> tuple[str, bool]:
    """Return (new_text, changed). Idempotent.

    A marker already present does NOT mean "leave it alone": a hand edit (or a half-applied patch)
    could leave the guard present but no longer wrapping the `#if CONFIG_KXLD` block, and returning
    "already applied" would then certify a tree the 988 switch does not protect. The checker catches
    that class, but the stager must not claim success either (`hfs_patch_root_rw.py`'s rule).
    """
    if MARK in text:
        if not _guard_is_present_and_wraps(text.split("\n")):
            raise SystemExit(
                "patch_d13_kextb_login: the 988 marker is present but the guard no longer wraps "
                "`#if CONFIG_KXLD` (moved by a hand edit, or half-applied). Refusing rather than "
                "reporting a patch the switch does not actually carry."
            )
        return text, False
    if text.count(_OPEN_ANCHOR) != 1:
        raise SystemExit(
            "patch_d13_kextb_login: `#if CONFIG_KXLD` is not present exactly once - the tree is not "
            "the expected revision, or it was edited by hand. Refusing."
        )
    if text.count(_CLOSE_ANCHOR) != 1:
        raise SystemExit(
            "patch_d13_kextb_login: `#endif /* CONFIG_KXLD */` is not present exactly once - cannot "
            "pin the end of the block to guard. Refusing."
        )
    new = text.replace(_OPEN_ANCHOR, _OPEN_NEW, 1)
    new = new.replace(_CLOSE_ANCHOR, _CLOSE_NEW, 1)
    return new, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_kextb_login.py <XNU_TREE>", file=sys.stderr)
        return 2
    path = sys.argv[1] + "/libkern/c++/OSKext.cpp"
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print(f"patch_d13_kextb_login: no {path}", file=sys.stderr)
        return 2

    new, changed = patch_text(text)
    if not changed:
        print(f"patch_d13_kextb_login: already applied ({path})")
        return 0

    # Sanity by value, before writing: the guard must have landed wrapping the block.
    if not _guard_is_present_and_wraps(new.split("\n")):
        raise SystemExit("patch_d13_kextb_login: the guard did not land wrapping the `#if CONFIG_KXLD` "
                         "block - refusing to write")

    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"patch_d13_kextb_login: the CONFIG_KXLD __LINKEDIT teardown is now guarded by "
          f"STAGE90_XNU_KEXTB_LOGIN ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())