#!/usr/bin/env bash
#
# The `-D` flags Apple gives each kernel component, DERIVED from the selected tree's own
# `<component>/conf/Makefile.template` (913).
#
#   ./tools/xnu_config/component_defines.sh bsd        # the flags for a bsd/ source file
#   ./tools/xnu_config/component_defines.sh --table    # component<TAB>flags, for a checker
#
# The tree is `XNU_TREE` (else the modern `external/xnu-4570.1.46`), the same selector the rest of
# the harness uses. This used to be a hand-written shell table — a *transcription* of the templates,
# which is exactly this project's recurring defect (one value, two definitions): if the table drifted
# from Apple's, the build compiled against a flag set Apple's does not use and nothing said so. A
# transcription cannot drift if there is no transcription, and the templates are the source of truth
# Apple's own build reads. `tools/check_component_defines.py` still cross-checks a second, independent
# parsing of the same templates, so a bug in this derivation is still caught.
#
# Why this is per-component and not one global flag set. Apple's build sets these in
# `<component>/conf/Makefile.template`, in the `CFLAGS+=` line each component's generated Makefile
# starts from. The seven `*_KERNEL_PRIVATE` names are exactly the set `MakeInc.def:695`
# (`XNU_PRIVATE_UNIFDEF`) undefines when it builds the *public* SDK headers, which is the
# confirmation that they are per-component switches, not one global one.
#
# It matters because they are not independent. `MACH_KERNEL_PRIVATE` is what makes
# `osfmk/kern/kern_types.h:192` pull in `kern/misc_protos.h`, whose `ffs`/`fls`/`copyinstr`
# declarations collide with `bsd/libkern/libkern.h`'s (reached by `bsd/sys/systm.h:113` in every BSD
# translation unit), so a translation unit that sees both does not compile. Defining
# MACH_KERNEL_PRIVATE for the whole build put the Mach-private view into every BSD file and cost 127
# of the minimal configuration's 205 failures.
#
# `meta_features.h` is stripped: every component force-includes it, and it is the one header Apple's
# build generates and does not ship.

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}

# The parsing is Python because the `CFLAGS+=` blocks use backslash continuations and trailing `#`
# comments; the same logic lives in check_component_defines.py, which is the independent second
# opinion. `-DNAME` and `-DNAME=1` are normalised to the latter (a bare `-D` means 1 to the
# preprocessor), matching the spellings the rest of the build uses.
derive_flags() {
    python3 - "$XNU" "$1" <<'PY'
import os, re, sys
xnu, component = sys.argv[1], sys.argv[2]
path = os.path.join(xnu, component, "conf", "Makefile.template")
if not os.path.isfile(path):
    sys.exit(0)                     # no template (e.g. san) -> no flags, not an error
text = re.sub(r"\\\n", " ", open(path, encoding="utf-8", errors="replace").read())
found = []
for line in text.splitlines():
    s = line.strip()
    if s.startswith("CFLAGS+="):
        s = re.sub(r"\s#.*$", "", s[len("CFLAGS+="):])
        found = [t for t in s.split() if t.startswith("-D")]
        break
out = []
for tok in found:
    body = tok[2:]
    if body.endswith("meta_features.h"):
        continue
    if "=" not in body:
        body += "=1"
    out.append("-D" + body)
# De-duplicate while preserving order (iokit's template includes meta_features.h twice).
seen, uniq = set(), []
for f in out:
    if f not in seen:
        seen.add(f); uniq.append(f)
print(" ".join(uniq))
PY
}

# The set of components is the ones with a `conf/Makefile.template`; that is a property of the tree,
# so it is read from the tree rather than hardcoded, but the ordering is fixed for a stable --table.
COMPONENTS="osfmk bsd libkern iokit pexpert libsa security san"

case "${1:-}" in
    --table)
        for c in $COMPONENTS; do
            printf '%s\t%s\n' "$c" "$(derive_flags "$c")"
        done
        ;;
    "")
        echo "usage: component_defines.sh <component>|--table" >&2
        exit 2
        ;;
    *)
        derive_flags "$1"
        ;;
esac