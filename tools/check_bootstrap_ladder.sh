#!/usr/bin/env bash
#
# Is `kernel_bootstrap`'s call ladder still the one the bring-up's frontier model assumes?
#
# WHY THIS EXISTS.  Every step of the D13 entry walk predicts "the run stops at X, whose caller is
# the `bl X` inside `kernel_bootstrap`".  That whole method is only valid while `kernel_bootstrap`'s
# ORDER is unchanged: `ipc_bootstrap -> ipc_init -> PMAP_ACTIVATE_KERNEL -> machine_init ->
# clock_init -> ledger_init -> task_init -> thread_init -> kernel_thread_create -> load_context`.  A
# re-vendored or patched `startup.c` that reorders or renames one rung silently invalidates the
# caller-address of the next stop, which is the single most expensive class of error in this walk
# (a prediction read off the wrong frame).  This script rereads the ladder from the tree and refuses
# a reorder/rename.
#
# What this checks, and what it does NOT:
#
#   - it reads `osfmk/kern/startup.c`'s `kernel_bootstrap` body BY VALUE and asserts each named
#     rung is present, and that the rungs appear in the expected ORDER (by line number);
#   - it asserts the macro step `PMAP_ACTIVATE_KERNEL(master_cpu)` is still spelled that way (the
#     ladder's only macro invocation, so a rename to a helper is caught);
#   - and it asserts the ladder ENDS at `load_context(thread)` and that `kernel_thread_create`
#     precedes it, because the 973/975 non-return is explained by exactly that tail.
#   - it does NOT disassemble the image, does NOT check the objects linked, and does NOT prove the
#     rungs complete at runtime - it is a SOURCE-order assertion only (source half: no compiler, no
#     device).
#
# NOTE ON `pipefail`.  Matching uses here-strings (`<<<"$var"`), never `producer | grep -q`, so a
# first-match `grep -q` cannot SIGPIPE a producer and turn a true fact into a false negative.
#
# Usage: ./check_bootstrap_ladder.sh [--tree <XNU_TREE>] [--selftest]
# Exit 0 when the ladder and its order hold; 1 when a rung moved; 2 usage.

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

STARTUP=$TREE/osfmk/kern/startup.c

refuse() { printf 'check_bootstrap_ladder: %s\n' "$1" >&2; exit 1; }

# The ladder, in order. Each entry is "call-text-label|regex". A regex matches a whole statement
# whose call token is the label. `PMAP_ACTIVATE_KERNEL` is the macro rung.
LADDER=(
    "ipc_bootstrap|ipc_bootstrap[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "ipc_init|ipc_init[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "PMAP_ACTIVATE_KERNEL|PMAP_ACTIVATE_KERNEL[[:space:]]*\([[:space:]]*master_cpu[[:space:]]*\)[[:space:]]*;"
    "machine_init|machine_init[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "clock_init|clock_init[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "ledger_init|ledger_init[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "task_init|task_init[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "thread_init|thread_init[[:space:]]*\([[:space:]]*\)[[:space:]]*;"
    "kernel_thread_create|kernel_thread_create[[:space:]]*\("
    "load_context|load_context[[:space:]]*\([[:space:]]*thread[[:space:]]*\)[[:space:]]*;"
)

check_ladder() {
    [[ -f $STARTUP ]] || refuse "no D13 startup.c at $STARTUP"

    # The kernel_bootstrap body: brace-counted from the `kernel_bootstrap(void)` header.
    local body
    body=$(awk '
        !inb && /kernel_bootstrap\(void\)/ { want=1 }
        want && !inb && index($0,"{") { inb=1 }
        inb { print; d=gsub(/\{/,"{"); u=gsub(/\}/,"}"); depth+=d-u; if (depth<=0 && seen) exit; if (depth>0) seen=1 }
    ' "$STARTUP" || true)
    [[ -n $body ]] || refuse "kernel_bootstrap has no body in $STARTUP - the entry to the ladder moved or was renamed"

    # Every rung present.
    local entry label re
    for entry in "${LADDER[@]}"; do
        label=${entry%%|*}; re=${entry#*|}
        grep -qE "$re" <<<"$body" \
            || refuse "kernel_bootstrap no longer contains the ladder rung '$label' - a rename or removal invalidates the frontier's caller addresses"
    done

    # Rungs in order (by first matching line).
    local prev=0 ln
    for entry in "${LADDER[@]}"; do
        label=${entry%%|*}; re=${entry#*|}
        ln=$(grep -nE "$re" <<<"$body" | head -1 | cut -d: -f1)
        [[ -n $ln ]] || refuse "could not locate the ladder rung '$label'"
        [[ $ln -gt $prev ]] \
            || refuse "kernel_bootstrap's ladder is OUT OF ORDER at '$label' (line $ln, previous rung line $prev) - the frontier model's callers are now wrong"
        prev=$ln
    done

    # The ladder must end at load_context, not somewhere else.
    local last
    last=$(grep -nE 'load_context[[:space:]]*\(' <<<"$body" | tail -1 | cut -d: -f1)
    local kt
    kt=$(grep -nE 'kernel_thread_create[[:space:]]*\(' <<<"$body" | tail -1 | cut -d: -f1)
    [[ -n $kt && -n $last && $kt -lt $last ]] \
        || refuse "kernel_bootstrap does not reach load_context after kernel_thread_create - the ladder tail (the step the 973/975 non-return lives on) moved"

    printf 'check_bootstrap_ladder: ok - %d rungs present and in order, ipc_bootstrap -> ... -> load_context\n' "${#LADDER[@]}"
}

if [[ $SELFTEST -eq 1 ]]; then
    # Mutation 1: swap machine_init and clock_init -> the order test must refuse.
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$tmp/osfmk/kern"
    # A minimal startup.c: swap the two rungs.
    cat > "$tmp/osfmk/kern/startup.c" <<'EOF'
kernel_bootstrap(void)
{
	ipc_bootstrap();
	ipc_init();
	PMAP_ACTIVATE_KERNEL(master_cpu);
	clock_init();
	machine_init();
	ledger_init();
	task_init();
	thread_init();
	kernel_thread_create((thread_continue_t)kernel_bootstrap_thread, NULL, MAXPRI_KERNEL, &thread);
	load_context(thread);
}
EOF
    if "$0" --tree "$tmp" >/dev/null 2>&1; then
        refuse "selftest: a ladder with clock_init BEFORE machine_init was accepted - the order test does not bite"
    fi
    # Mutation 2: drop a rung -> the presence test must refuse.
    cat > "$tmp/osfmk/kern/startup.c" <<'EOF'
kernel_bootstrap(void)
{
	ipc_bootstrap();
	ipc_init();
	PMAP_ACTIVATE_KERNEL(master_cpu);
	machine_init();
	clock_init();
	ledger_init();
	task_init();
	thread_init();
	kernel_thread_create((thread_continue_t)kernel_bootstrap_thread, NULL, MAXPRI_KERNEL, &thread);
}
EOF
    if "$0" --tree "$tmp" >/dev/null 2>&1; then
        refuse "selftest: a ladder missing load_context was accepted - the tail test does not bite"
    fi
    printf 'check_bootstrap_ladder: selftest ok - a reordered and a truncated ladder are both refused\n'
    exit 0
fi

check_ladder