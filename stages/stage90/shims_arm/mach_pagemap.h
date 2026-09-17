#ifndef MI4IOS6_SHIM_MACH_PAGEMAP_H
#define MI4IOS6_SHIM_MACH_PAGEMAP_H
/*
 * <mach_pagemap.h> — empty, and the reason is a measurement rather than a guess.
 *
 * osfmk/vm/vm_object.h:71 includes it. No `mach_pagemap.defs` exists anywhere in the
 * xnu-4570.1.46 tarball, so unlike <mach/mach_host.h> — which is MIG output from a `.defs` that IS
 * published, and which tools/gen_mach_headers.sh now produces — there is nothing to generate it
 * from. It comes from a different component of Apple's build.
 *
 * Checked before choosing an empty header over a guess at its contents: `pagemap` appears exactly
 * once in vm_object.h, in the include line itself, and nothing in the entry path references a
 * symbol from it. So for this path the include is vestigial. A file that does use one will fail to
 * compile, loudly, which is the correct failure.
 */
#endif
