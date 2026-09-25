/*
 * 692: the storage probe's one entry point. 692 then pressed it, and the press made the probe's first
 * act a second mapping: the GATE's megabyte (`0xFC4`, the GCC block, which also holds the reset path's
 * PS_HOLD store) is in no table this image builds, so the arm installs it before it reads anything and
 * the storage block's install follows.
 *
 * The probe is `entry_storage.c` and the reason it is a file of its own rather than four more lines in
 * `entry_gic.c` is 483's own split: an object is where one definition of "how this image talks to a
 * device" lives. The GIC probe is that object for the distributor; this is that object for the eMMC
 * controller and for the GCC gate it hangs off, and the three share only the mapper
 * (`entry_mmio_section`) and the live channel.
 *
 * It is compiled in every build and its records are not, which is the same split `entry_timebase.c`
 * and `entry_gic.c` make and for the same reason: the measurement is the step and the records are the
 * instrument. A build with `STAGE90_XNU_STORAGE_PROBE=0` links a probe whose body does nothing at all.
 *
 * **696: the switch is a rung, and the entry point is one call either way.** `..._PROBE=1` is the
 * read-only probe above; `=2` is that probe **and** the vendor's mode sequence
 * (`sdhci-msm.c:2841-2868`: four stores, one bounded poll, a readback after each store), which makes
 * this object the first in the project that writes to a device block rather than reading one. The
 * caller does not change with the rung - `entry_storage_probe` is still one call from
 * `__wrap_platform_cache_idle_exit`, still before 690's clock block, still once per boot - because a
 * rung is a property of the arm's own body and not of where in the boot it happens. What does change
 * is the one thing the caller must not have to know: with the rung at 2, the call can write.
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
