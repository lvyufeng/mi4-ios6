/*
 * The ARM RTABI memory entry points, as tail branches into XNU's own implementations.
 *
 * Why these are needed at all: clang lowers a struct assignment or a call to a builtin into the
 * *EABI* name, not the C one. `arm_init.c`'s first real statement - `const_boot_args = *args;`, a
 * 320-byte struct copy - compiles to `bl __aeabi_memcpy4`, and `__aeabi_memcpy4` is defined
 * nowhere in XNU's tree:
 *
 *     grep -rn "__aeabi_memcpy" $XNU/osfmk $XNU/bsd $XNU/libkern   # nothing
 *
 * It comes from the compiler's runtime library, which Apple links into the real kernel as
 * `libcc_kext`. What XNU *does* have is the optimized assembly the RTABI routines are defined in
 * terms of: `osfmk/arm/bcopy.s` defines `memcpy`, `memmove`, `bcopy` and `ovbcopy`, and
 * `osfmk/arm/bzero.s` defines `memset`, `bzero` and `secure_memset`. So the RTABI name is an alias
 * for the tree's own code rather than a second implementation - one of these hand-written would be
 * a copy routine whose first bug would corrupt the boot_args instead of the data it was copied.
 *
 * Two of the four families are a tail branch and two are not, and the difference is not cosmetic:
 *
 *   __aeabi_memcpy{,4,8}   (void *d, const void *s, size_t n)  ->  memcpy   arguments already match
 *   __aeabi_memmove{,4,8}  (void *d, const void *s, size_t n)  ->  memmove  arguments already match
 *   __aeabi_memclr{,4,8}   (void *d, size_t n)                 ->  bzero    arguments already match
 *   __aeabi_memset{,4,8}   (void *d, size_t n, int c)          ->  memset   ** n and c are swapped **
 *
 * so the memset family swaps r1 and r2 first. ARM RTABI (IHI 0043) section 4.3.4 has the
 * signatures; lib1funcs.S in libgcc does the same swap, for the same reason.
 *
 * A tail branch (`b`, not `bl`) keeps the frame identical, so the alias returns to whatever called
 * the RTABI name, with the same register results - alignment guarantees on the `4`/`8` variants
 * only make `memcpy`'s job easier, never different.
 */

    .syntax unified
    .arm

    .section .text.eabi, "ax", %progbits

/*
 * The macro emits the global, its type, and the branch. `sym` and `target` are spelled out at each
 * use so the object's symbol list reads like the table above.
 */
    .macro RTABI_ALIAS sym, target
    .global \sym
    .type   \sym, %function
\sym:
    b       \target
    .size   \sym, . - \sym
    .endm

/* Copy: same argument order as XNU's memcpy. */
    RTABI_ALIAS __aeabi_memcpy,   memcpy
    RTABI_ALIAS __aeabi_memcpy4,  memcpy
    RTABI_ALIAS __aeabi_memcpy8,  memcpy

/* Move: same argument order, and the overlapping case is why it is a separate routine. */
    RTABI_ALIAS __aeabi_memmove,  memmove
    RTABI_ALIAS __aeabi_memmove4, memmove
    RTABI_ALIAS __aeabi_memmove8, memmove

/* Clear: (dest, n) is already bzero's argument order. */
    RTABI_ALIAS __aeabi_memclr,   bzero
    RTABI_ALIAS __aeabi_memclr4,  bzero
    RTABI_ALIAS __aeabi_memclr8,  bzero

/*
 * Set: (dest, n, c) against memset's (dest, c, n). Swap r1 and r2 through r3 - which the RTABI
 * leaves free at entry on these routines - and then branch.
 */
    .macro RTABI_SET_ALIAS sym
    .global \sym
    .type   \sym, %function
\sym:
    mov     r3, r1
    mov     r1, r2
    mov     r2, r3
    b       memset
    .size   \sym, . - \sym
    .endm

    RTABI_SET_ALIAS __aeabi_memset
    RTABI_SET_ALIAS __aeabi_memset4
    RTABI_SET_ALIAS __aeabi_memset8
