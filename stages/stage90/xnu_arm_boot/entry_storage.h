/*
 * 692: the storage probe's one entry point.
 *
 * The probe is `entry_storage.c` and the reason it is a file of its own rather than four more lines in
 * `entry_gic.c` is 483's own split: an object is where one definition of "how this image talks to a
 * device" lives. The GIC probe is that object for the distributor; this is that object for the eMMC
 * controller, and the two share only the mapper (`entry_mmio_section`) and the live channel.
 *
 * It is compiled in every build and its records are not, which is the same split `entry_timebase.c`
 * and `entry_gic.c` make and for the same reason: the measurement is the step and the records are the
 * instrument. A build with `STAGE90_XNU_STORAGE_PROBE=0` links a probe whose body does nothing at all.
 */
#ifndef STAGE90_ENTRY_STORAGE_H
#define STAGE90_ENTRY_STORAGE_H

#include <stdint.h>

/*
 * One call, once, and it never touches a register the caller has to have prepared. The probe takes its
 * own mapping, reads its own gate and publishes its own writes count, so its call site needs to know
 * nothing about the storage line - which is what lets that site be the idle exit's own wrapper.
 */
void entry_storage_probe(void);

#endif /* STAGE90_ENTRY_STORAGE_H */
