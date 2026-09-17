#ifndef MI4IOS6_SHIM_TASK_SWAPPER_H
#define MI4IOS6_SHIM_TASK_SWAPPER_H
/*
 * task_swapper.h does not exist anywhere in the xnu-4570.1.46 tarball. osfmk/vm/vm_map.h:104
 * includes it under MACH_KERNEL_PRIVATE, so it is part of the kernel's own include closure and was
 * simply never published.
 *
 * What it holds on a real Darwin kernel is the compressed-memory ("swap to compressed memory")
 * interface: the task_swapper_* entry points and the compressor's per-task state. None of that is
 * reachable from the entry path, and this shim declares none of it — a file that calls one of them
 * will fail to compile, loudly, which is the correct failure and the point of making this a
 * documented empty header rather than a guess at signatures.
 */
#endif
