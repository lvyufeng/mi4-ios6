#!/usr/bin/env python3
"""Assert BY VALUE that the D13 tree's `hfs_mountroot` clears `MNT_RDONLY` before it calls `hfs_mountfs`
(rung 905 B2, pool switch `STAGE90_HFS_ROOT_RW`), and - when a linked image is given - that the built
`hfs_mountroot` in it actually CALLS `vfs_clearflags`.

THE PROPERTY.  `vfs_rootmountalloc_internal` hard-codes `mp->mnt_flag = MNT_RDONLY | MNT_ROOTFS`, and
`hfs_mountfs` latches it ONCE from `vfs_isrdonly(mp)`.  The one edit between those two that makes the
root mount read-write is a guarded `vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);` inside `hfs_mountroot`,
immediately BEFORE its root `hfs_mountfs(rvp, mp, NULL, 0, context)` call.

THE TEXT CHECK IS STRUCTURAL, NOT A GREP FOR THE MARKER.  It extracts the body of `hfs_mountroot` (from
its definition to its matching close brace, by brace counting - so the enclosing function of an anchor
is decided by structure, not by grep) and asserts:
  (1) the root `hfs_mountfs(rvp, mp, NULL, 0, context)` call is present in that body, exactly once;
  (2) the ONLY `vfs_clearflags(mp, ...MNT_RDONLY...)` in the WHOLE file lies inside that body;
  (3) it precedes the call;
  (4) it is GUARDED by `STAGE90_HFS_ROOT_RW` - and the guard's region does NOT extend past the anchor
      (so the `hfs_mountfs` call is never compiled out, only the clear is) - bounded by this script's
      markers OR by a `#endif` on the way to the anchor.

WHY THIS DISCRIMINATES.  On the UN-STAGED file there is no `vfs_clearflags(mp, ...MNT_RDONLY...)`
anywhere (the file's other three `vfs_clearflags` clear `MNT_UNKNOWNPERMISSIONS` / `MNT_JOURNALED`), so
(2) fails - the check is FALSE on a read-only tree.  It is TRUE only after the clear is inserted before
the root call.

THE IMAGE CHECK (optional `<elf>`).  In the linked `xnu_arm_entry.elf`, `hfs_mountroot` must have a
`bl` to `<vfs_clearflags>` within its own body (read by symbol, by value) - because the pool object a
record names could predate the clear, and `mi4-a-claim-in-a-comment-is-not-a-check` says read the
LINKED artifact.  This is the host-side sibling of `build_entry.sh`'s own after-link clause; it does not
replace it.

Exit 0 iff every check passes.
"""
import os
import re
import subprocess
import sys

SWITCH = "STAGE90_HFS_ROOT_RW"
ANCHOR = "hfs_mountfs(rvp, mp, NULL, 0, context)"
# The clear this patch inserts: `vfs_clearflags(mp, (u_int64_t)MNT_RDONLY);`.  Matched on the
# MNT_RDONLY token so the file's OWN `vfs_clearflags(mp, (u_int64_t)((unsigned int)MNT_UNKNOWNPERMISSIONS))`
# and `vfs_clearflags(hfsmp->hfs_mp, (u_int64_t)((unsigned int)MNT_JOURNALED))` do NOT match.
CLEAR = re.compile(r"vfs_clearflags\s*\(\s*mp\s*,[^;]*\bMNT_RDONLY\b[^;]*\)\s*;")
IDENTITY = "int\nhfs_mountroot(mount_t mp, vnode_t rvp, vfs_context_t context)"
MARK = "IOS7HFSRW_BEGIN"
_END = "IOS7HFSRW_END"

DEFAULT_TREE = "external/xnu-hd2-darwin13/xnu"
REL = "bsd/hfs/hfs_vfsops.c"
LIVE_ELF = "out/stage90/xnu_arm_entry.elf"
OBJDUMP = "arm-none-eabi-objdump"


def find_body(text, start):
    """The substring of `text` from the FIRST `{` at or after `start` to its matching close brace
    (brace counting over the source).  Returns (body, body_start, end) or (None, None, None) if
    unbalanced.  `body_start` is the ABSOLUTE index of the opening brace, so an offset within `body`
    is converted to an absolute index by `body_start + off` - not by the function-name's start."""
    i = text.find("{", start)
    if i < 0:
        return None, None, None
    depth, j = 1, i + 1
    while j < len(text) and depth:
        c = text[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        j += 1
    if depth:
        return None, None, None
    return text[i:j], i, j


def check_text(path):
    if not os.path.isfile(path):
        print(f"  FAIL no {path} - the tree is not provisioned, nothing to check")
        return False
    text = open(path, encoding="utf-8", errors="surrogateescape").read()
    ok = True

    at = text.find(IDENTITY)
    if at < 0:
        print(f"  FAIL no `hfs_mountroot` definition in {path}")
        return False
    body, body_at, _ = find_body(text, at)
    if body is None:
        print(f"  FAIL could not bound `hfs_mountroot`'s body (unbalanced braces)")
        return False
    body_end = body_at + len(body)

    # (1) the ROOT call, exactly once, inside the body.
    n_call = body.count(ANCHOR)
    if n_call != 1:
        print(f"  FAIL the root `hfs_mountfs(rvp, mp, NULL, 0, context)` call occurs {n_call}x in "
              f"hfs_mountroot's body (expected 1)")
        return False
    call_at = body_at + body.index(ANCHOR)
    print(f"  ok   hfs_mountroot's body carries the root `hfs_mountfs(rvp, mp, NULL, 0, context)` call "
          f"once (at {call_at})")

    # (2) the ONLY rw-clear in the file is inside this body.
    clears = [(m.start(), m.group(0)) for m in CLEAR.finditer(text)]
    if len(clears) != 1:
        print(f"  FAIL the file carries {len(clears)} `vfs_clearflags(mp, ...MNT_RDONLY...)` clear(s), "
              f"expected exactly 1 - the root does NOT clear MNT_RDONLY (this is a read-only tree)"
              + (f" [{', '.join(c for _, c in clears)}]" if clears else ""))
        return False
    c_at = clears[0][0]
    if not (body_at <= c_at < body_end):
        print(f"  FAIL the MNT_RDONLY clear at {c_at} is NOT inside hfs_mountroot's body "
              f"[{body_at}, {body_end})")
        return False
    print(f"  ok   the ONLY `vfs_clearflags(mp, ...MNT_RDONLY...)` in the file (at {c_at}) is inside "
          f"hfs_mountroot")

    # (3) BEFORE the call.
    if not (c_at < call_at):
        print(f"  FAIL the clear ({c_at}) does NOT precede the root hfs_mountfs call ({call_at}) - "
              f"hfs_mountfs would latch HFS_READ_ONLY from the stale flag")
        return False
    print(f"  ok   the clear precedes the root call ({c_at} < {call_at}) - hfs_mountfs latches the "
          f"CLEARED flag")

    # (4) GUARDED by the switch, and the guard's `#endif` falls BEFORE the root call (so only the
    # clear is conditional; the `hfs_mountfs` call is always compiled).
    win_start = max(body_at, c_at - 600)
    pre = text[win_start:c_at]
    guards = [m for m in re.finditer(rf"^[ \t]*#[ \t]*if(?:def)?[ \t]+(?:defined\s*\(\s*)?{SWITCH}\b",
                                     pre, re.M)]
    if not guards:
        print(f"  FAIL the clear is NOT guarded by `{SWITCH}` - an unguarded clear would move every "
              f"D13 arm's HFS object off the port-off bytes")
        return False
    guard_end = win_start + guards[-1].end()
    if not re.search(r"^[ \t]*#[ \t]*endif\b", text[guard_end:call_at], re.M):
        print(f"  FAIL the `{SWITCH}` guard's region has no `#endif` between the clear and the root "
              f"call - the hfs_mountfs CALL itself would be compiled out")
        return False
    print(f"  ok   the clear is guarded by `{SWITCH}` and the guard's `#endif` sits before the root "
          f"call (the call is always compiled)")

    # (4b) marker-bounded-ness is stated, but a legacy marker-less guarded form is accepted too.
    if MARK in text or _END in text:
        if text.count(MARK) != 1 or text.count(_END) != 1:
            print(f"  FAIL the {MARK}/{_END} markers are not a single matched pair "
                  f"({text.count(MARK)}/{text.count(_END)})")
            return False
        print(f"  ok   the clear is marker-bounded ({MARK}../{_END})")
    else:
        print(f"  note the clear carries no {MARK} markers (legacy marker-less guarded form) - "
              f"accepted, since the guard check above is by structure")
    return ok


def _func_bodies(path):
    funcs, cur = {}, None
    lab = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
    ins = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\t([a-z0-9.]+)\s*(.*)$")
    for line in subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True, check=True).stdout.splitlines():
        m = lab.match(line)
        if m:
            if not m.group(2).startswith("."):
                cur = m.group(2)
                funcs.setdefault(cur, [])
            continue
        m = ins.match(line)
        if m and cur is not None:
            funcs[cur].append((int(m.group(1), 16), m.group(2), m.group(3).split(";")[0].strip()))
        elif line and not line.startswith((" ", "\t")) and ":" not in line:
            cur = None
    return funcs


def check_elf(path):
    if not os.path.isfile(path):
        print(f"  note no {path} - the linked-image clause is skipped (nothing to read)")
        return True
    try:
        funcs = _func_bodies(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP} could not read {path}: {exc}")
        return False
    body = funcs.get("hfs_mountroot")
    if body is None:
        print(f"  FAIL no `hfs_mountroot` body in {path} - the HFS port is not linked")
        return False
    calls = [f"{va:#010x}" for va, mn, ops in body
             if mn in ("bl", "blx") and "vfs_clearflags" in ops.split("<", 1)[-1]]
    if not calls:
        print(f"  FAIL the linked hfs_mountroot ({path}) does NOT call <vfs_clearflags> - the pool "
              f"object predates the clear or was built without -D{SWITCH}=1")
        return False
    print(f"  ok   the linked hfs_mountroot calls <vfs_clearflags> (bl at {', '.join(calls)})")
    return True


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    tree = argv[0] if len(argv) > 0 else DEFAULT_TREE
    elf = argv[1] if len(argv) > 1 else None
    print(f"check_d13_hfs_rw_root: {tree}/{REL}")
    ok = check_text(os.path.join(tree, REL))
    if elf is not None:
        ok = check_elf(elf) and ok
    print(f"check_d13_hfs_rw_root: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())