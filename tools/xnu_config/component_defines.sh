#!/usr/bin/env bash
#
# The `-D` flags Apple gives each kernel component, read off its own Makefile template.
#
#   ./tools/xnu_config/component_defines.sh bsd        # the flags for a bsd/ source file
#   ./tools/xnu_config/component_defines.sh --table    # component<TAB>flags, for a checker
#
# Why this is a per-component table and not one global flag set. Apple's build sets these in
# `<component>/conf/Makefile.template`, in the `CFLAGS+=` line each component's generated Makefile
# starts from:
#
#   osfmk/conf/Makefile.template:19     -DMACH_KERNEL_PRIVATE -DMACH_KERNEL
#   bsd/conf/Makefile.template:41-43    -DDRIVER_PRIVATE -D_KERNEL_BUILD -DKERNEL_BUILD
#                                       -DMACH_KERNEL -DBSD_BUILD -DBSD_KERNEL_PRIVATE
#                                       -DLP64_DEBUG=0
#   libkern/conf/Makefile.template:19   -DLIBKERN_KERNEL_PRIVATE -DOSALLOCDEBUG=1
#   iokit/conf/Makefile.template:19-20  -DDRIVER_PRIVATE -DIOKIT_KERNEL_PRIVATE
#                                       -DIOMATCHDEBUG=1 -DIOALLOCDEBUG=1
#   pexpert/conf/Makefile.template:19   -DPEXPERT_KERNEL_PRIVATE
#   libsa/conf/Makefile.template:19     -DLIBSA_KERNEL_PRIVATE
#   security/conf/Makefile.template:19  -DBSD_KERNEL_PRIVATE
#   san/conf/Makefile.template:16       (none)
#
# and the seven `*_KERNEL_PRIVATE` names are exactly the set `MakeInc.def:586` undefines when it
# builds the *public* SDK headers - `XNU_PRIVATE_UNIFDEF = -UMACH_KERNEL_PRIVATE
# -UBSD_KERNEL_PRIVATE -UIOKIT_KERNEL_PRIVATE -ULIBKERN_KERNEL_PRIVATE -ULIBSA_KERNEL_PRIVATE
# -UPEXPERT_KERNEL_PRIVATE -UXNU_KERNEL_PRIVATE`. That is the confirmation that they are
# per-component switches, not one global one.
#
# It matters because they are not independent. `MACH_KERNEL_PRIVATE` is what makes
# `osfmk/kern/kern_types.h:192` pull in `kern/misc_protos.h`, and `misc_protos.h:70,76,107` declare
#
#   ffs(unsigned int), fls(unsigned int), copyinstr(const user_addr_t, char *, vm_size_t, vm_size_t *)
#
# while `bsd/libkern/libkern.h:145,147,183` - reached by `bsd/sys/systm.h:113`, in every BSD
# translation unit that includes it - declare
#
#   ffs(int), fls(int), copyinstr(const user_addr_t, void *, size_t, size_t *)
#
# Different types, same names, so a translation unit that sees both does not compile. Defining
# MACH_KERNEL_PRIVATE for the whole build (which is what this project did until 2026-09-17) put the
# Mach-private view into every BSD file and cost 127 of the minimal configuration's 205 failures.
#
# `meta_features.h` is stripped: every component force-includes it, and it is the one header Apple's
# build generates and does not ship (nothing in the tarball matches `meta_features*`). The kernel
# build script's own force-include set stands in for it; see tools/build_xnu_arm_kernel.sh.
#
# `tools/check_component_defines.py` parses those templates and fails if they and this table
# disagree, so the table cannot drift the way a hand-copied value can.

set -uo pipefail

table() {
    cat <<'EOF'
osfmk	-DMACH_KERNEL_PRIVATE=1 -DMACH_KERNEL=1
bsd	-DDRIVER_PRIVATE=1 -D_KERNEL_BUILD=1 -DKERNEL_BUILD=1 -DMACH_KERNEL=1 -DBSD_BUILD=1 -DBSD_KERNEL_PRIVATE=1 -DLP64_DEBUG=0
libkern	-DLIBKERN_KERNEL_PRIVATE=1 -DOSALLOCDEBUG=1
iokit	-DDRIVER_PRIVATE=1 -DIOKIT_KERNEL_PRIVATE=1 -DIOMATCHDEBUG=1 -DIOALLOCDEBUG=1
pexpert	-DPEXPERT_KERNEL_PRIVATE=1
libsa	-DLIBSA_KERNEL_PRIVATE=1
security	-DBSD_KERNEL_PRIVATE=1
san
EOF
}

case "${1:-}" in
    --table) table ;;
    "")      echo "usage: component_defines.sh <component>|--table" >&2; exit 2 ;;
    *)
        # The component of a source path is its first path element under the tree root, which is
        # what MakeInc.def:47 derives from RELATIVE_SOURCE_PATH.
        table | awk -F'\t' -v c="$1" '$1 == c { print $2 }'
        ;;
esac
