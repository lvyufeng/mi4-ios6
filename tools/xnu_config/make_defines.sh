#!/bin/bash
#
# Turn an expanded MASTER configuration into the -D flag list a compile uses.
#
#   ./tools/xnu_config/make_defines.sh RELEASE          # the ARM RELEASE kernel's options
#   ./tools/xnu_config/make_defines.sh DEVELOPMENT      # the same, with the dev attributes
#
# This is the step that replaces guessing. `expand.sh` produces the option lines Apple's own
# configuration selects; this turns them into what a compiler needs, and separates the three kinds
# that look alike in MASTER but are not:
#
#   options  NAME            -> -DNAME=1        a feature switch
#   options  NAME=VALUE      -> -DNAME=VALUE    a value
#   options  NAME="expr"     -> -DNAME=expr     a computed value (quotes stripped)
#
# `pseudo-device` and `machine`/`makeoptions` lines are dropped: they configure the old BSD config
# tool's device tables, not the preprocessor.
#
# What this does NOT do: produce the whole build. A real build also needs `MKHEADERS` output and the
# per-target `*.objects` manifests, which is what `SETUP/config`'s own binary does. This is the
# preprocessor half, which is the half that decides whether a source file compiles.

set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)

"$HERE/expand.sh" "${1:?usage: make_defines.sh CONFIG}" \
    | awk '
        $1 == "options" {
            # The option name is the last field on the line, and it may carry a value.
            name = $NF
            # Strip a trailing comment if one survived.
            sub(/[[:space:]]*#.*$/, "", name)
            if (name == "") next
            # NAME="expr" -> NAME=expr. The quotes are the config tool'"'"'s, not the compiler'"'"'s.
            gsub(/"/, "", name)
            if (index(name, "=") > 0) print "-D" name
            else                      print "-D" name "=1"
        }
    ' | sort -u
