#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_kernel_source_manifest.py - record and verify the CONTENT of the XNU kernel sources a pool
was built from (the fix for the preflight staleness hole, experiment 980).

THE HOLE THIS CLOSES
--------------------
`scripts/preflight_boot_check.sh` decides whether the payload image is stale by comparing MTIMES of
`src/*.c` against the image.  The D13 kernel objects - `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`,
`..._arm_vm_init.o` and the rest - are compiled by `tools/build_xnu_arm_kernel.sh` from
`external/xnu-hd2-darwin13/xnu/**` and merely LINKED by `src/entry/build_entry.sh`.  The gate has no
test over that tree at all: `grep -n xnu-hd2-darwin13 scripts/preflight_boot_check.sh` finds nothing.
So an edit to `pmap.c` (or `arm_vm_init.c`) that is NOT rebuilt leaves a stale `.o` in the pool; the
entry image links it; and the recorded hash, the entry-source manifest, `SHA256SUMS.txt` and the
revert-set record all agree with EACH OTHER and the gate prints its PASS - the exact blind direction
the freshness sweep's own comment (`preflight_boot_check.sh:233-238`) is about, reached through the
kernel pool instead of `src/`.

WHY CONTENT AND NOT MTIME
-------------------------
An mtime test over the tree would REFUSE a correct arm.  The HFS sources are STAGED into the tree by
`tools/stage_hfs.sh` and then marked (`hfs_vfsops.c` is `git`-modified with 905 B2's guarded
`vfs_clearflags`), and a re-provision or a checkout resets an unchanged file's mtime.  Measured on the
press-ready arm `armed-d13-fa301bd7` this session: `bsd/hfs/hfs_vfsops.c` is 25 minutes newer than its
`.o` yet the pool object and the linked image BOTH contain the staged patch - an mtime test would call
that arm stale and block the press.  So the record is a CONTENT hash, taken at the moment the pool was
built, and the gate re-hashes the same paths.

THE TWO KEYS ARE THE SAME FILE
------------------------------
`<manifest>` is the source list `tools/xnu_config/list_sources.py RELEASE --write` produces
(`$MANIFEST`, `tools/xnu_tree_roots.sh:67`), exactly the loop `tools/build_xnu_arm_kernel.sh:1420`
compiles from.  This tool reads it, so the recorded set is the compiled set by construction - not a
second list that could drift from it (`mi4-one-value-two-definitions`).

Absent-from-tarball sources (the build reports them as `absent`) have no file to hash and are skipped;
the manifest is written AFTER the loop, so a file that was not compiled is not recorded, and a file
that stops existing is a MISSING path at verify time - both are refusals, not passes.

Usage:
  gen_kernel_source_manifest.py <manifest> --write <out.sha256>   # after the pool is built
  gen_kernel_source_manifest.py --verify <out.sha256>             # from the gate; exit 1 on drift
"""

import hashlib
import os
import sys


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_manifest(manifest):
    """The manifest's source paths, in file order, blank/comment lines dropped."""
    out = []
    with open(manifest, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            out.append(line)
    return out


def write_record(manifest, out_path):
    paths = read_manifest(manifest)
    rows = []
    for p in paths:
        # Absent-from-tarball sources are not compiled and have no bytes to record.
        if not os.path.isfile(p):
            continue
        # **986: `.s`/`.S` sources are NOT compiled by this pool either, so they are not recorded.**
        # `tools/build_xnu_arm_kernel.sh`'s compile loop skips every `.s`/`.S` (it is a C/C++ compile
        # loop; the ARM assembly is assembled by `tools/assemble_arm_layer.sh` into a *different* pool,
        # `out/xnu_asm_obj*/`), and `list_sources.py` still lists them because the manifest is the
        # tree's source list. Recording them made an asm-only edit - 986's `hw_lock.s` fix - trip this
        # record and report "the kernel pool is stale" for a pool whose objects never depended on that
        # file. The record's own claim is "the sources THIS POOL WAS BUILT FROM", so it must be the
        # compiled set: same extension filter, one definition ([[mi4-one-value-two-definitions]]).
        if p.endswith((".s", ".S")):
            continue
        rows.append((p, sha256_file(p)))
    rows.sort()
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("# sha256 of every XNU kernel source this pool was built from, by content.\n")
        f.write("# Written by tools/gen_kernel_source_manifest.py after the compile loop of\n")
        f.write("# tools/build_xnu_arm_kernel.sh. Read by scripts/preflight_boot_check.sh, which\n")
        f.write("# re-hashes each path below and refuses on any difference or a missing file: a\n")
        f.write("# stale .o linked into the entry image is the hole this record closes.\n")
        for p, h in rows:
            f.write("%s  %s\n" % (h, p))
    return len(rows)


def verify_record(record_path):
    """Re-hash every recorded path.  Return the list of drift lines (empty == clean)."""
    drift = []
    with open(record_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("  ", 1)
            if len(parts) != 2:
                drift.append("malformed record line: %r" % line)
                continue
            want, path = parts
            if not os.path.isfile(path):
                drift.append("%s  (MISSING - the source the pool was built from is gone)" % path)
                continue
            got = sha256_file(path)
            if got != want:
                drift.append("%s  (content changed: pool hash %s, now %s)" % (path, want[:16], got[:16]))
    return drift


def selftest():
    """Prove the record BITES: a content change refuses, an mtime-only change does not, a
    missing source refuses, and a clean tree verifies.  Uses a throwaway tree under a temp dir."""
    import tempfile
    fails = []

    def ok(label, cond):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            fails.append(label)

    with tempfile.TemporaryDirectory() as d:
        a = os.path.join(d, "a.c")
        b = os.path.join(d, "b.c")
        with open(a, "w") as f:
            f.write("int a;\n")
        with open(b, "w") as f:
            f.write("int b;\n")
        man = os.path.join(d, "manifest.txt")
        with open(man, "w") as f:
            f.write(a + "\n" + b + "\n" + os.path.join(d, "absent.c") + "\n")
        rec = os.path.join(d, "rec.sha256")

        # **986: a `.s`/`.S` source in the manifest is NOT recorded.** This pool skips every assembly
        # file (the ARM layer assembles them into a different pool), so recording one made an
        # asm-only edit refuse a pool whose objects never read that file - the false-stale 986's
        # `hw_lock.s` fix hit. The `.s` here exists on disk, so its absence from the record is the
        # filter and not the absent-file skip.
        s = os.path.join(d, "c.s")
        with open(s, "w") as f:
            f.write("/* asm */\n")
        with open(man, "w") as f:
            f.write(a + "\n" + b + "\n" + s + "\n" + os.path.join(d, "absent.c") + "\n")

        n = write_record(man, rec)
        ok("writes one row per PRESENT compiled source (absent AND .s skipped)", n == 2)
        ok("a clean tree verifies", verify_record(rec) == [])
        with open(rec) as f:
            ok("a present .s source is NOT recorded", s not in f.read())

        # mtime-only change: must NOT drift (the false-stale the content check exists to avoid).
        os.utime(a, (0, 0))
        ok("an mtime-only change does NOT drift", verify_record(rec) == [])

        # content change: MUST drift.
        with open(a, "w") as f:
            f.write("int a = 1;\n")
        ok("a content change DOES drift", len(verify_record(rec)) == 1)
        with open(a, "w") as f:
            f.write("int a;\n")

        # missing source: MUST drift.
        os.remove(b)
        drift = verify_record(rec)
        ok("a MISSING source DOES drift", len(drift) == 1 and "MISSING" in drift[0])

    if fails:
        print("SELFTEST FAILED: " + ", ".join(fails))
        return 1
    print("SELFTEST PASSED: content drift and a missing source refuse; mtime alone does not.")
    return 0


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    if argv[0] == "--selftest":
        return selftest()
    if argv[0] == "--verify":
        if len(argv) != 2:
            sys.stderr.write("usage: gen_kernel_source_manifest.py --verify <record>\n")
            return 2
        if not os.path.isfile(argv[1]):
            # No record is "not recorded", which the caller decides how to read - this tool
            # only answers about a record that exists.
            sys.stderr.write("no record at %s\n" % argv[1])
            return 3
        drift = verify_record(argv[1])
        for d in drift:
            print(d)
        return 1 if drift else 0

    if len(argv) != 3 or argv[1] != "--write":
        sys.stderr.write("usage: gen_kernel_source_manifest.py <manifest> --write <out>\n")
        return 2
    manifest, out_path = argv[0], argv[2]
    if not os.path.isfile(manifest):
        sys.stderr.write("no manifest at %s\n" % manifest)
        return 2
    n = write_record(manifest, out_path)
    print("wrote %s (%d source file(s) hashed)" % (out_path, n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))