#!/usr/bin/env bash
#
# The defines the ARM assembly layer is assembled with: the configuration's own options, from the
# same `make_defines.sh` the C objects are compiled with, minus a named list of exceptions.
#
#   ./tools/xnu_config/arm_asm_defines.sh [CONFIG]      # the -D flags, one per line
#   ./tools/xnu_config/arm_asm_defines.sh --exceptions  # the exception list, for a check
#
# Why this exists, and it is the finding of experiment 466: the two scripts that assemble a tree
# `.s` file for this project (`tools/assemble_arm_layer.sh`, `stages/stage90/xnu_arm_assemble.sh`)
# passed the *toolchain* flags - `-DASSEMBLER=1`, `-Dfmrx=vmrs`, `-DSLIDABLE=0` - and **none of the
# configuration's options**, while every C object got all of them. Apple's build gives a component's
# assembly the same `-D` list as its C, so the assembly and the C were compiled for two different
# kernels, in the direction that is hardest to see: a `#if` in `.s` is not a compile error either
# way, and only its *consequences* are visible later.
#
# Measured (the numbers are in experiment 466's document):
#
#   - `osfmk/arm/locore.s` is the **only** file in the ARM manifest that consults a `CONFIG_`
#     macro, and it consults three: `CONFIG_SKIP_PRECISE_USER_KERNEL_TIME` (9 sites),
#     `CONFIG_TELEMETRY` (5) and `CONFIG_DTRACE` (8; not defined by this configuration, so it
#     agreed by accident).
#   - With the options, `locore.o` stops calling `timer_state_event_kernel_to_user` and
#     `timer_state_event_user_to_kernel` - two functions `CONFIG_SKIP_PRECISE_USER_KERNEL_TIME=1`
#     does not compile into `osfmk/arm/machine_routines.c`, which is why they are undefined in
#     **both** links (`out/link/RELEASE-measure-undef.txt:42,43` and the entry image's own list) and
#     could never be resolved by linking an object: the right answer was to not call them.
#   - and it starts referencing `telemetry_needs_record`/`telemetry_mark_curthread`, which
#     `osfmk/kern/telemetry.c` does define for `CONFIG_TELEMETRY=1`.
#   - re-assembling the 17 manifest `.s` files changes **4** objects (`locore.o` by 72 bytes of
#     code, `cswitch.o`, `caches_asm.o` and `start.o` by one 4-byte literal-pool word each, with no
#     code byte moved) and leaves the other 13 byte-identical.
#
# The exceptions are named rather than inferred, and each one is a *toolchain* limit with a
# measurement behind it:
#
#   SLIDABLE   `config/MASTER.arm:77` is `options SLIDABLE=1  # Use PIE-assembly in *.s`, so the
#              configuration's value is 1 - and with it `osfmk/arm/globals_asm.h:29` does not
#              assemble at all:
#
#                  error: expected string in directive
#
#              the tree's SLIDABLE `LOAD_ADDR` is the Darwin 32-bit `$non_lazy_ptr` idiom, which
#              clang's ELF assembler has no equivalent for. Both scripts therefore keep
#              `-DSLIDABLE=0`, as they did before this file existed, and the C side keeps the
#              configuration's 1. That the two disagree is *recorded* here rather than fixed: making
#              the C side 0 is a whole-kernel re-baseline with nothing in this step to measure it
#              against, and the entry image is linked at a fixed base so its own literals are
#              correct either way.
#
# `--exceptions` prints the list so a caller can assert each name still means something: an
# exception that has stopped being an exception must fail the build rather than stay on the list.

set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$HERE/../.." && pwd)

# Kept in one place so the check below and the filter cannot drift apart.
ARM_ASM_EXCEPTIONS=(SLIDABLE)

if [[ ${1:-} == --exceptions ]]; then
    printf '%s\n' "${ARM_ASM_EXCEPTIONS[@]}"
    exit 0
fi

CONFIG=${1:-${XNU_KERNEL_CONFIG:-RELEASE}}

while IFS= read -r d; do
    [[ -n $d ]] || continue
    skip=0
    for e in "${ARM_ASM_EXCEPTIONS[@]}"; do
        [[ $d == -D$e=* || $d == -D$e ]] && skip=1
    done
    [[ $skip -eq 0 ]] && printf '%s\n' "$d"
done < <("$HERE/make_defines.sh" "$CONFIG")

# The exceptions are a claim about the configuration, so it is checked here: dropping a name that
# the configuration no longer sets would silently stop the filter from doing anything.
defines=$("$HERE/make_defines.sh" "$CONFIG")
for e in "${ARM_ASM_EXCEPTIONS[@]}"; do
    if ! grep -q -- "-D$e=" <<<"$defines" && ! grep -qw -- "-D$e" <<<"$defines"; then
        echo "arm_asm_defines.sh: exception '$e' is not an option of $CONFIG" >&2
        echo "  the exception list is a claim about the configuration; take the name off it" >&2
        exit 1
    fi
done
