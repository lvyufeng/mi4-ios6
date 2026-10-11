#!/usr/bin/env bash
#
# Do the `task_init`/`thread_init` dependency edges that `kernel_bootstrap` relies on still exist?
#
# WHY THIS EXISTS.  `kernel_bootstrap` reaches `task_init()` then `thread_init()` then
# `kernel_thread_create(...)` then `load_context(thread)` in that order (see
# `tools/check_bootstrap_ladder.sh`).  That order is not arbitrary - each rung DEFINES state the
# next one READS, and the frontier walk treats "the run reached X" as proof that everything before X
# completed.  If one of these edges moves, a run that reports past a rung no longer implies what the
# walk assumes:
#
#   - task_init      defines `task_zone` (zinit) and, via task_create_internal, assigns the global
#                    `kernel_task`; it also inits `tasks_threads_lock`.
#   - thread_init     defines `thread_zone` (zinit) and calls stack_init / machine_thread_init /
#                     init_thread_ledgers.
#   - kernel_thread_create reads the global `kernel_task` and, through thread_create_internal,
#                     `zalloc(thread_zone)` - so it is only sound AFTER task_init AND thread_init.
#
# What this checks, and what it does NOT:
#
#   - it reads `osfmk/kern/task.c` and `osfmk/kern/thread.c` BY VALUE and asserts, for each rung,
#     that the DEFINITION (write) side and the READ (use) side both exist and are spelled as above;
#   - it asserts `kernel_thread_create` really references `kernel_task`, so the task_init ->
#     kernel_thread_create edge is a real read, not a comment;
#   - it asserts `thread_create_internal` (the function kernel_thread_create calls) really reads
#     `thread_zone`, so the thread_init -> kernel_thread_create edge is real.
#   - it does NOT run the kernel, does NOT disassemble, and does NOT prove the edges are exercised at
#     runtime - it is a SOURCE-shape assertion only (source half: no compiler, no device).
#   - it is deliberately NARROW: it names only edges it can point at a real line for. Rather than
#     assert a lock-order or an init-completeness claim it cannot ground, it stops here.
#
# NOTE ON `pipefail`.  Matching uses here-strings (`<<<"$var"`), never `producer | grep -q`, so a
# first-match `grep -q` cannot SIGPIPE a producer and turn a true fact into a false negative.
#
# Usage: ./check_task_init_deps.sh [--tree <XNU_TREE>] [--selftest]
# Exit 0 when every edge holds; 1 when an edge moved; 2 usage.

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

TREE=${XNU_TREE:-$REPO_ROOT/external/xnu-hd2-darwin13/xnu}
SELFTEST=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --tree) TREE=$2; shift 2 ;;
        --selftest) SELFTEST=1; shift ;;
        *) echo "usage: $0 [--tree <XNU_TREE>] [--selftest]" >&2; exit 2 ;;
    esac
done

TASK_C=$TREE/osfmk/kern/task.c
THREAD_C=$TREE/osfmk/kern/thread.c

refuse() { printf 'check_task_init_deps: %s\n' "$1" >&2; exit 1; }
has() { grep -qE "$2" <<<"$1"; }

_body() {
    awk -v fn="$1" '
        !inb && $0 ~ ("(^|[^A-Za-z0-9_])" fn "\\([^;]*\\)?[[:space:]]*$") { want=1 }
        want && !inb && index($0,"{") { inb=1 }
        inb { print; d=gsub(/\{/,"{"); u=gsub(/\}/,"}"); depth+=d-u; if (depth<=0 && seen) exit; if (depth>0) seen=1 }
    ' "$2" 2>/dev/null || true
}

assert_edges() {
    local taskf=$1 threadf=$2

    [[ -f $taskf   ]] || refuse "no D13 task.c at $taskf"
    [[ -f $threadf ]] || refuse "no D13 thread.c at $threadf"

    local tinit thinit ktc tci
    tinit=$(_body task_init "$taskf")
    [[ -n $tinit ]] || refuse "task_init has no body in $taskf - the rung moved or was renamed"

    # task_init defines task_zone and the kernel task.
    has "$tinit" 'task_zone[[:space:]]*=[[:space:]]*zinit[[:space:]]*\(' \
        || refuse "task_init no longer defines 'task_zone = zinit(' - the zone the walk assumes exists is gone"
    has "$tinit" 'task_create_internal[[:space:]]*\([^;]*kernel_task' \
        || refuse "task_init no longer creates 'kernel_task' via task_create_internal - the global kernel_thread_create reads is unset"
    has "$tinit" 'lck_mtx_init[[:space:]]*\([[:space:]]*&tasks_threads_lock' \
        || refuse "task_init no longer inits 'tasks_threads_lock' - the lock kernel_thread_create unlocks is unset"

    thinit=$(_body thread_init "$threadf")
    [[ -n $thinit ]] || refuse "thread_init has no body in $threadf - the rung moved or was renamed"

    # thread_init defines thread_zone and calls its three init helpers.
    has "$thinit" 'thread_zone[[:space:]]*=[[:space:]]*zinit[[:space:]]*\(' \
        || refuse "thread_init no longer defines 'thread_zone = zinit(' - the zone thread_create_internal allocates from is gone"
    has "$thinit" 'stack_init[[:space:]]*\(' \
        || refuse "thread_init no longer calls stack_init - kernel stacks are not set up before kernel_thread_create"
    has "$thinit" 'machine_thread_init[[:space:]]*\(' \
        || refuse "thread_init no longer calls machine_thread_init - the arm per-thread init is not run"
    has "$thinit" 'init_thread_ledgers[[:space:]]*\(' \
        || refuse "thread_init no longer calls init_thread_ledgers - the thread ledger template is not set"

    ktc=$(_body kernel_thread_create "$threadf")
    [[ -n $ktc ]] || refuse "kernel_thread_create has no body in $threadf - the ladder tail moved or was renamed"

    # kernel_thread_create READS kernel_task (the task_init edge).
    has "$ktc" 'kernel_task' \
        || refuse "kernel_thread_create no longer reads the global 'kernel_task' - the task_init -> kernel_thread_create edge is gone"

    # kernel_thread_create calls thread_create_internal, which READS thread_zone (the thread_init edge).
    has "$ktc" 'thread_create_internal[[:space:]]*\(' \
        || refuse "kernel_thread_create no longer calls thread_create_internal - the edge into the thread zone is gone"
    tci=$(_body thread_create_internal "$threadf")
    [[ -n $tci ]] || refuse "thread_create_internal has no body in $threadf"
    has "$tci" 'zalloc[[:space:]]*\([[:space:]]*thread_zone' \
        || refuse "thread_create_internal no longer zalloc's from 'thread_zone' - the thread_init -> kernel_thread_create edge is gone"
}

if [[ $SELFTEST -eq 1 ]]; then
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT

    # Mutation A: drop the `kernel_task` read from kernel_thread_create -> the task_init edge is
    # severed and must be refused.
    awk '
        /^kernel_thread_create/ { inb=1 }
        inb && /kernel_task/ { print "	thread_t t; task_t task = TASK_NULL;"; next }
        { print }
    ' "$THREAD_C" > "$tmp/thread_no_ktask.c"
    if ( assert_edges "$TASK_C" "$tmp/thread_no_ktask.c" ) >/dev/null 2>&1; then
        refuse "selftest: a kernel_thread_create without the kernel_task read was accepted - the edge test does not bite"
    fi

    # Mutation B: drop stack_init from thread_init -> must be refused.
    awk '
        /^thread_init/ { inb=1 }
        inb && /stack_init[[:space:]]*\(/ { next }
        { print }
    ' "$THREAD_C" > "$tmp/thread_no_stack.c"
    if ( assert_edges "$TASK_C" "$tmp/thread_no_stack.c" ) >/dev/null 2>&1; then
        refuse "selftest: a thread_init without stack_init was accepted - the helper test does not bite"
    fi

    # Mutation C: sever the thread_zone edge in thread_create_internal -> must be refused.
    awk '
        /^thread_create_internal/ { inb=1 }
        inb && /zalloc[[:space:]]*\([[:space:]]*thread_zone/ { sub(/thread_zone/,"some_other_zone"); print; next }
        { print }
    ' "$THREAD_C" > "$tmp/thread_wrong_zone.c"
    if ( assert_edges "$TASK_C" "$tmp/thread_wrong_zone.c" ) >/dev/null 2>&1; then
        refuse "selftest: a thread_create_internal allocating from the wrong zone was accepted - the zone test does not bite"
    fi

    # Sanity: the unmutated tree must PASS.
    if ! ( assert_edges "$TASK_C" "$THREAD_C" ) >/dev/null 2>&1; then
        refuse "selftest: the unmutated tree FAILED the edge check - the check is broken, not the tree"
    fi

    printf 'check_task_init_deps: selftest ok - severed kernel_task/stack_init/thread_zone edges are all refused; the real tree passes\n'
    exit 0
fi

assert_edges "$TASK_C" "$THREAD_C"
printf 'check_task_init_deps: ok - task_init defines task_zone+kernel_task+tasks_threads_lock; thread_init defines thread_zone+stack_init+machine_thread_init+init_thread_ledgers; kernel_thread_create reads kernel_task and zalloc(thread_zone) via thread_create_internal\n'