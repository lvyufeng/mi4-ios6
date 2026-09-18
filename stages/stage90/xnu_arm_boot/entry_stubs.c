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
 * `do_cpuid()` - `osfmk/arm/cpuid.c:85`, and the first symbol `cpu_init` asks for.
 *
 * Experiment 187 stopped at `timer_call_get_priority_params`, the first statement of
 * `timer_call_init_abstime`. This run links the object that defines it, so the whole of that
 * function is real now - including the loop that converts every threshold in arm_timer.c's table
 * from nanoseconds to `rtclock_sec_divisor` ticks - and `timer_call_init` and
 * `kernel_early_bootstrap` return, `arm_init` continues, and the next thing it asks for is
 * `cpu_init()`.
 *
 * That is what makes this probe one function further out than the last one rather than one symbol
 * further along. The last three probes were placed inside the function the frontier lived in.
 * This one cannot be: `timer_call_init_abstime` ends with the loop, `timer_call_init` ends with it,
 * and `kernel_early_bootstrap` (`startup.c:225`) ends with `timer_call_init()` - its caller is
 * `arm_init` (`arm_init.c:269`), whose next statement is `cpu_init()`. So the frontier moved from
 * "a function in `timer_call.c`" to "the first line of the next function in `arm_init`", and the
 * rule that finds it is still the same: read the function the frontier is in, and take the next
 * symbol in it that nothing defines.
 *
 * `cpu_init` (`cpu.c:198`) is `cdp = getCpuDatap(); if (cdp->cpu_type != CPU_TYPE_ARM) { ... }`,
 * and `getCpuDatap` is `current_thread()->machine.CpuDatap` (`cpu_data.h:79`) - which `arm_init`
 * set to `&BootCpuData` at `arm_init.c:250`, four statements before it called
 * `kernel_early_bootstrap`. So the condition is true on this CPU and the branch taken is the one
 * containing `do_cpuid`, `do_cacheid` and `do_mvfpid`. `pmap_cpu_data_init` is the *other*
 * branch's first call, defined here too so that the run reports which one was taken instead of
 * taking one and silently reaching the other; both are `void f(void)`, so neither can disagree
 * with the declaration the linker sees.
 *
 * The eight values are arm_timer.o's table and `timer_call.c`'s arithmetic over it, read back
 * through the two structs - which is the first time in this sequence that a newly linked object's
 * work can be checked against *its own source* rather than against the run continuing past it:
 *
 *   - `timer_call_get_priority_params()` is now the real one, so calling it is itself the check
 *     that `arm_timer.o` displaced the stub: a stub returns 0 and every read below faults.
 *   - `tcoal_prio_params_init` is `static` in `arm_timer.c` and cannot be named from here. Its
 *     four members are read at their offsets through the returned pointer instead.
 *   - `tcoal_prio_params` is the *destination*, and it is a global in `timer_call.c`
 *     (`timer_call.h:173`), which the image already links, so it is named directly.
 *
 * Every offset below is one the compiler used, read out of this image's own
 * `osfmk_kern_timer_call.o` rather than from the header - `timer_call_init_abstime` is `static` and
 * was inlined into `timer_call_init`, and its disassembly stores exactly these:
 *
 *     ldr r0, [r4]        -> str r1, [r6, #4]      source 0   dest 4    (idle)
 *     ldr r0, [r4, #8]    -> str r1, [r6, #12]     source 8   dest 12   (resort)
 *     add r0, r4, #12     -> add r1, r6, #16       shifts, 5 words, 12->16
 *     ldrd r8, [r4, #32]  -> add r2, r6, #40       rt_ns_max 32 -> 40
 *     ldr r0, [r4, #72]   -> str r0, [r6, #80]     scale 72 -> 80
 *     ldrd r8, [r4, #96]  -> add r2, r6, #104      qos[0] 96 -> 104
 *     ldr r2, [r4, #144]  -> str r2, [r6, #152]    rate_limited 144 -> 152
 *
 * where r4 is the ns struct and r6 the abstime one, and `str r4`/`r6` are the two MOVW/MOVT pairs
 * of `timer_call_get_priority_params` and `tcoal_prio_params`. The two layouts differ by exactly
 * the `powergate_latency_abstime` word the abstime struct has in front (`timer_call.h:151`), which
 * is why the same member is four bytes further along in r6 than in r4.
 *
 * The numbers they should hold, from the two sources:
 *
 *   ns[0]            5000 * NSEC_PER_USEC        = 5,000,000   = 0x004c4b40
 *   ns[96]           1 * NSEC_PER_MSEC           = 1,000,000   = 0x000f4240
 *   ab[4]   5,000,000 ns   x 19200000 / 1e9      =    96,000   = 0x00017700
 *   ab[12]  50,000,000 ns  x 19200000 / 1e9      =   960,000   = 0x000ea600
 *   ab[20]  tcoal_prio_params_init.timer_coalesce_bg_shift = -5 = 0xfffffffb
 *   ab[128] 75,000,000 ns  x 19200000 / 1e9      = 1,440,000   = 0x0015f900
 *   ab[136] 10,000,000,000 ns x 19200000 / 1e9   = 192,000,000 = 0x0b71b000
 *
 * 19200000 is the timebase `nanoseconds_to_absolutetime` divides by, measured from both directions
 * in exp-184 and exp-186. The five ns numbers are `timer_coalescing_priority_params_ns_t` in
 * `timer_queue.h:114` and the last two are `timer_coalescing_priority_params_t` in `timer_call.h:150`;
 * the ns struct is 168 bytes and `arm_timer.o`'s data section is 168 bytes, which is the check
 * that the table linked and the struct read are the same shape.
 *
 * `ab[20]` is worth its own line: the five shift fields are copied as one 20-byte block by
 * `vld1.32 {d16-d17}`/`vst1.32`, not field by field, and `timer_coalesce_bg_shift` is `-5`, so it is
 * the one value in that block that cannot be produced by zeroed memory or by a stale copy of the
 * wrong member. `ab[136]` is 10 seconds' worth of ticks - 192,000,000 - which is the largest number
 * in the table and the one furthest from any alignment padding.
 */
typedef struct timer_coalescing_priority_params_ns timer_coalescing_priority_params_ns_t;

/* Both real now: `timer_call_get_priority_params` from `arm_timer.o`, the other from `timer_call.o`. */
timer_coalescing_priority_params_ns_t *timer_call_get_priority_params(void);
extern uint8_t tcoal_prio_params[];
extern uint8_t past_deadline_timer_adjustment[];

/* Source = `timer_queue.h:114`; dest = `timer_call.h:150`. Offsets measured from the compiled code. */
#define NS_SRC_IDLE_OFF      0u
#define NS_SRC_QOS0_MAX_OFF  96u
#define AB_DST_IDLE_OFF      4u
#define AB_DST_RESORT_OFF    12u
#define AB_DST_BG_SHIFT_OFF  20u
#define AB_DST_QOS3_MAX_OFF  128u
#define AB_DST_QOS4_MAX_OFF  136u

static uint32_t rd32(const volatile uint8_t *base, uint32_t off)
{
    return *(const volatile uint32_t *)(const void *)(base + off);
}

static void tcoal_probe(const char *hit)
{
    timer_coalescing_priority_params_ns_t *src = timer_call_get_priority_params();

    entry_kv("xnu_entry_tcoal_src_idle_ns",      rd32((const volatile uint8_t *)src, NS_SRC_IDLE_OFF));
    entry_kv("xnu_entry_tcoal_src_qos0_ns",      rd32((const volatile uint8_t *)src, NS_SRC_QOS0_MAX_OFF));
    entry_kv("xnu_entry_tcoal_idle_abstime",     rd32(tcoal_prio_params, AB_DST_IDLE_OFF));
    entry_kv("xnu_entry_tcoal_resort_abstime",   rd32(tcoal_prio_params, AB_DST_RESORT_OFF));
    entry_kv("xnu_entry_tcoal_bg_shift",         rd32(tcoal_prio_params, AB_DST_BG_SHIFT_OFF));
    entry_kv("xnu_entry_tcoal_qos3_abstime_max", rd32(tcoal_prio_params, AB_DST_QOS3_MAX_OFF));
    entry_kv("xnu_entry_tcoal_qos4_abstime_max", rd32(tcoal_prio_params, AB_DST_QOS4_MAX_OFF));
    entry_kv("xnu_entry_past_deadline_adj",      rd32(past_deadline_timer_adjustment, 0u));

    entry_stub_hit(hit);
}

void do_cpuid(void)
{
    tcoal_probe("do_cpuid");
}

void pmap_cpu_data_init(void)
{
    tcoal_probe("pmap_cpu_data_init");
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
