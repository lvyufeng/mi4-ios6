#!/bin/sh
#
# The ARM target triple this project compiles XNU's C and C++ with. One value, one place: four
# scripts spelled it out and a triple is exactly the kind of value that ends up different in two of
# them - see mi4-one-value-two-definitions.
#
#   ./tools/xnu_config/arm_target.sh          # prints the triple
#
# `armv7-unknown-netbsd-eabi` is not an accident of toolchain availability. It is the only triple
# this host has that is both ELF and ILP32-with-Darwin's-type-widths:
#
#   __SIZE_TYPE__     long unsigned int     (not `unsigned int`)
#   __UINTPTR_TYPE__  long unsigned int
#   __INTPTR_TYPE__   long int
#   __WCHAR_TYPE__    int
#
# `armv7-apple-ios` and `armv7-apple-darwin` give those widths and produce **Mach-O**, which
# experiment-150 left as a dead end: `ld64.lld` cannot link 32-bit ARM Mach-O, so the dialect
# translation to ELF is the only linkable object format. `armv7-none-eabi` produces ELF and gives
# `unsigned int` - and the difference is not cosmetic. XNU mixes `size_t`, `unsigned long`, `u_long`
# and `uint32_t` as if they were interchangeable, and on an ILP32 target they are the same *width*
# but not the same *type*, so a bare-metal triple rejects declarations Apple's compiler accepts.
# Measured in experiment-161: three files, 35 symbols.
#
# Two ELF triples have the widths - `armv7-unknown-netbsd-eabi` and `armv7-unknown-openbsd`. NetBSD
# is chosen because it defines strictly fewer macros this tree has no use for: OpenBSD additionally
# claims `unix`, `__PIC__`/`__PIE__` and `__SSP_STRONG__`. All of those are inert here (no XNU header
# reads them; `-fno-pic` was verified to win over OpenBSD's PIE default, byte for byte), so this is a
# preference for the smaller delta and not a measured advantage.
#
# Three things the change does cost, all measured and all in experiment-161:
#
#   * `__NetBSD__` becomes defined. One branch in the tree reads it - `bsd/netinet/ip_compat.h:122` -
#     and it selects the same `typedef u_int32_t u_32_t` as the `#else` path it replaces.
#   * `__PTRDIFF_TYPE__` becomes `long int` where Darwin has `int`. `ptrdiff_t` is therefore a
#     different type from Apple's, in the opposite direction from `size_t`. Both are 32 bits.
#   * The EH model changes from ARM EHABI to DWARF (`__ARM_DWARF_EH__`), so no `.ARM.exidx` sections
#     are emitted. Nothing in XNU, in the measurement link or in the entry image reads them; the
#     undefined-symbol lists are the measurement that says so.
#
# The assembler is deliberately NOT this triple. `assemble_arm_layer.sh` translates Darwin-dialect
# `.s` and experiment-142 measured that triple by assembling, not by compiling; the two paths are
# already separate, and the object format they produce is the same either way.
#
printf '%s' "${XNU_ARM_TARGET:-armv7-unknown-netbsd-eabi}"
