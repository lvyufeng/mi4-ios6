#ifndef MI4IOS6_SHIM_MACH_COUNTERS_H
#define MI4IOS6_SHIM_MACH_COUNTERS_H
/*
 * `mach_counters.h` is build-generated: the kernel build emits it from a `.counts` file, and
 * neither is in the tarball. osfmk/kern/counters.h includes it unconditionally at line 62.
 *
 * Empty, after checking rather than assuming: counters.h uses the include for the *list* of
 * counter declarations, and everything it needs to *type* them it declares itself on the next
 * lines (line 90 onward: `typedef unsigned int mach_counter_t;` then the `c_*` externs). So an
 * empty header costs nothing here; a file that wants a specific generated counter fails loudly.
 */
#endif
