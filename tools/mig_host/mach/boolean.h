/* Host build of MIG: the mach types MIG's own code uses, and nothing else.
 * Values are copied from osfmk/mach/*.h in the 4570 tree so the generator sees the real
 * constants; only the packaging is ours. */
#ifndef _MACH_BOOLEAN_H_
#define _MACH_BOOLEAN_H_
typedef int boolean_t;
#define TRUE  1
#define FALSE 0
#endif
