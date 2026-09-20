#!/bin/bash
# doconf's selection pipeline, bash instead of csh (see tools/xnu_config/README.md).
set -euo pipefail
# The XNU tree to read MASTER files from.
REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MD=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}/config
SYS=$1
# A configuration can also be declared in a local fragment, which is how Apple's own doconf
# documents MASTER.local. The declarations are the `#  NAME = [ ... ]` comment lines, so a fragment
# only has to add one of those. $LOCAL is appended to both halves of the pipeline, which is where
# doconf puts its .local files too.
# **And when the caller names no fragment, the tree's own is found by convention (488).** Before this,
# `XNU_MASTER_LOCAL` was the caller's business: `build_xnu_arm_kernel.sh` takes it as an argument and
# passes it on, and nothing else does - so `make_defines.sh STAGE90_XNU` run by `arm_asm_defines.sh`
# (and therefore the whole ARM assembly layer, and the entry image's own `locore.o`) expanded the
# configuration with **10** `-D` flags instead of 110. Ten is the always-on set: a configuration the
# tree does not declare selects every untagged `options` line and no tagged one, which is a *plausible*
# answer, and it is why the mismatch was invisible (see `expand.sh`'s new refusal, and experiment 488).
# The fragment is `tools/xnu_config/<dir>/<CONFIG>.local` - `boot/` and `minimal/` today - so the
# search is by name rather than by a list of directories that would have to be kept up to date.
LOCAL=${XNU_MASTER_LOCAL:-}
if [[ -z $LOCAL ]]; then
    for _f in "$REPO_ROOT"/tools/xnu_config/*/"$SYS".local; do
        [[ -f $_f ]] && { LOCAL=$_f; break; }
    done
fi
EXTRA=()
[[ -n $LOCAL && -f $LOCAL ]] && EXTRA=("$LOCAL")

echo +$SYS | cat $MD/MASTER $MD/MASTER.arm "${EXTRA[@]}" - $MD/MASTER $MD/MASTER.arm "${EXTRA[@]}" | sed -n \
  -e "/^+/{" \
     -e "s;[-+];#&;gp" \
        -e 't loop' \
     -e ': loop' \
        -e 'n' \
        -e '/^#/b loop' \
        -e '/^$/b loop' \
        -e 's;^\([^#]*\).*#[ 	]*<\(.*\)>[ 	]*$;\2#\1;' \
        -e 't not' \
        -e 's;\([^#]*\).*;#\1;' \
        -e 't not' \
     -e ': not' \
        -e 's;[ 	]*$;;' \
        -e 's;^\!\(.*\);\1#\!;' \
        -e 'p' \
        -e 't loop' \
        -e 'b loop' \
  -e '}' \
  -e "/^[^#]/d" \
  -e 's;	; ;g' \
  -e "s;^# *\([^ ]*\)[ ]*=[ ]*\[\(.*\)\].*;\1#\2;p"
