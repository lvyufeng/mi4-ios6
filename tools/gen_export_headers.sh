#!/usr/bin/env bash
#
# Build Apple's exported-header roots: out/xnu_exports/<component>/...
#
#   ./tools/gen_export_headers.sh            # build the roots
#   ./tools/gen_export_headers.sh --dry-run  # report what would be copied
#
# Why this exists. Each component's build sees the others through a **filtered** view, not through
# their whole source trees. `makedefs/MakeInc.def:463-469`:
#
#   INCFLAGS_IMPORT = $(patsubst %, -I$(OBJROOT)/EXPORT_HDRS/%, $(COMPONENT_IMPORT_LIST))
#   EXPDIR          = EXPORT_HDRS/$(COMPONENT)
#
# and `EXPORT_HDRS/<component>/` is populated from each directory's `EXPORT_MI_LIST` (machine
# independent) and `EXPORT_MD_LIST` (machine dependent), at subdirectory `EXPORT_MI_DIR` and
# `EXPORT_MD_DIR` respectively.
#
# That distinction is not cosmetic and this project measured it: putting `osfmk/libsa`'s *source*
# directory on the include path cost four files, because it also holds a bootloader `string.h` and a
# `sys/` subdirectory that shadow the real ones. Apple exports a selected list. See
# docs/experiments/experiment-117.
#
# How the lists are read: each component `Makefile` defines them as plain Make variables and then
# includes four `MakeInc.*` files that carry the build rules. This script evaluates the variable
# definitions with GNU make against a *stub* SRCROOT whose `makedefs/MakeInc.*` are empty, so the
# rules are no-ops and only the lists come out. Apple's definitions, unmodified.
#
# TRIED AND REJECTED as an include path - measured, three arrangements, all worse than not using it:
#
#     plain source trees only            196 of 401   <- the baseline at the time
#     export roots only                  167
#     export roots first, trees after    185
#     trees first, export roots after    185
#
# (The baseline is 288 of 401 since experiment-118 adopted Apple's *per-component* defines, which is
# the other half of the split this note is about: the export roots were not what was missing.)
#
# The reason is in Apple's own flags: `INCFLAGS_GEN` is `-I$(SRCROOT)/$(COMPONENT)
# -I$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)` - the *own* component's source tree comes first and the
# export root only supplements it, while a consuming component sees imports. A single build with no
# per-component Makefiles cannot reproduce that split, and approximating it by putting the filtered
# view in front of the full tree makes things worse, because the filtered view is smaller.
#
# The tool is kept because it is a faithful reproduction of the mechanism and will matter the
# moment anything *is* built per component. It is not wired into build_xnu_arm_kernel.sh.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
OUT=${XNU_EXPORTS:-$REPO_ROOT/out/xnu_exports}

DRY_RUN=0
[[ ${1:-} == --dry-run ]] && DRY_RUN=1

# The components, in the order MakeInc.def:46 lists them.
COMPONENTS=(osfmk bsd libkern iokit pexpert libsa security san)

# A stub SRCROOT, so `include $(SRCROOT)/makedefs/MakeInc.*` finds empty files instead of Apple's
# build rules. Nothing else in the component Makefiles is read from SRCROOT for the lists.
STUB=$(mktemp -d)
trap 'rm -rf "$STUB"' EXIT
mkdir -p "$STUB/makedefs"
: > "$STUB/makedefs/MakeInc.cmd"
: > "$STUB/makedefs/MakeInc.def"
: > "$STUB/makedefs/MakeInc.rule"
: > "$STUB/makedefs/MakeInc.dir"

cat > "$STUB/dump.mk" <<'EOF'
SRCROOT = $(STUB)
include $(COMPONENT_MK)
dump:
	@echo "MI=$(EXPORT_MI_LIST)"
	@echo "MIDIR=$(EXPORT_MI_DIR)"
	@echo "MD=$(EXPORT_MD_LIST)"
	@echo "MDDIR=$(EXPORT_MD_DIR)"
EOF

# Walk every Makefile under a component and copy what it exports. A component has several - one per
# directory - and each contributes into the same component root at its own EXPORT_*_DIR.
eval_makefile() {
    make -s -f "$STUB/dump.mk" STUB="$STUB" COMPONENT_MK="$1" dump 2>/dev/null
}

total_copied=0
total_missing=0

for component in "${COMPONENTS[@]}"; do
    [[ -d $XNU/$component ]] || continue
    root=$OUT/$component
    copied=0
    missing=0

    while IFS= read -r mk; do
        src_dir=$(dirname "$mk")
        out=$(eval_makefile "$mk")
        [[ -n $out ]] || continue

        for half in MI MD; do
            dir=$(printf '%s\n' "$out" | sed -n "s/^${half}DIR=//p")
            list=$(printf '%s\n' "$out" | sed -n "s/^${half}=//p")
            [[ -n $dir && -n $list ]] || continue
            # The list may contain a glob - iokit's is literally `IOKit/*.h` - so it is expanded
            # relative to the directory the Makefile lives in, not to this script's cwd.
            for f in $(cd "$src_dir" && ls -d $list 2>/dev/null); do
                src="$src_dir/$f"
                dest="$root/$dir/$f"
                if [[ ! -f $src ]]; then
                    # `files.arm`-style entries and generated names land here; the count is reported
                    # rather than hidden, because a silent skip is how a header goes missing later.
                    printf '    absent: %s\n' "${src#$XNU/}"
                    missing=$((missing + 1))
                    total_missing=$((total_missing + 1))
                    continue
                fi
                if [[ $DRY_RUN -eq 0 ]]; then
                    mkdir -p "$(dirname "$dest")"
                    cp "$src" "$dest"
                fi
                copied=$((copied + 1))
                total_copied=$((total_copied + 1))
            done
        done
    done < <(find "$XNU/$component" -maxdepth 3 -name Makefile -not -path "*/conf/*" | sort)

    printf '  %-10s %3d header(s) exported\n' "$component" "$copied"
done

echo
if [[ $DRY_RUN -eq 1 ]]; then
    echo "dry run: $total_copied would be copied, $total_missing listed but absent"
else
    echo "$total_copied header(s) in $OUT, $total_missing listed but absent"
    echo "use it with: -I $OUT/<component> in MakeInc.def's COMPONENT_IMPORT_LIST order"
fi
