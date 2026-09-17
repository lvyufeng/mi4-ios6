#!/usr/bin/env bash
#
# Build Apple's MIG (the Mach Interface Generator) on this Linux host.
#
#   ./tools/build_mig.sh            # fetch and build into out/mig/
#   ./tools/build_mig.sh --verbose
#
# Why this exists. Phase 4's wall was described in this project's notes as "the build configuration
# is absent", and the sharpest component of it was that XNU's kernel sources include MIG-generated
# headers — `<mach/mach_host.h>`, `<mach/mach_port.h>` — which do not exist in the tarball because
# the build generates them from the `.defs` files, 40 of which *are* published. The notes said MIG
# "is not in the tarball and not in this host's package repository", and both halves of that are
# true; what they missed is that **it is published elsewhere and fetchable**:
#
#   apple-oss-distributions/bootstrap_cmds, directory migcom.tproj
#
# That is the real MIG — `parser.y`, `lexxer.l`, ~550 KB of C — not a reimplementation. This script
# builds it, which turns "the generated headers are unavailable" into "the generated headers are a
# command".
#
# Building it needs one non-obvious thing: MIG is a *host* program that reads *Darwin* headers
# (`<mach/message.h>` and friends). Compiling those against glibc collides, because Darwin's
# `sys/cdefs.h` and glibc's disagree about `__THROW` and half a dozen other macros, and Darwin's
# `bsd/sys/types.h` drags in a `_pthread` directory that is not in the tarball. So `tools/mig_host/`
# carries a small set of mach headers written for host compilation, with every constant copied from
# the 4570 tree rather than invented.
#
# What this does NOT do: it does not verify that MIG's *output* matches the 4570 ABI. MIG reads the
# `.defs` and emits what they say, so the content is driven by 4570's own files; what a different
# MIG version could change is the *style* of the output. The measurement that settles it is
# compiling 4570 sources against the result — see tools/gen_mach_headers.sh.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)
OUT=${MIG_OUT:-$REPO_ROOT/out/mig}
SRC=${MIG_SRC:-$OUT/bootstrap_cmds}
JOBS=${JOBS:-$(nproc)}

VERBOSE=0
[[ ${1:-} == --verbose ]] && VERBOSE=1

say() { printf '%s\n' "$*"; }
run() { [[ $VERBOSE -eq 1 ]] && printf '  %s\n' "$*"; "$@"; }

for tool in gcc bison flex git; do
    command -v "$tool" >/dev/null 2>&1 || {
        say "missing build tool: $tool (apt-get install $tool)" >&2; exit 2; }
done

mkdir -p "$OUT"

say "== fetching bootstrap_cmds (migcom.tproj) =="
if [[ -d $SRC/migcom.tproj ]]; then
    say "  already present: $SRC/migcom.tproj"
else
    run git clone --depth 1 --filter=blob:none --sparse \
        https://github.com/apple-oss-distributions/bootstrap_cmds "$SRC"
    run git -C "$SRC" sparse-checkout set migcom.tproj
fi

BUILD=$OUT/build
rm -rf "$BUILD" && mkdir -p "$BUILD"
cp "$SRC"/migcom.tproj/*.c "$SRC"/migcom.tproj/*.h "$SRC"/migcom.tproj/*.l "$SRC"/migcom.tproj/*.y "$BUILD/"

cd "$BUILD"

say "== generating the parser and lexer =="
# -y makes bison emit yacc-compatible names. lexxer.l asks for y.tab.h, which -o parser.c does not
# produce, so it is linked to parser.h rather than left to fail.
run bison -y -d -o parser.c parser.y
ln -sf parser.h y.tab.h
run flex -o lexxer.c lexxer.l

say "== compiling =="
# -DMIG_VERSION: the real build sets it; mig.sh does not, so it comes from the build system that is
# not public. The value only ever appears in a "--version" line and in a comment MIG writes into
# each generated file, so it is a label, not a behaviour.
# -D__private_extern__=: a Darwin storage class with no glibc equivalent.
# -w: MIG is 1990s C and trips modern warnings by the hundred.
FLAGS=(
    -I"$TOOLS_DIR/mig_host" -I.
    -DMIG_VERSION='"mig-4570-host"'
    -D__private_extern__=
    -w -O2
)

# handler.c is excluded, and deliberately. It carries a second, older `WriteIncludes(FILE *)` that
# conflicts with write.h's `WriteIncludes(FILE *, boolean_t, boolean_t)` — legacy code left in the
# tree. Nothing references the older one; excluding the file removes the conflict without editing
# Apple's source. If a future MIG revision makes handler.c load-bearing, the link will fail loudly.
SOURCES=(error global header lexxer mig parser routine server statement string type user utils)

objects=()
for src in "${SOURCES[@]}"; do
    run gcc "${FLAGS[@]}" -c "$src.c" -o "$src.o"
    objects+=("$src.o")
done

run gcc -o migcom "${objects[@]}"
ln -sf migcom mig

say
say "migcom: $BUILD/migcom"
if [[ $VERBOSE -eq 1 ]]; then "$BUILD/mig" -version || true; fi
say "next: ./tools/gen_mach_headers.sh"
