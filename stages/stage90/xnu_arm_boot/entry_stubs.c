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
 * `processor_bootstrap()` - `osfmk/kern/processor.c:120`, and the next symbol `arm_init` reaches.
 *
 * Experiment 188 stopped at `do_cpuid`, the first symbol `cpu_init` asks for. This run links the
 * two objects that define it - `osfmk/arm/cpuid.o` and the `machine_cpuid.o` every one of its
 * `machine_*` references lives in - so the whole of the boot CPU's identification runs for real:
 *
 *     do_cpuid()      reads MIDR
 *     do_cacheid()    reads CLIDR, then CSSELR/CCSIDR for the L1 data cache and for L2
 *     do_mvfpid()     reads MVFR1
 *     do_debugid()    reads ID_DFR0 and, on a part that has one, DBGDIDR
 *     cpuid_info()    returns what the four above filled in
 *
 * and `cpu_init` finishes: the `switch (cpu_info_p->arm_info.arm_arch)` decides `cdp->cpu_subtype`
 * and the function returns. `arm_init` (`arm_init.c:269-274`) then does one store -
 * `EntropyData.index_ptr = EntropyData.buffer`, and `EntropyData` is defined in
 * `osfmk/prng/random.o`, which this image does not link, so it is a generated storage stub and the
 * store lands in `.bss` - and calls `processor_bootstrap()`, which nothing defines.
 *
 * This is the first object in the sequence whose work is reading the CPU. Every value reported
 * below is either a register the hardware reports or a number XNU computed from one, so the probe
 * is checking the new objects against the *device* rather than against the tree:
 *
 *   cpuid_info()->value     do_cpuid is `cpuid_cpu_info.value = machine_read_midr()` followed by
 *                           `bfi r0, r1, #16, #4` with r1 = 11 (the compiled code, not the
 *                           header: `ARMA7` makes `__ARM_SUB_ARCH__` `CPU_ARCH_ARMv7k` = 0xb), so
 *                           this word is MIDR with bits 19:16 replaced by 0xb. Our own payload
 *                           measured MIDR as 0x512f06f1 in experiment 06 (`cp15_midr`), so the
 *                           prediction is 0x512f06f1 -> **0x512b06f1**.
 *   cpuid_get_cpufamily()   switches on the implementor. `CPU_VID_` has ARM 0x41, DEC 0x44,
 *                           Motorola 0x4d, Marvell 0x56, Intel 0x69 and Apple 0x61 - and MIDR's
 *                           implementor here is 0x51, which has no case, so this is the `default:`
 *                           and the prediction is **0** (`CPUFAMILY_UNKNOWN`).
 *   BootCpuData.cpu_type    `cpu_init`'s first statement is `cdp->cpu_type != CPU_TYPE_ARM`, and
 *                           the compiled test is `ldr r0, [r4, #56]; cmp r0, #12`, so after the
 *                           `mov r0, #12 / str r0, [r4, #56]` that follows it, offset 56 of
 *                           `BootCpuData` holds **12**.
 *   BootCpuData.cpu_subtype the switch's answer, stored at offset 60. arm_arch is 0xb, the jump
 *                           table is indexed by `(arch - 2)` over 10 entries, and entry 9 is
 *                           `mov r0, #12` - `CPU_SUBTYPE_ARM_V7K`. Prediction **12**.
 *   cache_info()            what `do_cacheid` computed from its two CCSIDR reads. Offsets 4 and 12
 *                           are both the same product for the L1 data cache (**0x4000**, 16 KB -
 *                           XNU writes the D size into the I field too, which is why both are the
 *                           same number). Offsets 24, 28 and 32 are the L2's line size (**0x80**,
 *                           128 bytes), associativity (**8**) and size (**0x200000**, 2 MB).
 *                           Offsets 0 and 20 are `(Ctype1 == 0x4) ? 1 : 0` for a unified L1
 *                           (**0** - Krait's L1 is separate) and the CCSIDR type mapping. The
 *                           second of those came out **4**, `CACHE_UNKNOWN`, where the predication
 *                           said 1, and the reason is a field in the wrong place: XNU declares
 *                           `c_type` as a 4-bit field of the CCSIDR at bits 31:28
 *                           (`cpuid.c:72`) and switches over the one-hot nibble values 1, 2, 4
 *                           and 8, while the ARM ARM puts CCSIDR.Type at [2:0] and leaves [31:28]
 *                           RES0 - so on a conforming implementation the field XNU reads is always
 *                           zero and the switch always falls to its `default:`. The compiled code
 *                           is that switch: `add r3, r0, r1, lsr #28` with r0 = -1 indexes a table
 *                           over 1..8 and leaves `mov r2, #4` in place. The other three CCSIDR
 *                           fields XNU reads *are* the ARM ARM's (`and r3, r1, #7` for LineSize at
 *                           [2:0], `ubfx r1, r1, #3, #10` for Assoc at [12:3], `ubfx r6, r1, #13,
 *                           #15` for NumSets at [27:13]), which is what makes the type the odd one
 *                           out rather than the struct being for another register entirely.
 *
 *                           The first full run of this experiment is what makes offsets 24 and 28
 *                           worth naming twice. It predicted 0x40 at 24 and 0x200 for the colors
 *                           below, and read 0x80 and 0x40. The cause is one line of the source:
 *                           `do_cacheid` writes `c_linesz` and `c_assoc` once for L1
 *                           (`cpuid.c:231-232`) and then **again** for L2 inside the level-2 block
 *                           (`cpuid.c:275-276`), so by the time the function returns both fields
 *                           hold the L2's geometry and the L1's is gone. It is visible in the
 *                           compiled code as `stm ip, {r2, r3, r6}` at offset 20 and then
 *                           `str r2, [r5, #24]` / `str r7, [r5, #28]` in the second block - the
 *                           same two offsets, written twice.
 *   vm_cache_geometry_colors the global `do_cacheid` writes into. `cpuid.c:290` is
 *                           `((NumSets + 1) * c_linesz) / PAGE_SIZE`, and `c_linesz` is the L2's,
 *                           so this is the L2's **sets x line size / 4096** and the L1's 16 KB is
 *                           not in it at all: 2048 x 128 / 4096 = **0x40**. It is defined in
 *                           `osfmk/vm/vm_resident.o`, which this image does not link, so it is a
 *                           generated storage stub here and the store lands in `.bss` harmlessly.
 *
 *                           The three L2 numbers then have to agree with each other, and they do
 *                           exactly: sets x line size x ways = 2048 x 128 x 8 = 2,097,152 =
 *                           0x200000 = the measured `c_l2size`, with the sets from the measured
 *                           colors and the line size measured directly. That is what makes the
 *                           prediction for `c_assoc` a real one rather than a restatement.
 *   arm_mvfp_info()         `mrc mvfr0` then `vmrs r0, mvfr1`, and the compiled code keeps only
 *                           the second: `ubfx r1, r0, #20, #4` -> offset 4, `ubfx r0, r0, #16, #4`
 *                           -> offset 0. So this is MVFR1's HPFP nibble and SP nibble, and the two
 *                           values are reported rather than predicted, because the A7 the tree
 *                           names is not the A7 this device has and MVFR1 is the register that
 *                           says so.
 *
 * Every offset above is one the compiler used, read out of this image's own
 * `osfmk_arm_cpuid.o`, `osfmk_arm_machine_cpuid.o` and `osfmk_arm_cpu.o` - the same rule as
 * exp-186's `add r0, r4, #28` and exp-188's `str r1, [r6, #4]`. The `mov r1, #11` in `do_cpuid` is
 * the reason this matters: the header says `CPU_ARCH_ARMv7` is 8, and the code says 11, because
 * `ARMA7` selects `__ARM_SUB_ARCH__` before it can select `CPU_ARCH_ARMv7`. Reading the header
 * instead would have predicted an arch nibble of 8 and a `cpu_subtype` of 9.
 *
 * `vmrs` needs the VFP coprocessor enabled, which `_start` does twice: `CPACR |= 0xf << 20`
 * (`start.s:371-377`) and `FPEXC.EN` (`start.s:404-413`), the second with the comment that VFP is
 * enabled for the `arm_init` path precisely so it cannot take an undefined-instruction fault before
 * there is a handler. Exp-188 confirmed it on this device the hard way: the shift block in
 * `timer_call_init` is copied with `vld1.32`/`vst1.32`, and the run read back the right shifts.
 *
 * Both branches of `cpu_init`'s `if (cdp == &BootCpuData)` have their first call defined here -
 * `do_cpuid` is real now, and `pmap_cpu_data_init` stops and names itself - so a run that took the
 * other branch says so instead of stopping somewhere further along with no explanation.
 *
 * ------------------------------------------------------------------ `kprintf` in this kernel
 *
 * The first run of this experiment did not reach the probe. It stopped at
 * `stub_hit=_consume_kprintf_args`, which is `do_cacheid`'s own debug print: `cpuid.c:290` and
 * `:298` both `kprintf(...)` the cache geometry they have just computed, and the print is the
 * first thing after the last store, so linking `cpuid.o` put it on the path.
 *
 * `_consume_kprintf_args` is not a print. This kernel is built with `CONFIG_NO_KPRINTF_STRINGS=1`
 * - `RELEASE` is `build_xnu_arm_kernel.sh`'s default configuration (`:84`), Apple's
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
 * So the probe defines it, and the definition is not an approximation of the real one - an empty
 * variadic function is the real one. Linking `printf.o` to obtain those four bytes would bring
 * 5607 bytes of text across 23 functions and 24 obligations the image does not have, nearly all of
 * them the console stack (`cnputc`, `cnputc_unbuffered`, `PE_kputc`, `console_printbuf_*`,
 * `debug_putc`, `disable_serial_output`, `os_log_with_args`, `paniclog_flush`, `bsd_log_init`) that
 * `no_printf_str` and `no_kprintf_str` are the configuration's way of keeping out. The print
 * subsystem is not on this kernel's boot path by construction, and the first run is the evidence:
 * the only thing that stopped the image was the function that means "nothing to print".
 */

/*
 * `osfmk/kern/printf.c:197`, an empty variadic function. The `(void)a` is for `-Wunused-parameter`;
 * the body stays empty so the compiled code is the `bx lr` the real one is.
 */
void _consume_kprintf_args(int a, ...)
{
    (void)a;
}

typedef struct { uint32_t neon; uint32_t neon_hpfp; } arm_mvfp_info_t;

/*
 * Return types matter here, and they are the one thing the compiler cannot check for us: these are
 * the real signatures from `cpuid.h`/`machine_cpuid.h`. A pointer returned where a `uint64_t` was
 * declared is the class of defect that cost exp-184 a run (there through *out-parameters*, which
 * leave a register the callee then stores through); a `void *` against a real pointer type is not,
 * because both are one register wide.
 */
void *cpuid_info(void);
void *cache_info(void);
arm_mvfp_info_t *arm_mvfp_info(void);
int cpuid_get_cpufamily(void);

/* `data.s` and, until vm_resident.o is linked, a generated storage stub; both typed as bytes. */
extern uint8_t BootCpuData[];
extern uint8_t vm_cache_geometry_colors[];

#define CPU_INFO_VALUE_OFF       0u    /* arm_cpu_info_t.value; arm_info.arm_arch is bits 19:16 */
#define CACHE_UNIFIED_OFF        0u    /* `(Ctype1 == 0x4) ? 1 : 0` - Krait's L1 is separate */
#define CACHE_ISIZE_OFF          4u    /* do_cacheid writes the L1D size here */
#define CACHE_DSIZE_OFF         12u    /* ... and here */
#define CACHE_TYPE_OFF          20u    /* CACHE_UNKNOWN = 4 - the nibble XNU reads is [31:28] */
#define CACHE_LINESZ_OFF        24u    /* written twice: L1's line size, then L2's */
#define CACHE_ASSOC_OFF         28u    /* written twice as well */
#define CACHE_L2SIZE_OFF        32u
#define MVFP_NEON_OFF            0u    /* MVFR1[19:16] */
#define MVFP_HPFP_OFF            4u    /* MVFR1[23:20] */
#define BOOTCPU_TYPE_OFF        56u    /* `ldr r0, [r4, #56]; cmp r0, #12` */
#define BOOTCPU_SUBTYPE_OFF     60u    /* `str r0, [r4, #60]`, from the arm_arch switch */

static uint32_t rd32(const volatile uint8_t *base, uint32_t off)
{
    return *(const volatile uint32_t *)(const void *)(base + off);
}

void processor_bootstrap(void)
{
    const volatile uint8_t *ci = (const volatile uint8_t *)cpuid_info();
    const volatile uint8_t *cc = (const volatile uint8_t *)cache_info();
    const volatile uint8_t *mv = (const volatile uint8_t *)arm_mvfp_info();

    entry_kv("xnu_entry_cpuid_value",    rd32(ci, CPU_INFO_VALUE_OFF));
    entry_kv("xnu_entry_cpuid_family",   (uint32_t)cpuid_get_cpufamily());
    entry_kv("xnu_entry_cpu_type",       rd32(BootCpuData, BOOTCPU_TYPE_OFF));
    entry_kv("xnu_entry_cpu_subtype",    rd32(BootCpuData, BOOTCPU_SUBTYPE_OFF));
    entry_kv("xnu_entry_cache_unified",  rd32(cc, CACHE_UNIFIED_OFF));
    entry_kv("xnu_entry_cache_isize",    rd32(cc, CACHE_ISIZE_OFF));
    entry_kv("xnu_entry_cache_dsize",    rd32(cc, CACHE_DSIZE_OFF));
    entry_kv("xnu_entry_cache_type",     rd32(cc, CACHE_TYPE_OFF));
    entry_kv("xnu_entry_cache_linesz",   rd32(cc, CACHE_LINESZ_OFF));
    entry_kv("xnu_entry_cache_assoc",    rd32(cc, CACHE_ASSOC_OFF));
    entry_kv("xnu_entry_cache_l2size",   rd32(cc, CACHE_L2SIZE_OFF));
    entry_kv("xnu_entry_cache_colors",   rd32(vm_cache_geometry_colors, 0u));
    entry_kv("xnu_entry_mvfp_neon",      rd32(mv, MVFP_NEON_OFF));
    entry_kv("xnu_entry_mvfp_hpfp",      rd32(mv, MVFP_HPFP_OFF));

    entry_stub_hit("processor_bootstrap");
}

/* The other branch of `cpu_init`'s boot-CPU test, defined so the run reports which one it took. */
void pmap_cpu_data_init(void)
{
    entry_kv("xnu_entry_cpu_init_other_branch", 1u);
    entry_stub_hit("pmap_cpu_data_init");
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
