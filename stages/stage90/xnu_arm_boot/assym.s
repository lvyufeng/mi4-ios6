/*
 * Minimal assym.s for assembling XNU's ARM entry point.
 *
 * The real assym.s is build-generated: makedefs runs genassym.c through the compiler and scrapes
 * `offsetof()` values out of its assembly output. It is absent from the OSS tarball, and it is the
 * single largest piece of "the build configuration" that osfmk/arm's assembly needs.
 *
 * This file supplies only what osfmk/arm/start.s uses. Everything here is either derivable from a
 * structure this project already ABI-checks, or a constant Apple sets per SoC.
 *
 * BA_* - offsets into struct boot_args. Derivable exactly: tools/check_xnu_struct_abi.py compares
 *        our boot_args field by field against XNU's, and genassym.c:352-359 declares these as
 *        plain offsetof()s, so if the structs agree the offsets agree.
 * SS_* - offsets into struct arm_saved_state (osfmk/mach/arm/thread_status.h:221-233): r[13] then
 *        sp, lr, pc, cpsr, fsr, far, exception. Countable by hand; also 4-byte aligned throughout.
 * CPU_* - offsets into cpu_data_t. NOT derivable by hand: cpu_data_t's layout depends on
 *        __ARM_SMP__, the cache configuration and a dozen CONFIG_* macros. The values below are
 *        placeholders, and they are load-bearing only for resume_idle_cpu/start_cpu - the
 *        secondary-CPU entry points - never for the _start path this project assembles first.
 *        Producing them properly means compiling osfmk/arm/cpu_data_internal.h, which is a
 *        bounded, stated piece of work rather than an unknown (see docs/status/roadmap.md).
 * MACH_TRAP_TABLE_ENTRY_SIZE_NUM - sizeof(mach_trap_t) on 32-bit ARM. locore.s handles only 12,
 *        16 and 20; mach_trap_t is {fn, arg_count, arg_bytes} (osfmk/kern/syscall_sw.h:79-85), so
 *        on a 32-bit target it is 12 and on a 64-bit one 16. 12 is not a guess.
 */
#define BA_VIRT_BASE 4
#define BA_PHYS_BASE 8
#define BA_MEM_SIZE 12
#define BA_TOP_OF_KERNEL_DATA 16

#define SS_R0 0
#define SS_R12 48
#define SS_LR 56
#define SS_PC 60
#define SS_CPSR 64
#define SS_SIZE 80
#define VSS_SIZE 0

/* genassym.c declares PGBYTES; ARMv7 small pages are 4 KB. */
#define PGBYTES 4096

#define MACH_TRAP_TABLE_ENTRY_SIZE_NUM 12

/* Placeholders - secondary-CPU paths only. See the header comment. */
#define CPU_NUMBER_GS 0
#define CPU_INTSTACK_TOP 0
#define CPU_ISTACKPTR 0
#define CPU_ACTIVE_STACK 0
