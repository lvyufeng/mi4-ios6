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
 *   - It runs with XNU's page tables live, which map only [physBase, physBase + memSize) - 8 MB
 *     here. `ram_console` is at 0xde500000, far outside it, so it cannot be written yet.
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
 *
 * They are stand-ins only while `arm_vm_init` is not in the image. Experiment 194 linked
 * `osfmk/arm/arm_vm_init.o`, which defines all three, so with `STAGE90_ENTRY_REAL_ARM_INIT=1` the
 * definitions here are a duplicate and are removed - and the values then come from
 * `arm_vm_init:351-353`, which assigns them from the boot_args rather than from a constant. That
 * is also the reason these three were never allowed to be `const`: the real definitions are
 * assigned to, in place, and a stand-in that cannot be written would fault the moment
 * `arm_vm_init` ran.
 */
#ifndef STAGE90_ENTRY_REAL_ARM_INIT
unsigned long gPhysBase = 0x00200000ul;
unsigned long gPhysSize = 0x00200000ul;
unsigned long gVirtBase = 0x00200000ul;
#endif

uint32_t kdebug_enable;

/* ------------------------------------------------------------------ the version strings */

/*
 * `version` and `osversion` - two symbols whose object this project does not compile, and the only
 * two the entry image has ever held as the wrong *kind* of stand-in.
 *
 * `libkern/libkern/version.h.template:105-109` declares them
 *
 *     extern const char version[];
 *     #define OSVERSIZE 256
 *     extern char osversion[];
 *
 * and `config/version.c:3,17` defines them - but that file is a *template*: its strings carry
 * Apple's `###KERNEL_VERSION_LONG###` / `###KERNEL_BUILD_DATE###` placeholders, substituted by a
 * build step this project does not run, so it is never compiled here.
 *
 * That left both symbols undefined, and `build_entry.sh`'s generator takes a storage symbol's size
 * from `nm -S` over this project's object pool - where neither name appears at all. Its `case` falls
 * through to the function branch and emits `void version(void) { entry_stub_hit("version"); }`,
 * which links, and which nothing here would ever trip over, because the only thing in this image
 * that touches either name is `osfmk/arm/lowmem_vectors.c:37-41`, taking their *addresses* for a
 * structure a debugger reads. It is still a stand-in that lies about what it is: a reader of the
 * linker map would find two functions where the kernel has two strings, and if anything ever
 * printed `version` it would print code.
 *
 * So they are defined here, at the size and of the type `version.h.template` states. The string is a
 * placeholder and is written to look like one, because XNU's real value is assembled from the build
 * date and builder of a build this project does not perform - a plausible-looking date here would be
 * a number no measurement produced, which is the one thing this project does not ship.
 */
const char version[] = "Darwin Kernel Version ###not-built-by-apple###";
char osversion[256];

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
 * One character into the same buffer `entry_write` appends to. Split out because the callers below
 * build a string a character at a time from a value in a register, and there is no room in this
 * image's `.bss` for a formatting buffer that would survive the teardown anyway.
 */
static void entry_putc(volatile uint8_t *data, uint32_t *size_p, uint32_t max, char c)
{
    if (*size_p < max) {
        data[*size_p] = (uint8_t)c;
        *size_p = *size_p + 1u;
    }
}

/*
 * ` key=0xvalue` straight into the ram_console, one character at a time.
 *
 * The difference between this and `entry_write` is not the destination, it is where the characters
 * come from. `entry_write` takes a string, so a *value* would have to be formatted into memory
 * first - and the only memory reachable before the teardown is this image's own `.bss`, which is
 * cacheable, which makes the formatting subject to exactly the cache problem the teardown exists to
 * solve. This takes the value as a *register* argument and never stores it anywhere, so it survives
 * the teardown by construction. That is what makes it usable for reporting on the teardown itself,
 * and it is why experiment 195's log could not say what it was supposed to say.
 *
 * `key` must be a string in this image; `.rodata` lands inside `.text`, inside the window, so
 * reading it with the MMU off is a physical read of this image, which is where it is.
 */
static void entry_write_kv(const char *key, uint32_t value)
{
    static const char hex[] = "0123456789abcdef";
    volatile uint32_t *sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    volatile uint32_t *size_p = (volatile uint32_t *)(uintptr_t)(RAM_CONSOLE_BASE + 8u);
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)(RAM_CONSOLE_BASE + 12u);
    const uint32_t max = 0x00200000u - 12u;
    uint32_t size;

    if (*sig != RAM_CONSOLE_SIG) {
        /* Nothing sane to append to; start a fresh buffer so the line is not lost. */
        *sig = RAM_CONSOLE_SIG;
        size = 0u;
    } else {
        size = *size_p;
    }

    entry_putc(data, &size, max, ' ');
    while (*key != '\0') {
        entry_putc(data, &size, max, *key++);
    }
    entry_putc(data, &size, max, '=');
    entry_putc(data, &size, max, '0');
    entry_putc(data, &size, max, 'x');
    for (unsigned i = 0; i < 8u; i++) {
        entry_putc(data, &size, max, hex[(value >> (28u - (i * 4u))) & 0xfu]);
    }
    entry_putc(data, &size, max, '\n');

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

    /*
     * Read the state of the results buffer *before* anything is torn down, and hold it in
     * callee-saved registers rather than in memory.
     *
     * This is what experiment 195 cost. `g_kv_len` and `g_kv_buf` live in this image's `.bss`, which
     * is cacheable, so everything the probes record sits in the D-cache until this function gets it
     * to DRAM. When that transfer fails the log shows an *empty* buffer, and an empty buffer cannot
     * be told apart from a probe that never ran - which is exactly the ambiguity that run produced.
     * A value in a register cannot be lost that way, so it is the honest report of what was written.
     */
    register uint32_t kv_len_written __asm__("r8");
    register uint32_t csselr_before __asm__("r9");
    register uint32_t ccsidr_before __asm__("r10");
    register uint32_t ccsidr_l1 __asm__("r11");

    kv_len_written = g_kv_len;

    /*
     * CCSIDR describes whichever cache CSSELR selects, and *nothing here ever selected one*.
     *
     * Experiment 195 measured what that costs. The sweep below enumerates the D-cache by set and
     * way from CCSIDR, and the value it was handed - `cssidr_before` below, 0xf0ffe03b on this
     * device - decodes to 4096 sets of 8 ways of 128-byte lines, four megabytes. That is not a
     * 16-or-32 KB L1, so the sweep was enumerating an L1-shaped address space from a description of
     * something else, and the results buffer did not reach DRAM: the log came out with an empty
     * buffer and no way to say whether the probe had run. `cache_ops.c` states the same assumption
     * ("which defaults to the L1 data cache") and gets away with it because the payload is the only
     * thing that has run when it does this.
     *
     * So CSSELR is now written rather than assumed. The two readings are kept and printed because
     * the pair is the evidence: what the sweep would have used, and what the L1 actually is.
     */
    __asm__ volatile ("mrc p15, 2, %0, c0, c0, 0" : "=r"(csselr_before));
    __asm__ volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr_before));
    __asm__ volatile ("mcr p15, 2, %0, c0, c0, 0" :: "r"(0u) : "memory");   /* level 1 data */
    __asm__ volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr_l1));

    __asm__ volatile ("cpsid if" ::: "memory");

    /*
     * Clean the results buffer BY ADDRESS, before the sweep by set and way below.
     *
     * The sweep is only as right as the geometry it enumerates, and experiment 195 is what a sweep
     * that misses the buffer's lines costs: they stay dirty, the log reads back the zeroes `.bss`
     * was filled with, and the measurement is lost. A clean by MVA names the lines directly and
     * cannot miss them; 32-byte steps cover any line size this core has, since the hardware ignores
     * the bits below the line. This is what makes the results arrive; the sweep stays as the
     * backstop that covers every *other* line this image wrote.
     */
    for (uintptr_t p = (uintptr_t)&g_kv_len; p < (uintptr_t)&g_kv_buf[ENTRY_KV_BUF]; p += 32u) {
        __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(p) : "memory");
    }
    __asm__ volatile ("dsb sy" ::: "memory");

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
     * Geometry from CCSIDR read with the L1 selected, and the operand built the way XNU builds
     * it: the set field starts at the line size and the *way* is right-justified at bit 31, which is
     * what `MMU_I7WAY` means in `osfmk/arm/proc_reg.h` (30 for a 4-way cache, 31 for 2, 29 for the
     * L2's 8). Putting the way at `line_log2 + log2(ways)` instead - where this loop used to put it -
     * lands it inside the set field, so no way is ever selected.
     */
    {
        uint32_t ccsidr, line_log2, ways, sets, way_shift, way, set, n, w;

        ccsidr = ccsidr_l1;
        line_log2 = (ccsidr & 0x7u) + 4u;
        ways = ((ccsidr >> 3) & 0x3ffu) + 1u;
        sets = ((ccsidr >> 13) & 0x7fffu) + 1u;
        n = 0u;
        for (w = ways; w > 1u; w >>= 1) {
            n++;
        }
        way_shift = 32u - n;

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

    /*
     * Printed unconditionally, and that is the point of them.
     *
     * `xnu_entry_kv_written` is what the probes recorded, read from a register, so it is right even
     * if the transfer to DRAM was not. `xnu_entry_kv_in_dram` is what the transfer actually
     * produced - they differ only when it failed, and an empty results buffer with a non-zero
     * `kv_written` is a failed transfer rather than a probe that never ran. The three cache values
     * are the inputs the sweep runs on: what CSSELR was, what CCSIDR said through it, and what it
     * says once the level 1 data cache is selected.
     */
    entry_write_kv("xnu_entry_kv_written", kv_len_written);
    entry_write_kv("xnu_entry_kv_in_dram", g_kv_len);
    entry_write_kv("xnu_entry_csselr_before", csselr_before);
    entry_write_kv("xnu_entry_ccsidr_before", ccsidr_before);
    entry_write_kv("xnu_entry_ccsidr_l1", ccsidr_l1);

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
 * `pmap_bootstrap()` - `osfmk/arm/pmap.c:2764`. **Retired as a probe by experiment 197**, which
 * links `osfmk_arm_pmap.o`: the real function now defines this symbol, so this one is compiled out
 * and the twelve values below cannot be taken again. They are kept, and kept compiling under their
 * own switch, because they are the record of what `arm_vm_init`'s arithmetic produced on this
 * device - and because the same twelve names are the ones to compare against if this probe is ever
 * needed again on a different boot_args.
 *
 * Experiment 194 stopped at `vm_set_page_size` (`osfmk/vm/vm_resident.c:480`) because that object
 * was not in the image. Experiment 195 linked `osfmk_vm_vm_resident.o`, so `vm_set_page_size` ran -
 * it is twelve statements with no calls in it - and the front of `arm_vm_init` continued through
 * `set_mmu_ttb`, `set_mmu_ttb_alternate` and `flush_mmu_tlb`, all real, and the block of `vm_*`
 * stores that follows them, which are stores into this object's own globals.
 *
 * So this was the first probe in a while that fired, and the values below are the whole of
 * `arm_vm_init`'s arithmetic read back out of the globals it wrote. `arm_vm_init` is where the
 * kernel decides what memory it has, and every number here is derived from the boot_args the
 * payload built and the Mach-O header exp-194 added:
 *
 *   `avail_start` is `args->topOfKernelData + 4 pages + 6 pages` - the boot translation table
 *   copied to the page after `boot_tte`, then six pages reserved - and `avail_end` is
 *   `gPhysBase + mem_size`, where `mem_size` is `args->memSize` clamped against the `xmaxmem` that
 *   exp-193 measured as 0x5e500000 and therefore left alone.
 *
 *   `sane_size` is `mem_size - (avail_start - gPhysBase)`, the memory left after the kernel's own
 *   tables, and `end_kern` is `round_page(getlastaddr())` - the end of the image **as the Mach-O
 *   header describes it**, which is the number exp-194's `entry_macho.s` exists to make true.
 *
 *   `next_paddr` is the argument. `pmap_bootstrap` is called as
 *   `pmap_bootstrap((gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000)` with
 *   `MEM_SIZE_MAX = 0x40000000` (`arm_vm_init.c:134`), so it is the first physical address above a
 *   1 GB window rounded to a 4 MB boundary: `(0x00200000 + 0x40000000 + 0x3FFFFF) & 0xFFC00000`.
 *   It is the value the pmap is told it may start allocating from, and the object that uses it is
 *   now linked.
 *
 * `vm_kernel_slide` is `gVirtBase - 0x80000000`, which underflows in a 32-bit `vm_offset_t` and is
 * reported as measured for that reason: the field exists because a real kernel is linked at
 * 0x80000000 and this one is linked at 0x00200000, so the "slide" is negative and the value is what
 * that does to an unsigned word.
 *
 * `CPSR` is recorded for the same continuity as the last two experiments: the F bit is not a usable
 * check on this device (exp-193), so nothing here depends on it.
 */
#ifndef STAGE90_ENTRY_REAL_PMAP_BOOTSTRAP
void pmap_bootstrap(uint32_t next_paddr);

extern uint32_t cpu_ttep;
extern uint32_t avail_start;
extern uint32_t avail_end;
extern uint32_t gVirtBase;
extern uint32_t gPhysSize;
extern uint32_t mem_size;
extern uint32_t static_memory_end;
extern uint32_t end_kern;
extern uint32_t sane_size;
extern uint32_t vm_kernel_slide;

void pmap_bootstrap(uint32_t next_paddr)
{
    uint32_t cpsr;

    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));

    entry_kv("xnu_entry_pmb_next_paddr",     next_paddr);
    entry_kv("xnu_entry_pmb_cpsr",           cpsr);
    entry_kv("xnu_entry_pmb_cpu_ttep",       cpu_ttep);
    entry_kv("xnu_entry_pmb_avail_start",    avail_start);
    entry_kv("xnu_entry_pmb_avail_end",      avail_end);
    entry_kv("xnu_entry_pmb_gvirtbase",      gVirtBase);
    entry_kv("xnu_entry_pmb_gphyssize",      gPhysSize);
    entry_kv("xnu_entry_pmb_mem_size",       mem_size);
    entry_kv("xnu_entry_pmb_static_mem_end", static_memory_end);
    entry_kv("xnu_entry_pmb_end_kern",       end_kern);
    entry_kv("xnu_entry_pmb_sane_size",      sane_size);
    entry_kv("xnu_entry_pmb_kernel_slide",   vm_kernel_slide);

    entry_stub_hit("pmap_bootstrap");
}
#endif /* !STAGE90_ENTRY_REAL_PMAP_BOOTSTRAP */

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
