#ifndef MI4IOS6_SHIM_MACH_DEBUG_H
#define MI4IOS6_SHIM_MACH_DEBUG_H
/*
 * <mach_debug.h> — the real type definitions, and nothing from the three generated interfaces.
 *
 * The real header is at osfmk/mach_debug/mach_debug.h and it is not what the entry path needs. It
 * is a wrapper around four MIG-generated interface headers:
 *
 *     <mach_debug/mach_debug_types.h>  ->  present, and itself pulls six more present headers
 *     <mach/mach_host.h>               ->  NOT IN THE TARBALL, MIG-generated from mach_host.defs
 *     <mach/mach_port.h>               ->  NOT IN THE TARBALL, MIG-generated from mach_port.defs
 *     <mach/mach_interface.h>          ->  present, and pulls ~17 more generated headers
 *
 * So the redirect version of this shim led into a closure of twenty-odd build-generated headers,
 * which is a real component of Phase 4's wall — but a measurement first, before accepting it:
 * **every file in the entry path that includes <mach_debug.h> uses nothing from it.** Checked by
 * symbol, not by eye:
 *
 *     osfmk/arm/arm_vm_init.c   includes it, references no host_*, mach_debug_*, mach_port_* symbol
 *     osfmk/ipc/ipc_port.h      same
 *     osfmk/arm/bsd_arm.c       same
 *
 * So for the entry path the include is vestigial as far as its *symbols* go.
 *
 * It is NOT vestigial as far as its *types* go, and an empty shim proves it: with nothing at all,
 * arm_init.c goes from 4 reported errors to 20. The difference is mach_debug_types.h, which is real
 * and present, and which the rest of the chain needs. So this shim includes it and stops there.
 *
 * What this is NOT: a substitute for MIG. Any file that genuinely calls host_* or the mach
 * debug interfaces will fail to compile against this, loudly, which is the correct failure. The
 * moment something does, this shim should be replaced by real MIG output — and the reason MIG is
 * absent is recorded in docs/experiments/experiment-107: the .defs files are public (40 of them
 * under osfmk/mach/) and MIG itself is open source, but it is not in the tarball and not in this
 * host's package repository.
 */
#include <mach_debug/mach_debug_types.h>

#endif
