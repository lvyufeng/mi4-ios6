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
LOCAL=${XNU_MASTER_LOCAL:-}
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
