#!/bin/bash
# doconf's selection pipeline, bash instead of csh (see tools/xnu_config/README.md).
set -euo pipefail
# The XNU tree to read MASTER files from.
REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MD=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}/config
SYS=$1
echo +$SYS | cat $MD/MASTER $MD/MASTER.arm - $MD/MASTER $MD/MASTER.arm | sed -n \
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
