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
# A local fragment can declare extra configurations; see minimal/STAGE90_BOOT.local.
export XNU_MASTER_LOCAL=${XNU_MASTER_LOCAL:-}

"$HERE/expand.sh" "${1:?usage: make_defines.sh CONFIG}" \
    | awk '
        # Apple*s grammar is `Opt_list: Opt_list COMMA Option` (parser.y:437-438), so a top-level
        # comma separates **two options**, not one value: `options TIMEZONE=0, PST=0` (`bsd/conf/
        # MASTER:81`) is `opt[]` with two entries, and mkmakefile.c:269-272 writes them as two
        # flags, `-DTIMEZONE=0 -DPST=0`. Emitting one `-DTIMEZONE=0, PST=0` made `param.c:85`*s
        # `struct timezone tz = { TIMEZONE, PST }` expand to `{ 0, PST=0 }` with `PST` undeclared -
        # a compile error on Darwin-13 and silent on 4570, which has no such line. A comma **inside
        # double quotes** is a value*s own (`KAUTH_CRED_PRIMES="{5, 17, 97}"` is one option), so the
        # split tracks quote state rather than splitting on every comma.
        function split_unquoted(s, arr,    i, m, c, q, cur, n) {
            m = length(s); q = 0; cur = ""; n = 0
            for (i = 1; i <= m; i++) {
                c = substr(s, i, 1)
                if (c == "\"") { q = 1 - q; cur = cur c; continue }
                if (c == "," && q == 0) { arr[++n] = cur; cur = ""; continue }
                cur = cur c
            }
            arr[++n] = cur
            return n
        }
        $1 == "options" {
            # Everything after `options`, not the last field - and this was a real bug rather than
            # a style choice. `options CONFIG_NMBCLUSTERS="((1024 * 256) / MCLBYTES)"` has a value
            # containing spaces, so taking $NF gave `MCLBYTES)"`, which stripped to `MCLBYTES)` and
            # became -DMCLBYTES)=1: one of Apple*s genuine macros redefined to garbage, which then
            # corrupted every use of it. It cost a 45-minute clang hang on bsd/netinet/ip_input.c
            # before anyone looked, because the build script had no per-file timeout either.
            name = $0
            sub(/^[[:space:]]*options[[:space:]]+/, "", name)
            # Strip a trailing comment if one survived.
            sub(/[[:space:]]*#.*$/, "", name)
            # Trim any trailing whitespace.
            sub(/[[:space:]]+$/, "", name)
            if (name == "") next
            n = split_unquoted(name, parts)
            for (k = 1; k <= n; k++) {
                s = parts[k]
                sub(/^[[:space:]]+/, "", s)
                sub(/[[:space:]]+$/, "", s)
                if (s == "") continue
                # NAME="expr" -> NAME=expr. The quotes are the config tool*s, not the compiler*s.
                gsub(/"/, "", s)
                # A value may still contain spaces (`((1024 * 256) / MCLBYTES)`); the shell keeps it
                # as one word because make_defines.sh emits one define per line, one per option.
                if (index(s, "=") > 0) print "-D" s
                else                   print "-D" s "=1"
            }
        }
    ' | sort -u
