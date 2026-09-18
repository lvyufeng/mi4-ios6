/*
 * The symbols XNU's osfmk/arm/start.s needs, so that its `_start` can be linked and entered.
 *
 * Why this is a separate image and not part of the payload
 * --------------------------------------------------------
 * XNU's `_start` is position-dependent: it converts the addresses it was linked at into physical
 * ones with `addr - virtBase + physBase`, builds its own page tables at `topOfKernelData`, and
 * switches TTBR0/TTBR1 to them. So it has to be linked as a kernel would be, at a base the
 * payload does not occupy, and entered with a `boot_args` that describes that base.
 *
 * What is here, and what each thing is for
 * ----------------------------------------
 * start.s's undefined list, from `./xnu_arm_assemble.sh`:
 *
 *   _fleh_*  (8)          the exception vector targets. `_start` patches ExceptionVectorsTable
 *                         with their addresses and turns on SCTLR.HIGHVEC, so after the switch
 *                         an exception lands here. They are real, not stubs: see entry_epilogue.
 *   _ExceptionVectorsBase the vector code page; `_start` maps it at HIGH_EXC_VECTORS (0xffff0000).
 *   _ExceptionVectorsTable the 8-entry table `_start` fills in.
 *   _intstack_top / _fiqstack_top  `_start` loads SP from `intstack_top`, so these are load-bearing.
 *   _gPhysBase / _gPhysSize / _gVirtBase  XNU's globals; `_start` does not read them, its callers do.
 *   _kdebug_enable / _EntropyData         the same.
 *   _arm_init, _arm_init_cpu, _arm_init_idle_cpu  `_start` sets lr to arm_init and branches to it.
 *
 * **What changes when the real XNU objects are linked (`STAGE90_ENTRY_REAL_ARM_INIT=1`).** Three
 * names above are then no longer this file's business, because the objects that own them are in the
 * image: `osfmk/arm/data.s` (`out/xnu_asm_obj/data.o`) carries the real `intstack`, `fiqstack`,
 * `CpuDataEntries`, `BootCpuData` and `RTClockData`, and `osfmk/arm/bcopy.s` + `bzero.s` carry the
 * real `memcpy`/`memmove`/`memset`/`bzero` that the compiler's `__aeabi_mem*` calls are aliased to.
 * Whatever this file still defines that one of those objects also defines is a link error, not a
 * silent override: the definitions here shrink as the real objects join, and the link is what says
 * which ones had to go.
 *
 * Every one of these is reachable *after* `_start` has switched to its own page tables, which map
 * exactly [physBase, physBase + memSize). So all of this code and data must live in that window,
 * which is what the linker script arranges.
 *
 * The epilogue, and why it is shaped the way it is
 * ------------------------------------------------
 * `entry_epilogue` is what both `arm_init` and the exception stubs call. It is the only place
 * this project will ever get evidence out of XNU's own entry path, and the constraints on it are
 * unusual:
 *
 *   - It runs with XNU's page tables live, which map only the 2 MB window. `ram_console` is at
 *     0xde500000, far outside it, so it cannot be written yet.
 *   - Therefore the epilogue first turns the caches and the MMU **off**. After that every address
 *     is physical, so `ram_console` becomes reachable at its own address - and so does this code,
 *     because the window is an identity mapping.
 *   - Caches off before MMU off, and never the reverse: a dirty cache line cannot survive, and
 *     with the MMU off the cacheability of a physical address is no longer describable.
 *
 * The message is written straight into the ram_console buffer rather than through the payload's
 * logger, because the payload is gone at this point - this image was jumped to and does not
 * return. It appends, exactly as `log_puts` does, so the line lands after the payload's own log.
 */

#include <stdint.h>

#define RAM_CONSOLE_BASE   0xde500000u
#define RAM_CONSOLE_SIG    0x43474244u  /* 'DBGC' */
#define RESTART_REASON     0x0fa0065cu
#define RESTART_NORMAL     0x78665501u
#define MSM8974_PSHOLD     0xfc4ab000u

#define ENTRY_STACK_BYTES  0x8000u

/* ------------------------------------------------------------------ storage XNU expects */

/*
 * Told to XNU through boot_args; `_start` does not read them, but its callers do.
 *
 * `unsigned long`, not `const uint32_t`, and both halves of that matter. XNU declares these in
 * `osfmk/arm/arm_vm_init.c:80` as `unsigned long gVirtBase, gPhysBase, gPhysSize;` and **assigns**
 * them at `:351-353` from the boot_args; a `const` stand-in puts them in `.rodata`, so the first
 * real code to write one faults into the read-only section instead of setting a variable, and a
 * `uint32_t` one is half the size XNU's own type says. The values below are the boot_args this
 * payload hands `_start`, which is what XNU would have computed from the same arguments.
 */
unsigned long gPhysBase = 0x00200000ul;
unsigned long gPhysSize = 0x00200000ul;
unsigned long gVirtBase = 0x00200000ul;

uint32_t kdebug_enable;

/*
 * 68 bytes, not `uint64_t[2]` (16). The real thing is `entropy_data_t`:
 *
 *     struct entropy_data { uint32_t *index_ptr; uint32_t buffer[ENTROPY_BUFFER_SIZE]; };
 *
 * - `osfmk/prng/random.h:47` - and `nm -S --defined-only out/xnu_kernel_obj/osfmk_prng_random.o`
 * reports its definition as `EntropyData D 0 44`, 0x44 = 68. The earlier 16-byte stand-in was
 * undersized by a factor of four, which nothing noticed because the only code that would write
 * past 16 bytes - `early_random` - is a stub in this image. Sizing a stand-in from the symbol it
 * stands for is the general rule; see the storage sizing in build_entry.sh, which fails the build
 * rather than guess.
 */
uint8_t EntropyData[68] __attribute__((aligned(8)));

/*
 * `_start` loads SP from intstack_top, so this must be real, writable, and in the window - but only
 * when XNU's own `osfmk/arm/data.s` is not in the image. With `STAGE90_ENTRY_REAL_ARM_INIT=1` it
 * is, and it defines the real `intstack` (4 pages), `fiqstack` (1 page), `excepstack`,
 * `CpuDataEntries`, `BootCpuData`, `RTClockData` and `kd_early_buffer` - 48 KB of the layout XNU
 * expects, at the sizes `data.s` states. Defining them here as well is a duplicate definition, so
 * these two stacks are removed rather than left to collide.
 */
#ifndef STAGE90_ENTRY_REAL_ARM_INIT
#define ENTRY_STACK_BYTES  0x8000u
static uint8_t g_intstack[ENTRY_STACK_BYTES] __attribute__((aligned(8)));
static uint8_t g_fiqstack[ENTRY_STACK_BYTES] __attribute__((aligned(8)));
uint32_t intstack_top = (uint32_t)(uintptr_t)&g_intstack[ENTRY_STACK_BYTES];
uint32_t fiqstack_top = (uint32_t)(uintptr_t)&g_fiqstack[ENTRY_STACK_BYTES];
#endif /* !STAGE90_ENTRY_REAL_ARM_INIT */

/* The vector table `_start` fills in with the fleh_* addresses below. */
uint32_t ExceptionVectorsTable[8] __attribute__((aligned(32)));

/*
 * The stack the exception handlers run on, and why they need one of their own.
 *
 * `_start` sets the SVC stack to `intstack_top - SS_SIZE` (`start.s:310-311`) and does not touch
 * the banked stack pointers at all. So when an exception is taken, SP becomes whatever that mode
 * was left with - set by the bootloader or by the payload for *its* page tables, not for XNU's.
 * XNU's tables map `[physBase, physBase + memSize)` = `[0x00200000, 0x00a00000)` and nothing else,
 * and the payload is at 0x00008000, so an inherited stack pointer is outside the map.
 *
 * The consequence is not a wrong answer, it is the loss of the answer. The handler's first push
 * data-aborts, the abort is taken again in the same mode with the same stack, and the CPU recurses
 * on itself until the watchdog resets the device. Nothing reaches the log, and a fault inside
 * `arm_init` - the one thing this image exists in order to report - looks exactly like a hang.
 *
 * `entry_vectors.s` therefore loads SP from `entry_vectors_stack_top` before branching to a
 * handler. The stack is defined there rather than here because the literal needs the address, not
 * a variable holding it; it lives in `.bss`, inside the window, which is the only other place
 * XNU's page tables map.
 */

/*
 * The vector code page, defined in entry_vectors.s: eight branches into eight trampolines that
 * load the fleh_* addresses. It is a separate file because an ARM vector slot is four bytes and
 * the handler addresses are far out of `b` range, which needs assembly rather than C.
 */
extern uint8_t ExceptionVectorsBase[];

/*
 * ------------------------------------------------------------------ results carried to the log
 *
 * `arm_init` runs with XNU's page tables live, where `ram_console` is unreachable, so anything it
 * learns has to be stashed and written out later by the epilogue. Small fixed table, no
 * allocation, because this runs before any allocator exists.
 */
/*
 * Rendered to text as they arrive, into one flat buffer, rather than stored as a table of
 * key/value pairs to be formatted later.
 *
 * The first version kept two parallel arrays - pointers to the key strings, and the values - and
 * read them back in the epilogue. The keys came out empty and the pairs came out misaligned, while
 * the values were mostly right. Rather than keep chasing that, this removes the indirection
 * entirely: by the time anything reads this, it is the exact characters that will be written, in
 * one contiguous block, with no pointers to be wrong.
 */
#define ENTRY_KV_BUF 768
static char g_kv_buf[ENTRY_KV_BUF];
static uint32_t g_kv_len;

void entry_kv(const char *key, uint32_t value)
{
    static const char hex[] = "0123456789abcdef";

    if (g_kv_len + 40u >= ENTRY_KV_BUF) {
        return;
    }
    g_kv_buf[g_kv_len++] = ' ';
    while (*key != '\0' && g_kv_len + 20u < ENTRY_KV_BUF) {
        g_kv_buf[g_kv_len++] = *key++;
    }
    g_kv_buf[g_kv_len++] = '=';
    g_kv_buf[g_kv_len++] = '0';
    g_kv_buf[g_kv_len++] = 'x';
    for (unsigned i = 0; i < 8u; i++) {
        g_kv_buf[g_kv_len++] = hex[(value >> (28u - (i * 4u))) & 0xfu];
    }
    g_kv_buf[g_kv_len++] = '\n';
    g_kv_buf[g_kv_len] = '\0';
}

/* ------------------------------------------------------------------ the evidence path */

static void entry_write(const char *s);

static void entry_write(const char *s)
{
    volatile uint32_t *sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    volatile uint32_t *size_p = (volatile uint32_t *)(uintptr_t)(RAM_CONSOLE_BASE + 8u);
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)(RAM_CONSOLE_BASE + 12u);
    uint32_t size = *size_p;
    const uint32_t max = 0x00200000u - 12u;

    if (*sig != RAM_CONSOLE_SIG) {
        /* Nothing sane to append to; start a fresh buffer so the line is not lost. */
        *sig = RAM_CONSOLE_SIG;
        size = 0u;
    }
    while (*s != '\0' && size < max) {
        data[size++] = (uint8_t)*s++;
    }
    *size_p = size;
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

/*
 * The one exit. `why` must be in read-only memory inside the window, which everything in this
 * image is.
 */
__attribute__((noreturn, noinline)) static void entry_epilogue(const char *why)
{
    uint32_t sctlr;

    __asm__ volatile ("cpsid if" ::: "memory");

    /*
     * Clean and invalidate the D-cache by set and way, BEFORE touching SCTLR.
     *
     * This is not optional and it is the one thing this epilogue got wrong first time. Everything
     * `arm_init` learned was written into g_kv_key/g_kv_val with XNU's D-cache on, so it was
     * sitting dirty in the cache; clearing SCTLR.C then discards it, and the log came out with
     * empty keys and a garbage count. The Phase 1 documents say exactly this - a dirty cache line
     * cannot survive the cache being turned off - and here is a second place it bites, in an image
     * that had not read them.
     *
     * Geometry from CCSIDR rather than assumed, same as cache_ops.c: a wrong set or way count
     * leaves lines behind, and the failure looks like partial corruption rather than loss.
     */
    {
        uint32_t ccsidr, line_log2, ways, sets, way_shift, way, set, n, w;

        __asm__ volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
        line_log2 = (ccsidr & 0x7u) + 4u;
        ways = ((ccsidr >> 3) & 0x3ffu) + 1u;
        sets = ((ccsidr >> 13) & 0x7fffu) + 1u;
        n = 0u;
        for (w = ways; w > 1u; w >>= 1) {
            n++;
        }
        way_shift = line_log2 + n;

        for (way = 0u; way < ways; way++) {
            for (set = 0u; set < sets; set++) {
                uint32_t val = (way << way_shift) | (set << line_log2);
                __asm__ volatile ("mcr p15, 0, %0, c7, c14, 2" :: "r"(val) : "memory");
            }
        }
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    }

    /* Now the caches can be turned off: with the MMU off there is no descriptor left to say what
     * is cacheable. */
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~((1u << 2) | (1u << 12));
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory");
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");

    /* Then the MMU. After this every address is physical. */
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~1u;
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory");
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");

    entry_write("\nMI4IOS6_STAGE90_XNU real XNU entry: ");
    entry_write(why);
    entry_write("\n");

    if (g_kv_len != 0u) {
        entry_write("MI4IOS6_STAGE90_XNU real XNU entry");
        entry_write(g_kv_buf);
    }

    *(volatile uint32_t *)(uintptr_t)RESTART_REASON = RESTART_NORMAL;
    __asm__ volatile ("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)MSM8974_PSHOLD = 0u;
    __asm__ volatile ("dsb sy" ::: "memory");

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/*
 * `_start` branches here (via lr) once its page tables are live and the MMU is on.
 *
 * Reaching this function at all is the result the experiment is for: it means XNU's own entry
 * sequence ran to completion on this device - I-cache enabled, boot_args read, vectors patched,
 * TTBR0/TTBR1/TTBCR written, a V=P section and a `memSize`-sized kernel mapping built, TLB
 * flushed, DACR/PRRR/NMRR set, SCTLR programmed with TEX remap and high vectors, VFP enabled -
 * and then branched to a function that does not exist in XNU, which is why the line it writes
 * says so.
 *
 * **With `STAGE90_ENTRY_REAL_ARM_INIT=1` this definition is gone and the real `arm_init.o` from
 * the compiled kernel is linked in its place.** Then the interesting line is no longer this one but
 * the *first* one from `entry_stub_hit` below: the name of the first thing XNU's own `arm_init`
 * calls that this image does not provide. That is the measurement - it is what says which part of
 * the compile graph to build next, from the device rather than from a call-graph tool.
 */
#ifndef STAGE90_ENTRY_REAL_ARM_INIT
void arm_init(void *boot_args)
{
    (void)boot_args;   /* outside the post-switch window; deliberately not dereferenced */
    entry_epilogue("_start ran to completion and branched to arm_init");
}
#endif

/*
 * `panic`. 4570's device_tree.c calls it on a malformed tree, which is a real assertion and the
 * reason it is here rather than stubbed to nothing: if XNU's reader rejects the tree this project
 * built, that is the finding, and it should reach the log by the same route as everything else.
 * The variadic arguments are ignored - the message this project can act on is which call failed,
 * and the tree is already the thing under test.
 */
void panic(const char *fmt, ...);
void panic(const char *fmt, ...)
{
    (void)fmt;
    entry_epilogue("panic() - XNU rejected something; see which call precedes this line");
}

/* The secondary-CPU entry points. Unused on a single-core bring-up; present so the link closes. */
#ifndef STAGE90_ENTRY_REAL_ARM_INIT
void arm_init_cpu(void) { entry_epilogue("arm_init_cpu (unexpected)"); }
void arm_init_idle_cpu(void) { entry_epilogue("arm_init_idle_cpu (unexpected)"); }
#endif

/*
 * Every symbol the real `arm_init` needs that this image does not provide lands here, one
 * generated function per name (see build_entry.sh - the list is read off the link, not written by
 * hand). Reaching any of them is the result: it stops the run and writes the name.
 *
 * The name goes into the same flat buffer `entry_kv` uses, for the same reason - it is written with
 * XNU's caches on and read back after the epilogue has cleaned them and turned the MMU off, so it
 * has to be characters in one contiguous block inside the window, with no pointers to be wrong.
 *
 * `panic` is deliberately NOT among the generated ones: it is real here, and a device-tree or an
 * assertion failure reaching it should say so by name rather than looking like a missing symbol.
 */
void entry_stub_hit(const char *name)
{
    static const char prefix[] = " stub_hit=";

    if (g_kv_len + sizeof prefix + 1u < ENTRY_KV_BUF) {
        for (const char *p = prefix; *p != '\0'; p++) {
            g_kv_buf[g_kv_len++] = *p;
        }
        while (*name != '\0' && g_kv_len + 1u < ENTRY_KV_BUF) {
            g_kv_buf[g_kv_len++] = *name++;
        }
        g_kv_buf[g_kv_len++] = '\n';
        g_kv_buf[g_kv_len] = '\0';
    }

    entry_epilogue("real arm_init reached a symbol this image does not provide");
}

#ifdef STAGE90_ENTRY_REAL_ARM_INIT
/*
 * ------------------------------------------------------------------ `kprintf` in this kernel
 *
 * `_consume_kprintf_args` is not a print, and it is here because experiment 189's first run ended
 * at it. This kernel is built with `CONFIG_NO_KPRINTF_STRINGS=1` - `RELEASE` is
 * `build_xnu_arm_kernel.sh`'s default configuration (`:84`), Apple's
 * `BSD_RELEASE = [ BSD_BASE no_printf_str no_kprintf_str secure_kernel ]` (`MASTER.arm:24`) is its
 * BSD half, and `make_defines.sh RELEASE` emits the define - so `pexpert.h:204-210` rewrites every
 * `kprintf` in osfmk:
 *
 *     #define kprintf(x, ...) _consume_kprintf_args( 0, ## __VA_ARGS__ )
 *
 * and the real function is `osfmk/kern/printf.c:197`:
 *
 *     void _consume_kprintf_args(int a __unused, ...) { }
 *
 * Its whole body is one instruction - the disassembly of `osfmk_kern_printf.o` at offset 8 is
 * `bx lr`, and the symbol is 4 bytes - so what the kernel hands it, it throws away. The
 * configuration strips kprintf *strings*; this function exists so the arguments are still compiled
 * and still evaluated, which is what keeps a `kprintf` from changing the surrounding code.
 *
 * So this file defines it, and the definition is not an approximation of the real one - an empty
 * variadic function is the real one. Linking `printf.o` to obtain those four bytes would bring
 * 5607 bytes of text across 23 functions and 24 obligations the image does not have, nearly all of
 * them the console stack (`cnputc`, `cnputc_unbuffered`, `PE_kputc`, `console_printbuf_*`,
 * `debug_putc`, `disable_serial_output`, `os_log_with_args`, `paniclog_flush`, `bsd_log_init`) that
 * `no_printf_str` and `no_kprintf_str` are the configuration's way of keeping out. The print
 * subsystem is not on this kernel's boot path by construction, and that run is the evidence: the
 * only thing that stopped the image was the function that means "nothing to print".
 *
 * `cpuid.o`, linked since experiment 189, is what keeps the reference alive: `do_cacheid` prints
 * its cache geometry through it (`cpuid.c:290,298`).
 */

/*
 * `osfmk/kern/printf.c:197`, an empty variadic function. The `(void)a` is for `-Wunused-parameter`;
 * the body stays empty so the compiled code is the `bx lr` the real one is.
 */
void _consume_kprintf_args(int a, ...)
{
    (void)a;
}

/*
 * `processor_data_init()` - `osfmk/kern/processor_data.c:39`, and the first symbol the run reaches
 * after the two objects experiment-189 named are linked.
 *
 * Experiment 189 stopped at `processor_bootstrap`, the next symbol `arm_init` reaches. This run
 * links `osfmk/kern/processor.o`, which defines it, so `processor_bootstrap` (`processor.c:120`)
 * runs for real - and so does `processor_init` (`processor.c:135`), because it is in the same
 * object. That is what moves the frontier out of the object that was just linked: the last four
 * probes were each at the first undefined symbol *inside* a function the previous run had made
 * real, and this one is at the first undefined symbol inside `processor_init`, which this run is
 * what makes real.
 *
 * `processor_init`'s body, statement by statement, until it reaches something nothing defines:
 *
 *     if (processor != master_processor)      - skipped: processor_bootstrap passes master_processor
 *         SCHED(processor_init)(processor);
 *     processor->state = PROCESSOR_OFF_LINE;  - and the ~20 field stores after it
 *     processor_state_update_idle(processor); - in processor.o
 *     timer_call_setup(&processor->quantum_timer, thread_quantum_expire, processor);
 *                                             - timer_call.o, real since exp-185
 *     processor_data_init(processor);         - processor_data.c, undefined -> the stop
 *
 * `processor_bootstrap` runs first, and the two lines of it that matter here are
 * `master_processor = cpu_to_processor(master_cpu)` and `processor_init(master_processor,
 * master_cpu, &pset0)`. So the argument this probe receives is the processor pointer the
 * scheduler just built, and the probe is the first code in this sequence that gets to *check* a
 * pointer the kernel computed rather than one this file assumed.
 *
 * The values are `struct processor` fields read at offsets taken from the compiled code - from
 * this image's own `osfmk_kern_processor.o`, in `processor_init`, where each store below is
 * visible with its offset. Reading the header instead would be reading a struct whose layout
 * depends on which `#if` blocks the configuration selected (`CONFIG_SCHED_TRADITIONAL`,
 * `CONFIG_SCHED_MULTIQ`), and the whole point of these offsets is that they are the ones the
 * compiler used.
 *
 *     str r9, [r4, #8]      offset    8 = state          r9 = 0  -> PROCESSOR_OFF_LINE (0)
 *     str r0, [r4, #16]     offset   16 = is_recommended bit cpu_id of pset0.recommended_bitmask
 *     str r6, [r4, #32]     offset   32 = processor_set  r6 = r2 = pset = &pset0
 *     str fp, [r4, #56]     offset   56 = cpu_id         fp = r1 = cpu_id
 *     str r0, [r4, #128]    offset  128 = quantum_end    r0 = -1, then [r4, #132] = -1 too
 *     str r0, [r4, #144]    offset  144 = deadline       r0 = -1
 *     str r4, [r4, #1236]   offset 1236 = processor_primary, storing the processor into itself
 *
 * `quantum_timer` is the `timer_call_setup` argument, `add r0, r4, #64`, which is what makes 128
 * the end of that struct and not some other field. `master_cpu`, `master_processor`, `pset0` and
 * `processor_count` are all defined in `processor.o`, which is linked this run, so they can be
 * named directly rather than read through a pointer.
 *
 * What each one is worth:
 *
 *   processor pointer       the probe's own argument. It should equal `master_processor`, which is
 *                           `cpu_to_processor(master_cpu)` - a function in `cpu_common.o` that
 *                           nothing had called before this run.
 *   &pset0 / processor_set  `processor_set` is a **non-zero** pointer and `pset0` is the address
 *                           the kernel handed `processor_init`. A stub `processor_init` cannot
 *                           produce either: `.bss` is zeroed by the payload, so a field a stub
 *                           left alone reads 0.
 *   quantum_end/deadline    0xffffffff twice each, i.e. `UINT64_MAX` - the value `processor_init`
 *                           stores. Also not reachable from zeroed memory, and it is the first
 *                           time an XNU timer field is set to a deadline in this image.
 *   cpu_id / master_cpu     equal to each other, and to whatever `arm_init` decided the boot CPU
 *                           is. It is the value that indexes `processor_array` in
 *                           `cpu_to_processor`, so the pair checks the pointer above twice.
 *   is_recommended          `(pset->recommended_bitmask & (1ULL << cpu_id)) ? TRUE : FALSE`, a
 *                           bit test on a `pset0` field `pset_init` wrote. Its being 0 or 1 says
 *                           something about the device rather than about the code, so it is
 *                           reported rather than predicted - and read from a nonzero `pset0` it is
 *                           still only meaningful if the other values came out right.
 *   processor_count         still **0**: `processor_init` increments it after
 *                           `processor_data_init` returns, which is what makes it the one value
 *                           here that must *not* have happened yet.
 *
 * Nothing is predicted about `processor_bootstrap`'s own body - `pset_init`, the four `queue_init`
 * calls and `simple_lock_init` - because it has to have run to completion for any of this to be
 * here at all, and `processor_init` has to have been called with the pointers it computed.
 */

/*
 * The return type and the parameter are the real ones, and the parameter is the reason this probe
 * is placed here rather than at `processor_bootstrap`: `processor_data_init` is
 * `void processor_data_init(processor_t)`, one pointer in r0, so a probe that takes that argument
 * gets the kernel's own value rather than having to guess it. A declaration that disagreed with
 * the callee - the defect that cost exp-184 a run - is not possible for a one-register pointer,
 * but it is worth saying that this probe *uses* the argument rather than only receiving it, which
 * is what turns the placement into a measurement.
 */
void processor_data_init(void *processor);

extern int master_cpu;
extern void *master_processor;
extern uint8_t pset0[];
extern uint32_t processor_count;

#define PROC_STATE_OFF           8u    /* `str r9, [r4, #8]`, r9 = PROCESSOR_OFF_LINE = 0 */
#define PROC_IS_RECOMMENDED_OFF 16u    /* `str r0, [r4, #16]`, a bit test on pset0 */
#define PROC_SET_OFF            32u    /* `str r6, [r4, #32]`, r6 = &pset0 */
#define PROC_CPU_ID_OFF         56u    /* `str fp, [r4, #56]`, fp = the cpu_id argument */
#define PROC_QUANTUM_END_OFF   128u    /* after `add r0, r4, #64` for timer_call_setup */
#define PROC_DEADLINE_OFF      144u
#define PROC_PRIMARY_OFF      1236u    /* `str r4, [r4, #1236]` - the self-pointer */
#define PSET_CPU_SET_COUNT_OFF  52u    /* `ldr r0, [r6, #52]` ... `str r1, [r6, #52]` after the call */

static uint32_t rd32(const volatile uint8_t *base, uint32_t off)
{
    return *(const volatile uint32_t *)(const void *)(base + off);
}

void processor_data_init(void *processor)
{
    const volatile uint8_t *p = (const volatile uint8_t *)processor;

    entry_kv("xnu_entry_proc_ptr",       (uint32_t)(uintptr_t)processor);
    entry_kv("xnu_entry_master_processor", (uint32_t)(uintptr_t)master_processor);
    entry_kv("xnu_entry_pset0_addr",     (uint32_t)(uintptr_t)pset0);
    entry_kv("xnu_entry_master_cpu",     (uint32_t)master_cpu);
    entry_kv("xnu_entry_proc_cpu_id",    rd32(p, PROC_CPU_ID_OFF));
    entry_kv("xnu_entry_proc_state",     rd32(p, PROC_STATE_OFF));
    entry_kv("xnu_entry_proc_set",       rd32(p, PROC_SET_OFF));
    entry_kv("xnu_entry_proc_primary",   rd32(p, PROC_PRIMARY_OFF));
    entry_kv("xnu_entry_proc_is_recommended", rd32(p, PROC_IS_RECOMMENDED_OFF));
    entry_kv("xnu_entry_proc_quantum_end_lo", rd32(p, PROC_QUANTUM_END_OFF));
    entry_kv("xnu_entry_proc_quantum_end_hi", rd32(p, PROC_QUANTUM_END_OFF + 4u));
    entry_kv("xnu_entry_proc_deadline_lo",    rd32(p, PROC_DEADLINE_OFF));
    entry_kv("xnu_entry_pset0_cpu_set_count", rd32(pset0, PSET_CPU_SET_COUNT_OFF));
    entry_kv("xnu_entry_processor_count",     processor_count);

    entry_stub_hit("processor_data_init");
}
#endif /* STAGE90_ENTRY_REAL_ARM_INIT */

/*
 * The exception vector targets. The trampolines in `entry_vectors.s` branch here after loading SP
 * from `__entry_vectors_stack_top` - without that this code cannot run at all, because the banked
 * stack pointer the exception inherited is outside XNU's page tables. `_start` turns on
 * SCTLR.HIGHVEC, so if XNU's entry path takes any exception before reaching `arm_init` it lands in
 * one of these, and each one names itself, because which vector fired is the whole diagnostic.
 *
 * The two abort handlers also read the fault registers, which are only valid in the handler: DFAR
 * says which address was touched and DFSR says how, IFAR/IFSR the same for instruction fetches.
 * "Data abort" alone says a fault happened somewhere; `dfar=0x00000000` says the image stored
 * through a pointer it set to zero, and `dfar=0x0020xxxx` says the code was jumped to rather than
 * reached. The distance between those two diagnoses is the whole reason these four lines exist.
 */
void fleh_reset(void) { entry_epilogue("exception: reset"); }
void fleh_undef(void) { entry_epilogue("exception: undefined instruction"); }
void fleh_swi(void) { entry_epilogue("exception: svc/swi"); }

void fleh_prefabt(void)
{
    uint32_t ifar, ifsr;

    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));

    entry_kv("xnu_entry_prefetch_abort_ifar", ifar);
    entry_kv("xnu_entry_prefetch_abort_ifsr", ifsr);
    entry_epilogue("exception: prefetch abort");
}

void fleh_dataabt(void)
{
    uint32_t dfar, dfsr;

    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));

    entry_kv("xnu_entry_data_abort_dfar", dfar);
    entry_kv("xnu_entry_data_abort_dfsr", dfsr);
    entry_epilogue("exception: data abort");
}

void fleh_addrexc(void) { entry_epilogue("exception: address exception"); }
void fleh_irq(void) { entry_epilogue("exception: irq"); }
void fleh_decirq(void) { entry_epilogue("exception: decrementer irq"); }
