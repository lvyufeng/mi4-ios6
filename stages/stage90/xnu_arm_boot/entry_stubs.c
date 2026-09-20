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

/* `struct arm_saved_state`'s six offsets, one definition for the image and for the check that
 * compares them against Apple's header and this configuration's generated `assym.s`. See the file. */
#include "entry_saved_state.h"

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

/*
 * `bsd/sys/kdebug.h:1034` declares it, and `osfmk/arm/start.s` references `_kdebug_enable` by name -
 * which is why a stand-in has been here since before `arm_init` was real, long before anything in the
 * image could have cared what the variable *is*.
 *
 * **Retired by experiment 225**, which links `bsd_kern_kdebug.o` for `kernel_debug_string_early`. That
 * object defines it (`bsd/kern/kdebug.c:310`, `unsigned int kdebug_enable = 0;`, `nm -S` says `B 0 4`),
 * so the stand-in becomes a duplicate and is compiled out. It is the first collision in this file that
 * the object list did not predict: nothing in the last three hundred experiments had linked the object
 * that owns the name, and the link is what said so.
 *
 * The replacement is *equivalent* to the stand-in - same size, same type (a 32-bit unsigned), same
 * zero initial value - which distinguishes it from `EntropyData` above, whose replacement is an
 * initialized struct and where the stand-in was the right size and the wrong value. Nothing is
 * discovered by retiring this one; there is no measurement here to have got wrong.
 */
#ifndef STAGE90_ENTRY_REAL_KDEBUG_ENABLE
uint32_t kdebug_enable;
#endif /* !STAGE90_ENTRY_REAL_KDEBUG_ENABLE */

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
 * through to the function branch and emits `void version(void) { entry_stub_hit("version", <lr>); }`,
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
 *
 * Retired by experiment 211, the step that linked `osfmk_prng_random.o`. That is the fifth
 * stand-in this project has retired under its own switch, after `arm_init` (198), the
 * `pmap_bootstrap` probe (200), `_consume_kprintf_args` (201) and `panic` (201) - and the second
 * whose replacement is an *initialized* value rather than zero, after the four regions of
 * `data.s` (196). `entropy_data_t EntropyData = { .index_ptr = EntropyData.buffer }` is why that
 * object's `.data` is 488 bytes rather than empty: this stand-in was the right size and the wrong
 * value, which is a class of defect a size check cannot catch.
 *
 * The guard is not incidental. Without it the link reports `multiple definition of 'EntropyData'`
 * - the same signal that ends every probe in this project, arriving here from a stand-in that had
 * been left unguarded because nothing had ever linked its object.
 */
#ifndef STAGE90_ENTRY_REAL_ENTROPY_DATA
uint8_t EntropyData[68] __attribute__((aligned(8)));
#endif /* !STAGE90_ENTRY_REAL_ENTROPY_DATA */

/*
 * `kperf_pending_ipis` - the counter experiment 468's configuration change made undefined, supplied
 * as the object rather than as a stand-in.
 *
 * `osfmk/kperf/kperf.h:139` declares it `extern _Atomic long long kperf_pending_ipis;`, and the only
 * definition in the tree is `osfmk/kperf/kperfbsd.c:77`. That file is the one source file in this
 * configuration that does not compile - `bsd/libkern/libkern.h:145` declares `ffs(int)` where
 * `osfmk/kern/misc_protos.h:70` declares `ffs(unsigned int)`, and the two meet in that file's
 * include set - so `osfmk/kperf/arm/kperf_mp.c:90`'s
 * `atomic_fetch_add_explicit(&kperf_pending_ipis, 1, ...)` had nothing to resolve to once
 * `development` put that object's code on the link's undefined list. It was not undefined before,
 * which is why this is 468's and not an older step's.
 *
 * **The object and not a stand-in, and the reason is the one `tools/check_stub_kinds.py` exists
 * for.** The generator decides function-or-storage from `nm` over the object pool, and a name no
 * object defines has no type information at all, so it falls through to a *function* stand-in. A
 * function stand-in that is *written to* - which is what `atomic_fetch_add_explicit` does - is a
 * store into `.text`. The undefined list gives names and never kinds; this is the kind, and the
 * check refuses the build rather than shipping one.
 *
 * What is written here is the definition rather than an approximation of one: the type is the
 * header's `_Atomic long long`, and 0 is what a count of pending kperf IPIs is before any have been
 * sent. Nothing in this boot schedules kperf - the kperf syscall is unreachable and its file is not
 * linked - so the counter is touched by no one, and its job is to be a real 8-byte object at the
 * address that one IPI handler would use.
 */
_Atomic long long kperf_pending_ipis = 0;

/* ------------------------------------------------------------------ bpfread_filtops - RETIRED */

/*
 * **This stand-in is gone, and it is the same retirement as `etherbroadcastaddr` and `lo_ifp` in the
 * generated stubs - the cause was one row of a hand-written table.** It was written in experiment 279
 * for exactly one reason: `bsd/net/bpf.c` is `optional bpfilter` in `bsd/conf/files:192`, the
 * hand-written device table did not select `bpfilter`, so bpf.c was not in the manifest and no object
 * in the pool defined `bpfread_filtops` - while `bsd/kern/kern_event.c:390` declares it and `:448`
 * puts its address in `kern_event_filtops[]` at `EVFILTID_BPFREAD`.
 *
 * Experiment 440 derived the device conditions from the configuration, which declares
 * `pseudo-device bpfilter 4 init bpf_init`. `bsd/net/bpf.o` is now in the pool and defines
 * `bpfread_filtops` as `SECURITY_READ_ONLY_EARLY(struct filterops)` - a real 0x28-byte `.rodata`
 * table, and the *measurement* of the size this file had derived two ways (fifteen sibling filter
 * tables at 0x28, and `struct filterops` = 2 + 2 padding + 9 * 4). Both roads were right; the value
 * is no longer zero.
 *
 * Leaving it in place does not compile the image: the link reports `multiple definition of
 * 'bpfread_filtops'` between `bsd_net_bpf.o (.rodata+0x0)` and this file. **That link error is the
 * check** - a hand-written stand-in that outlives its cause is otherwise silent in the one direction
 * that matters, because a zero table and a real table are the same number of bytes.
 *
 * The 40 bytes this file used to place are gone from `.bss`; the object's own section is in the
 * copied image, which is the one difference that is not a wash.
 */

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

/* The image's own extent, defined by `entry.ld` and used to decide what is worth dereferencing. */
extern char __entry_text_start[];
extern char __entry_image_end[];

/*
 * The stack the exception handlers run on, and why they need one of their own.
 *
 * `_start` sets the SVC stack to `intstack_top - SS_SIZE` (`start.s:310-311`) and does not touch
 * the banked stack pointers at all. So when an exception is taken, SP becomes whatever that mode
 * was left with - set by the bootloader or by the payload for *its* page tables, not for XNU's.
 * XNU's tables map `[physBase, physBase + memSize)` and nothing else, and the payload is at
 * 0x00008000, so an inherited stack pointer is outside the map. (Under experiment 241's base that
 * window is `[0x80000000, 0x80800000)`; before it, `[0x00200000, 0x00a00000)`.)
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
 *
 * The size is a real limit, and until experiment 268 it failed **silently**: `entry_kv` returning
 * without writing left the caller with no way to tell "this key was never set" from "this key was
 * dropped", and the log then reads as if the code that would have set it never ran. Experiment 268
 * lost the second half of a trace that way and very nearly concluded that `ipc_table_init`'s two
 * `kalloc` calls never happened, when the last record it wrote was simply the last one that fit.
 * The counter below is the repair: every dropped record increments `g_kv_dropped`, and the epilogue
 * reports it as `xnu_entry_kv_dropped` next to `xnu_entry_kv_written`, so a full buffer is a number
 * in the log rather than an absence.
 *
 * Experiment 237 grew the buffer 768 -> 1024 for the same reason, and experiment 240 grew it 1024 ->
 * 2048, which is what experiment 240's 25 keys need at the 40 bytes each call reserves - the run
 * wrote 831 bytes for those 25, and the reserve is deliberately pessimistic. Experiment 269 grows it
 * 2048 -> 8192: 268's instrument writes five records per allocation and the boot reaches
 * `ipc_voucher_init` with far more allocations than 2048 bytes hold (268 measured 2038 bytes for the
 * first six `kalloc` calls and nine `kernel_memory_allocate` calls, and the region it needed to
 * observe was still ahead of it). It costs `.bss` only - the buffer is zero-initialized - and the
 * headroom below `topOfKernelData` is over 1.2 MB. What it moves is the image's `.bss` end, and with
 * it the *derived* `boot_args` offset, so the payload has to be rebuilt from the regenerated header;
 * that is the layout block working, not a hazard, because nothing in it is hard-coded.
 *
 * One layout property is *not* free, and has to be re-checked whenever this grows: the exception
 * handlers run on `entry_vectors_stack`, whose top `entry_vectors.s` defines as the end of its own
 * `.space`, and that top must stay at or below this buffer's first byte or a handler frame would
 * land inside the results. It holds for 268 and 269 - `entry_vectors_stack` is
 * 0x800f7f80..0x800f8f80, the stack top (and so the handlers' first push, at top-4) is 0x800f8f80,
 * and `g_kv_buf` starts above it (0x800f8f98 in 269's canon build, 0x800f9004 after the 269
 * diagnostics) - and `nm -n` on the image is the check. It is adjacency the linker script happens
 * to produce, not a guarantee it makes, so `tools/host_resolve_entry_addr.sh` is not the tool for
 * it: compare `entry_vectors_stack_top` against `g_kv_buf` in `nm -n` output. Note what the layout
 * does *not* protect: the stack grows down from 0x800f8f80, so an overflow writes *below*
 * `entry_vectors_stack` (0x800f7f80), not into the results - the invariant is about the frames'
 * upper bound, and the stack's own 4 KB is the thing a storm would exhaust.
 */
#define ENTRY_KV_BUF 8192
static char g_kv_buf[ENTRY_KV_BUF];
static uint32_t g_kv_len;

/*
 * ---------------------------------------------------------------- 461: the trap's own buffer
 *
 * **The record of the trap was written into the tracer's buffer, and the tracer had filled it.**
 * That is the whole of why 459's run could not say what it stopped on: its log carries
 * `xnu_entry_kv_written = xnu_entry_kv_in_dram = 0x1fea` - 8170 of these 8192 bytes, with
 * `xnu_entry_kv_dropped = 0x66bb` (26299) refusals - and `fleh_undef`, whose report is the trap's
 * PC and the panic's `%s`, writes through `entry_kv` like everything else. So the keys went in and
 * were *dropped in silence*, and the run ended with a trap nobody could name. It is this project's
 * ledger entry 170 ("the report path's buffer was the tracer's and was full when the report was
 * written") and 167's rule after it: a refusal has to be visible, and a report has to have a place
 * of its own.
 *
 * The place of its own is this buffer. `entry_panic_kv` is the *same writer* as `entry_kv` - one
 * `entry_kv_into`, two callers, because "one value, two definitions" is this project's most
 * expensive defect class and two copies of "how a record is spelled" would be exactly that - and it
 * is bounded by its own `ENTRY_PANIC_BUF`. The epilogue prints it under its own heading, so the
 * trap's record is in the log whatever the tracer did.
 *
 * 8192 because the record has to fit *empty*: `fleh_undef` has 47 `entry_kv` call sites and four of
 * them are loops (the eight frame words, the `replay.node_n` ring times three keys, the chunk
 * hashes), so a bad tree walks the record up past 5 KB. A buffer that could refuse the trap is the
 * defect this exists to remove. The `.bss` cost is checked by 459's own device check, which refuses
 * a build whose RAM disk is not inside the image's bounds.
 */
#define ENTRY_PANIC_BUF 8192u
static char g_panic_buf[ENTRY_PANIC_BUF];
static uint32_t g_panic_len;
static uint32_t g_panic_dropped;
/* How many times the trap handler entered the writer. Zero says the trap never happened at all,
 * which an empty `g_panic_len` cannot say on its own - a handler that ran and wrote nothing and a
 * handler that never ran produce the same log otherwise. */
static uint32_t g_panic_entered;

/*
 * The two addresses XNU's own boot path writes to, and the instruction used to put them back.
 *
 * These are constants because XNU's are. `cpu_machine_idle_init` (`osfmk/arm/cpu.c:570-580`) ends
 * its `from_boot` branch with two `bcopy_phys` calls whose destinations it computes from its own
 * link, not from anything this project chooses:
 *
 *     dst_boot_args       = gPhysBase + (&ResetHandlerData.boot_args       - &ExceptionLowVectorsBase)
 *     dst_cpu_data_entries = gPhysBase + (&ResetHandlerData.cpu_data_entries - &ExceptionLowVectorsBase)
 *
 * and on this image that is 0x80000000 + 0x2408 and 0x80000000 + 0x2404. So the destinations are
 * fixed for as long as XNU's `reset_handler_data_t` layout and `gPhysBase` are, which is the point:
 * this image cannot move the target, it can only choose what sits there. What sits there is the NOP
 * pad in `entry_epilogue`'s cache sweep, and because the *report* executes the pad after XNU has
 * written to it, the pad is restored there before the sweep reaches it. `build_entry.sh` asserts in
 * every build that both addresses really are `nop` in the linked image.
 */
/* The two addresses XNU writes into this image - 0x80002404 and 0x80002408 - are not named here,
 * because nothing in this file may depend on their values: the pad below is *structure* rather than
 * content, and the addresses live in `build_entry.sh`'s check, which is where they can fail a build.
 * See experiment 282 for the arithmetic that produces them. */

/*
 * Counts records `entry_kv` refused for want of room. Read by the epilogue, never by the boot path,
 * so it costs the run one store per dropped line and nothing else.
 */
static uint32_t g_kv_dropped;

/*
 * How many times the data-abort handler has been entered, and what the *first* entry saw.
 *
 * Added by experiment 269, because its run reported `xnu_entry_kv_written=0x1fe4` - a full
 * 8164-byte results buffer - for a boot that only reaches one stub, and `xnu_entry_kv_dropped=0x11`
 * = 17, which is *exactly* the number of `entry_kv` calls `fleh_dataabt` makes. That pairing says
 * the handler ran, and the buffer's content - some three hundred copies of the string
 * `xnu_entry_data_abort_dfar` with no `=` and no value after any of them - says it ran many times
 * and that each entry stopped somewhere between writing its first key and writing that key's value.
 *
 * Counting and dating the entries is what turns that reading into a measurement: `entries` says
 * whether there was a storm and how big, `first_dfar`/`first_pc` say what faulted the first time,
 * and `first_kv_len` says how much of the buffer had already been written when it started - which
 * distinguishes "the storm is the whole run" from "the run got to the stub and then faulted".
 *
 * They are plain `.bss` globals rather than register-held values like the epilogue's own, because
 * they are written long before the teardown and read after it, and the teardown's set/way sweep
 * covers the whole D-cache - which is the same reason `g_kv_len` can be read back as
 * `xnu_entry_kv_in_dram`.
 */
static uint32_t g_abort_entries;
static uint32_t g_first_abort_dfar;
static uint32_t g_first_abort_pc;
static uint32_t g_first_abort_kv_len;

/*
 * The tracer's terminal record, and why it is not an `entry_kv` record.
 *
 * Experiment 445 ran the hang tracer and got the line it was built for - `t268: thread_block was
 * called` - and not the call site that line is useless without. The wrapper's two `entry_kv` records
 * are written **after** everything the boot recorded, so they are the first records a full buffer
 * refuses: `xnu_entry_kv_dropped` read 16269 against 280 records that fitted. The drop counter made
 * the refusal visible, which is 269's repair, and nothing made the record *survive*. The terminal
 * wrapper's answer is the newest thing a run produces, and a collector that keeps the oldest refuses
 * exactly that one.
 *
 * So these are `.bss` slots, written by the wrapper and printed by the epilogue **outside** the dump -
 * the mechanism the abort slots above already use, for the same reason. `g_block_kv_len` is the
 * position the refused records would have had, so a report can say how deep into the run the block
 * was. They exist only when the tracer is built (`STAGE90_ENTRY_TRACE`, passed to this file's compile
 * by `build_entry.sh`), so their presence in a report means the tracer was in the image, and a value
 * of 0 means the wrapper never ran.
 */
#ifdef STAGE90_ENTRY_TRACE
uint32_t g_block_caller;
uint32_t g_block_continuation;
uint32_t g_block_kv_len;
/*
 * Experiment 450. 446-448 wrote "the run ends in `ml_get_max_cpus`'s `thread_block`" and built a
 * frontier model on it, and 449 measured that the sentence is true of the *report* and false of the
 * *boot*: `__wrap_thread_block` had been terminal since experiment 268, so every run since then
 * stopped at the first `thread_block`, which is one instruction before the scheduler would hand the
 * CPU to the matching thread `registerService` had already created. These four slots are what a
 * **non**-terminal block needs to stay legible:
 *
 *   - `g_block_count` counts the calls, and `g_block_returned` counts the ones that came back. The
 *     pair separates three outcomes a single caller cannot: the boot thread blocked and was woken
 *     (count and returned both rise), it blocked and never came back (count rises, returned does not
 *     - the match ran but nothing set `max_cpus_initialized`), or the run never reached a block at
 *     all (both 0).
 *   - `g_block_ring_*` is the first **eight** `(caller, continuation)` pairs in order. The first is
 *     `ml_get_max_cpus+0x3c` if the frontier is still where 447 put it; the next ones are the sites
 *     the boot blocks at *after* the scheduler has run, which is the reading this step exists for.
 *     A ring rather than a single slot because the interesting call is not the first one any more.
 *   - `g_block_last_*` is the site the run was in when it stopped, and `g_block_kv_len` the position
 *     of the boot's own record at that moment.
 */
uint32_t g_block_count;
uint32_t g_block_returned;
uint32_t g_block_first_return_caller;
uint32_t g_block_last_return_caller;
uint32_t g_block_last_caller;
uint32_t g_block_last_continuation;
uint32_t g_block_ring_caller[8];
uint32_t g_block_ring_continuation[8];
/*
 * Experiment 453. 450's ring says *where* a thread blocked and never *which thread*, and 452's trace
 * has four live at once by the time it publishes `IOBSD` - the boot thread, `_IOConfigThread::main`,
 * `IOWorkLoop::threadMain` and the pool threads - so a flat sequence mixes them: `lck_mtx_sleep_
 * deadline+0x88` (the last record) could be the boot thread waiting for `bsd_autoconf`'s work or a
 * worker parking, and nothing in the log says which.
 *
 * The thread is free to read on this target. `current_thread()` on ARMv7 is one instruction -
 * `mrc p15, 0, r, c13, c0, 4`, TPIDRPRW (`osfmk/arm/cpu_data.h:58`) - and `cswitch.s` writes that
 * register on every context switch, so a thread pointer read at any instruction between two switches
 * is the thread that is running there. The wrapper reads it and carries it with the caller, which
 * makes every block record a triple: `(thread, caller, continuation)`.
 */
uint32_t g_block_thread;            /* the thread current at the last block */
uint32_t g_block_first_thread;      /* ... at the first, which is the boot thread */
uint32_t g_block_last_thread;
uint32_t g_block_ring_thread[8];
/*
 * Experiment 454 adds the clock to the same record, for one comparison: `g_block_first_now` and
 * `g_block_last_now` are the low words of the counter `ml_get_timebase` reads (`mrrc p15, 0, .., c14`),
 * taken in the wrapper at the first and last block. With 454's `_dl_lo`/`_now` from the IOKit wait,
 * the pair says whether the trace's **last** record is later than the deadline that wait was given -
 * and a last record that is *past* a deadline that never fired is a measurement of "no timer
 * interrupt", not an argument for it.
 */
uint32_t g_block_first_now;
uint32_t g_block_last_now;
/*
 * Experiment 478. `thread_block` returns `self->wait_result` and **that value is the argument of the
 * panic 476 and 477 both stop on**: `ipc_mqueue_receive`'s tail is `ipc_mqueue_receive_results(wresult)`
 * and its `default:` arm panics for anything outside `THREAD_AWAKENED` (0), `THREAD_TIMED_OUT` (1),
 * `THREAD_INTERRUPTED` (2) and `THREAD_RESTART` (3). Until this step the wrapper threw the value away,
 * so the run could name the site and not the number.
 *
 * `g_block_results[]` is the histogram of every return with the enum's members as their own slots -
 * `ENTRY_BLOCK_RESULT_SLOTS` of them, the last one for a value the enum does not have, which is the
 * one that matters. A *slot* rather than six named counters because the mapping from `wait_result_t`
 * to slot is `entry_block_result_slot` and the report names the slots in an array of key names, the
 * arrangement the device-tree walk's report already uses; the alternative is six counters written out
 * by hand in three places.
 *
 * `g_block_first_odd_*` is the one that has to survive a run that dies: the first return the receive
 * path could not accept, with its caller, its sequence number and the thread, written to the *live*
 * channel as well as to `.bss` - because the read this step exists for is the one immediately before
 * the panic, and a panic is not a place to be waiting for an epilogue.
 */
#define ENTRY_BLOCK_RESULT_SLOTS 7u
uint32_t g_block_results[ENTRY_BLOCK_RESULT_SLOTS];
uint32_t g_block_last_result;
uint32_t g_block_last_result_caller;
uint32_t g_block_first_odd_result;
uint32_t g_block_first_odd_caller;
uint32_t g_block_first_odd_seq;
uint32_t g_block_ring_result[8];
/*
 * Experiment 479. The first syscall process 1 makes is `getpid`, and the wrapper on `sysent[20]`
 * records what the kernel answered: `g_getpid_first_value` is `p->p_pid` as the kernel wrote it into
 * the process's own return slot, `g_getpid_first_error` is the syscall's return value beside it, and
 * `g_getpid_first_caller` is the `bl` site in the kernel's dispatcher the answer came back through.
 *
 * `g_getpid_calls` counts, and it is the only number here that grows without bound - which is why the
 * count is written to the live channel on powers of two and not on every call: the loop in
 * `entry_ramdisk.s` runs as fast as the CPU allows, and `entry_live_write`'s 4096-record cap is a
 * bound that the boot's own report depends on (461: the report path's buffer was the tracer's and was
 * full when the report was written). A power-of-two record is a *monotone* reading - the last one in
 * the log is the count rounded down, and the count only ever doubles - so nothing is lost by not
 * writing the odd numbers.
 *
 * `g_getpid_first_change` is the first answer that was not the first answer. It is deliberately not
 * "the first answer that is not 1": the expected pid is the *fixture's* statement about the kernel
 * (`EXPECTED_PID` in `entry_ramdisk.s`, where the `cmp` that acts on it lives) and writing it again
 * here would be the second, uncompared definition this project keeps meeting. What the instrument
 * can say without repeating it is that the answer *moved*.
 */
uint32_t g_getpid_calls;
uint32_t g_getpid_first_value = 0xFFFFFFFFu;
uint32_t g_getpid_first_error;
uint32_t g_getpid_first_caller;
uint32_t g_getpid_last_value = 0xFFFFFFFFu;
uint32_t g_getpid_first_change_value;
uint32_t g_getpid_first_change_seq;
uint32_t g_getpid_first_change_error;

/*
 * Experiment 480. Process 1's second syscall is `mmap`, and what this instrument keeps is not just
 * the address the kernel returned but the **argument buffer** the armv7k munger produced:
 * `g_mmap_args[0..7]` are the eight words that were in `uthread->uu_arg` when `mmap` was called, in
 * the order the wrapper read them (`r0..r5`, `r6`, `r8`).
 *
 * **Why the eight words and not the six the syscall declares.** `struct mmap_args` is five words plus
 * an 8-byte `off_t`, so words 0..4 are `addr, len, prot, flags, fd` and words 6..7 are `pos` - and
 * word 5 is the *padding* the compiler's alignment of `off_t` leaves between them, which the munger
 * writes from `r5` and `mmap` never reads. The fixture deliberately puts `0x5a5a` in `r5`, so word 5
 * is a marker: the layout claim is a *reading* in the log, `arg4` and `arg6` are what would move if
 * the munge were not the one this build believes, and the call itself would have worked either way,
 * because `MAP_ANON` makes `fd` and `pos` inoperative. A run that shows these eight words is the only
 * evidence this image has that its argument path takes the registers the ABI says it takes.
 *
 * The initialiser is `0xFFFFFFFF` rather than the `.bss` zero every stand-in in this image starts
 * with: zero is a value the munger could legitimately have produced (words 0, 6 and 7 are all zero
 * here), and "nothing wrote this" has to be distinguishable from "the argument was zero".
 */
uint32_t g_mmap_calls;
uint32_t g_mmap_first_caller;
uint32_t g_mmap_first_error;
uint32_t g_mmap_first_value = 0xFFFFFFFFu;
uint32_t g_mmap_args[8] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
/*
 * **And the three words the fault handler itself will read**, taken in the same wrapper, on the same
 * thread, one syscall before the fixture touches the page `mmap` just entered into it.
 *
 * `sleh_abort` services every fault through exactly two dereferences of the current thread's state:
 * `map = thread->map` for an address below the kernel's range (`osfmk/arm/trap.c:446`) and then
 * `arm_fast_fault(map->pmap, ...)` (`:449`). **474's run stopped on those two with the map pointer
 * zero**: the record's own fault was at `far 0x00000028` - `MAP_PMAP`, a load through a NULL map - and
 * each re-entry took the same path 0x250 bytes further down the kernel stack until the stack ran out
 * and nothing wrote a report. That was a fault in the *kernel*, on the exec's `copyin`; 480 makes
 * process 1 fault **in user mode**, where the map choice is `thread->map` and not `kernel_map`, so the
 * same two words are on the path again and 474's question ("why is it zero") has never been answered.
 *
 * These three numbers make it a reading. `g_mmap_thread` is `TPIDRPRW`, so the log's thread can be
 * compared with 474's `0xc0495480`; `g_mmap_map` is `thread->map` at offset `STAGE90_ACT_MAP`;
 * `g_mmap_pmap` is `map->pmap` at `STAGE90_MAP_PMAP`. A zero in `_map`, or a fault reported at
 * `far 0x28` immediately after this record, is then the answer rather than a mystery.
 *
 * **`_pmap` is read only when `_map` is a kernel address.** Dereferencing a map pointer to report
 * whether dereferencing it is safe would be this instrument taking the fault it exists to predict -
 * and taking it here, in the syscall, on the same path 474 died on. The kernel's own range is what
 * the choice of map is about (`pmap_kernel_va` = `[0x80000000, 0xFFFEFFFF]`, the constant this
 * project has paid for before), so anything else is reported as `0xFFFFFFFF` - "not read" - and the
 * wrapper's own `_map` record says what it was.
 */
uint32_t g_mmap_thread = 0xFFFFFFFFu;
uint32_t g_mmap_map = 0xFFFFFFFFu;
uint32_t g_mmap_pmap = 0xFFFFFFFFu;

/* `current_thread()` on ARMv7, one instruction: `mrc p15, 0, r, c13, c0, 4` is TPIDRPRW
 * (`osfmk/arm/cpu_data.h:58`) and `cswitch.s` writes it on every switch, so a read taken between two
 * switches is the thread running there. It cannot fault, reads no memory and holds no lock. */
static inline uint32_t entry_stubs_thread_pointer(void)
{
    uint32_t t;

    __asm__ volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r"(t));
    return t;
}

/* `osfmk/kern/kern_types.h:76-81` by name and value: 0..3 keep their numbers, 10 and -1 get the two
 * slots after them, and everything else is the last one. A function and not a table so the values are
 * written where a reader can compare them with the header. */
static unsigned entry_block_result_slot(uint32_t result)
{
    switch (result) {
    case 0u:          return 0u;          /* THREAD_AWAKENED     */
    case 1u:          return 1u;          /* THREAD_TIMED_OUT    */
    case 2u:          return 2u;          /* THREAD_INTERRUPTED  */
    case 3u:          return 3u;          /* THREAD_RESTART      */
    case 10u:         return 4u;          /* THREAD_NOT_WAITING  */
    case 0xFFFFFFFFu: return 5u;          /* THREAD_WAITING      */
    default:          return 6u;          /* not one of them     */
    }
}

/* The slot's key name, so the seven literals exist once: `entry_write_478_kv` writes the histogram
 * from `.bss` at the epilogue and `entry_note_block_return` writes it live at the first unusable
 * return, and the two reports have to name the same slots the same way or the pair is unreadable. */
static const char *entry_block_result_key(unsigned slot)
{
    static const char *const names[ENTRY_BLOCK_RESULT_SLOTS] = {
        "xnu_entry_block_results_k0", "xnu_entry_block_results_k1",
        "xnu_entry_block_results_k2", "xnu_entry_block_results_k3",
        "xnu_entry_block_results_k4", "xnu_entry_block_results_k5",
        "xnu_entry_block_results_k6"
    };

    return names[slot < ENTRY_BLOCK_RESULT_SLOTS ? slot : ENTRY_BLOCK_RESULT_SLOTS - 1u];
}
uint32_t g_vmwait_caller;
uint32_t g_vmwait_count;
/*
 * Experiment 451's live console, and experiment 452's correction of it. 450 retired the terminal
 * block so the boot could proceed, and the run came back with **nothing at all**: every record this
 * instrument keeps lives in `g_kv_buf` (or in these `.bss` slots) and only the epilogue writes them
 * out, so a boot that hangs after the jump takes the whole measurement with it. The ram console is
 * the one thing that survives - it is where every log this project has read came from - but
 * `entry_write_kv` writes it at VA 0xde500000, and that address has no translation while XNU's page
 * tables are live (268's `exception: data abort`, `dfar=0xde500000`, `pc` inside `entry_write_kv`).
 *
 * So the channel is one section descriptor written into XNU's live L1. **451 wrote that descriptor
 * too and the run was still silent, because the descriptor's protection bits were derived from a
 * neighbour section that does not exist.** 451 assumed XNU maps up to the console and searched for
 * the BLOCK whose PA field is `RAM_CONSOLE_BASE - 1 MB`; the boot_args actually declare
 * `memSize = 0x01000000` - the entry window, not the console's address (`xnu_entry_jump.c`) - so
 * `start.s`'s section loop covers VA/PA `[0x80000000, 0x81000000)` and nothing else, the scan ran
 * all 4096 entries without a match, and it refused with 1. The refusal lived in `.bss` and only the
 * epilogue prints `.bss`, so *the channel's own failure was the silence it was built to remove* -
 * 441's rule with the instrument on the wrong side of it.
 *
 * 452 derives no bits from an anchor. The protection bits are XNU's own section template
 * (`ARM_TTE_TYPE_BLOCK | ARM_TTE_BLOCK_AF | ARM_TTE_BLOCK_SH`, proc_reg.h - `AF` is AP[0], i.e.
 * privileged read-write and user no access, and XNU sets the same two on every kernel section),
 * the *memory type* is derived at runtime from `SCTLR.TRE` and `PRRR` because that is the one thing
 * that must not be inherited (`CACHE_ATTRINDX_DEFAULT` is write-back, and a console whose writes
 * live in a cache is a console a hang erases), the table base comes from `TTBCR.N` rather than from
 * assuming TTBR1, the slot is required to be invalid, domain 0 is checked against DACR, and the
 * console's own signature read back *through the new descriptor* is the acceptance test.
 *
 * Two more things 452 knows that 451 had to learn: nothing the payload plants in the boot table
 * before the jump can survive, because `start.s`'s `invalidate_tte` writes FAULT over 10240 entries
 * starting at `topOfKernelData` - the whole 16 KB boot table plus more - at `_start`; and the
 * address being one section past the end of the mapped window is not special, it is simply the
 * console's address. The install is therefore a runtime act, and the only visibility a *refusal*
 * can ever have is the next experiment's reading of these slots.
 */
uint32_t g_live_state;      /* 0 = not tried, 1 = live, 2 = refused */
uint32_t g_live_attempts;   /* init calls, so a retry and a refusal can be told apart */
uint32_t g_live_records;
uint32_t g_live_refusals;
uint32_t g_live_refuse;     /* 1 no usable table, 2 slot in use, 3 domain fault, 4 no signature,
                             * 5 no uncached encoding */
uint32_t g_live_ttbr0;
uint32_t g_live_ttbr1;
uint32_t g_live_ttbcr;
uint32_t g_live_dacr;
uint32_t g_live_l1;
uint32_t g_live_installed;  /* bitmask: 1 the console's own MB, 2 the next MB, 4 the alias VA */
uint32_t g_live_slot_before;
uint32_t g_live_desc;
uint32_t g_live_desc2;
uint32_t g_live_alias_read;
uint32_t g_live_sctlr;
uint32_t g_live_prrr;
uint32_t g_live_attr;
/*
 * Experiment 453. 452's frontier is the one record after `publishResource("IOBSD")` - a block at
 * `lck_mtx_sleep_deadline+0x88` that never returns - and `entry_note_block` cannot say who asked for
 * it, because it records the address the wrapper was called from: that names the *primitive* that
 * slept, and the primitive has no way to name its caller.
 *
 * One frame up is not reachable by walking: this tree compiles C with `-fomit-frame-pointer` (only 9
 * functions in the whole image set `fp` from `sp` - `backtrace`, `m_devget`, `inet_pton4` and six
 * others - and none of them is on this path: `msleep`'s prologue is `push {r4-r9, fp, lr}`, where the
 * `fp` is in the list for 8-byte stack alignment and is never written), so `__builtin_return_address(1)`
 * and an r11 chain are both fiction here. It *is* reachable by wrapping, and the wrap set is
 * exactly the family that can be wrapped: `_sleep` is `static` and `lck_mtx_lock_contended` is a
 * local symbol (`nm` says `t`), so neither can be intercepted - but a `grep -n '_sleep('` over
 * `bsd/kern/kern_synch.c` finds exactly **seven** call sites (311, 328, 346, 357, 371, 386, 397)
 * beside `_sleep`'s own definition and the `lck_mtx_sleep` inside it, and all seven are global
 * functions, all seven `T` in the image (`sleep`, `msleep`, `msleep0`, `msleep1`, `tsleep`,
 * `tsleep0`, `tsleep1`). Wrapping those seven wraps *every* entry into `_sleep`, and therefore every
 * sleep in the BSD boot, without a frame pointer and without touching a line of XNU.
 *
 * So each wrapper records what its own `lr` was on entry - the function that asked to sleep, one
 * frame above `_sleep` - plus the arguments the source's own branch tests use, so a report can name
 * the wait in the site's own words: `chan` is the channel, `wmsg` the message string (`NULL` for the
 * callers that pass none), `pri` the priority, and `tmo` the fifth argument, whose *shape* the
 * entry's own id gives:
 *
 *     id  function   fifth argument
 *     0   sleep      -
 *     1   msleep     struct timespec *   (absolute time, 0 = no timeout)
 *     2   msleep0    int timo in seconds (0 = no timeout)
 *     3   msleep1    u_int64_t abstime   (absolute deadline, 0 = none)
 *     4   tsleep     int timo in seconds
 *     5   tsleep0    int timo in seconds
 *     6   tsleep1    u_int64_t abstime
 *
 * The last call is kept in `.bss` for the epilogue and every call goes to the live console in order,
 * which matters more here than for the block ring: a boot that hangs inside a sleep never reaches the
 * epilogue, so the live sequence is the only channel that can say which site asked last.
 */
#define ENTRY_SLEEP_MAX 7u
uint32_t g_sleep_calls;
uint32_t g_sleep_count[ENTRY_SLEEP_MAX];
uint32_t g_sleep_last_ent;
uint32_t g_sleep_last_site;
uint32_t g_sleep_last_thread;
uint32_t g_sleep_last_chan;
uint32_t g_sleep_last_wmsg;
uint32_t g_sleep_last_pri;
uint32_t g_sleep_last_tmo;
/*
 * Experiment 454. 453's negative result is that the boot thread's deadline wait did not come through
 * any of the seven sleep entries, and the image says what it did come through:
 * `lck_mtx_sleep_deadline` has exactly three callers there - `_sleep`, `IOLockSleepDeadline` and
 * `IORecursiveLockSleepDeadline` - so wrapping the two IOKit ones wraps the *whole* remaining path,
 * the same argument 453 made for the sleep family and one level further out. Both are global (`T`),
 * both are `int (lock, event, AbsoluteTime deadline, UInt32 interType)`, and the deadline is a
 * register pair (`r2:r3`), which the disassembly of both confirms: each stores `{r2, r3}` to the stack
 * and loads `interType` from `[sp, #16]` / `[sp, #24]` - so a wrapper with a `uint64_t` third
 * parameter is ABI-identical, and `__OSAbsoluteTime` is the identity here (the pair goes through
 * unmodified).
 *
 * Two readings come out of one call, and they are the reason this step is not only a name:
 *
 *   - `_site` names the function that asked IOKit to wait (the wrapper's own `lr`), which is the site
 *     the whole of 452 and 453 walked down to;
 *   - `_dl_lo`/`_dl_hi` are the deadline the caller passed and `_now` is the **counter the deadline is
 *     compared against**, read in the same wrapper with one `mrrc p15, 0, lo, hi, c14` - the same
 *     read `ml_get_timebase` makes (`0x8000f374`, whose `mach_absolute_time` is a four-byte tail
 *     branch to it). The deadline is only ever delivered by a *timer interrupt*
 *     (`assert_wait_deadline` -> the thread's own timer, `locks.c:892`), and this boot has no timer
 *     driver at all - so if the counter is not advancing, or the deadline is behind it and nothing
 *     woke the thread, the wait is one that *cannot* end by time and can only end by `thread_wakeup`.
 *     Recording both ends of that comparison is what turns "waiting" into either "waiting for a
 *     timer that does not exist" or "waiting for a publisher that never came".
 */
#define ENTRY_IOLOCK_MAX 2u
uint32_t g_iolock_calls;
uint32_t g_iolock_count[ENTRY_IOLOCK_MAX];
uint32_t g_iolock_first_ent;
uint32_t g_iolock_first_site;
uint32_t g_iolock_first_thread;
uint32_t g_iolock_last_ent;
uint32_t g_iolock_last_site;
uint32_t g_iolock_last_thread;
uint32_t g_iolock_last_lock;
uint32_t g_iolock_last_event;
uint32_t g_iolock_last_inter;
uint32_t g_iolock_last_dl_lo;
uint32_t g_iolock_last_dl_hi;
uint32_t g_iolock_last_now;
/*
 * Experiment 455. 454 named the wait and closed the clock question; what is left is *why the match
 * never comes*, and there is exactly one place in the source to look:
 *
 *     OSObject * IOService::copyExistingServices(OSDictionary * matching, IOOptionBits inState,
 *                                                IOOptionBits options)        // IOService.cpp:4264
 *     {
 *         if((obj = matching->getObject(gIOProviderClassKey))
 *           && gIOResourcesKey && gIOResourcesKey->isEqualTo(obj)
 *           && (service = gIOResources))
 *         {
 *             if( (inState == (service->__state[0] & inState))              // (a) the fast path
 *               && (0 == (service->__state[0] & kIOServiceInactiveState))
 *               &&  service->matchPassive(matching, options))               // (b) the verdict
 *             { ... return the service ... }
 *         }
 *         else { ... search the plane for a candidate in `inState` ... }
 *     }
 *
 * `IOFindBSDRoot`'s dictionary is `serviceMatching(gIOResourcesKey)` plus
 * `IOResourceMatched = "IOBSD"`, so it takes that fast path, and `waitForMatchingService` sleeps only
 * when the call returns 0. Two questions decide everything after that, and both are answerable from
 * the wrappers alone - no frame pointer, no virtual call, nothing the instrument has to guess:
 *
 *   (a) is `gIOResources` in the state the query asks for? `waitForMatchingService` passes
 *       `kIOServiceMatchedState` (bit 4) and the test is `inState == (__state[0] & inState)`, so bit 4
 *       of `__state[0]` decides whether the fast path is taken **at all** - the general path looks for
 *       a candidate in the same state, so a clear bit 4 fails both. The two words are readable through
 *       `IOService::getResourceService()` plus the offsets `IOService::getState()` itself uses:
 *       `ldr r0, [r0, #36]` is `__state[0]` and `registerService` loads `[r4, #36]`/`[r4, #40]` as the
 *       pair `__state[0]`/`__state[1]`, and the build checks both against this image.
 *   (b) if the state is right, `matchPassive` is asked and returns 0 or 1. It is non-virtual - the
 *       image calls it with a plain `bl` from six places, two of them inside `copyExistingServices` -
 *       so it can be wrapped, and the one call that matters is **the one whose `this` is
 *       `gIOResources`**: the plane search calls the same function on *candidates*, never on the
 *       resource root. Filtering on that pointer instead of on a call site keeps this step free of
 *       absolute addresses, which the next relink would invalidate.
 *
 * The pointer is taken from the image's own accessor rather than from an address written here:
 * `IOService::getResourceService()` is `movw r0,#0xdc88 / movt r0,#0x8052 / ldr r0,[r0] / bx lr` - a
 * leaf with no call and no push, which the build asserts - because `_ZL12gIOResources` is a *local*
 * symbol and cannot be named from another object at all.
 */
extern void *_ZN9IOService18getResourceServiceEv(void);
#define ENTRY_MATCH_MAX 6u
#define ENTRY_DICT_MAX 8u
uint32_t g_match_calls;
uint32_t g_match_hits;
uint32_t g_match_site[ENTRY_MATCH_MAX];
uint32_t g_match_dict[ENTRY_MATCH_MAX];
uint32_t g_match_in[ENTRY_MATCH_MAX];
uint32_t g_match_opts[ENTRY_MATCH_MAX];
uint32_t g_match_res[ENTRY_MATCH_MAX];
uint32_t g_match_last_site;
uint32_t g_match_last_dict;
uint32_t g_match_last_in;
uint32_t g_match_last_opts;
uint32_t g_match_last_res;
uint32_t g_mpass_calls;
uint32_t g_mpass_rs_calls;
uint32_t g_mpass_rs_site;
uint32_t g_mpass_rs_dict;
uint32_t g_mpass_rs_opts;
uint32_t g_mpass_rs_res;
uint32_t g_mpass_rs_this;
uint32_t g_mpass_last_site;
uint32_t g_mpass_last_dict;
uint32_t g_mpass_last_res;
uint32_t g_mpass_last_this;
uint32_t g_dict_calls;
uint32_t g_dict_site[ENTRY_DICT_MAX];
uint32_t g_dict_name[ENTRY_DICT_MAX];
uint32_t g_dict_out[ENTRY_DICT_MAX];
uint32_t g_dict_last_site;
uint32_t g_dict_last_name;
uint32_t g_dict_last_in;
uint32_t g_dict_last_out;
uint32_t g_rs_ptr;
uint32_t g_rs_state0;
uint32_t g_rs_state1;
/* Experiment 456: the registry reading, live only (see `entry_registry_probe`). Seven slots because
 * the reading is a triple - the state pair, the property's array, and the array's own answer - and the
 * run has to be able to tell "no array" from "array without `IOBSD`" from "asked and got index 0". */
uint32_t g_reg_calls;
uint32_t g_reg_ptr;
uint32_t g_reg_state0;
uint32_t g_reg_state1;
uint32_t g_reg_rm;
uint32_t g_reg_count;
uint32_t g_reg_idx;
/*
 * Experiment 457: the candidate set, live only. 456 named this as the one ingredient it could not
 * see from where it stood - `doServiceMatch` decides everything on `matches->getCount()`, and
 * `matches` *is* `gIOCatalogue->findDrivers(this, &generation)` (`IOService.cpp:3688`) - and
 * `findDrivers(IOService *, SInt32 *)` is in another object from its only caller (one `bl` in the
 * whole image, at `IOService.cpp`'s `doServiceMatch`), so unlike `copyExistingServices` it can be
 * wrapped. Five slots: the ordinal (it is called once per registration of every service, so the
 * count says how many registrations the boot got through), the site, the service, the set the
 * catalogue returned, and the set's own `getCount()` - where `0xffffffff` is not a count but the
 * sentinel for "`findDrivers` returned nothing at all", which 455's rule about a refusal having to
 * be visible makes the honest value for the one case that has no count.
 */
uint32_t g_finddrv_calls;
uint32_t g_finddrv_site;
uint32_t g_finddrv_svc;
uint32_t g_finddrv_set;
uint32_t g_finddrv_count;
/*
 * Experiment 447. 446 resolved the block to `ml_get_max_cpus` and the run could not say *which* of
 * that function's six callers it was, so the next reading is the caller itself - and the second is
 * whether anything ever set the flag the function waits on.
 *
 * `ml_get_max_cpus` **returns** once `max_cpus_initialized` is `MAX_CPUS_SET`, so unlike
 * `thread_block` it can be wrapped *non*-terminally: the wrapper records and calls through, and the
 * block that ends the run still happens inside the real function. What has to be kept is therefore
 * the **first** caller and not the last - the first call is the one that finds the flag unset, since
 * the only thing that sets it is `ml_init_max_cpus` and a call that found it set would not block - so
 * `entry_note_maxcpus` keeps the first site and counts, while `entry_note_vmwait` keeps the last.
 * The count is what distinguishes "the flag was never set" from "it was set after an earlier call".
 *
 * `ml_init_max_cpus` is the writer, `void` and non-blocking, and its two in-image callers are two
 * different stories: `IOCPUInterruptController::initCPUInterruptController(int,int)` is Apple's path
 * and does not run here (its platform hook, `createCPUInterruptController`, is not in this tree),
 * while `MSM8974PlatformExpert::start` is this project's own experiment-405 probe. `_arg` is the
 * number announced, and a count of 0 is the reading that the probe never ran.
 */
uint32_t g_maxcpus_caller;
uint32_t g_maxcpus_count;
uint32_t g_initmax_cpus_caller;
uint32_t g_initmax_cpus_count;
uint32_t g_initmax_cpus_arg;

/*
 * Experiment 448. 447 measured `ml_init_max_cpus`'s count as **0**, and that number is a deduction
 * rather than a guess: `MSM8974PlatformExpert::start` is
 *
 *     if (!super::start(provider)) return false;      // super = IODTPlatformExpert
 *     IOService::publishResource("IORTC");
 *     ml_init_max_cpus(1);
 *
 * and every link in that guard is unconditional in the source - `IOService::start` is
 * `{ return true; }` (`IOService.cpp:513`), `IOPlatformExpert::start` ends `return configure(provider)`
 * (`IOPlatformExpert.cpp:184`), `IOPlatformExpert::configure` ends `return true;` (`:213`), and
 * `IODTPlatformExpert::configure` returns false only if that returns false (`:1269`). So if the
 * platform expert's `start` is entered at all it reaches its third statement, and a zero count means
 * **it was never entered** - the failure is before `start`, in `StartIOKit`'s first six statements.
 *
 * Of those, `registerService` is a virtual call and `initWithArgs` is the chain's other vtable slot
 * (`[vtable+0x340]`), so neither can be `--wrap`ped; what can be, and what splits the space, is the
 * direct call *inside* `initWithArgs` and the direct calls inside the one function that can make it
 * return false. (**Not** `new IOPlatformExpertDevice`: 448's measurement corrected the prediction that
 * had called it an `allocClassWithName` - `StartIOKit+0x104..+0x10c` is `mov r0,#0x60` /
 * `bl OSObject::operator new` / `bl IOPlatformExpertDevice::IOPlatformExpertDevice()`, a direct
 * allocation and constructor, so the class cannot fail to exist, and `allocClassWithName`'s two call
 * sites in this image are both downstream of a match.)
 *
 *   - `IODeviceTreeAlloc` has **exactly one** call site in the whole image
 *     (`initWithArgs+0x14`), so a count of 0 says the nub was never created and a count of 1 says
 *     `initWithArgs` ran - with the tree pointer it was handed and the nub it built.
 *   - `IOPlatformExpertDevice::initWithArgs` returns **false** by exactly one path, and the
 *     disassembly is unambiguous about which: `r5` is set from `bl IOWorkLoop::workLoop` and nothing
 *     else (`0x80160890`, `cmp r0,#0` / `movne r5,#1`). So `workLoop`'s return value *is* the answer
 *     to "did the platform expert's nub come up".
 *   - `IOWorkLoop::init` returns false at six guards, each a direct call whose result it tests, and
 *     the four that can plausibly fail on a boot this young are wrapped here: `IORecursiveLockAlloc`,
 *     `IOSimpleLockAlloc`, `IOCommandGate::commandGate(...)` and `kernel_thread_start`. (The other two
 *     are `OSObject::init` and `IOMalloc`, both far too common to wrap without drowning the report.)
 *
 * For those four the slot kept is the **first non-zero return** and the site that got it, not the
 * last: the question is "did one of them ever fail", and a later success would erase it. Zero is a
 * legitimate success for all four, so a slot of 0 and a count of 0 are different readings - "no
 * failure seen" against "never called".
 */
uint32_t g_dtalloc_caller;
uint32_t g_dtalloc_arg;
uint32_t g_dtalloc_ret;
uint32_t g_dtalloc_count;
uint32_t g_workloop_caller;
uint32_t g_workloop_ret;
uint32_t g_workloop_count;

/*
 * 461's four readings - see `entry_note_dtpath` below for what each one is for. Every one of them is
 * a "last" slot beside a live record and a count, and the counts are kept apart deliberately: a
 * `g_dtprop_hits` of 0 with `g_dtprop_calls` of 3 is "the plane was asked three times and never had
 * the property", while 0/0 is "the question was never reached" - the two readings 459's run could
 * not tell apart, because it had neither number.
 */
uint32_t g_dtplane;
uint32_t g_dtplane_dt_top;
uint32_t g_dtplane_root;
uint32_t g_dtplane_map_ret;
uint32_t g_dtplane_count;
uint32_t g_dtpath_count;
uint32_t g_dtpath_caller;
uint32_t g_dtpath_w0;
uint32_t g_dtpath_w1;
uint32_t g_dtpath_w2;
uint32_t g_dtpath_plane;
uint32_t g_dtpath_ret;
uint32_t g_dtpath_null;
uint32_t g_dtprop_calls;
uint32_t g_dtprop_hits;
uint32_t g_dtprop_entry;
uint32_t g_dtprop_key0;
uint32_t g_dtprop_key1;
uint32_t g_dtprop_obj;
uint32_t g_dtprop_w0;
uint32_t g_dtprop_w1;
uint32_t g_dtprop_bytes;
uint32_t g_mdevadd_calls;
uint32_t g_mdevadd_caller;
uint32_t g_mdevadd_devid;
uint32_t g_mdevadd_base;
uint32_t g_mdevadd_size;
uint32_t g_mdevadd_phys;
uint32_t g_mdevadd_ret;
uint32_t g_mdevlookup_calls;
uint32_t g_mdevlookup_caller;
uint32_t g_mdevlookup_devid;
uint32_t g_mdevlookup_ret;
/*
 * 471's two readings, and each is a *pair* of moments around one call - see `entry_trace.c` for why
 * the exec failure and not the panic is the frontier. `g_osr_*` is the exit reason the exec path
 * created (`os_reason_create`'s two arguments, and the caller that named it); `g_lmf_*` is
 * `load_machfile`'s return, recorded so that "our Mach-O loaded" is a reading and not an inference
 * from the reason code. Both keep the last call only, because the one that matters is the last one
 * before the panic; `_calls` is what says whether there was more than one.
 */
uint32_t g_osr_calls;
uint32_t g_osr_caller;
uint32_t g_osr_ns;
uint32_t g_osr_code;
uint32_t g_osr_ret;
uint32_t g_lmf_calls;
uint32_t g_lmf_caller;
uint32_t g_lmf_header;
uint32_t g_lmf_ret;

/*
 * 462's reading of the walk from the registry root, and it is a *pair* of moments in one run: `t1` is
 * the reading taken from the `IODeviceTreeAlloc` wrapper, which is the moment 461's successful probe
 * was taken at, and `t2` is the reading taken as the OS's own `fromPath` is entered - the frontier
 * itself. The four numbers in each are the four places `fromPath`'s first component can fail (the
 * meta root, the entry the walk starts from, that entry's child set, and how many children it has),
 * and `g_walk_control` is the instrument's own call with the OS's own path at the same instant.
 *
 * `g_walk_probes` counts the readings, which is what tells a run where the walk was read to the end
 * from a run where the probe site was never reached; the `t2` slots hold the **last** reading, so a
 * boot that makes more `fromPath` calls than 461's two keeps the last one rather than the first.
 */
uint32_t g_walk_probes;
uint32_t g_walk_t1_root;
uint32_t g_walk_t1_count;
uint32_t g_walk_t1_first;
uint32_t g_walk_t1_set;
uint32_t g_walk_t1_kids;
uint32_t g_walk_t1_control;
uint32_t g_walk_t2_root;
uint32_t g_walk_t2_count;
uint32_t g_walk_t2_first;
uint32_t g_walk_t2_set;
uint32_t g_walk_t2_kids;
uint32_t g_walk_t2_control;

/*
 * 463's readings of the wait that does not return, and the four links of the chain its predicate runs
 * before it fails. `IOSecureBSDRoot` (`iokit/bsddev/IOKitBSDInit.cpp:664`) asks
 * `waitForMatchingService(serviceMatching("IOPlatformExpert"), 30ULL * kSecondScale)` and never comes
 * back; the wait's predicate is the *first* thing it does, before it takes `gNotificationLock` or
 * sleeps:
 *
 *     result = copyExistingServices( matching, kIOServiceMatchedState, kIONotifyOnce );
 *
 * and 462 left two candidate mechanisms for its failure that the source allows and the run could not
 * separate - `OSMetaClass::applyToInstancesOfClassName`'s registry set, and the
 * `inState == (service->__state[0] & inState)` bit test that `IOService::instanceMatch` applies to
 * every instance the set yields. Every value below is read by the instrument *calling XNU's own
 * functions*; `entry_trace.c` holds the calls and the wrapper they are taken from, and its comment
 * carries the derivation of why each one is a link of this chain and not a proxy for it.
 *
 *   `wmatch_*`  the wait itself: its caller, its dictionary, its timeout in nanoseconds, and - when it
 *               comes back at all - its answer. `ent = 0` is the record written on entry and `ent = 1`
 *               the one written after the real call, so **a boot that hangs in the wait shows the
 *               former and nothing else**, which is what makes "waited and never woke" a reading
 *               rather than an absence.
 *   `wsvc_*`    the predicate by the wait's own arguments (`p4`: `inState = kIOServiceMatchedState`)
 *               and then the *same call* with the state test disabled (`p0`: `inState = 0`, which
 *               `instanceMatch`'s `state == (state & __state[0])` makes true by construction). The
 *               pair is the separation 462's doc named as the next step's object: `p4 = 0` with
 *               `p0 != 0` is the state bit, `p4 = 0` with `p0 = 0` is upstream of it.
 *   `wcls_*`    the class-name lookup behind the general path. `sym` is the dictionary's own
 *               `IOProviderClass` object - the OS's own symbol, read out of the dictionary rather
 *               than re-spelled here - `meta` is `OSMetaClass::getMetaClassWithName(sym)`, `rsvc` is
 *               `IOService::getResourceService()` (so one instance in the walk can be *named* by
 *               value), and `seen`/`shown` are how many instances XNU's own
 *               `applyToInstancesOfClassName` yielded to the instrument's applier, and how many of
 *               them had room in the record. **`seen = 0` with a non-zero `meta` is the third reading
 *               under the two 462 named, and neither of them: the metaclass exists and has no
 *               instances**, because `OSMetaClass::addInstance` has exactly one caller in this tree
 *               (`IOService::registerService`, `IOService.cpp:3694`).
 *   `wcls_inst/_state[]`  the first `ENTRY_WCLS_MAX` of those instances and each one's
 *               `IOService::getState()` - i.e. `__state[0]`, the word the state test reads, taken
 *               through the only implementation of `getState` in the tree (`IOService.h:499`).
 */
#define ENTRY_WCLS_MAX 4u
uint32_t g_wmatch_calls;
uint32_t g_wmatch_last_ent;
uint32_t g_wmatch_last_caller;
uint32_t g_wmatch_last_dict;
uint32_t g_wmatch_last_to_lo;
uint32_t g_wmatch_last_to_hi;
uint32_t g_wmatch_last_ret;
uint32_t g_wsvc_calls;
uint32_t g_wsvc_last_dict;
uint32_t g_wsvc_last_sym;
uint32_t g_wsvc_last_p4;
uint32_t g_wsvc_last_p0;
uint32_t g_wcls_calls;
uint32_t g_wcls_last_sym;
uint32_t g_wcls_last_meta;
uint32_t g_wcls_last_rsvc;
uint32_t g_wcls_last_name0;
uint32_t g_wcls_last_name1;
uint32_t g_wcls_seen;
uint32_t g_wcls_shown;
uint32_t g_wcls_inst[ENTRY_WCLS_MAX];
uint32_t g_wcls_state[ENTRY_WCLS_MAX];
/*
 * The walk in progress. `entry_note_wcls_begin` zeroes it, the applier counts into it, and
 * `entry_note_wcls_end` publishes it - the applier cannot publish its own total, because the total is
 * only known once `applyToInstancesOfClassName` has returned.
 */
uint32_t g_wls_seen;
uint32_t g_rlock_bad;
uint32_t g_rlock_caller;
uint32_t g_rlock_count;
uint32_t g_slock_bad;
uint32_t g_slock_caller;
uint32_t g_slock_count;
uint32_t g_cgate_bad;
uint32_t g_cgate_caller;
uint32_t g_cgate_count;
uint32_t g_kthread_bad;
uint32_t g_kthread_caller;
uint32_t g_kthread_count;

/*
 * 449 extends 448's `kernel_thread_start` record with the **continuation** of each of the first four
 * calls. 448 counted two calls and could not say *which* two threads they were; the two candidates on
 * this boot's path are `IOWorkLoop::init`'s (`ioworkloop.cpp:?)` and `_IOServiceJob::pingConfig`'s -
 * the work loop and the async service-matching thread - and the whole "nothing matched" chain turns on
 * whether the second of those was created. The entry pointer names it, and it is a value this project
 * can resolve in the image, so the count becomes an identity.
 */
uint32_t g_kthread_cont[4];
uint32_t g_kthread_site[4];

/*
 * Experiment 449. 448 left exactly one unmeasured step between a nub that came up and a driver that
 * never started: the **catalogue**. `StartIOKit` was proved to reach `rootNub->registerService()`
 * (because `IOPlatformExpertDevice::initWithArgs` returned a non-NULL work loop, which by the
 * disassembly's arithmetic is that function's return value), and `MSM8974PlatformExpert::start` was
 * proved never to be entered (because 447's writer count is 0 and every link of `start`'s own guard is
 * unconditional in the source). Nothing matched and **nothing panicked**, and the two facts together
 * put the empty match list in one place.
 *
 * The personalities come from one string through one call, so the question is bracketed by three
 * counts and one return value:
 *
 *   - `iokit_post_constructor_init` has **exactly one** call site in the whole image and it is a *tail
 *     branch* from `last_kernel_constructor` (`b 0x8011bc4c`), the `.init_array` entry this project
 *     links last on purpose. A `b` and a `bl` are both relocations against the same symbol, so
 *     `--wrap` catches both - and this count is the only direct reading of whether the `.init_array`
 *     walk reached the entry whose whole job is to run after the others.
 *   - `IOCatalogue::initialize` and `IOService::initialize` each have **one** call site too, both
 *     inside `iokit_post_constructor_init` (`+0x18` and `+0x10`).
 *   - `OSUnserialize(gIOKernelConfigTables, &errorString)` has **one** call site, inside
 *     `IOCatalogue::initialize`, and its return is whether the two personalities were parsed at all.
 *     A NULL there leaves `gIOCatalogue->init(NULL)` — and with `assert` compiled out in this
 *     configuration nothing reports it, so the catalogue exists and is empty: no match, no fallback,
 *     no panic, and no driver. That is the measured state exactly. The image has **three** call sites
 *     for it in total (`IOCatalogue::initialize`, `IODTMatchNubWithKeys`, `IODTFindMatchingEntries`),
 *     so this keeps three records rather than one: the first call in time is not necessarily the
 *     catalogue's, and keeping only the first would trade a certain answer for an ordering
 *     assumption.
 *   - `OSMetaClass::allocClassWithName(OSSymbol *)` is what matching instantiates a class *by name*
 *     with; a count of 0 means the match list was empty rather than that a candidate was skipped.
 *     (It is *not* how `StartIOKit` makes the nub: that is `OSObject::operator new` plus a direct
 *     constructor, then a virtual `initWithArgs`. The two call sites here are `probeCandidates` and
 *     `newUserClient`, and both are downstream of a match.)
 *   - `IOService::publishResource(const char *, OSObject *)` is the statement between
 *     `MSM8974PlatformExpert::start`'s guard and its `ml_init_max_cpus(1)`, so any record at all
 *     refutes the deduction instead of confirming it. Its nine call sites span more than one function
 *     and more than one moment, so it keeps the first four (caller and key each) — one record would
 *     have to assume that the platform expert's call is the first one in the boot.
 */
uint32_t g_postctor_caller;
uint32_t g_postctor_count;
uint32_t g_catinit_caller;
uint32_t g_catinit_count;
uint32_t g_unser_caller[3];
uint32_t g_unser_ret[3];
uint32_t g_unser_count;
uint32_t g_alloc_name;
uint32_t g_alloc_count;
uint32_t g_pub2_caller[4];
uint32_t g_pub2_key[4];
uint32_t g_pub2_count;

/* ------------------------------------------------- 467: what the kernel's own abort handler did */
/*
 * **The record of a data abort that is no longer this instrument's to stop.**
 *
 * Until 466 the vector page's slot 4 branched to `fleh_dataabt` in this file, whose design is to
 * record into `.bss` and leave - which is what a fault deserves when the fault *is* the end of the
 * run. 466's run produced the other kind: `copyout`'s first store to the page `load_init_program`
 * had just allocated (`dfar = 0x1000`, `dfsr = 0x805`), i.e. the demand fault XNU pages in and
 * retries. 467 installs Apple's own first-level handler in that slot (`locore_fleh_dataabt`, linked
 * by 466), so after this step a data abort is the *kernel's decision* and not a stop, and these are
 * the slots that record the decision.
 *
 * The place every path through that handler passes is `sleh_abort` (`osfmk/arm/trap.c:274`), which
 * `locore.s:1140` calls once per abort: it either **returns** - the page was paged in and the
 * faulting instruction is retried - or does not return at all, because it panicked (`trap.c:313`
 * "at interrupt context", `:393` prefetch in kernel mode, `:464` a failed `vm_fault` with no recover
 * handler) or because it re-entered itself. So the pair below is the reading:
 *
 *   - `g_sleh_seq` counts entries into the second-level handler, `g_sleh_back` counts returns from
 *     it. A handler entered and never returned is a *gap* between them, which is what makes "it
 *     panicked" distinguishable from "it serviced the fault and the boot went on".
 *   - `g_sleh_fsr`/`g_sleh_far` are the **latest** entry's, which is what makes a retry loop
 *     readable: a fault that is serviced advances the boot and the next fault is a different
 *     address, while one that is retried forever repeats `0x805`/`0x1000`. **476 renamed these from
 *     `g_sleh_dfsr`/`g_sleh_dfar`, and the keys with them** (`xnu_live_sleh_fsr`/`_far` and
 *     `xnu_entry_sleh_fsr`/`_far`): the pair that fills them is chosen by the abort class
 *     (`STAGE90_T_PREFETCH_ABT` reads IFSR/IFAR, everything else DFSR/DFAR), so a name that said
 *     "data" would be false on the prefetch path - and a false name on a fault register is the kind
 *     of claim this project has had to retract before. Runs before 476 print the old two names.
 *   - `g_sleh_type` is `T_DATA_ABT` (4, `osfmk/arm/trap.h:70`) **or, since 476, `T_PREFETCH_ABT`
 *     (3)** - that sentence used to end "by construction here, the other abort vectors are still this
 *     file's", which 476 makes false: the prefetch slot went to Apple's handler the way 467 gave away
 *     the data slot. Both are recorded, which is why giving away a slot moved a number in the log
 *     rather than requiring a sentence in a document to be believed.
 *   - `g_sleh_storm` is non-zero only if the live cap below was reached, and it is the only place
 *     the number past the cap appears.
 *
 * **`.bss` and the live channel, both, because neither alone can carry this run.** The `.bss` half
 * needs an epilogue and a serviced fault may never produce one; the live half needs nothing but the
 * ram console mapping, and it is what a boot that continues leaves behind. The record is written to
 * the live channel *after* the counters are updated, and only for the first `SLEH_LIVE_MAX`
 * entries: this is the one record whose own failure can re-enter it (269's storm was a fault inside
 * a reporting path), and a cap is what turns "the machine is looping" into eight records and a
 * counter instead of an unbounded stream.
 *
 * ------------------------------------------------------- 474: the frame, and the three bands
 *
 * 473's run was served four aborts, returned from all four, recorded a fifth and then wrote nothing
 * else at all - and the record that would have named the fifth's address was the one `SLEH_LIVE_MAX`
 * cut. So the cap is now three caps, each one bounding a different question, and the entry takes
 * `struct arm_saved_state *` so that the two numbers it has never carried are in the record.
 *
 *   - `SLEH_LIVE_MAX` (4 in 474, **8 since 476**) - the entries whose fault pair and `TPIDRPRW` are
 *     recorded. 474 kept it at 4 deliberately, so that `xnu_live_sleh_storm = 5` would be comparable
 *     with 473's, and 476 moves it for a reason 474's own run produced: **with both abort slots now
 *     the kernel's, an abort is a normal event and not a stop** - the boot's early demand faults
 *     consume the head of the list, and 475's run is exactly the case where the record that mattered
 *     would have been the ninth. The cost is named: `_storm` is now the entry *past eight*, so a later
 *     run's `_storm = 9` and 473's `= 5` are readings of different caps, and the two numbers are not
 *     comparable even though the key is the same.
 *   - `SLEH_LIVE_FRAMES` (8 in 474, **16 since 476**) - the entries whose saved state is recorded:
 *     `pc` (the faulting instruction - the vector has already applied its `sub lr, lr, #8`), `lr`,
 *     `sp`, `cpsr` (whose mode bits are Apple's own user/kernel test), and the frame's own fault pair.
 *     474 set it to twice the first band, because the storm's head is entry 5 and the question is not
 *     only what it was but whether entries 5, 6, 7 and 8 are the same address repeating or a walk;
 *     476 doubles it again for the same reason one band further out.
 *   - `SLEH_LIVE_SEEN` (64, unchanged) - one record per entry, a running count. This is the band that
 *     separates "the handler was entered five times and then the machine stopped asking it" from "the
 *     handler is being entered forever": the first leaves `_seen` at 5, the second takes it to 64, and
 *     the 473 log could not tell those apart because past the cap it recorded nothing at all.
 *
 * **`xnu_live_sleh_frame_ok` is the instrument checking itself, in the log, before the frontier.**
 * The vector stored the same `cp15` numbers 467's record reads into `SS_STATUS`/`SS_VADDR`, so a
 * frame read at the right offsets must report them back. Entries 1 to 4 are aborts whose `DFSR`/
 * `DFAR` are already known from 472's and 473's logs (`0x805/0x1000`, `0x807/0xc8105000`,
 * `0x807/0xc8146000`, `0x805/0x00101f28`), which makes them a control and not a sample: if the
 * offsets are wrong the run says so on four known aborts, and every `pc` and `cpsr` beside them is
 * named as unreliable in the same breath.
 *
 * The prediction, written before the build. Entry 5's frame will separate the readings 473 could not:
 * (a) `cpsr` in supervisor mode (`& 0x1F != 0x10`) with `pc` in the kernel's text (`>= 0x80000000`) -
 * a kernel load or store of an unmapped user address, i.e. a `copyin`/`copyout` on the exec path, and
 * `far` will be a user address; (b) `cpsr` in user mode with `pc` a low address - the process-1
 * thread taken a fault on its own account; (c) `pc` inside this image's own `entry_*` code (the
 * symbols are in the entry ELF) - a fault inside the reporting path, 269's storm; and the count will
 * say in every case whether it happens once or forever. What is *not* predicted to appear at all is
 * `pc = 0x10e0`: that is the RAM disk's `udf #0`, an undefined instruction, which reaches
 * `sleh_undef` and never this function.
 */
uint32_t g_sleh_seq;
uint32_t g_sleh_back;
uint32_t g_sleh_type;
uint32_t g_sleh_fsr;
uint32_t g_sleh_far;
uint32_t g_sleh_storm;
uint32_t g_sleh_pc;
uint32_t g_sleh_lr;
uint32_t g_sleh_sp;
uint32_t g_sleh_cpsr;
uint32_t g_sleh_fsr_frame;
uint32_t g_sleh_far_frame;
uint32_t g_sleh_frame_ok;
uint32_t g_sleh_user_mode;

#define SLEH_LIVE_MAX 8u
#define SLEH_LIVE_FRAMES 16u
#define SLEH_LIVE_SEEN 64u

void entry_live_write(const char *key, uint32_t value);

void entry_note_sleh(uint32_t type, uint32_t fsr, uint32_t far_, uint32_t thread,
                     const uint32_t *frame)
{
    uint32_t sp = 0u, lr = 0u, pc = 0u, cpsr = 0u, fsr_frame = 0u, far_frame = 0u;

    g_sleh_seq++;
    g_sleh_type = type;
    g_sleh_fsr = fsr;
    g_sleh_far = far_;

    /*
     * A word index into `struct arm_saved_state`, from `entry_saved_state.h`. The six offsets are
     * all word-aligned by construction, so the division is exact; `frame` is only dereferenced when
     * it is not NULL, because a NULL here would mean the abort vector changed shape and a fault
     * inside the record is the one fault this function must not take (269).
     */
    if (frame != 0) {
        sp        = frame[STAGE90_SS_SP     / 4];
        lr        = frame[STAGE90_SS_LR     / 4];
        pc        = frame[STAGE90_SS_PC     / 4];
        cpsr      = frame[STAGE90_SS_CPSR   / 4];
        fsr_frame = frame[STAGE90_SS_STATUS / 4];
        far_frame = frame[STAGE90_SS_VADDR  / 4];
    }

    g_sleh_sp = sp;
    g_sleh_lr = lr;
    g_sleh_pc = pc;
    g_sleh_cpsr = cpsr;
    g_sleh_fsr_frame = fsr_frame;
    g_sleh_far_frame = far_frame;
    g_sleh_frame_ok = ((fsr_frame == fsr) && (far_frame == far_)) ? 1u : 0u;
    g_sleh_user_mode = ((cpsr & STAGE90_PSR_MODE_MASK) == STAGE90_PSR_USER_MODE) ? 1u : 0u;

    if (g_sleh_seq <= SLEH_LIVE_MAX) {
        entry_live_write("xnu_live_sleh_seq", g_sleh_seq);
        entry_live_write("xnu_live_sleh_type", type);
        entry_live_write("xnu_live_sleh_fsr", fsr);
        entry_live_write("xnu_live_sleh_far", far_);
        entry_live_write("xnu_live_sleh_thr", thread);
    } else if (g_sleh_seq == SLEH_LIVE_MAX + 1u) {
        g_sleh_storm = g_sleh_seq;
        entry_live_write("xnu_live_sleh_storm", g_sleh_storm);
    }

    if (g_sleh_seq <= SLEH_LIVE_SEEN) {
        entry_live_write("xnu_live_sleh_seen", g_sleh_seq);
    }

    if (g_sleh_seq <= SLEH_LIVE_FRAMES) {
        entry_live_write("xnu_live_sleh_pc", pc);
        entry_live_write("xnu_live_sleh_lr", lr);
        entry_live_write("xnu_live_sleh_sp", sp);
        entry_live_write("xnu_live_sleh_cpsr", cpsr);
        entry_live_write("xnu_live_sleh_fsr_frame", fsr_frame);
        entry_live_write("xnu_live_sleh_far_frame", far_frame);
        entry_live_write("xnu_live_sleh_frame_ok", g_sleh_frame_ok);
        entry_live_write("xnu_live_sleh_user", g_sleh_user_mode);
    }
}

void entry_note_sleh_back(void)
{
    g_sleh_back++;
    if (g_sleh_seq <= SLEH_LIVE_MAX) {
        entry_live_write("xnu_live_sleh_back", g_sleh_back);
        entry_live_write("xnu_live_sleh_at_back", g_sleh_seq);
    }
}
#endif

/*
 * Set by the IRQ vector so the epilogue knows to name the interrupt it stopped on.
 *
 * Experiment 308's run ended on `exception: irq` and the report could not say *which* interrupt:
 * the vector is the only place that has the GIC in front of it, and the GIC cannot be read from
 * there - `fleh_irq` runs with XNU's page tables live, which map [physBase, physBase + memSize) and
 * nothing at 0xf9002000. The epilogue is where the MMU comes off, so the read belongs there, and
 * this flag is what carries the request across the teardown.
 *
 * Reading `GICC_IAR` acknowledges the interrupt as a side effect. That is deliberate: the machine
 * is about to be stopped and reported, and acknowledging is what turns "an interrupt arrived" into
 * "interrupt N arrived" - it does not change whether the report happens.
 */
static uint32_t g_irq_report_pending;

/*
 * Where `entry_kv` was when the fault happened, and what it was about to store.
 *
 * Added by experiment 269 on the strength of its measured first abort alone. That measurement says
 * `pc = 0x800020dc`, which the image's own disassembly makes `entry_kv.part.0+0xb8`:
 *
 *     add r3, r4, r3       ; r3 = &g_kv_buf + g_kv_len
 *     str r2, [lr]         ; g_kv_len += 12
 *     strb r1, [r3, #11]   ; g_kv_buf[g_kv_len + 11] = '\n'      <- the fault
 *
 * and `dfar = 0x3f` with `first_kv_len = 0x34`. Those two numbers cannot both be true of that store
 * as written: `r4` is `&g_kv_buf` (0x800f8f98, `nm` on the image), so the address is
 * 0x800f8f98 + 0x34 + 11 = 0x800f8fc7, and with `first_kv_len` at the value *before* the `str r2, [lr]`
 * the address the handler *should* have seen is 0x800f8fc7 either way. `0x3f` is `g_kv_len + 11` with
 * `&g_kv_buf` = 0 - i.e. the base register held zero at that store - and nothing in that function
 * leaves `r4` anything but `&g_kv_buf`, on either of its two paths into the value block (0x80002040
 * and 0x800020f4, both `movw`/`movt` pairs, both intact in the linked image).
 *
 * So the two readings disagree, and the disagreement is the thing to measure rather than argue:
 * `g_kv_step`/`g_kv_step_addr` are written by `entry_kv` immediately before each of its four groups
 * of stores, and read by the handler, which says what the code *believed* it was doing a few
 * instructions before it faulted. `g_first_abort_dfsr` says whether this was even a translation
 * fault, and `g_first_abort_insn` says whether `pc_abt` is really an instruction.
 *
 * They are `volatile` on purpose: a diagnostic that the optimizer is free to move is a diagnostic
 * that can report the state of a program that never ran.
 */
static volatile uint32_t g_kv_step;
static volatile uint32_t g_kv_step_addr;
static uint32_t g_kv_hex_in_use;
static uint32_t g_kv_hex_arg;

/*
 * `entry_epilogue`'s `why` string, copied out of the parameter and into `.bss` on entry. *
 * The parameter is correct and the compiler does the right thing with it - `entry_epilogue` stores
 * it at `[sp, #4]` on entry and reloads it from there for the report (`ldr r0, [sp, #4]` at
 * 0x80002458 in the 269 image). What the run says is that the *slot* is wrong by the time it is
 * read: the line comes out as `MI4IOS6_STAGE90_XNU real XNU entry: ` with nothing after it, i.e. a
 * pointer to a nul byte, and in the storm run the same line read `47`. Both are what a clobbered
 * stack slot looks like, and neither is what the string says. So the report uses this copy, taken
 * before anything is torn down, and prints the pointer itself beside it - a value in `.bss` cannot
 * be clobbered by whatever is going on with the stack, and the pair (pointer, text) says which of
 * the two readings the next run should trust.
 */
static const char *g_why;

/*
 * Where `entry_stub_hit` put the caller's hex digits in `g_kv_buf`, so the epilogue can read those
 * bytes back out and print them as words. Added by experiment 271, which is the third time this
 * project has had a *report* that was the wrong thing rather than the code it was reporting on.
 *
 * That run's caller record came out as ` xnu_entry_stub_caller=0x800:;?=4` where the prediction was
 * 0x800abfd4. The four wrong characters are not noise: ':' is 0x3a, and `entry_kv` writes a
 * non-decimal digit as `'a' + (d - 10)` = 0x57 + d, while a decimal digit is `'0' + d` = 0x30 + d.
 * 0x30 + 0xa = 0x3a ✓, 0x30 + 0xb = 0x3b ';' ✓, 0x30 + 0xf = 0x3f '?' ✓, 0x30 + 0xd = 0x3d '=' ✓ -
 * so the strings says the *conditional* add (`addls r4, r3, #0x30`, after `cmp r3, #9` and
 * `add r4, r3, #0x57`) took the decimal branch for all four non-decimal nibbles, and the true value
 * is recoverable from the corruption: `:;?=` decodes to a, b, f, d, which is 0x800abfd4 exactly.
 *
 * Reading the buffer back is what separates the three places the fault could be - the value, the
 * bytes `entry_kv` stored in `.bss`, or the transfer to the ram console - and the value itself is
 * printed a second time through `entry_write_kv`, whose digit path is a table read in `.rodata` and
 * has produced a correct letter on every line of every report so far. **Carrying the same value by
 * two independent routes and reporting both is the defence this project keeps having to relearn**
 * (see mi4-measurement-defects): a single writer's output cannot be checked against itself.
 */
static uint32_t g_stub_caller_digits;
static uint32_t g_stub_caller;
/*
 * 465. How many times a stand-in has been entered, and whether the caller record that `_w0`/`_w1`
 * read back was actually *written*. Both are needed to read the pair above correctly: 464's run had
 * `g_kv_len = 0x2000` when the stub hit, so `g_stub_caller_digits` was `0x2019` - 25 bytes **past**
 * the end of the 8192-byte buffer, because the two `entry_kv` caller records were refused - and
 * `entry_image_ptr` passed, so `_w0`/`_w1` printed bytes of the kernel's own neighbouring data
 * (`ExceptionVectorsTable`, at `0x80558000`) as if they were digits. `_in_buf` says which case a
 * run is in; a run with `_in_buf = 0` has no caller record in the buffer to read.
 */
static uint32_t g_stub_hit_count;
static uint32_t g_stub_caller_in_buf;
static uint32_t g_first_abort_dfsr;
static uint32_t g_first_abort_lr;
static uint32_t g_first_abort_insn;
static uint32_t g_first_abort_step;
static uint32_t g_first_abort_step_addr;
static uint32_t g_first_abort_kvbuf;
static uint32_t g_first_abort_sp;
static uint32_t g_first_abort_spsr;
static uint32_t g_first_abort_ttbr0;
static uint32_t g_first_abort_ttbr1;
static uint32_t g_first_abort_ttbcr;
static uint32_t g_first_abort_sctlr;
static uint32_t g_first_abort_cpu_ttep;
static uint32_t g_first_abort_avail_start;
static uint32_t g_first_abort_gphysbase;
static uint32_t g_first_abort_mem_size;
static uint32_t g_first_abort_end_kern;
static uint32_t g_first_abort_prelink_b;
static uint32_t g_first_abort_prelink_size;
static uint32_t g_first_abort_hex;
static uint32_t g_first_abort_hex_page;
static uint32_t g_first_abort_hex_used;
static uint32_t g_first_abort_hex_arg;

/*
 * The hex digits, at file scope rather than inside `entry_kv` so that the epilogue can report the
 * address the code has for it (see `xnu_entry_hex_addr`). It is the one `.rodata` object in
 * `entry_kv`'s value path, and 269's first fault decodes to a read at `low16(&this) + 8` with a
 * fault status of "translation fault, read" - an address that cannot be right and a status that
 * cannot be true of a store, which is why the number is worth having beside the linker's own.
 */
static const char g_hex[] = "0123456789abcdef";

/*
 * The writer, once. `entry_kv` and `entry_panic_kv` below are the two callers and differ only in
 * which buffer, which counter and which bound they hand it - so "how a record is spelled" has one
 * definition and the trap's record cannot drift away from the tracer's. Experiment 461 split this
 * out; the body is `entry_kv`'s own, with the globals it used to close over replaced by its
 * arguments.
 */
static void entry_kv_into(char *buf, uint32_t *len_p, uint32_t *dropped_p, uint32_t max,
                          const char *key, uint32_t value)
{
    if (*len_p + 40u >= max) {
        (*dropped_p)++;
        return;
    }
    g_kv_step = 1u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&buf[*len_p];
    buf[(*len_p)++] = ' ';
    g_kv_step = 2u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&buf[*len_p];
    while (*key != '\0' && *len_p + 20u < max) {
        buf[(*len_p)++] = *key++;
    }
    g_kv_step = 3u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&buf[*len_p];
    buf[(*len_p)++] = '=';
    buf[(*len_p)++] = '0';
    buf[(*len_p)++] = 'x';
    g_kv_step = 4u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&buf[*len_p];
    /*
     * `g_kv_hex_in_use` is the address this function has for the digit table, written into `.bss`
     * immediately before the loop that reads it, and `g_kv_hex_arg` is the first index that will be
     * used. Experiment 269's first fault decodes to a read of `low16(&table) + 8` - the table's own
     * low half and a nibble - with a *translation* fault on section zero, which is unmapped. The
     * table is in `.text` at 0x800CA644 in this build, so the high half of the address is precisely
     * what is missing from that fault, and these two words say whether the code's own constant lost
     * it (a code-generation question) or whether the address was right and the fault status is
     * describing something else (a measurement question). The digits themselves are computed
     * arithmetically now, so nothing in this path depends on the answer.
     */
    g_kv_hex_in_use = (uint32_t)(uintptr_t)g_hex;
    g_kv_hex_arg = (value >> 28u) & 0xfu;
    for (unsigned i = 0; i < 8u; i++) {
        unsigned d = (value >> (28u - (i * 4u))) & 0xfu;
        buf[(*len_p)++] = (char)(d < 10u ? ('0' + d) : ('a' + (d - 10u)));
    }
    g_kv_step = 5u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&buf[*len_p + 11];
    buf[(*len_p)++] = '\n';
    buf[*len_p] = '\0';
    g_kv_step = 0u;
}

void entry_kv(const char *key, uint32_t value)
{
    entry_kv_into(g_kv_buf, &g_kv_len, &g_kv_dropped, ENTRY_KV_BUF, key, value);
}

/* The trap's own channel - see the buffer's comment. Used by `fleh_undef` for every key it writes
 * and by the three keys the epilogue writes after the MMU is off, i.e. by every writer on a path
 * that runs *because* the run has stopped. Nothing else may use it: the point of a second buffer is
 * that the trap's record cannot be crowded out by the trace, and a trace that wrote here would put
 * it back in the same competition. */
void entry_panic_kv(const char *key, uint32_t value)
{
    entry_kv_into(g_panic_buf, &g_panic_len, &g_panic_dropped, ENTRY_PANIC_BUF, key, value);
}

/* ------------------------------------------------------------------ the evidence path */

static void entry_write(const char *s);

/*
 * Both are defined far below, beside the exception handlers that read fault addresses; experiment
 * 271's caller probe uses them from `entry_epilogue`, which is above them, so they are declared
 * here rather than moved.
 */
static uint32_t entry_word_at(uintptr_t p);
static int entry_image_ptr(uintptr_t p);

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

#ifdef STAGE90_ENTRY_TRACE
/*
 * Defined below, in the block that has always owned it: the live console appends through the same
 * function the epilogue uses - one writer, one size field, one place a record can be refused.
 */
void entry_write_kv(const char *key, uint32_t value);

/* 481's own block of keys, defined in `entry_timebase.c` along with the state they read - the
 * registering wrapper, the `cpu_data` copy and the countdown sample all live in that file, because
 * the step they belong to is one object and its readings should be readable in one place. */
void entry_timebase_write_kv(void);

/*
 * Experiment 451's live console, corrected by 452. See the slots' comment for the whole story; this
 * is the mechanism.
 *
 * `entry_live_refuse` records the *first* refusal, not the last: the interesting one is the check
 * that failed on the attempt that was made, and a later retry cannot un-make it.
 */
static void entry_live_refuse(uint32_t why)
{
    if (g_live_refuse == 0u)
        g_live_refuse = why;
    g_live_state = 2u;
}

/*
 * The section descriptor's bit fields, spelled with the tree's own names in the comments. `AF` is
 * AP[0] and `SH` is the shareable bit - they are what `start.s` sets on every kernel section
 * (`ARM_TTE_BLOCK_AF`, `ARM_TTE_BLOCK_SH`), and `AP_RWNA` contributes nothing because it is AP[2:0]
 * = 0b001 with AP[2] in bit 15 clear. The domain field is bits [8:5] and is left at 0, which is the
 * domain `ARM_DAC_SETUP = 0x1` enables and the one XNU's own sections use.
 */
#define LIVE_TTE_TYPE_MASK  0x00000003u
#define LIVE_TTE_TYPE_BLOCK 0x00000002u
#define LIVE_TTE_PA_MASK    0xfff00000u
#define LIVE_TTE_BLOCK_AF   0x00000400u   /* AP[0]: privileged RW, user no access */
#define LIVE_TTE_BLOCK_SH   0x00010000u   /* shared (SMP) mapping */

/*
 * The second VA the console is mapped at, so the mapping can be confirmed through a *different*
 * descriptor and the log can say which of the two carried the records. 1 MB past the console is
 * inside the console's own 2 MB window and is an address XNU's 16 MB window does not reach, which is
 * the only property that matters: it has to be a VA nothing else in the boot wants.
 */
#define LIVE_CONSOLE_ALIAS_BASE (RAM_CONSOLE_BASE + 0x01000000u)

/*
 * One descriptor into one slot, with the checks that make the write safe. Returns 1 on success and 0
 * if the slot was already occupied - an occupied slot is *skipped*, not clobbered, because whatever
 * put something there is not this instrument's to overwrite.
 *
 * It takes the attribute as an argument and writes no key of its own: the caller owns both, so the
 * numbers a log records are the numbers this function used, and there is exactly **one** spelling of
 * the descriptor recipe and exactly one of the attribute. 482 split this out of `entry_live_map`
 * instead of writing a second mapper for the GIC, because a second spelling that differs in one bit
 * is a mapping that works until it does not - the twenty-four "one value, two definitions" defects,
 * with a silently wrong memory type as the failure instead of a wrong number.
 */
static uint32_t entry_section_install(uint32_t va, uint32_t pa, uint32_t l1, uint32_t attr,
                                      uint32_t *slot_before_out, uint32_t *desc_out)
{
    uint32_t index = va >> 20;
    volatile uint32_t *slot;
    uint32_t before, desc;

    if (index >= 4096u)
        return 0u;
    slot = (volatile uint32_t *)(uintptr_t)(l1 + 4u * index);
    before = *slot;
    *slot_before_out = before;
    if ((before & LIVE_TTE_TYPE_MASK) != 0u)
        return 0u;

    desc = (pa & LIVE_TTE_PA_MASK) | LIVE_TTE_TYPE_BLOCK | LIVE_TTE_BLOCK_AF | LIVE_TTE_BLOCK_SH |
           attr;
    *slot = desc;
    *desc_out = desc;

    /*
     * Caches are on by the time this runs (`start.s` sets SCTLR.C and SCTLR.I), so the modified
     * descriptor is cleaned to the point of unification before the TLB is told to forget the address
     * - belt and braces for a walk that is itself cacheable and coherent, and the reason the
     * invalidate cannot be the only step.
     */
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(slot) : "memory");
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 1" :: "r"(va) : "memory");
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    return 1u;
}

static uint32_t entry_live_map(uint32_t va, uint32_t pa, uint32_t l1, uint32_t bit)
{
    uint32_t before = 0u, desc = 0u;
    uint32_t ok = entry_section_install(va, pa, l1, g_live_attr, &before, &desc);

    /* The `before` of the *first* call is recorded even when that call was refused - it is the one
     * number that says what was already in the slot this instrument wanted. */
    if (g_live_slot_before == 0u)
        g_live_slot_before = before;
    if (ok == 0u)
        return 0u;

    if (g_live_desc == 0u)
        g_live_desc = desc;
    else
        g_live_desc2 = desc;
    return bit;
}

/*
 * **482: one device-register section for a caller outside this file.** It is the live channel's own
 * mapping - same L1, same attribute, same recipe - and it is deliberately unavailable before the
 * live channel exists: `g_live_attr` is computed once, from this machine's `PRRR` and `SCTLR.TRE`,
 * by the descriptor a console write has already proved, so a caller that asked earlier would be
 * asking for an attribute nobody has read yet. A live state of 1 *is* that proof.
 *
 * The attribute is the thing that must not be re-derived here. `g_live_attr` is 0xc on this device -
 * `PRRR`'s first strongly-ordered encoding, read out of PRRR rather than guessed - and a GIC mapped
 * with a Normal attribute is a peripheral whose registers read as the last value a cache line held.
 * That failure has no symptom until it has a wrong one, which is why this function does not take an
 * attribute argument.
 */
uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                            uint32_t *desc_out)
{
    if (g_live_state != 1u)
        return 0u;
    return entry_section_install(va, pa, g_live_l1, g_live_attr, slot_before_out, desc_out);
}

static void entry_live_init(void)
{
    const uint32_t retry_limit = 8u;
    uint32_t ttbr0, ttbr1, ttbcr, dacr, sctlr, prrr, l1, n, i, attr, installed;

    g_live_attempts++;

    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ttbr1));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr));
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(dacr));
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    __asm__ volatile ("mrc p15, 0, %0, c10, c2, 0" : "=r"(prrr));
    g_live_ttbr0 = ttbr0;
    g_live_ttbr1 = ttbr1;
    g_live_ttbcr = ttbcr;
    g_live_dacr = dacr;
    g_live_sctlr = sctlr;
    g_live_prrr = prrr;

    /*
     * Which table answers for the console's address. `TTBCR.N` is the boundary: N == 0 means every
     * address goes through TTBR0, and N >= 1 sends the upper window to TTBR1 - and the console is
     * above 0x80000000, so those are the only two cases. The low 14 bits are the walk attribute, not
     * part of the base.
     */
    n = ttbcr & 7u;
    l1 = (((n == 0u) ? ttbr0 : ttbr1)) & 0xffffc000u;
    g_live_l1 = l1;

    /*
     * The base is a *physical* address and this code reads it as a virtual one, which is sound only
     * because the entry image's premise is `physBase == virtBase` (`xnu_entry_jump.c` sets both to
     * 0x80000000) - the same premise 444 relies on for the instrument's own memory. A table outside
     * the kernel's window is a reason to wait rather than a reason to guess: the first live write can
     * fire before XNU has switched tables at all, and a retry later in the boot is free.
     */
    if (l1 < 0x80000000u || (l1 & 0x3fffu) != 0u) {
        if (g_live_attempts < retry_limit)
            return;                       /* state stays 0: try again on the next record */
        entry_live_refuse(1u);
        return;
    }

    /*
     * The memory type, and the one thing here that must not be inherited: `CACHE_ATTRINDX_DEFAULT`
     * is write-back, and a console whose writes live in a cache is a console a hang erases. With
     * `SCTLR.TRE` set the encoding is remapped through PRRR, and the Strongly-ordered encodings are
     * exactly those whose two-bit PRRR field is 0 - read out of PRRR rather than guessed (236's era
     * measured `sctlr = 0x30c5787d`, so TRE is set on this device). Without TRE remap, the
     * architecture itself makes `TEX=000 C=0 B=0` Strongly-ordered, which is `attr = 0`.
     */
    attr = 0u;
    if ((sctlr & 0x10000000u) != 0u) {            /* SCTLR.TRE */
        for (i = 0u; i < 8u; i++) {
            if (((prrr >> (2u * i)) & 3u) == 0u)
                break;
        }
        if (i == 8u) {
            /* Every encoding this machine has is Normal: no uncached section is available. */
            entry_live_refuse(5u);
            return;
        }
        /* ARM_TTE_BLOCK_ATTRINDX(i): B = i[0], C = i[1], TEX[2] = i[2]. */
        attr = (((i >> 1) & 1u) << 3) | ((i & 1u) << 2) | (((i >> 2) & 1u) << 12);
    }
    g_live_attr = attr;

    /* Domain 0 is the domain this section is placed in, so DACR has to permit *that* one. */
    if ((dacr & 3u) == 0u) {
        entry_live_refuse(3u);
        return;
    }

    /*
     * Three descriptors: the console's own MB, the MB after it (the buffer is 2 MB), and one VA well
     * clear of both as a second, independent path to the same first MB. The console's own VA is the
     * one that must take - `entry_write_kv` writes there and nowhere else.
     */
    installed = entry_live_map(RAM_CONSOLE_BASE, RAM_CONSOLE_BASE, l1, 1u);
    installed |= entry_live_map(RAM_CONSOLE_BASE + 0x00100000u, RAM_CONSOLE_BASE + 0x00100000u, l1,
                                2u);
    installed |= entry_live_map(LIVE_CONSOLE_ALIAS_BASE, RAM_CONSOLE_BASE, l1, 4u);
    g_live_installed = installed;

    if ((installed & 1u) == 0u) {
        /* Every candidate was already mapped: the address is someone else's and the console is off. */
        entry_live_refuse(2u);
        return;
    }

    /* The proof that the descriptor works is the console's own signature through it. */
    if (*(volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE != RAM_CONSOLE_SIG) {
        entry_live_refuse(4u);
        return;
    }

    /* And the same word through the second VA, when the second VA took. */
    if ((installed & 4u) != 0u)
        g_live_alias_read = *(volatile uint32_t *)(uintptr_t)LIVE_CONSOLE_ALIAS_BASE;

    g_live_state = 1u;
    g_live_records = 0u;
    entry_write_kv("xnu_live_console", 1u);
    entry_write_kv("xnu_live_attempts", g_live_attempts);
    entry_write_kv("xnu_live_ttbr0", ttbr0);
    entry_write_kv("xnu_live_ttbr1", ttbr1);
    entry_write_kv("xnu_live_ttbcr", ttbcr);
    entry_write_kv("xnu_live_dacr", dacr);
    entry_write_kv("xnu_live_l1", l1);
    entry_write_kv("xnu_live_installed", installed);
    entry_write_kv("xnu_live_slot_before", g_live_slot_before);
    entry_write_kv("xnu_live_desc", g_live_desc);
    entry_write_kv("xnu_live_desc2", g_live_desc2);
    entry_write_kv("xnu_live_alias_read", g_live_alias_read);
    entry_write_kv("xnu_live_sctlr", sctlr);
    entry_write_kv("xnu_live_prrr", prrr);
    entry_write_kv("xnu_live_attr", attr);
}

/*
 * Every record the instrument wants to survive a run that never reaches the epilogue goes through
 * here. The first call pays for the mapping and writes the mapping's own numbers, so a log that
 * contains the header and nothing else still says how far XNU's tables allowed the instrument to go.
 *
 * The cap is a *bound*, not a budget: the ram console holds 2 MB and `entry_write_kv` refuses
 * silently past it, so a run that recorded without bound would lose the end of its own trace - the
 * part that says where it stopped. 4096 records is ~200 KB, comfortably inside, and the one record
 * written when the cap is reached says so, which is 441's rule about a refusal having to be visible.
 *
 * A state of 0 means "not installed yet", and 452 keeps it that way for one case only: the live
 * table not being the one this address needs yet, which is what the very first calls of a boot can
 * see before XNU has switched tables. Every other refusal is final, because every other refusal is a
 * property of the machine rather than of the moment. `_attempts` in the report is what tells a
 * retried success from a first-try one.
 */
void entry_live_write(const char *key, uint32_t value)
{
    if (g_live_state == 0u)
        entry_live_init();

    if (g_live_state != 1u) {
        g_live_refusals++;
        return;
    }

    if (g_live_records >= 4096u) {
        if (g_live_records == 4096u)
            entry_write_kv("xnu_live_capped", g_live_records);
        g_live_records++;
        return;
    }

    g_live_records++;
    entry_write_kv(key, value);
}
#endif /* STAGE90_ENTRY_TRACE */

/*
 * **The ram console carries two writers, and these are the numbers that keep them apart.** Records
 * append at the buffer's size field, which *is* their cursor - and so it was for three other writers
 * before 458 (this function, and the two `entry_write` forms below). Experiment 458's console
 * capture is the fourth, and a second cursor cannot be added to that field: two readers would take
 * the same value and space-fill over each other, and because every one of these writers puts its
 * characters down *before* the size field covers them, a record in flight would be written over by a
 * chunk and a chunk by a record.
 *
 * The console capture therefore does **not** take a cursor of its own. It takes one *block*, once:
 * on its first character it reads the size field, publishes `size + ENTRY_OS_BLOCK` into it, and
 * space-fills that block - so from then on the records are above the whole block and cannot reach
 * into it, whatever order they run in, and the block cannot reach up into them. **One** read-modify-
 * write on the shared field replaces one per kilobyte, and it is self-healing: every kilobyte of
 * text re-publishes the block's end if the field has fallen below it, which is the only thing a
 * collision at that instant can do. The residual exposure is stated rather than fixed - a record
 * whose own read-modify-write window straddles the reservation instant can leave the field short by
 * one record - and it is small enough to leave alone: the alternative is `ldrex`/`strex` on this
 * region's mapping, which is unpredictable on ARMv7 and would be a new fault site on the path every
 * reading depends on.
 */
#define ENTRY_OS_BLOCK   0x00020000u   /* 128 KB, reserved once and space-filled in full */

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
 *
 * Non-static since experiment 268, and for one caller only: `entry_trace.c`, which uses it to say
 * which allocator frame a *hang* stopped in - a case where no stub is hit and the epilogue never
 * runs, so the stub path's own report is the one thing that cannot be used. The *declaration* has
 * to be non-static too, and that is not a detail: a function declared `static` once keeps internal
 * linkage no matter what the definition says, which is what the first link of this instrument
 * reported as "undefined reference to `entry_write_kv`" from an object file both the tracer and the
 * stubs were in.
 */
void entry_write_kv(const char *key, uint32_t value)
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

/* ------------------------------------------- Experiment 458: the OS's own console into the log */
/*
 * 457 read the OS's IOKit state through wrappers, but every word of it was *this* instrument's: the
 * OS's own `printf`/`IOLog` output goes to a sink this image cannot see. The source says where, and
 * it is one place:
 *
 *   `printf`  (`osfmk/kern/printf.c:861`, `vprintf_internal`) -> `console_printbuf_putc`
 *   `IOLog`   (`iokit/Kernel/IOLib.cpp:1153` -> `_IOLogv`)     -> the same printbuf
 *   `kprintf` (`:851`, gated by `disable_serial_output`)       -> `_doprnt_log(..., PE_kputc, 16)`
 *
 * and the printbuf ends in `console_write` (`osfmk/console/serial_console.c:450`), which puts every
 * character into `console_ring`; `console_ring_try_empty` (`:361`) drains it through
 * `cons_ops[cons_ops_index].putc` (`:300`) - entry 0 is `_serial_putc` (`:620`), entry 1 `vcputc`.
 * So the two sinks are `vcputc` and, under it, `uart_putc`, and the wrappers in `entry_trace.c` sit
 * in front of both. `vcputc` is reached only through the ops table, i.e. through a *reference that
 * is an address* - `--wrap` rewrites that too, and `build_entry.sh` checks the effect in the linked
 * image rather than trusting it (`cons_ops[1].putc` must hold `__wrap_vcputc`). The serial side is
 * wrapped at `uart_putc` rather than at `serial_putc` for a reason only the image gives:
 * `PE_init_kprintf` (`pexpert/arm/pe_kprintf.c:24-42`) stores `serial_putc` into `PE_kputc` with a
 * `movw`/`movt` pair against a symbol its own object defines, which `--wrap` cannot reach, so a
 * wrapper on `serial_putc` would miss the one sink `kprintf` and `panic` print through;
 * `serial_putc` is a tail call into `uart_putc` (`pe_serial.c:813`), and that call is cross-object,
 * so wrapping `uart_putc` captures the console route, kdp's two `pal_serial_*` and the stored
 * pointer alike, with nothing counted twice. `build_entry.sh` reads that tail call out of the image
 * as well.
 *
 * Both sinks are also *gates* in this build, which is why nothing the OS has printed has ever been
 * visible: `uart_putc` returns unless the *local* `uart_initted` is 1, and `vcputc` returns unless
 * `gc_initialized` is 1. A wrapper in front of them therefore captures text the OS produced and then
 * discarded in silence - which is exactly the text worth having.
 *
 * The characters go into the *same* ram console the live records use, and the way the two writers
 * are kept apart is the block reservation described above `ENTRY_OS_BLOCK`: one 128 KB block, taken
 * contiguously out of the shared cursor on the first character and space-filled in full, so the
 * records are above it from then on and the block is a single self-contained run of text below them.
 * Each capture is bounded and counted, and the counters are in the log (`xnu_live_ostext_*`),
 * including what was refused and what the `.bss` holding tank had to drop before the live channel's
 * page tables were installed - the console is up, and printing, before this instrument's first live
 * record, so the tank is not a nicety.
 *
 * Live only, like every reading taken inside XNU: the boot that hangs at the frontier never reaches
 * `entry_epilogue`, which is 455's whole lesson.
 */
#define ENTRY_OS_TANK    8192u
#define ENTRY_OS_HEAL    1024u        /* characters between re-publishings of the block's end */

uint32_t g_os_calls;          /* calls into the two sinks */
uint32_t g_os_vc_calls;       /* of those, vcputc - the video console the ring drains through */
uint32_t g_os_ser_calls;      /* of those, uart_putc - every serial route, kprintf's included */
uint32_t g_os_chars;          /* characters offered by the OS */
uint32_t g_os_lines;          /* of those, '\n' */
uint32_t g_os_total;          /* characters stored */
uint32_t g_os_chunks;         /* heal re-publishings of the block's end, one per kilobyte of text */
uint32_t g_os_tank_dropped;
uint32_t g_os_limited;        /* 1 the block filled, 2 the buffer had no room */
uint32_t g_os_first;

static uint8_t  g_os_tank[ENTRY_OS_TANK];
static uint32_t g_os_tank_n;
static uint32_t g_os_at;      /* the next character's data offset; 0 until the block is reserved */
static uint32_t g_os_end;     /* one past the block's last byte */
static uint32_t g_os_at_base; /* the offset the last heal published from */

/*
 * The console's own state, read **by name** so that a misspelling is a build failure rather than a
 * generated stub that reads zero - 455's defect twice over and 456's rule. `uart_initted`,
 * `gc_initialized`, `gc_enabled` and `console_suspended` are *local* symbols in this image (`nm`
 * says `b`), so no other object can read them at all; they are deliberately not in this list, and
 * what they would have said is reproducible from the two sinks' counters instead.
 */
extern uint32_t cons_ops_index;               /* `D`, serial_console.c:130 */
extern uint32_t disable_serial_output;        /* `D`, pexpert/arm/pe_kprintf.c:20 */
extern int      disableConsoleOutput;         /* `B`, bsd/dev/arm/km.c:46 */
extern void   (*PE_kputc)(char c);            /* `B`, pexpert/arm/pe_kprintf.c:17 */
extern uint32_t kernel_debugger_entry_count;  /* `B`, osfmk/kern/debug.c */

static void entry_os_state_record(void)
{
    entry_write_kv("xnu_live_console_opsidx", cons_ops_index);
    entry_write_kv("xnu_live_console_kputc", (uint32_t)(uintptr_t)PE_kputc);
    entry_write_kv("xnu_live_console_noserial", disable_serial_output);
    entry_write_kv("xnu_live_console_noconout", (uint32_t)disableConsoleOutput);
    entry_write_kv("xnu_live_console_dbgcnt", kernel_debugger_entry_count);
}

/*
 * Take the block, once, on the first character of text: read the size field, publish
 * `size + ENTRY_OS_BLOCK` into it, and space-fill the block in full. Publishing *before* filling is
 * the point - from the publish onward the records append above the block, so the fill cannot be
 * written over by a record, and the block cannot be written over by one either since its own writes
 * stay inside it. Returns 1 on success, 0 on either refusal, which it records once.
 */
static uint32_t entry_os_reserve(void)
{
    static const char marker[] = "\n[os-console-459]\n";
    volatile uint32_t *sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    volatile uint32_t *size_p = (volatile uint32_t *)(uintptr_t)(RAM_CONSOLE_BASE + 8u);
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)(RAM_CONSOLE_BASE + 12u);
    const uint32_t max = 0x00200000u - 12u;
    const char *m;
    uint32_t base, i;

    if (*sig != RAM_CONSOLE_SIG || *size_p + ENTRY_OS_BLOCK > max) {
        g_os_limited = 2u;
        entry_write_kv("xnu_live_ostext_limited", g_os_limited);
        entry_write_kv("xnu_live_ostext_chars", g_os_chars);
        return 0u;
    }
    base = *size_p;
    *size_p = base + ENTRY_OS_BLOCK;
    for (i = 0u; i < ENTRY_OS_BLOCK; i++)
        data[base + i] = (uint8_t)' ';
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    g_os_at = base;
    g_os_end = base + ENTRY_OS_BLOCK;
    g_os_at_base = base;

    entry_write_kv("xnu_live_ostext_at", base);
    entry_write_kv("xnu_live_ostext_block", ENTRY_OS_BLOCK);
    for (m = marker; *m != '\0'; m++)
        data[g_os_at++] = (uint8_t)*m;
    return 1u;
}

/*
 * Every character the OS offers its console, from the two sink wrappers in `entry_trace.c`. Nothing
 * here takes a lock, allocates or calls into XNU: `_cnputs` runs with preemption disabled and its
 * caller may hold interrupts off, and a console wrapper that behaved like a thread would be a new
 * fault site on the one path every later reading depends on.
 */
void entry_os_console_char(int ch, uint32_t which)
{
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)(RAM_CONSOLE_BASE + 12u);
    volatile uint32_t *size_p = (volatile uint32_t *)(uintptr_t)(RAM_CONSOLE_BASE + 8u);
    uint32_t n, i;

    g_os_calls++;
    if (which == 1u)
        g_os_vc_calls++;
    else
        g_os_ser_calls++;
    g_os_chars++;
    if (ch == '\n')
        g_os_lines++;

    if (g_live_state != 1u) {
        /* The live channel's tables are not installed yet: hold the text in this image's `.bss`. */
        if (g_os_tank_n < ENTRY_OS_TANK)
            g_os_tank[g_os_tank_n++] = (uint8_t)ch;
        else
            g_os_tank_dropped++;
        return;
    }

    if (g_os_first == 0u) {
        g_os_first = 1u;
        entry_os_state_record();
        entry_write_kv("xnu_live_ostext_tank", g_os_tank_n);
    }

    if (g_os_at == 0u && entry_os_reserve() == 0u) {
        g_os_tank_dropped += g_os_tank_n;
        g_os_tank_n = 0u;
        return;
    }

    if (g_os_tank_n != 0u) {
        n = g_os_tank_n;
        g_os_tank_n = 0u;
        for (i = 0u; i < n; i++) {
            if (g_os_at >= g_os_end) {
                g_os_tank_dropped += (n - i);
                return;
            }
            data[g_os_at++] = g_os_tank[i];
            g_os_total++;
        }
    }

    if (g_os_at >= g_os_end) {
        if (g_os_limited == 0u) {
            g_os_limited = 1u;
            entry_write_kv("xnu_live_ostext_limited", g_os_limited);
            entry_write_kv("xnu_live_ostext_chars", g_os_chars);
            entry_write_kv("xnu_live_ostext_lines", g_os_lines);
        }
        return;
    }
    data[g_os_at++] = (uint8_t)ch;
    g_os_total++;

    /*
     * The heal. A record whose own read-modify-write on the size field straddles the reservation
     * instant can leave the field below the block's end, which would cut the block out of the host's
     * dump - so every kilobyte of text puts the block's end back if it has fallen below it. It only
     * ever raises, so it cannot undo a record written above the block.
     */
    if (g_os_at - g_os_at_base >= ENTRY_OS_HEAL) {
        g_os_at_base = g_os_at;
        g_os_chunks++;
        if (*size_p < g_os_end)
            *size_p = g_os_end;
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
        entry_write_kv("xnu_live_ostext_heals", g_os_chunks);
        /*
         * 459: 458's owed gap, closed. The counters above existed but nothing wrote them unless the
         * block *filled*, so 458's own log has no `ostext_chars` in it and the count had to be taken
         * from the block's bytes by hand. A step whose whole product is a count should publish it,
         * and this is the place: the heal already has to write a record every kilobyte of text, so
         * the two counts ride along with it for two more keys. It also makes the *volume* a reading
         * rather than an inference from the block's length - which is what says whether the boot's
         * own text would have overflowed the 128 KB block.
         */
        entry_write_kv("xnu_live_ostext_chars", g_os_chars);
        entry_write_kv("xnu_live_ostext_total", g_os_total);
    }
}

/*
 * ------------------------------------------------------------------------------------------------
 * 459: the root device's memory - one RAM disk, in this image's `.bss`.
 * ------------------------------------------------------------------------------------------------
 *
 * 457 and 458 left the boot inside `IOFindBSDRoot`, in
 * `do { service = IOService::waitForService(matching, &t); } while (!service)` for an `IOMedia` with
 * `Content = Apple_HFS` (`iokit/bsddev/IOKitBSDInit.cpp:552-569`) - a device this machine cannot
 * have, because nothing in this image drives the eMMC. There is a second way out of that function
 * and it is *not* a device: the same function, a few lines earlier, turns a `/chosen/memory-map`
 * `RAMDisk` property into a memory device and returns as soon as the boot-arg `rd=md0` names it
 * (`:436-490`, `goto iofrootx`).
 *
 * The chain, from the source rather than from the name:
 *
 *   `/chosen/memory-map`'s `RAMDisk` is two machine words, {base, size}
 *   (`:445-447`), and `mdevadd(-1, ml_static_ptovirt(word0) >> 12, word1 >> 12, 0)` is called with
 *   them. `ml_static_ptovirt` is `phystokv` (`osfmk/arm/machine_routines.c:730-734`) and this
 *   image's boot_args have `physBase == virtBase` (`stages/stage90/xnu_entry_jump.c:131-141`), so
 *   the identity: **word0 is a virtual address in this image's own window and word1 is a byte
 *   count.** `mdevadd` stores base>>12 and size>>12 in pages (`bsd/dev/memdev.c:624-628`), adds a
 *   block and a character device through `bdevsw_add`/`cdevsw_add_with_bdev`, and makes `/dev/md0`
 *   and `/dev/rmd0` in the devfs tree (`:606-622`). `mdevlookup(0)` then returns that device
 *   number to `IOFindBSDRoot`, which sets `*root`, prints `BSD root: md0, major .., minor ..` and
 *   jumps past the 60-second wait.
 *
 * **The disk is this image's own `.bss`, and that is the point rather than a convenience.** The
 * memory device reads its contents through `mdBase` as a *virtual* address in this kernel's
 * address space (`phys = 0` in the `mdevadd` call), so the only regions it can name are ones this
 * image maps: the payload's `.bss` is at a different base and is not in these page tables. An array
 * here is therefore the smallest thing that can be a root device, and `build_entry.sh` reads its
 * address and its size out of the linked image and writes them into the generated header the
 * payload builds the device tree from - so the property and the symbol cannot disagree, which is
 * this project's oldest defect class and the reason the value is not a constant anywhere.
 *
 * **Experiment 468 moved the array out of this file and into `entry_ramdisk.s`, and gave it
 * contents.** It is no longer `.bss` and no longer empty: the disk now holds the Mach-O of the first
 * userland process, at offset 0, because 467's run measured that the exec reaches the image
 * activators and that every one of them declines a file whose first four bytes are zero. The reasons
 * for the new shape - the segment layout `parse_machfile` requires, the one instruction the process
 * runs, and why the object is padded to two pages so that the device's `si_devsize` is exactly its
 * size - are at the top of that file. The symbol, its 4096-byte alignment, its 0x2000-byte size and
 * its page-aligned `mdevadd` base are all still checked here, by `build_entry.sh`, over the linked
 * image rather than over this file.
 */

/*
 * Experiment 272's reading: the instruction words that are *in memory* over the whole report path,
 * printed as one line, written from the epilogue, where the caches and the mmu are both off and a
 * read is a read of memory.
 *
 * Experiment 271 left one question open and this measurement closed it. That run's `entry_kv`
 * computed `0x30 + d` for the non-decimal nibbles where its own instruction says `0x57 + d`, and the
 * same function, run from the epilogue with SCTLR.C and SCTLR.I clear and the mmu off, computed it
 * correctly. `entry_kv`'s digit loop is three instructions - `cmp r3, #9`, then the unconditional
 * `add r4, r3, #0x57`, then the conditional `addls r4, r3, #0x30` - and the corrupted digits are
 * exactly what executing the *conditional* instruction with the wrong flags produces: `:` for `a`,
 * `<` for `c`, every decimal digit right. There are only two ways that happens:
 *
 *   - the words *in memory* at that address are not the words in the file, or
 *   - the words in memory are right and the core fetched or executed something else - which on
 *     ARMv7-A is what an instruction fetch from Strongly-ordered memory is, and this stage maps
 *     every page of the window Strongly-ordered (`STAGE90_PMAP_ATTR_MODE_SO_ONLY`).
 *
 * **The answer is the second.** In the run this probe was built for, all 661 words - `entry_kv`,
 * `entry_write_kv`, `entry_epilogue` and `entry_stub_hit`, 2644 bytes, the whole path that produces
 * the report - matched `out/stage90/xnu_arm_entry.elf`'s `.text` byte for byte, read back here with
 * the mmu and the caches off. In that same run `entry_kv` wrote `800:<254` into `g_kv_buf` while
 * `entry_kv` called from *this* epilogue wrote `800ac254` for the same value: the right bytes were
 * in memory and the run's execution of them was not. Memory is ruled out; what is left is the
 * window's fetch.
 *
 * **The address is a symbol and the dump is the region, deliberately.** The first version took the
 * digit loop's address with GCC's `&&label` inside `entry_kv`; that build ran and then, three times
 * in a row, produced no report at all - the payload reached `jumping to XNU's _start` and the entry
 * image never wrote another byte, while the *unmodified* 271 image on the same device and the same
 * payload reached `stub_hit=mk_timer_init` as its prediction said. The only two instructions that
 * version added to a run-time path were the `adr` that takes the label and the `str` that keeps it,
 * both inside `entry_kv`, which runs while XNU's page tables are live - the same window and the same
 * function 271 watched misbehave. So this version adds nothing to `entry_kv`, and takes the base
 * from `&entry_kv`, which the linker fixes rather than a constant a later link would invalidate.
 *
 * **The 88-word version of *this* probe is the caution that comes with the reading.** It dumped
 * `entry_kv` alone, had the same `.text` size as this one, and its two runs were identical to each
 * other and wrong: `g_kv_len` read 1 where the register captured before the teardown held 0x5e,
 * `g_why` read 0x800e3d00 (the linker's `__entry_text_end`) where the caller passed 0x800ce9f4, and
 * the `g_first_abort_*` block read a stretch of `.rodata` string bytes. This build reads all of them
 * correctly and repeats. Same source, same size, one constant apart - so the entry image's *own
 * report* is not a function of its source either, and every field in it wants a second road before
 * it is believed. The fields that come from registers, from `g_kv_buf`, and from the stopped stub
 * have all survived every build so far; the ones that come from the small `.bss` globals are the
 * ones that have not.
 *
 * A word outside the image prints as `eeeeeeee` rather than `00000000`: zero is a legitimate
 * instruction word, and a sentinel that can be mistaken for one is the defect this sequence is
 * about.
 */
static void
entry_probe_dump_kv_words(const char *key, uint32_t base, uint32_t words)
{
    static const char hex[] = "0123456789abcdef";
    volatile uint32_t *sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    volatile uint32_t *size_p = (volatile uint32_t *)(uintptr_t)(RAM_CONSOLE_BASE + 8u);
    volatile uint8_t *data = (volatile uint8_t *)(uintptr_t)(RAM_CONSOLE_BASE + 12u);
    const uint32_t max = 0x00200000u - 12u;
    uint32_t size;

    if (*sig != RAM_CONSOLE_SIG) {
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
    for (uint32_t i = 0; i < words; i++) {
        uint32_t addr = base + (i * 4u);
        uint32_t w = entry_image_ptr((uintptr_t)addr) ? entry_word_at((uintptr_t)addr)
                                                      : 0xEEEEEEEEu;
        for (unsigned j = 0; j < 8u; j++) {
            entry_putc(data, &size, max, hex[(w >> (28u - (j * 4u))) & 0xfu]);
        }
    }
    entry_putc(data, &size, max, '\n');

    *size_p = size;
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

/*
 * The one exit. `why` must be in read-only memory inside the window, which everything in this
 * image is.
 */
/* Non-static since experiment 268, for `entry_trace.c`: an instrument that converts a hang into
 * the report this function exists to write has to be able to reach it. No static declaration
 * above it, deliberately - a `static` on either the declaration or the definition keeps internal
 * linkage, which is what the first link of this instrument reported as an undefined reference. */
/*
 * Experiment 455's keys, written from a function of their own - and the reason is a build failure
 * rather than taste. Adding these twenty-seven `entry_write_kv` calls to `entry_epilogue` moved that
 * function's constant pool past PC-relative range: the assembler refused the whole object with
 * `bad immediate value for offset (4508)` against `ldr r1, .L192`, where `.L192` is `entry_epilogue`'s
 * own pool at the end of its body and the first use of it is a few instructions in. `entry_epilogue`
 * is ~4.4 KB of `movw`/`movt`/`ldr`/`bl` per key and was already at the 4095-byte edge; the split
 * gives this block a pool of its own and the next experiment's keys the same room. The categorical fix
 * - one table of `{name, &value}` walked by a loop, so that adding a key costs data and no code - is
 * recorded as the next step's problem rather than done here, because the epilogue runs only when the
 * boot *returns* to the payload and cannot be exercised by any run of this frontier.
 */
/*
 * Experiment 269's abort readings, moved out of `entry_epilogue` by 467 for the reason given at
 * `entry_write_455_kv`: these twenty-five `entry_write_kv` calls, appended to that function, put its
 * constant pool past PC-relative range and the assembler refused the whole object with
 * `bad immediate value for offset (4096)` against `ldr r0, .L277`, a load from `entry_epilogue`'s own
 * pool 4096 bytes after it. The keys and the comment above them are unchanged; only the function that
 * holds them is, and the helper is called at exactly the position they used to occupy, so the order
 * of the records in the buffer is identical.
 */
__attribute__((noinline)) static void entry_write_269_kv(void)
{
    /*
     * Experiment 269's reading of a full results buffer, in four numbers. They are read from `.bss`
     * here rather than from registers because they are written long before the teardown and the
     * teardown's set/way sweep covers the whole D-cache - the same reason `xnu_entry_kv_in_dram`
     * works. `_entries` says whether the data-abort handler re-entered itself and how often;
     * `_first_dfar`/`_first_pc` say what the first fault actually was; `_first_kv_len` says how much
     * of the buffer was already written when it started.
     */
    entry_write_kv("xnu_entry_abort_entries", g_abort_entries);
    entry_write_kv("xnu_entry_abort_first_dfar", g_first_abort_dfar);
    entry_write_kv("xnu_entry_abort_first_pc", g_first_abort_pc);
    entry_write_kv("xnu_entry_abort_first_kv_len", g_first_abort_kv_len);
    /*
     * The second group, which is the one that decides between the two readings of the first. `_dfsr`
     * is the fault status word - bits 3:0 the fault type, bit 10 whether it was a write - and `_insn`
     * is the word at `pc_abt`, so a `pc_abt` that is not an instruction is visible as one rather than
     * assumed to be. `_step`/`_step_addr` come from `entry_kv` itself and say what it believed it was
     * about to store and where, `_kvbuf` is the address the *code* has for the buffer, and `_sp` is
     * the exception stack pointer at entry.
     */
    entry_write_kv("xnu_entry_abort_first_dfsr", g_first_abort_dfsr);
    entry_write_kv("xnu_entry_abort_first_lr", g_first_abort_lr);
    entry_write_kv("xnu_entry_abort_first_insn", g_first_abort_insn);
    entry_write_kv("xnu_entry_abort_first_step", g_first_abort_step);
    entry_write_kv("xnu_entry_abort_first_step_addr", g_first_abort_step_addr);
    entry_write_kv("xnu_entry_abort_first_kvbuf", g_first_abort_kvbuf);
    entry_write_kv("xnu_entry_abort_first_sp", g_first_abort_sp);
    entry_write_kv("xnu_entry_abort_first_spsr", g_first_abort_spsr);
    entry_write_kv("xnu_entry_abort_first_ttbr0", g_first_abort_ttbr0);
    entry_write_kv("xnu_entry_abort_first_ttbr1", g_first_abort_ttbr1);
    entry_write_kv("xnu_entry_abort_first_ttbcr", g_first_abort_ttbcr);
    entry_write_kv("xnu_entry_abort_first_sctlr", g_first_abort_sctlr);
    entry_write_kv("xnu_entry_abort_first_cpu_ttep", g_first_abort_cpu_ttep);
    entry_write_kv("xnu_entry_abort_first_avail_start", g_first_abort_avail_start);
    entry_write_kv("xnu_entry_abort_first_gphysbase", g_first_abort_gphysbase);
    entry_write_kv("xnu_entry_abort_first_mem_size", g_first_abort_mem_size);
    entry_write_kv("xnu_entry_abort_first_end_kern", g_first_abort_end_kern);
    entry_write_kv("xnu_entry_abort_first_prelink_b", g_first_abort_prelink_b);
    entry_write_kv("xnu_entry_abort_first_prelink_size", g_first_abort_prelink_size);
    entry_write_kv("xnu_entry_abort_first_hex", g_first_abort_hex);
    entry_write_kv("xnu_entry_abort_first_hex_page", g_first_abort_hex_page);
    entry_write_kv("xnu_entry_abort_first_hex_used", g_first_abort_hex_used);
    entry_write_kv("xnu_entry_abort_first_hex_arg", g_first_abort_hex_arg);
}

__attribute__((noinline)) static void entry_write_455_kv(void)
{
    entry_write_kv("xnu_entry_match_calls", g_match_calls);
    entry_write_kv("xnu_entry_match_hits", g_match_hits);
    entry_write_kv("xnu_entry_match_last_site", g_match_last_site);
    entry_write_kv("xnu_entry_match_last_dict", g_match_last_dict);
    entry_write_kv("xnu_entry_match_last_in", g_match_last_in);
    entry_write_kv("xnu_entry_match_last_opts", g_match_last_opts);
    entry_write_kv("xnu_entry_match_last_res", g_match_last_res);
    entry_write_kv("xnu_entry_mpass_calls", g_mpass_calls);
    entry_write_kv("xnu_entry_mpass_rs_calls", g_mpass_rs_calls);
    entry_write_kv("xnu_entry_mpass_rs_site", g_mpass_rs_site);
    entry_write_kv("xnu_entry_mpass_rs_dict", g_mpass_rs_dict);
    entry_write_kv("xnu_entry_mpass_rs_opts", g_mpass_rs_opts);
    entry_write_kv("xnu_entry_mpass_rs_res", g_mpass_rs_res);
    entry_write_kv("xnu_entry_mpass_rs_this", g_mpass_rs_this);
    entry_write_kv("xnu_entry_mpass_last_site", g_mpass_last_site);
    entry_write_kv("xnu_entry_mpass_last_dict", g_mpass_last_dict);
    entry_write_kv("xnu_entry_mpass_last_res", g_mpass_last_res);
    entry_write_kv("xnu_entry_mpass_last_this", g_mpass_last_this);
    entry_write_kv("xnu_entry_dict_calls", g_dict_calls);
    entry_write_kv("xnu_entry_dict_last_site", g_dict_last_site);
    entry_write_kv("xnu_entry_dict_last_name", g_dict_last_name);
    entry_write_kv("xnu_entry_dict_last_in", g_dict_last_in);
    entry_write_kv("xnu_entry_dict_last_out", g_dict_last_out);
    entry_write_kv("xnu_entry_rs_ptr", g_rs_ptr);
    entry_write_kv("xnu_entry_rs_state0", g_rs_state0);
    entry_write_kv("xnu_entry_rs_state1", g_rs_state1);
    /* Experiment 454's two IOKit deadline waits, moved here for the same reason as 455's keys. */
    entry_write_kv("xnu_entry_iolock_calls", g_iolock_calls);
    entry_write_kv("xnu_entry_iolock_c0", g_iolock_count[0]);
    entry_write_kv("xnu_entry_iolock_c1", g_iolock_count[1]);
    entry_write_kv("xnu_entry_iolock_first_ent", g_iolock_first_ent);
    entry_write_kv("xnu_entry_iolock_first_site", g_iolock_first_site);
    entry_write_kv("xnu_entry_iolock_first_thread", g_iolock_first_thread);
    entry_write_kv("xnu_entry_iolock_last_ent", g_iolock_last_ent);
    entry_write_kv("xnu_entry_iolock_last_site", g_iolock_last_site);
    entry_write_kv("xnu_entry_iolock_last_thread", g_iolock_last_thread);
    entry_write_kv("xnu_entry_iolock_last_lock", g_iolock_last_lock);
    entry_write_kv("xnu_entry_iolock_last_event", g_iolock_last_event);
    entry_write_kv("xnu_entry_iolock_last_inter", g_iolock_last_inter);
    entry_write_kv("xnu_entry_iolock_last_dl_lo", g_iolock_last_dl_lo);
    entry_write_kv("xnu_entry_iolock_last_dl_hi", g_iolock_last_dl_hi);
    entry_write_kv("xnu_entry_iolock_last_now", g_iolock_last_now);
}

/*
 * 459: the OS's own console text, counted, at the one place that always runs.
 *
 * 458's run is why this exists and why it is not in the capture path. The counters were already
 * there (`g_os_chars`, `g_os_total`, `g_os_lines`, `g_os_limited`) but only the *rare* exits
 * published them - the block filling, or a refusal - so a run whose captured text is a few hundred
 * bytes, which is every run so far, has none of them in its log and the count has to be taken from
 * the block's bytes by hand. The heal above publishes them every kilobyte, which is the right
 * cadence for a *long* run and still silent for a short one.
 *
 * The epilogue is the other end: it runs on every run that returns to the payload at all, which is
 * every run of this frontier, and it is where the rest of the entry's `xnu_entry_*` state is
 * already written. A function of its own for the same reason `entry_write_455_kv` is one - the
 * epilogue's constant pool is at the ±4095-byte PC-relative edge and each new key costs code in
 * whichever function holds it.
 */
__attribute__((noinline)) static void entry_write_459_kv(void)
{
    entry_write_kv("xnu_entry_ostext_chars", g_os_chars);
    entry_write_kv("xnu_entry_ostext_total", g_os_total);
    entry_write_kv("xnu_entry_ostext_lines", g_os_lines);
    entry_write_kv("xnu_entry_ostext_heals", g_os_chunks);
    entry_write_kv("xnu_entry_ostext_limited", g_os_limited);
}

/*
 * 461's keys, in a function of their own for 455's reason - the epilogue's constant pool is at the
 * PC-relative edge and each new key costs code in whichever function holds it.
 *
 * Three groups. The **panic buffer's** three, which say whether the trap record in this report is
 * complete or was refused (`_dropped`), and whether the handler ran at all (`_entered`, which an
 * empty buffer cannot say). The **plane's** four, which are the object 459's frontier left
 * unmeasured: `dtplane` is `gIODTPlane` after `IODeviceTreeAlloc` returned, and `dtplane_map` is
 * `fromPath("/chosen/memory-map", gIODTPlane)` called at that moment - a non-zero reading is the
 * plane holding the node *before* BSD init ever asks for it. The **chain's** own counts, which are
 * what turn a missing key into a named one: `dtpath_count` with `dtpath_null`, then `dtprop_calls`
 * against `dtprop_hits`, then `mdevadd_calls`.
 */
__attribute__((noinline)) static void entry_write_461_kv(void)
{
    entry_write_kv("xnu_entry_panic_entered", g_panic_entered);
    entry_write_kv("xnu_entry_panic_len", g_panic_len);
    entry_write_kv("xnu_entry_panic_dropped", g_panic_dropped);
    entry_write_kv("xnu_entry_dtplane", g_dtplane);
    entry_write_kv("xnu_entry_dtplane_dt_top", g_dtplane_dt_top);
    entry_write_kv("xnu_entry_dtplane_root", g_dtplane_root);
    entry_write_kv("xnu_entry_dtplane_map", g_dtplane_map_ret);
    entry_write_kv("xnu_entry_dtplane_count", g_dtplane_count);
    entry_write_kv("xnu_entry_dtpath_count", g_dtpath_count);
    entry_write_kv("xnu_entry_dtpath_caller", g_dtpath_caller);
    entry_write_kv("xnu_entry_dtpath_w0", g_dtpath_w0);
    entry_write_kv("xnu_entry_dtpath_w1", g_dtpath_w1);
    entry_write_kv("xnu_entry_dtpath_w2", g_dtpath_w2);
    entry_write_kv("xnu_entry_dtpath_plane", g_dtpath_plane);
    entry_write_kv("xnu_entry_dtpath_ret", g_dtpath_ret);
    entry_write_kv("xnu_entry_dtpath_null", g_dtpath_null);
    entry_write_kv("xnu_entry_dtprop_calls", g_dtprop_calls);
    entry_write_kv("xnu_entry_dtprop_hits", g_dtprop_hits);
    entry_write_kv("xnu_entry_dtprop_entry", g_dtprop_entry);
    entry_write_kv("xnu_entry_dtprop_key0", g_dtprop_key0);
    entry_write_kv("xnu_entry_dtprop_key1", g_dtprop_key1);
    entry_write_kv("xnu_entry_dtprop_obj", g_dtprop_obj);
    entry_write_kv("xnu_entry_dtprop_w0", g_dtprop_w0);
    entry_write_kv("xnu_entry_dtprop_w1", g_dtprop_w1);
    entry_write_kv("xnu_entry_dtprop_bytes", g_dtprop_bytes);
    entry_write_kv("xnu_entry_mdevadd_calls", g_mdevadd_calls);
    entry_write_kv("xnu_entry_mdevadd_caller", g_mdevadd_caller);
    entry_write_kv("xnu_entry_mdevadd_devid", g_mdevadd_devid);
    entry_write_kv("xnu_entry_mdevadd_base", g_mdevadd_base);
    entry_write_kv("xnu_entry_mdevadd_size", g_mdevadd_size);
    entry_write_kv("xnu_entry_mdevadd_phys", g_mdevadd_phys);
    entry_write_kv("xnu_entry_mdevadd_ret", g_mdevadd_ret);
    entry_write_kv("xnu_entry_mdevlookup_calls", g_mdevlookup_calls);
    entry_write_kv("xnu_entry_mdevlookup_caller", g_mdevlookup_caller);
    entry_write_kv("xnu_entry_mdevlookup_devid", g_mdevlookup_devid);
    entry_write_kv("xnu_entry_mdevlookup_ret", g_mdevlookup_ret);
    entry_write_kv("xnu_entry_osr_calls", g_osr_calls);
    entry_write_kv("xnu_entry_osr_caller", g_osr_caller);
    entry_write_kv("xnu_entry_osr_ns", g_osr_ns);
    entry_write_kv("xnu_entry_osr_code", g_osr_code);
    entry_write_kv("xnu_entry_osr_ret", g_osr_ret);
    entry_write_kv("xnu_entry_lmf_calls", g_lmf_calls);
    entry_write_kv("xnu_entry_lmf_caller", g_lmf_caller);
    entry_write_kv("xnu_entry_lmf_header", g_lmf_header);
    entry_write_kv("xnu_entry_lmf_ret", g_lmf_ret);
}

/*
 * 462's keys, in a function of their own for 455's reason - the epilogue's constant pool is at the
 * PC-relative edge and each new key costs code in whichever function holds it.
 *
 * Thirteen keys, and they are one reading taken twice: the walk from the registry root at the moment
 * `IODeviceTreeAlloc` returned (`t1`) and at the moment the OS's own `fromPath` was entered (`t2`).
 * `walk_probes` is what makes an all-zero `t2` readable - one probe and a zero `t2` is "the walk was
 * read once", while two probes and a zero `t2` would be "the second reading found nothing", and the
 * two cases must not look alike. `control` is the instrument's own `fromPath` with the OS's own path
 * at that instant: non-zero while the OS's own call returns zero says the two calls differ, and both
 * zero says the registry does.
 */
__attribute__((noinline)) static void entry_write_462_kv(void)
{
    entry_write_kv("xnu_entry_walk_probes", g_walk_probes);
    entry_write_kv("xnu_entry_walk_t1_root", g_walk_t1_root);
    entry_write_kv("xnu_entry_walk_t1_count", g_walk_t1_count);
    entry_write_kv("xnu_entry_walk_t1_first", g_walk_t1_first);
    entry_write_kv("xnu_entry_walk_t1_set", g_walk_t1_set);
    entry_write_kv("xnu_entry_walk_t1_kids", g_walk_t1_kids);
    entry_write_kv("xnu_entry_walk_t1_control", g_walk_t1_control);
    entry_write_kv("xnu_entry_walk_t2_root", g_walk_t2_root);
    entry_write_kv("xnu_entry_walk_t2_count", g_walk_t2_count);
    entry_write_kv("xnu_entry_walk_t2_first", g_walk_t2_first);
    entry_write_kv("xnu_entry_walk_t2_set", g_walk_t2_set);
    entry_write_kv("xnu_entry_walk_t2_kids", g_walk_t2_kids);
    entry_write_kv("xnu_entry_walk_t2_control", g_walk_t2_control);
}

/*
 * 463's keys, in a function of their own for 455's reason - the epilogue's constant pool is at the
 * PC-relative edge, and each new key costs code in whichever function holds it.
 *
 * Twenty-eight keys, and the group is one chain read end to end: the wait, its timeout, the predicate
 * twice, the class lookup, and the instances the lookup yielded with each one's `__state[0]`. The
 * `_calls` and `_seq` counters are the reason an all-zero group is readable - a `wsvc_last_p4 = 0`
 * next to `wsvc_calls = 0` says the probe never ran, and the same zero next to `wsvc_calls = 4` says
 * the predicate answered with nothing four times.
 */
__attribute__((noinline)) static void entry_write_463_kv(void)
{
    entry_write_kv("xnu_entry_wmatch_calls", g_wmatch_calls);
    entry_write_kv("xnu_entry_wmatch_last_ent", g_wmatch_last_ent);
    entry_write_kv("xnu_entry_wmatch_last_caller", g_wmatch_last_caller);
    entry_write_kv("xnu_entry_wmatch_last_dict", g_wmatch_last_dict);
    entry_write_kv("xnu_entry_wmatch_last_to_lo", g_wmatch_last_to_lo);
    entry_write_kv("xnu_entry_wmatch_last_to_hi", g_wmatch_last_to_hi);
    entry_write_kv("xnu_entry_wmatch_last_ret", g_wmatch_last_ret);
    entry_write_kv("xnu_entry_wsvc_calls", g_wsvc_calls);
    entry_write_kv("xnu_entry_wsvc_last_dict", g_wsvc_last_dict);
    entry_write_kv("xnu_entry_wsvc_last_sym", g_wsvc_last_sym);
    entry_write_kv("xnu_entry_wsvc_last_p4", g_wsvc_last_p4);
    entry_write_kv("xnu_entry_wsvc_last_p0", g_wsvc_last_p0);
    entry_write_kv("xnu_entry_wcls_calls", g_wcls_calls);
    entry_write_kv("xnu_entry_wcls_last_sym", g_wcls_last_sym);
    entry_write_kv("xnu_entry_wcls_last_meta", g_wcls_last_meta);
    entry_write_kv("xnu_entry_wcls_last_rsvc", g_wcls_last_rsvc);
    entry_write_kv("xnu_entry_wcls_last_name0", g_wcls_last_name0);
    entry_write_kv("xnu_entry_wcls_last_name1", g_wcls_last_name1);
    entry_write_kv("xnu_entry_wcls_seen", g_wcls_seen);
    entry_write_kv("xnu_entry_wcls_shown", g_wcls_shown);
    entry_write_kv("xnu_entry_wcls_inst0", g_wcls_inst[0]);
    entry_write_kv("xnu_entry_wcls_inst1", g_wcls_inst[1]);
    entry_write_kv("xnu_entry_wcls_inst2", g_wcls_inst[2]);
    entry_write_kv("xnu_entry_wcls_inst3", g_wcls_inst[3]);
    entry_write_kv("xnu_entry_wcls_state0", g_wcls_state[0]);
    entry_write_kv("xnu_entry_wcls_state1", g_wcls_state[1]);
    entry_write_kv("xnu_entry_wcls_state2", g_wcls_state[2]);
    entry_write_kv("xnu_entry_wcls_state3", g_wcls_state[3]);
    entry_write_kv("xnu_entry_wls_seen", g_wls_seen);
}

/*
 * 467's keys, in a function of their own for 455's reason - and this one is not a precaution: adding
 * these six to `entry_epilogue` **measured** the limit. The compile failed with
 * `Assembler messages: bad immediate value for offset (4188)`, i.e. a `ldr rX, .Lpool+N` whose
 * distance had just crossed 4095, and the message names a line of compiler output and not the
 * function that grew; removing only these six lines and rebuilding is the control that identifies
 * them. 463's comment above says the same thing about the same function, which is what makes this a
 * defect of method rather than of the toolchain (see `mi4-measurement-defects`, 190).
 *
 * The reading itself: `_seq` and `_back` are the pair that separates a fault the kernel *serviced* -
 * the page was paged in and the faulting instruction retried - from a handler that was entered and
 * never came back, which is a panic or a re-entry. `_dfsr`/`_dfar` are the latest entry's, so a
 * retry of the same address is visible as a repetition, and `_type` moves if a later step hands a
 * different abort vector to the kernel. `_storm` is non-zero only if the live cap cut the live
 * records short, and it carries the entry count the cap stopped at.
 *
 * `.bss` here and live records during the run, both, deliberately: a serviced fault means the boot
 * goes on and may never reach this epilogue, and a panic means it does.
 */
__attribute__((noinline)) static void entry_write_467_kv(void)
{
    entry_write_kv("xnu_entry_sleh_seq", g_sleh_seq);
    entry_write_kv("xnu_entry_sleh_back", g_sleh_back);
    entry_write_kv("xnu_entry_sleh_type", g_sleh_type);
    entry_write_kv("xnu_entry_sleh_fsr", g_sleh_fsr);
    entry_write_kv("xnu_entry_sleh_far", g_sleh_far);
    entry_write_kv("xnu_entry_sleh_storm", g_sleh_storm);
}

/*
 * 474's keys, in a function of their own - 455's and 463's lesson about the epilogue's constant
 * pool, which is at the PC-relative edge: six more `entry_write_kv` calls in `entry_write_467_kv`
 * is exactly the edit that overflowed it once already (`bad immediate value for offset (4188)`),
 * and the failure names a line of compiler output rather than the function that grew.
 *
 * These are the reading the *epilogue* channel can carry, and they are the same eight numbers the
 * live channel records per entry - except that `.bss` holds only the latest entry's, so this
 * channel answers "what was the last abort" and the live channel answers "what were aborts 1
 * through 8". The two are not redundant in the runs this step is for: a boot that reaches this
 * epilogue has already said, by getting here, that the abort path was entered and left.
 *
 * There is deliberately no `_seen` key here. It is the live channel's, and it means "the entry
 * count as of this abort" - bounded, so that its *stopping* at 64 is a reading. In `.bss` the same
 * number is `xnu_entry_sleh_seq`, written six lines above, and a second key carrying one value
 * would be the "one value, two definitions" shape this project keeps meeting.
 */
__attribute__((noinline)) static void entry_write_474_kv(void)
{
    entry_write_kv("xnu_entry_sleh_pc", g_sleh_pc);
    entry_write_kv("xnu_entry_sleh_lr", g_sleh_lr);
    entry_write_kv("xnu_entry_sleh_sp", g_sleh_sp);
    entry_write_kv("xnu_entry_sleh_cpsr", g_sleh_cpsr);
    entry_write_kv("xnu_entry_sleh_fsr_frame", g_sleh_fsr_frame);
    entry_write_kv("xnu_entry_sleh_far_frame", g_sleh_far_frame);
    entry_write_kv("xnu_entry_sleh_frame_ok", g_sleh_frame_ok);
    entry_write_kv("xnu_entry_sleh_user", g_sleh_user_mode);
}

/*
 * 478's keys, in a function of their own for 455's reason - the epilogue's constant pool is at the
 * PC-relative edge and each new key costs code in whichever function holds it.
 *
 * Sixteen keys, and the group answers one question three ways, because the run this step is for may
 * die at the panic and only the **live** channel survives that:
 *
 *   - `_result`/`_result_caller` are the last return, which is the value `ipc_mqueue_receive`'s
 *     `switch` was handed - the argument of the panic 476 and 477 both stopped on.
 *   - `_results_k0 .. _results_k6` are the histogram, one slot per enum member. Slot 6 is "not one of
 *     them" and slots 4 and 5 are the two candidates the 477 doc named, `THREAD_NOT_WAITING` (10) and
 *     `THREAD_WAITING` (-1), so **a run whose counts are all in slots 0..3 has no explanation to
 *     give** and the reading has to be looked for elsewhere rather than read off the histogram.
 *   - `_first_odd_*` is the first return outside 0..3 with its caller and its place in the sequence.
 *     It exists because the histogram alone cannot say *when* the bad value appeared: the last result
 *     before a panic and the first unusable one are the same event only if the panic followed it
 *     immediately, and that is an assumption this key makes unnecessary.
 *
 * The slot names come from `entry_block_result_key` and not from a table here for the reason 478's
 * slot comment gives: `entry_block_result_slot` is the mapping, and writing it out again in the
 * report is the "one value, two definitions" shape. `_results_k%d` keeps the suffix convention the
 * sleep and iolock counters use.
 */
__attribute__((noinline)) static void entry_write_478_kv(void)
{
    entry_write_kv("xnu_entry_block_result", g_block_last_result);
    entry_write_kv("xnu_entry_block_result_caller", g_block_last_result_caller);
    for (unsigned i = 0; i < ENTRY_BLOCK_RESULT_SLOTS; i++)
        entry_write_kv(entry_block_result_key(i), g_block_results[i]);
    entry_write_kv("xnu_entry_block_first_odd_seq", g_block_first_odd_seq);
    entry_write_kv("xnu_entry_block_first_odd_result", g_block_first_odd_result);
    entry_write_kv("xnu_entry_block_first_odd_caller", g_block_first_odd_caller);
    for (unsigned i = 0; i < 8u; i++) {
        static const char *const br[8] = {
            "xnu_entry_block0_result", "xnu_entry_block1_result",
            "xnu_entry_block2_result", "xnu_entry_block3_result",
            "xnu_entry_block4_result", "xnu_entry_block5_result",
            "xnu_entry_block6_result", "xnu_entry_block7_result"
        };

        entry_write_kv(br[i], g_block_ring_result[i]);
    }
}

__attribute__((noinline)) static void entry_write_479_kv(void)
{
    entry_write_kv("xnu_entry_getpid_calls", g_getpid_calls);
    entry_write_kv("xnu_entry_getpid_value", g_getpid_first_value);
    entry_write_kv("xnu_entry_getpid_error", g_getpid_first_error);
    entry_write_kv("xnu_entry_getpid_caller", g_getpid_first_caller);
    entry_write_kv("xnu_entry_getpid_last", g_getpid_last_value);
    entry_write_kv("xnu_entry_getpid_change_seq", g_getpid_first_change_seq);
    entry_write_kv("xnu_entry_getpid_change_value", g_getpid_first_change_value);
    entry_write_kv("xnu_entry_getpid_change_error", g_getpid_first_change_error);
}

__attribute__((noinline)) static void entry_write_480_kv(void)
{
    static const char *const arg[8] = {
        "xnu_entry_mmap_arg0", "xnu_entry_mmap_arg1",
        "xnu_entry_mmap_arg2", "xnu_entry_mmap_arg3",
        "xnu_entry_mmap_arg4", "xnu_entry_mmap_arg5",
        "xnu_entry_mmap_arg6", "xnu_entry_mmap_arg7"
    };
    unsigned i;

    entry_write_kv("xnu_entry_mmap_calls", g_mmap_calls);
    entry_write_kv("xnu_entry_mmap_caller", g_mmap_first_caller);
    entry_write_kv("xnu_entry_mmap_error", g_mmap_first_error);
    entry_write_kv("xnu_entry_mmap_value", g_mmap_first_value);
    entry_write_kv("xnu_entry_mmap_thread", g_mmap_thread);
    entry_write_kv("xnu_entry_mmap_map", g_mmap_map);
    entry_write_kv("xnu_entry_mmap_pmap", g_mmap_pmap);
    for (i = 0u; i < 8u; i++)
        entry_write_kv(arg[i], g_mmap_args[i]);
}

__attribute__((noreturn, noinline)) void entry_epilogue(const char *why)
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
    /*
     * Records `entry_kv` refused for want of room. Reported for the same reason and from a register
     * for the same reason as `kv_len_written` above: "the buffer was full" and "the code that would
     * have written the key never ran" produce the identical log otherwise, and experiment 268 spent
     * a measurement on that ambiguity.
     */
    register uint32_t kv_dropped_written __asm__("r7");

    kv_len_written = g_kv_len;
    kv_dropped_written = g_kv_dropped;
    g_why = why;

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
    {
        /*
         * The range is taken over the objects that hold results rather than trusting the order the
         * compiler chose for them in `.bss`. `g_kv_dropped` was added by experiment 269, and a
         * fixed lower bound of `&g_kv_len` would leave it out of the clean if the compiler placed it
         * below - and a line left dirty in a cache that is about to be switched off reads back as
         * the zeroes `.bss` was filled with, which is the exact failure this loop exists to prevent.
         * Here, "which addresses hold the answer" must not be a guess.
         */
        uintptr_t lo = (uintptr_t)&g_kv_len;
        uintptr_t hi = (uintptr_t)&g_kv_buf[ENTRY_KV_BUF];

        if ((uintptr_t)&g_kv_dropped < lo) {
            lo = (uintptr_t)&g_kv_dropped;
        }
        if ((uintptr_t)&g_kv_dropped + sizeof g_kv_dropped > hi) {
            hi = (uintptr_t)&g_kv_dropped + sizeof g_kv_dropped;
        }
        if ((uintptr_t)&g_why < lo) {
            lo = (uintptr_t)&g_why;
        }
        if ((uintptr_t)&g_why + sizeof g_why > hi) {
            hi = (uintptr_t)&g_why + sizeof g_why;
        }

        for (uintptr_t p = lo; p < hi; p += 32u) {
            __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(p) : "memory");
        }

        /*
         * 461: the trap's buffer, cleaned the same way and **separately**, not by widening the range
         * above. Widening it would clean everything the linker happened to place between the two
         * buffers - and that neighbourhood is the payload's 256 KB RAM disk (`g_stage90_ramdisk`),
         * which has nothing to do with the report and would be 8192 pointless cache operations on a
         * path that only runs once the machine has already stopped. The same min/max rule as above
         * for the same reason: which addresses hold the trap's record is the linker's choice, and a
         * dirty line in a cache that is about to be switched off reads back as zeroes.
         */
        uintptr_t plo = (uintptr_t)&g_panic_len;
        uintptr_t phi = (uintptr_t)&g_panic_buf[ENTRY_PANIC_BUF];

        if ((uintptr_t)&g_panic_dropped < plo) {
            plo = (uintptr_t)&g_panic_dropped;
        }
        if ((uintptr_t)&g_panic_dropped + sizeof g_panic_dropped > phi) {
            phi = (uintptr_t)&g_panic_dropped + sizeof g_panic_dropped;
        }
        if ((uintptr_t)&g_panic_entered < plo) {
            plo = (uintptr_t)&g_panic_entered;
        }
        if ((uintptr_t)&g_panic_entered + sizeof g_panic_entered > phi) {
            phi = (uintptr_t)&g_panic_entered + sizeof g_panic_entered;
        }

        for (uintptr_t p = plo; p < phi; p += 32u) {
            __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(p) : "memory");
        }
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

        /*
         * A pad, because XNU writes eight bytes here - see experiment 282.
         *
         * `cpu_machine_idle_init` (`osfmk/arm/cpu.c:533`) ends its `from_boot` branch with two
         * `bcopy_phys` calls that store `BootArgs_paddr` and `CpuDataEntries_paddr` into what XNU
         * believes is `ResetHandlerData` inside the low exception vectors:
         *
         *     gPhysBase + (&ResetHandlerData.boot_args - &ExceptionLowVectorsBase)       -> 0x80002408
         *     gPhysBase + (&ResetHandlerData.cpu_data_entries - &ExceptionLowVectorsBase) -> 0x80002404
         *
         * The arithmetic assumes the vectors blob is linked at the kernel's physical base - true in
         * Apple's own armv7 link, where the blob *is* the first thing in the image, and false here,
         * where `ExceptionLowVectorsBase` is at 0x800dc4bc and `gPhysBase` is 0x80000000. So the
         * destination is not XNU's structure; it is 0x2404 and 0x2408 bytes into *this* image, which
         * is `way_shift = 32u - n` and `way = 0` two lines below - the two instructions the sweep
         * needs to be correct. The values written are addresses (0x80101000 and 0x80147000 on this
         * build), and an address decoded as an ARM data-processing instruction with `cond=HI` sets
         * neither register, so `way` is never zeroed and the way loop counts from whatever `lr` held.
         *
         * Four bytes overwritten in the middle of the only path that produces a log line is not
         * something to leave in place on the strength of an argument that it is survivable, and the
         * fix belongs on this side: an address XNU computes from its own link has no reason to be
         * moved, but where this image puts its code is this image's business. So the pad is placed
         * *first* in this block - as early in `entry_epilogue` as the function's own prologue allows
         * - and it is deliberately wider than the eight bytes at risk so that a small change to the
         * code before it cannot walk the two addresses off either end. Placing it first is not
         * cosmetic: the requirement is only that the pad *contains* those addresses, so the earlier
         * it starts the more room there is for the code that must follow it to grow, and sizing it
         * correctly means moving code *after* it, never before.
         *
         * ------------------------------------------------------------------ what the pad must be
         *
         * Two earlier versions of this fix were wrong, and the pad's final shape is the shape that
         * survives both of their failure modes, so it is worth recording what they were.
         *
         * **The first version was no pad at all**, and it lost the two instructions the sweep needs:
         * with 0x80002404 and 0x80002408 holding `way_shift = 32u - n` and `way = 0`, XNU's two
         * writes replace them with addresses - 0x80147000 decodes as `andshi r7, r4, r0`, 0x80101000
         * as `andshi r1, r0, r0` - and a register-form `ands` with `cond=HI` sets its destination
         * *conditionally*. The sweep then counts ways from whatever `lr` held, which is bounded but
         * astronomically long, and the epilogue never reaches its ram-console write.
         *
         * **The second version was a pad of NOPs.** It said: "NOPs are the right content because a
         * NOP overwritten by an `ands` is still a NOP". That is false, and it is false in the one way
         * that mattered here: a NOP is only still a NOP if nothing *executes* it after the write,
         * which is true of the boot path - the pad is inside this epilogue and the boot never runs
         * the epilogue - and exactly false of the report path, because every report runs through the
         * pad with XNU's data already in it. The two corrupted words still decode to `andshi r1, r0,
         * r0` and `andshi r7, r4, r0`, and `r7` is **live across the pad**: measured in the linked
         * image, `entry_epilogue` loads it in its prologue from `[entry_vectors_stack + 4]` and does
         * not touch it again until well past the pad, where it is used as a kv value, as half of a
         * pointer (`add r0, r7, #4`) and as the base of a byte-table read. A conditional write to a
         * live register there is a garbage report or a fault inside the report, and a fault inside
         * the report is indistinguishable from a hang in the boot. The consequence was that no probe
         * placed *after* `cpu_machine_idle_init` ever reported: `clean_dcache`,
         * `CleanPoC_DcacheRegion`, `machine_startup` and 280's own frontier were all silent for the
         * instrument's reason rather than the boot's, and the walk spent a session bisecting
         * `bcopy_phys`'s body to explain a silence that was its own. It is this project's "a
         * measurement can be the thing that is wrong" defect for the eighth time, and this time the
         * measurement was the *reporting path itself*.
         *
         * **The third version tried to repair the NOPs at run time** - store `nop` back over both
         * addresses before the sweep reads them, `dsb`/`isb` after. It was built, verified in the
         * linked image (the two stores precede the pad; both addresses read back as `nop`), and run:
         * **still silent.** Self-modifying code is why, and this project has no way to measure the
         * I-side of it from here: the store puts the new bytes in the D-cache and `dsb sy` makes them
         * visible at the point of coherency, but whether the *fetch* of the pad's line sees them
         * depends on whether that line was prefetched before the store, and no log line can say. An
         * instrument whose correctness rests on an unmeasurable cache property is not an instrument.
         *
         * ------------------------------------------------------------------ the pad, finally
         *
         * So the pad carries no content that matters, because it is never executed: it opens with a
         * branch over itself and the rest is NOPs. XNU's two writes still land inside it - on two of
         * the NOPs the branch skips - and a word that is never executed cannot break anything,
         * whatever it decodes to. That holds regardless of the D-cache, the I-cache, the prefetcher
         * and the flags at the time of the write, which is precisely the property the first two
         * versions lacked. The branch is at the pad's *first* word, four or more bytes below both of
         * XNU's addresses, so it is never one of the words XNU corrupts, and `build_entry.sh` checks
         * that and the two addresses' enclosure in every build, against the two labels below.
         *
         * ------------------------------------------------------------------ the pad, one more time
         *
         * **Experiment 288 found the pad's own contract broken by a constant.** The two addresses XNU
         * writes are `gPhysBase + (&ResetHandlerData.cpu_data_entries - &ExceptionLowVectorsBase)` and
         * `gPhysBase + (&ResetHandlerData.boot_args - &ExceptionLowVectorsBase)`; 281 measured that
         * difference as 0x2404/0x2408 and `build_entry.sh` compared against those two *literals* ever
         * since. The difference is not a constant: it spans the generated stub object, whose size
         * grows with every step of this walk, and by 288 it had grown to 0x24AC/0x24B0 - **0x58 bytes
         * past the end of a 128-byte pad**, back inside `entry_epilogue`'s code. Every report was
         * silent again, for 282's reason exactly, and the build's check passed because it was
         * checking the wrong number. The pad is therefore **512 bytes** now, and the check derives the
         * two addresses from the linked image's own `ResetHandlerData` and `ExceptionLowVectorsBase`
         * rather than from anything written down here - the same one-value-two-definitions defect the
         * project already has a memory about, caught this time by the instrument it disabled.
         *
         * --------------------------------------------------------- the pad's job, given to entry.ld
         *
         * **Experiment 291 ended the treadmill, and this pad is no longer where XNU's writes go.**
         * The derived check was right and the pad was the wrong shape of fix: the difference between
         * the two symbols is a difference between two *stub positions*, so it moves by 0x18 for every
         * stub name entering or leaving the alphabetically-ordered stub object between "E" and "R",
         * and the five measured values walk up and down - 0x2404 (281), 0x24a8 (288), 0x24c0 (289),
         * 0x2448 (290), and 0x2358 (291), which is **0x7c below this pad's start** and made the build
         * refuse the step. Widening cannot fix that; re-aiming would have to happen again next step.
         *
         * `entry.ld` now defines both names - `ExceptionLowVectorsBase` as the image base and
         * `ResetHandlerData` four bytes below a sixteen-byte reserved slot in `.bss` - so the two
         * writes land in zeroed, never-executed memory that no stub's position can move. See the note
         * there; it also makes `cpu.c`'s page copy from `&ExceptionLowVectorsBase` a page copied onto
         * itself, where before it took 4096 bytes of stub bodies over page zero.
         *
         * **This block stays.** It is a skipped 512-byte region between the cache sweep and the
         * geometry: it is never executed whatever it decodes to, `build_entry.sh` still checks that
         * the branch really skips exactly those bytes, and it costs image bytes and nothing else. It
         * is a second line of defence now rather than the first, which is how a region that once
         * absorbed XNU's writes should end up.
         */
        __asm__ volatile ("\n"
                          ".global entry_skip_pad\n"
                          "entry_skip_pad:\n\t"
                          "b 1f\n\t"
                          ".rept 127\n\t"
                          "nop\n\t"
                          ".endr\n"
                          "1:\n"
                          ".global entry_skip_pad_end\n"
                          "entry_skip_pad_end:\n" ::: "memory");

        /*
         * The geometry comes after the pad, not before it, for the reason above: everything this
         * block does before the pad pushes the pad towards 0x80002404, and there is no room to be
         * pushed.
         */
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
    entry_write(g_why);
    entry_write("\n");

    /*
     * Printed unconditionally, and that is the point of them.
     *
     * `xnu_entry_kv_written` is what the probes recorded, read from a register, so it is right even
     * if the transfer to DRAM was not. `xnu_entry_kv_in_dram` is what the transfer actually
     * produced - they differ only when it failed, and an empty results buffer with a non-zero
     * `kv_written` is a failed transfer rather than a probe that never ran. `xnu_entry_kv_dropped`
     * is the third of that family - the number of records the buffer was too full to hold, so that
     * a truncated probe set is a count in the log instead of an absence. The three cache values
     * are the inputs the sweep runs on: what CSSELR was, what CCSIDR said through it, and what it
     * says once the level 1 data cache is selected.
     */
    /*
     * The caller again, from *here* - after the caches are off, the mmu is off, and this image is
     * executing with SCTLR.I and SCTLR.C clear, where every fetch and every store goes straight to
     * memory. Paired with the two records `entry_stub_hit` wrote during the run, this is the third
     * road to the same eight characters, and the three differ in exactly the thing that is in doubt:
     * `entry_panic_kv` ran then with XNU's caches and page tables live, and runs now without either.
     *
     * 461 moved these four from `entry_kv` to `entry_panic_kv`, and it is not a tidy-up: the trap's
     * record is the whole point of this part of the report, and in 459's run the buffer these were
     * appended to was at 8170 of 8192 bytes, so they were *refused* too. That is why 459's log has
     * neither `xnu_entry_stub_caller_e` nor the IRQ words: the report path was writing to a full
     * buffer, which is the same defect as the trap handler's own writes.
     */
    entry_panic_kv("xnu_entry_stub_caller_e", g_stub_caller);

    /*
     * If the vector that got here was the IRQ one, name the interrupt.
     *
     * This is the first thing in the report that reads hardware rather than this image, and it can
     * only be done from here: the addresses below are physical, and this function is where the MMU
     * stops translating. `GICC_IAR` returns the acknowledged interrupt id in its low 10 bits
     * (`0x3ff` is the spurious value); `GICD_ISPENDR0` says what else was still asserted, which is
     * how a second source is told from a re-assertion of the first.
     *
     * `stage90_disarm_deadman_timer` was added to the payload in the same step, so the most likely
     * reading here is that this never runs at all. That is the point of keeping it: whether the
     * disarm worked and whether some *other* interrupt ends the run are two different results, and
     * without this they produce the identical log line.
     */
    if (g_irq_report_pending != 0u) {
        entry_panic_kv("xnu_entry_irq_iar", *(volatile uint32_t *)(uintptr_t)0xf900200cu);
        entry_panic_kv("xnu_entry_irq_ispendr0", *(volatile uint32_t *)(uintptr_t)0xf9000200u);
        entry_panic_kv("xnu_entry_irq_isenabler0", *(volatile uint32_t *)(uintptr_t)0xf9000100u);
    }

    entry_write_kv("xnu_entry_kv_written", kv_len_written);
    entry_write_kv("xnu_entry_kv_in_dram", g_kv_len);
    entry_write_kv("xnu_entry_kv_dropped", kv_dropped_written);
    entry_write_kv("xnu_entry_why", (uint32_t)(uintptr_t)g_why);
    entry_write_kv("xnu_entry_why_byte", (uint32_t)(uint8_t)g_why[0]);
    /*
     * The caller, three ways, because experiment 271's run reported it as `0x800:;?=4` where the
     * prediction was `0x800abfd4`. `_v` is the value itself through the ram-console path, whose
     * digits are a `.rodata` table read; `_w0`/`_w1` are the eight bytes `entry_kv` actually stored
     * in `g_kv_buf`, read back as words. If `_v` is right and the words show 0x3a-style bytes, the
     * fault is in `entry_kv`'s arithmetic; if the words are right and `_v` is right, the fault is in
     * the transfer. See `g_stub_caller_digits`.
     */
    entry_write_kv("xnu_entry_stub_caller_v", g_stub_caller);
    entry_write_kv("xnu_entry_stub_caller_digits", g_stub_caller_digits);
    /*
     * 465: `_in_buf` first, because it decides how `_w0`/`_w1` are to be read - and 464's run is the
     * reason. There `g_kv_len` was `0x2000` when the stub was hit, so `_digits` was `0x2019`: 25
     * bytes past the end of `g_kv_buf`, where the caller record *would* have started had
     * `entry_kv_into` accepted it. `entry_image_ptr` passed (the address is inside the image), so
     * the two words printed bytes of whatever `nm -n` puts above the buffer, which is the kernel's
     * `ExceptionVectorsTable` - a plausible-looking pair of numbers that is not a reading of
     * anything. With the flag, a run says which of the two it is; and with the gate, a run whose
     * caller record was refused prints zero rather than a neighbour's bytes.
     */
    entry_write_kv("xnu_entry_stub_hit_count", g_stub_hit_count);
    entry_write_kv("xnu_entry_stub_caller_in_buf", g_stub_caller_in_buf);
    entry_write_kv("xnu_entry_stub_caller_w0",
                   (g_stub_caller_in_buf != 0u) && entry_image_ptr((uintptr_t)&g_kv_buf[g_stub_caller_digits])
                       ? entry_word_at((uintptr_t)&g_kv_buf[g_stub_caller_digits]) : 0u);
    entry_write_kv("xnu_entry_stub_caller_w1",
                   (g_stub_caller_in_buf != 0u) && entry_image_ptr((uintptr_t)&g_kv_buf[g_stub_caller_digits + 4u])
                       ? entry_word_at((uintptr_t)&g_kv_buf[g_stub_caller_digits + 4u]) : 0u);
    /* The abort readings moved into a function of their own by 467 - see `entry_write_269_kv` -
     * for the constant-pool reason recorded there. Same position, so the same key order. */
    entry_write_269_kv();
#ifdef STAGE90_ENTRY_TRACE
    /*
     * The tracer's terminal record, printed here rather than from `g_kv_buf` - see the slots'
     * comment. `_caller` is the return address of the `bl` that reached the wrapper, so the call site
     * is `caller - 4`, this project's usual convention for a stub's report.
     *
     * Experiment 450: the block is no longer terminal, so what is printed is the whole sequence of
     * blocks - the first pair (the frontier 447 named), the last pair (where the run stopped), how
     * many there were, how many came back, and the first and last callers that *did* come back. A
     * `_count` well above `_returned` is the boot thread blocking without a wakeup; `_returned`
     * rising with `_count` is the scheduler working.
     */
    entry_write_kv("xnu_entry_block_caller", g_block_caller);
    entry_write_kv("xnu_entry_block_continuation", g_block_continuation);
    entry_write_kv("xnu_entry_block_kv_len", g_block_kv_len);
    entry_write_kv("xnu_entry_block_count", g_block_count);
    entry_write_kv("xnu_entry_block_returned", g_block_returned);
    entry_write_kv("xnu_entry_block_first_return_caller", g_block_first_return_caller);
    entry_write_kv("xnu_entry_block_last_return_caller", g_block_last_return_caller);
    entry_write_kv("xnu_entry_block_last_caller", g_block_last_caller);
    entry_write_kv("xnu_entry_block_last_continuation", g_block_last_continuation);
    /*
     * Experiment 453's thread identities. `block_thread` is the thread that ran the last block and
     * `block_first_thread` the one that ran the first - the boot thread, since the first block is
     * `ml_get_max_cpus` on the only thread that exists at that point - so the pair says whether the
     * trace's last record is the same thread as its first.
     */
    entry_write_kv("xnu_entry_block_thread", g_block_thread);
    entry_write_kv("xnu_entry_block_first_thread", g_block_first_thread);
    entry_write_kv("xnu_entry_block_last_thread", g_block_last_thread);
    /*
     * The first eight blocks in order. `block0_caller` is the site 446/447 resolved
     * (`ml_get_max_cpus+0x3c`), and `block1_caller` is the reading this step adds: the first site the
     * boot blocks at *after* a real block has switched the CPU away and something has woken it.
     * `caller - 4` is the `bl`, as everywhere else.
     */
    {
        static const char *const bc[8] = {
            "xnu_entry_block0_caller", "xnu_entry_block1_caller",
            "xnu_entry_block2_caller", "xnu_entry_block3_caller",
            "xnu_entry_block4_caller", "xnu_entry_block5_caller",
            "xnu_entry_block6_caller", "xnu_entry_block7_caller"
        };
        static const char *const bq[8] = {
            "xnu_entry_block0_continuation", "xnu_entry_block1_continuation",
            "xnu_entry_block2_continuation", "xnu_entry_block3_continuation",
            "xnu_entry_block4_continuation", "xnu_entry_block5_continuation",
            "xnu_entry_block6_continuation", "xnu_entry_block7_continuation"
        };
        static const char *const bt[8] = {
            "xnu_entry_block0_thread", "xnu_entry_block1_thread",
            "xnu_entry_block2_thread", "xnu_entry_block3_thread",
            "xnu_entry_block4_thread", "xnu_entry_block5_thread",
            "xnu_entry_block6_thread", "xnu_entry_block7_thread"
        };

        for (unsigned i = 0; i < 8u; i++) {
            entry_write_kv(bc[i], g_block_ring_caller[i]);
            entry_write_kv(bq[i], g_block_ring_continuation[i]);
            entry_write_kv(bt[i], g_block_ring_thread[i]);
        }
    }
    /*
     * Experiment 453's sleep family. `_calls` counts every entry into `_sleep`'s seven global callers
     * and `_c0 .. _c6` the per-entry counts in the id order the slot comment gives, so a report can
     * say which of the seven the boot used without reading the live sequence. The `_ent/_site/_thr/
     * _chan/_wmsg/_pri/_tmo` group is the **last** call before the epilogue ran, which is the
     * frontier whenever a boot does reach the epilogue, and `_site` is the whole point of the step:
     * `_sleep`'s caller, which is the function that asked to wait.
     */
    entry_write_kv("xnu_entry_sleep_calls", g_sleep_calls);
    entry_write_kv("xnu_entry_sleep_c0", g_sleep_count[0]);
    entry_write_kv("xnu_entry_sleep_c1", g_sleep_count[1]);
    entry_write_kv("xnu_entry_sleep_c2", g_sleep_count[2]);
    entry_write_kv("xnu_entry_sleep_c3", g_sleep_count[3]);
    entry_write_kv("xnu_entry_sleep_c4", g_sleep_count[4]);
    entry_write_kv("xnu_entry_sleep_c5", g_sleep_count[5]);
    entry_write_kv("xnu_entry_sleep_c6", g_sleep_count[6]);
    entry_write_kv("xnu_entry_sleep_ent", g_sleep_last_ent);
    entry_write_kv("xnu_entry_sleep_site", g_sleep_last_site);
    entry_write_kv("xnu_entry_sleep_thr", g_sleep_last_thread);
    entry_write_kv("xnu_entry_sleep_chan", g_sleep_last_chan);
    entry_write_kv("xnu_entry_sleep_wmsg", g_sleep_last_wmsg);
    entry_write_kv("xnu_entry_sleep_pri", g_sleep_last_pri);
    entry_write_kv("xnu_entry_sleep_tmo", g_sleep_last_tmo);
    /*
     * Experiment 454's clock, and the IOKit deadline waits. `block_first_now`/`block_last_now` are the
     * counter at the first and last block, and `iolock_dl_lo`/`_now` are the two ends of the comparison
     * the last IOKit wait will make: a `_block_last_now` at or past `_iolock_dl_lo` (and a `_dl_hi`
     * matching `_now`'s, or zero) is a deadline that came and went without waking the waiter.
     */
    entry_write_kv("xnu_entry_block_first_now", g_block_first_now);
    entry_write_kv("xnu_entry_block_last_now", g_block_last_now);
    /* Experiment 454's IOKit waits and experiment 455's registry query: see `entry_write_455_kv`,
     * which holds both because adding them *here* put this function's constant pool past
     * PC-relative range. */
    entry_write_455_kv();
    /* 459: the OS's own console text, counted, for the same reason - and this is the count that
     * 458's log had to be taken by hand. */
    entry_write_459_kv();
    /* 461: the trap record's state, the IODT plane, and the three calls of the root-device chain.
     * Same reason again, and one more: this is the group of keys that says which link of the chain
     * 459 stopped in, so it is the group a reader will look for first. */
    entry_write_461_kv();
    /* 462: the walk from the registry root, read at the moment the plane was built and at the moment
     * the OS asks for `/chosen` - the pair that says what changed in between, and the group a reader
     * will look for first. */
    entry_write_462_kv();
    /* 463: the wait `IOSecureBSDRoot` hangs in, its predicate's two answers, and the instances the
     * class-name lookup yielded with each one's `__state[0]` - the group that says which of the two
     * mechanisms 462 left open the wait dies of, and whether it is neither. */
    entry_write_463_kv();
    /*
     * Experiment 451's live console, printed here as well so a run that *does* report says whether
     * the live channel was working and, if it was refused, which check refused it. `_records` counts
     * the records that went out live, so a report with `_records = 0` and `_refusals > 0` is a run
     * whose progress was not in the console while it was happening. 452 added `_attempts` (how many
     * times the install was tried, since a retry is legal), `_installed` (which of the three
     * descriptors took) and `_alias_read` (the console's signature word read through the second VA,
     * so `0x43474244` there is the mapping confirmed twice).
     */
    entry_write_kv("xnu_entry_live_state", g_live_state);
    entry_write_kv("xnu_entry_live_attempts", g_live_attempts);
    entry_write_kv("xnu_entry_live_records", g_live_records);
    entry_write_kv("xnu_entry_live_refusals", g_live_refusals);
    entry_write_kv("xnu_entry_live_refuse", g_live_refuse);
    entry_write_kv("xnu_entry_live_ttbr0", g_live_ttbr0);
    entry_write_kv("xnu_entry_live_ttbr1", g_live_ttbr1);
    entry_write_kv("xnu_entry_live_ttbcr", g_live_ttbcr);
    entry_write_kv("xnu_entry_live_dacr", g_live_dacr);
    entry_write_kv("xnu_entry_live_l1", g_live_l1);
    entry_write_kv("xnu_entry_live_installed", g_live_installed);
    entry_write_kv("xnu_entry_live_slot_before", g_live_slot_before);
    entry_write_kv("xnu_entry_live_desc", g_live_desc);
    entry_write_kv("xnu_entry_live_desc2", g_live_desc2);
    entry_write_kv("xnu_entry_live_alias_read", g_live_alias_read);
    entry_write_kv("xnu_entry_live_sctlr", g_live_sctlr);
    entry_write_kv("xnu_entry_live_prrr", g_live_prrr);
    entry_write_kv("xnu_entry_live_attr", g_live_attr);
    entry_write_kv("xnu_entry_vmwait_caller", g_vmwait_caller);
    entry_write_kv("xnu_entry_vmwait_count", g_vmwait_count);
    /*
     * Experiment 447, the same slots-versus-buffer argument one frame up: the site that called
     * `ml_get_max_cpus` and whether the flag's writer ran at all. `_maxcpus_count` is 0 only if the
     * function was never called; a non-zero count with the run ending in its `thread_block` means the
     * *first* call found the flag unset, since the writer is the only thing that sets it. `- 4` of
     * `_caller` is the `bl`, as everywhere else in this report.
     */
    entry_write_kv("xnu_entry_maxcpus_caller", g_maxcpus_caller);
    entry_write_kv("xnu_entry_maxcpus_count", g_maxcpus_count);
    entry_write_kv("xnu_entry_initmax_cpus_caller", g_initmax_cpus_caller);
    entry_write_kv("xnu_entry_initmax_cpus_count", g_initmax_cpus_count);
    entry_write_kv("xnu_entry_initmax_cpus_arg", g_initmax_cpus_arg);
    /*
     * Experiment 448. The chain `StartIOKit` -> `IOPlatformExpertDevice::initWithArgs` ->
     * `IOWorkLoop::init`, read from the device. `_dtalloc_arg` is the tree `PE_state.deviceTreeHead`
     * held - the first run since 444 that reads the moved tree's address back from the machine -
     * and `_dtalloc_ret` is whether it parsed. `_workloop_ret` is `initWithArgs`'s return value by
     * the disassembly's own arithmetic, and the four `_bad`/`_caller` pairs name which guard inside
     * `IOWorkLoop::init` refused, if one did.
     */
    entry_write_kv("xnu_entry_dtalloc_caller", g_dtalloc_caller);
    entry_write_kv("xnu_entry_dtalloc_arg", g_dtalloc_arg);
    entry_write_kv("xnu_entry_dtalloc_ret", g_dtalloc_ret);
    entry_write_kv("xnu_entry_dtalloc_count", g_dtalloc_count);
    entry_write_kv("xnu_entry_workloop_caller", g_workloop_caller);
    entry_write_kv("xnu_entry_workloop_ret", g_workloop_ret);
    entry_write_kv("xnu_entry_workloop_count", g_workloop_count);
    entry_write_kv("xnu_entry_rlock_bad", g_rlock_bad);
    entry_write_kv("xnu_entry_rlock_caller", g_rlock_caller);
    entry_write_kv("xnu_entry_rlock_count", g_rlock_count);
    entry_write_kv("xnu_entry_slock_bad", g_slock_bad);
    entry_write_kv("xnu_entry_slock_caller", g_slock_caller);
    entry_write_kv("xnu_entry_slock_count", g_slock_count);
    entry_write_kv("xnu_entry_cgate_bad", g_cgate_bad);
    entry_write_kv("xnu_entry_cgate_caller", g_cgate_caller);
    entry_write_kv("xnu_entry_cgate_count", g_cgate_count);
    entry_write_kv("xnu_entry_kthread_bad", g_kthread_bad);
    entry_write_kv("xnu_entry_kthread_caller", g_kthread_caller);
    entry_write_kv("xnu_entry_kthread_count", g_kthread_count);
    entry_write_kv("xnu_entry_kthread_cont0", g_kthread_cont[0]);
    entry_write_kv("xnu_entry_kthread_site0", g_kthread_site[0]);
    entry_write_kv("xnu_entry_kthread_cont1", g_kthread_cont[1]);
    entry_write_kv("xnu_entry_kthread_site1", g_kthread_site[1]);
    entry_write_kv("xnu_entry_kthread_cont2", g_kthread_cont[2]);
    entry_write_kv("xnu_entry_kthread_site2", g_kthread_site[2]);
    entry_write_kv("xnu_entry_kthread_cont3", g_kthread_cont[3]);
    entry_write_kv("xnu_entry_kthread_site3", g_kthread_site[3]);
    /*
     * Experiment 449: the catalogue. `unserN_ret` is the Nth `OSUnserialize` return in the boot, so
     * the catalogue's own (`IOCatalogue::initialize`) is named by its caller rather than assumed to be
     * first; `alloc_count` is whether matching ever instantiated a class by name.
     */
    entry_write_kv("xnu_entry_postctor_caller", g_postctor_caller);
    entry_write_kv("xnu_entry_postctor_count", g_postctor_count);
    entry_write_kv("xnu_entry_catinit_caller", g_catinit_caller);
    entry_write_kv("xnu_entry_catinit_count", g_catinit_count);
    entry_write_kv("xnu_entry_unser_count", g_unser_count);
    entry_write_kv("xnu_entry_unser0_caller", g_unser_caller[0]);
    entry_write_kv("xnu_entry_unser0_ret", g_unser_ret[0]);
    entry_write_kv("xnu_entry_unser1_caller", g_unser_caller[1]);
    entry_write_kv("xnu_entry_unser1_ret", g_unser_ret[1]);
    entry_write_kv("xnu_entry_unser2_caller", g_unser_caller[2]);
    entry_write_kv("xnu_entry_unser2_ret", g_unser_ret[2]);
    entry_write_kv("xnu_entry_alloc_name", g_alloc_name);
    entry_write_kv("xnu_entry_alloc_count", g_alloc_count);
    entry_write_kv("xnu_entry_pub2_count", g_pub2_count);
    entry_write_kv("xnu_entry_pub20_caller", g_pub2_caller[0]);
    entry_write_kv("xnu_entry_pub20_key", g_pub2_key[0]);
    entry_write_kv("xnu_entry_pub21_caller", g_pub2_caller[1]);
    entry_write_kv("xnu_entry_pub21_key", g_pub2_key[1]);
    entry_write_kv("xnu_entry_pub22_caller", g_pub2_caller[2]);
    entry_write_kv("xnu_entry_pub22_key", g_pub2_key[2]);
    entry_write_kv("xnu_entry_pub23_caller", g_pub2_caller[3]);
    entry_write_kv("xnu_entry_pub23_key", g_pub2_key[3]);
    /* 467: the kernel's own second-level abort handler - entered how many times, returned how many
     * times, and what the latest fault was. In a function of its own for `entry_write_463_kv`'s
     * reason: six keys written here put `entry_epilogue`'s constant pool past PC-relative range and
     * the *C compile* fails (`bad immediate value for offset (4188)`), with nothing in the message
     * naming the function that grew. See `entry_write_467_kv`. */
    entry_write_467_kv();
    /* 474: the same handler's *frame* - the faulting instruction, the mode it was running in, and
     * the two numbers that say whether the offsets this image read it at are the kernel's. */
    entry_write_474_kv();
    /* 478: what `thread_block` returned - the value the panic 476 and 477 both stopped on is the
     * argument of. Last in this list because it is the newest, and live as well as here because the
     * run it is for may never reach this line. */
    entry_write_478_kv();
    /* 479: what the kernel answered `getpid` with. Last for the same reason, and here for a reading
     * the live channel cannot give: this epilogue runs at the end of `arm_init`, *before*
     * `load_init_program` creates the process whose syscalls 479 measures - so a zero here is the
     * statement that the kernel's own bring-up never asked, and every call the live channel records
     * after this line belongs to the fixture. */
    entry_write_479_kv();
    /* 480: the words process 1's second syscall was handed, and the address it got back. Written here
     * for the same reason 479's is and with the same expectation - this epilogue runs at the end of
     * `arm_init`, before the process exists, so a `_calls` of 0 is the statement that the boot's own
     * bring-up never called `mmap` - but its real value is on a path the report cannot reach: if the
     * fixture's access to the fresh page faults in a way the kernel cannot service, the trap report
     * and these words are read *together*, and the address the word in `arg0`..`arg4` describes says
     * whether the syscall was reached at all. */
    entry_write_480_kv();
    /* 481: the timer's owner. Unlike the three above this one is *inside* `arm_init`'s own window -
     * the registering `PE_init_platform` call is five instructions before the `arm_init` return that
     * reaches this epilogue - so these keys are expected to be non-zero here, and `_registered = 0`
     * would be the statement that the wrapper never saw the call it was written for. The rest of
     * 481's readings (`setPop`, the countdown sample) belong to later threads and are in
     * `entry_timebase.c`'s own keys and the live channel. */
    entry_timebase_write_kv();
#endif
    /*
     * Experiment 272. Runs here, after the first line of the report is already in the console, so
     * that a fault in this dump cannot cost the report that says which stub was hit - which is
     * exactly what the `&&label` version of this probe did, silently and three times.
     */
    entry_probe_dump_kv_words("xnu_entry_kv_words", (uint32_t)(uintptr_t)entry_kv, 661u);

    entry_write_kv("xnu_entry_csselr_before", csselr_before);
    entry_write_kv("xnu_entry_ccsidr_before", ccsidr_before);
    entry_write_kv("xnu_entry_ccsidr_l1", ccsidr_l1);

    if (g_kv_len != 0u) {
        entry_write("MI4IOS6_STAGE90_XNU real XNU entry");
        entry_write(g_kv_buf);
    }

    /*
     * 461: the trap's own record, from its own buffer, under a heading of its own.
     *
     * Printed unconditionally when the handler ran, and separately from the trace above, because the
     * two are written by different writers for different reasons: `g_kv_buf` holds what the probes
     * recorded while the machine was running, `g_panic_buf` holds what `fleh_undef` recorded once it
     * had stopped. One heading per buffer is also what makes the *absence* of one of them readable -
     * a report with no `trap record:` line and a non-zero `xnu_entry_panic_entered` is a trap whose
     * buffer did not reach DRAM, and a report with neither is a run that never trapped at all.
     */
    if (g_panic_entered != 0u) {
        entry_write("\nMI4IOS6_STAGE90_XNU trap record:");
        entry_write(g_panic_buf);
        entry_write("\n");
    }

    *(volatile uint32_t *)(uintptr_t)RESTART_REASON = RESTART_NORMAL;
    __asm__ volatile ("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)MSM8974_PSHOLD = 0u;
    __asm__ volatile ("dsb sy" ::: "memory");

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

#ifdef STAGE90_ENTRY_TRACE
/*
 * The ways `entry_trace.c` reaches this file, and all of them go around `entry_kv`.
 *
 * `entry_epilogue_block` is the terminal one: it stores the wrapper's two numbers in the `.bss` slots
 * and runs the same epilogue every other report runs, so the report's own header line carries what
 * the buffer could not. `why` is a literal in this image, as every other caller's is. **Since
 * experiment 450 nothing calls it**: it was `thread_block`'s report, and a block that reports is a
 * block that does not switch, which is what 449 found had been truncating every run since 268. It
 * stays because a *terminal* wrapper is still the right shape when the thing being wrapped is the
 * end of the run by definition - 451 or later will want it - and because deleting it would hide
 * which step retired it.
 *
 * `entry_note_vmwait` is not terminal - `vm_page_wait`'s wrapper calls the real one afterwards - and
 * it keeps the **last** caller and a count, because the question it answers is "was the allocator
 * waiting when the run ended", not "was it ever waiting". 450's `entry_note_block` and
 * `entry_note_block_return` are the same shape, split across the call: the first says where the block
 * was entered from, the second whether it ever came back.
 */
void entry_epilogue_block(const char *why, uint32_t caller, uint32_t continuation)
{
    g_block_caller = caller;
    g_block_continuation = continuation;
    g_block_kv_len = g_kv_len;
    entry_epilogue(why);
}

/*
 * Experiment 450. `thread_block` calls this *before* the real block and `entry_note_block_return`
 * after it returns, so the two of them together say whether the block switched and whether the boot
 * thread was ever woken. The slots are in `.bss` inside the window and the epilogue prints them
 * outside the dump, which is 446's mechanism and the reason they survive a run whose record buffer is
 * full (445's `entry_kv` pair did not).
 *
 * The ring keeps the first eight pairs rather than the first or the last one: the first is the
 * frontier 446/447 named and the second is the site the boot blocks at *after* a real switch, and
 * neither is the other's summary.
 *
 * Experiment 453 added the third element of each record, the thread. It is a parameter rather than a
 * read here because the read belongs where the `lr` is read - in the wrapper, which is the only place
 * that knows the value was taken between two context switches - and because this file must stay
 * callable from a build without `entry_trace.c`.
 *
 * Experiment 478 makes it return the index it used. The wrapper hands that back to
 * `entry_note_block_return`, which is what lets `block<N>_result` and `block<N>_caller` be the same
 * block: blocks are not nested one per stack, so a return's own ordinal and its entry's are two
 * numbers as soon as a block does not come back - see that function.
 */
uint32_t entry_note_block(uint32_t caller, uint32_t continuation, uint32_t thread, uint32_t now)
{
    uint32_t seq = g_block_count;

    if (g_block_count == 0) {
        g_block_caller = caller;
        g_block_continuation = continuation;
        g_block_first_thread = thread;
        g_block_first_now = now;
    }
    if (g_block_count < 8) {
        g_block_ring_caller[g_block_count] = caller;
        g_block_ring_continuation[g_block_count] = continuation;
        g_block_ring_thread[g_block_count] = thread;
    }
    g_block_last_caller = caller;
    g_block_last_continuation = continuation;
    g_block_last_thread = thread;
    g_block_thread = thread;
    g_block_last_now = now;
    g_block_kv_len = g_kv_len;
    entry_live_write("xnu_live_block_enter", caller);
    entry_live_write("xnu_live_block_thr", thread);
    entry_live_write("xnu_live_block_now", now);
    g_block_count++;
    entry_live_write("xnu_live_block_seq", g_block_count);

    return seq;
}

void entry_note_block_return(uint32_t caller, uint32_t result, uint32_t seq)
{
    unsigned slot = entry_block_result_slot(result);

    if (g_block_returned == 0)
        g_block_first_return_caller = caller;
    /*
     * `seq` is the index `entry_note_block` wrote this block into, **not** this return's ordinal.
     * They are different numbers as soon as one block does not come back - and the block that never
     * comes back is the only block this instrument has ever been asked about (447, 476, 477). Writing
     * the return into `g_block_returned`'s slot would put an N-th *return* beside an N-th *entry*'s
     * caller in the same `block<N>_*` group and label two different blocks with one index, which is
     * the misreading 476's `sort -u` incident was made of.
     */
    if (seq < 8u)
        g_block_ring_result[seq] = result;
    g_block_last_return_caller = caller;
    g_block_last_result = result;
    g_block_last_result_caller = caller;
    g_block_results[slot]++;
    g_block_returned++;
    entry_live_write("xnu_live_block_return", caller);
    entry_live_write("xnu_live_block_result", result);
    entry_live_write("xnu_live_block_returns", g_block_returned);
    /*
     * 478: the one record that has to exist before the panic does. The first return the receive path
     * cannot accept is written to the live channel *and* to `.bss`, with its caller and its place in
     * the sequence, so the run names the number even if it never reaches the epilogue - which is the
     * case 476 and 477 were both in, one statement away from the panic this value is the argument of.
     *
     * The test is `result > 3u` and not a slot number: 0..3 are `THREAD_AWAKENED` through
     * `THREAD_RESTART`, which is the set `ipc_mqueue_receive_results`'s switch accepts, and both
     * candidates the 477 doc named - 10 and -1 - are above 3. Written this way the condition is the
     * property rather than a restatement of the slot table, so adding a member to the enum cannot
     * silently widen it.
     */
    if (result > 3u && g_block_first_odd_seq == 0u) {
        g_block_first_odd_seq = g_block_returned;
        g_block_first_odd_result = result;
        g_block_first_odd_caller = caller;
        entry_live_write("xnu_live_block_odd_seq", g_block_first_odd_seq);
        entry_live_write("xnu_live_block_odd_result", result);
        entry_live_write("xnu_live_block_odd_caller", caller);
        /* The histogram, once, on the one event that makes it worth the channel's budget - written
         * here rather than on every return for the reason `entry_live_write`'s own comment gives:
         * the channel is a 4096-record budget and the records this step must not lose are the ones
         * immediately around the panic. */
        for (unsigned i = 0; i < ENTRY_BLOCK_RESULT_SLOTS; i++)
            entry_write_kv(entry_block_result_key(i), g_block_results[i]);
    }
}

/*
 * Experiment 479. One call per `getpid` from the wrapper in `entry_trace.c`, which is where the
 * kernel's answer is read: `value` is `*retval` - `p_pid` - and `error` is the syscall's own return,
 * both *after* the real `getpid` and both passed through unchanged by the wrapper.
 *
 * **Why the live channel and not only `.bss`.** The epilogue runs at the end of `arm_init`, which is
 * before `load_init_program` has created the process whose syscalls this instrument is for, so the
 * report keys written there can only ever be the *boot's* own calls. Everything this step measures
 * happens afterwards, when the fixture is running in user mode, and the only channel that survives
 * that - a loop with nothing in it to reach an epilogue - is the live one.
 *
 * **Why the count is written on powers of two.** The first call is written in full, because it is the
 * reading that ties the instrument to the fixture's first instruction, and after that only
 * `g_getpid_calls & (g_getpid_calls - 1) == 0` is written. The loop in `entry_ramdisk.s` runs as fast
 * as the CPU allows, and `entry_live_write`'s 4096-record cap is a *bound* the boot's own report
 * depends on - 461's defect was the report path's buffer being the tracer's and full when the report
 * was written. A doubling count loses nothing a reading needs: the last record in the log says the
 * loop ran at least that many times, and each earlier one says half as much.
 *
 * **What a *change* means, and what it deliberately does not.** `g_getpid_first_change_*` is the first
 * answer that was not the first answer, or the first call whose return value moved. It is not "the
 * first answer that is not the expected pid": the expected pid is the fixture's own statement about
 * the kernel - `EXPECTED_PID` in `entry_ramdisk.s`, where the `cmp` that acts on it lives - and
 * writing the number again here would be a second definition with nothing comparing it. What this
 * instrument can say without repeating it is that the answer stopped being constant, which is the
 * reading that a run reaching the fixture's `udf #1` cannot give on its own.
 */
void entry_note_getpid(uint32_t caller, uint32_t error, uint32_t value)
{
    g_getpid_calls++;

    if (g_getpid_calls == 1u) {
        g_getpid_first_value = value;
        g_getpid_first_error = error;
        g_getpid_first_caller = caller;
        entry_live_write("xnu_live_getpid_seq", 1u);
        entry_live_write("xnu_live_getpid_value", value);
        entry_live_write("xnu_live_getpid_error", error);
        entry_live_write("xnu_live_getpid_caller", caller);
    } else if ((g_getpid_calls & (g_getpid_calls - 1u)) == 0u) {
        entry_live_write("xnu_live_getpid_count", g_getpid_calls);
        entry_live_write("xnu_live_getpid_last", value);
    }

    if (g_getpid_first_change_seq == 0u &&
        (value != g_getpid_first_value || error != g_getpid_first_error)) {
        g_getpid_first_change_seq = g_getpid_calls;
        g_getpid_first_change_value = value;
        g_getpid_first_change_error = error;
        entry_live_write("xnu_live_getpid_change_seq", g_getpid_calls);
        entry_live_write("xnu_live_getpid_change_value", value);
        entry_live_write("xnu_live_getpid_change_error", error);
    }

    g_getpid_last_value = value;
}

/*
 * Experiment 480. One call per `mmap` from the wrapper in `entry_trace.c`, with `args` pointing at the
 * eight words the wrapper copied out of `uap` *before* the real call, `error` the syscall's own return
 * and `value` the address it wrote into the caller's `retval`. The wrapper passes all four through
 * unchanged; nothing here decides anything about the call.
 *
 * **Why every word is written, once.** The live channel is the only one a running user-mode process
 * has (see `entry_note_getpid`), and this is the one call whose *contents* are the reading, so all
 * eight words go out on the first call - the argument layout claim in `entry_stubs.c`'s globals above
 * has no other way to be measured. Unlike `getpid` this syscall is **not** in the fixture's loop: it
 * is called once, outside it, so the second record never comes and the count's job is to say that.
 * The powers-of-two form is kept for the case it does come - a fixture that leaked a page per
 * iteration would fill the ring, and `entry_live_write`'s 4096-record cap is a bound the boot's own
 * report depends on - so an unexpected flood costs eleven records and not four thousand.
 *
 * **A second call is a finding, not a detail.** `g_mmap_args` is kept as first-call-only for the same
 * reason `g_getpid_first_value` is: after a second call the buffer holds the second call's words, and
 * a report that could not tell which call it was describing would be a measurement that destroyed its
 * own subject.
 *
 * **And it takes the three readings on the fault path itself** - the thread, `thread->map` and
 * `map->pmap` - because this call is the last thing that happens on this thread before user mode
 * touches the new page. See the globals above; the reason they are read *here* rather than in the
 * abort wrapper is that they have to be taken while the answer is still the one the fault will use.
 */
void entry_note_mmap(uint32_t caller, const uint32_t *args, uint32_t error, uint32_t value)
{
    unsigned i;

    g_mmap_calls++;

    if (g_mmap_calls == 1u) {
        static const char *const key[8] = {
            "xnu_live_mmap_arg0", "xnu_live_mmap_arg1",
            "xnu_live_mmap_arg2", "xnu_live_mmap_arg3",
            "xnu_live_mmap_arg4", "xnu_live_mmap_arg5",
            "xnu_live_mmap_arg6", "xnu_live_mmap_arg7"
        };

        g_mmap_first_caller = caller;
        g_mmap_first_error = error;
        g_mmap_first_value = value;
        if (args != 0) {
            for (i = 0u; i < 8u; i++)
                g_mmap_args[i] = args[i];
        }

        /* The map the first user-mode fault of this walk will be serviced in - see the globals above
         * for why these three exist and why `_pmap` is conditional. */
        g_mmap_thread = entry_stubs_thread_pointer();
        if (g_mmap_thread != 0u) {
            g_mmap_map = *(volatile uint32_t *)(g_mmap_thread + STAGE90_ACT_MAP);
            if (g_mmap_map >= 0x80000000u && g_mmap_map <= 0xFFFEFFFFu) {
                g_mmap_pmap = *(volatile uint32_t *)(g_mmap_map + STAGE90_MAP_PMAP);
            }
        }

        entry_live_write("xnu_live_mmap_seq", 1u);
        entry_live_write("xnu_live_mmap_caller", caller);
        entry_live_write("xnu_live_mmap_error", error);
        entry_live_write("xnu_live_mmap_value", value);
        entry_live_write("xnu_live_mmap_thread", g_mmap_thread);
        entry_live_write("xnu_live_mmap_map", g_mmap_map);
        entry_live_write("xnu_live_mmap_pmap", g_mmap_pmap);
        for (i = 0u; i < 8u; i++)
            entry_live_write(key[i], g_mmap_args[i]);
    } else if ((g_mmap_calls & (g_mmap_calls - 1u)) == 0u) {
        entry_live_write("xnu_live_mmap_count", g_mmap_calls);
        entry_live_write("xnu_live_mmap_last", value);
    }
}

/* Experiment 456's probe, defined below its first caller; the declaration is here because
 * `entry_note_iolock` is where the reading is taken (see `entry_registry_probe`). */
__attribute__((noinline)) static void entry_registry_probe(uint32_t seq, uint32_t site);

/*
 * Experiment 454. One call per entry into the two IOKit deadline sleeps, from their wrappers. `site`
 * is the wrapper's own `lr` - the function that asked IOKit to wait - and `now` is the counter read in
 * the same wrapper, so the report carries both ends of the comparison `assert_wait_deadline` will make
 * on the device: `dl_lo`/`dl_hi` against `now`. Every record goes live, so a boot that hangs in the
 * wait still says where it entered and what it was waiting until.
 */
void entry_note_iolock(uint32_t ent, uint32_t site, uint32_t thread, uint32_t lock, uint32_t event,
                       uint32_t inter, uint32_t dl_lo, uint32_t dl_hi, uint32_t now)
{
    if (g_iolock_calls == 0) {
        g_iolock_first_ent = ent;
        g_iolock_first_site = site;
        g_iolock_first_thread = thread;
    }
    g_iolock_calls++;
    if (ent < ENTRY_IOLOCK_MAX)
        g_iolock_count[ent]++;
    g_iolock_last_ent = ent;
    g_iolock_last_site = site;
    g_iolock_last_thread = thread;
    g_iolock_last_lock = lock;
    g_iolock_last_event = event;
    g_iolock_last_inter = inter;
    g_iolock_last_dl_lo = dl_lo;
    g_iolock_last_dl_hi = dl_hi;
    g_iolock_last_now = now;
    entry_live_write("xnu_live_iolock_ent", ent);
    entry_live_write("xnu_live_iolock_site", site);
    entry_live_write("xnu_live_iolock_thr", thread);
    entry_live_write("xnu_live_iolock_lock", lock);
    entry_live_write("xnu_live_iolock_event", event);
    entry_live_write("xnu_live_iolock_inter", inter);
    entry_live_write("xnu_live_iolock_dl_lo", dl_lo);
    entry_live_write("xnu_live_iolock_dl_hi", dl_hi);
    entry_live_write("xnu_live_iolock_now", now);
    /* 456: the registry reading, taken in the wait itself - see `entry_registry_probe`. After the
     * records above, so the wait's own identity stays the first thing the log says about this call. */
    entry_registry_probe(g_iolock_calls, site);
}

/*
 * Experiment 455. The resource root and its two state words, taken from the image's own accessor.
 * `+36` and `+40` are `__state[0]`/`__state[1]`: `IOService::getState()` reads the first with
 * `ldr r0, [r0, #36]` and `registerService` loads the pair with `ldr r2, [r4, #36]` /
 * `ldr r5, [r4, #40]`, which is what the build checks - this comment is not the check. Called before
 * every match record so the report carries the state *at the call*: bit 4
 * (`kIOServiceMatchedState`) may be set later than the query that needed it, and that difference is
 * exactly the kind of thing a single end-of-run reading would hide.
 */
uint32_t entry_rs_state(uint32_t *state1_out)
{
    void *svc = _ZN9IOService18getResourceServiceEv();
    uint32_t *w = (uint32_t *)svc;

    if (w) {
        g_rs_ptr = (uint32_t)(uintptr_t)svc;
        g_rs_state0 = w[9];              /* __state[0], +36 - IOService::getState()'s own load */
        g_rs_state1 = w[10];             /* __state[1], +40 - registerService's second load */
    }
    if (state1_out)
        *state1_out = g_rs_state1;
    return g_rs_state0;
}

/*
 * One call per `copyExistingServices`, recorded **after** the call so the record carries the answer:
 * `res` is the pointer the query returned, and `0` is what makes `waitForMatchingService` sleep.
 * `dict` is the matching dictionary - the same pointer the dictionary factories below returned - so
 * the report can say which query found nothing, and `rs0`/`rs1` are the resource root's state words
 * as they were when the query ran.
 */
void entry_note_match(uint32_t site, uint32_t dict, uint32_t in_state, uint32_t options,
                      uint32_t result)
{
    uint32_t state1;

    entry_rs_state(&state1);
    if (g_match_calls < ENTRY_MATCH_MAX) {
        g_match_site[g_match_calls] = site;
        g_match_dict[g_match_calls] = dict;
        g_match_in[g_match_calls] = in_state;
        g_match_opts[g_match_calls] = options;
        g_match_res[g_match_calls] = result;
    }
    g_match_calls++;
    if (result)
        g_match_hits++;
    g_match_last_site = site;
    g_match_last_dict = dict;
    g_match_last_in = in_state;
    g_match_last_opts = options;
    g_match_last_res = result;
    entry_live_write("xnu_live_match_site", site);
    entry_live_write("xnu_live_match_dict", dict);
    entry_live_write("xnu_live_match_in", in_state);
    entry_live_write("xnu_live_match_opts", options);
    entry_live_write("xnu_live_match_res", result);
    entry_live_write("xnu_live_match_rs0", g_rs_state0);
    entry_live_write("xnu_live_match_rs1", g_rs_state1);
    entry_live_write("xnu_live_match_mpass", g_mpass_calls);
    entry_live_write("xnu_live_match_seq", g_match_calls);
}

/*
 * One call per `IOService::matchPassive`, with the `this` it was called on - which is the filter.
 * The plane search calls this function on *candidates*, the fast path in `copyExistingServices` calls
 * it on the resource root, and only the second is the question `IOFindBSDRoot` is asking. So the
 * sequence that matters is recorded live and the rest is only counted, which keeps a search over
 * many candidates from filling the console with records that answer nothing.
 */
void entry_note_mpass(uint32_t site, uint32_t dict, uint32_t options, uint32_t result, uint32_t self)
{
    entry_rs_state(0);

    g_mpass_calls++;
    g_mpass_last_site = site;
    g_mpass_last_dict = dict;
    g_mpass_last_res = result;
    g_mpass_last_this = self;
    if (self != 0u && self == g_rs_ptr) {
        g_mpass_rs_calls++;
        g_mpass_rs_site = site;
        g_mpass_rs_dict = dict;
        g_mpass_rs_opts = options;
        g_mpass_rs_res = result;
        g_mpass_rs_this = self;
        entry_live_write("xnu_live_mpass_site", site);
        entry_live_write("xnu_live_mpass_dict", dict);
        entry_live_write("xnu_live_mpass_opts", options);
        entry_live_write("xnu_live_mpass_res", result);
        entry_live_write("xnu_live_mpass_this", self);
        entry_live_write("xnu_live_mpass_all", g_mpass_calls);
        entry_live_write("xnu_live_mpass_seq", g_mpass_rs_calls);
    }
}

/*
 * Experiment 456. The registry's own answer, read *at the frontier*, from the one wrapper on this
 * boot's path whose call site the linker can see: `IORecursiveLockSleepDeadline`, entered by
 * `waitForMatchingService` from `IOService.cpp` - another object than `IOLib.cpp`, so this one is
 * reached (455's finding: a `--wrap` rewrites an *undefined* reference, and every call to
 * `copyExistingServices` is issued from the object that defines it). The reading is therefore taken
 * inside the wait that never returns, rather than by asking the query the boot does not get to ask.
 *
 * Three values, and they are the three the source's own branch tests:
 *
 *   state0/state1  `gIOResources`' `__state[0]`/`__state[1]`. The fast path in `copyExistingServices`
 *                  tests `(inState == (service->__state[0] & inState))` with
 *                  `inState = kIOServiceMatchedState = 0x4` and `0 == (__state[0] &
 *                  kIOServiceInactiveState)`, *before* the matcher is reached - and the only writer of
 *                  bit 0x4 is `copyNotifiers`' `orNewState` argument, called from `doServiceMatch`'s
 *                  tail behind `0 == (__state[1] & kIOServiceModuleStallState)` (`IOService.cpp:3748`).
 *                  So a clear 0x4 with a clear stall bit says the tail was skipped for another reason,
 *                  and a clear 0x4 with the stall bit set names the reason.
 *   rm_ptr         `copyProperty(gIOResources, gIOResourceMatchedKey)` - exactly the call
 *                  `IOResources::matchPropertyTable` makes (`IOService.cpp:5109`) - or NULL.
 *   rm_count/idx   `getCount()` on it and `getNextIndexOfObject(gIOBSDKey, 0)`: `0` means the array
 *                  exists *and* holds the `"IOBSD"` symbol, any other small value means it exists
 *                  without it, and NULL above means the resource root has no `IOResourceMatched`
 *                  property at all. That property is written from one place only, the same tail
 *                  (`if (resourceKeys) setProperty(...)`, `:3751`), where `resourceKeys` is a local
 *                  set in the `this == gIOResources` branch of
 *                  `if (keepGuessing && matches->getCount() && kIOReturnSuccess == getResources())`.
 *
 * Three things make this call safe here rather than merely convenient, and none of them is a guess.
 * `copyProperty` takes `IORecursiveLockLock(reserved->fLock)` (`IORegistryEntry.cpp:119`), the lock
 * `IORegistryEntry` already uses with `IORecursiveLockHaveLock`, and the matcher calls the very same
 * function from the very same context this probe runs in (`copyExistingServices` under
 * `gNotificationLock`), so this adds no lock order that the boot does not already take;
 * `IORegistryEntry::copyProperty(const OSSymbol *)` is called directly and not through a vtable
 * because nothing between `IOResources` and `IORegistryEntry` overrides it - the image defines one
 * `copyProperty(OSSymbol*)` under those two names and no `IOResources` one; and the object it returns
 * is retained, so it is released here with `OSObject::release`, which is the *only* `release` the
 * image defines and therefore cannot be the wrong override.
 *
 * The three mangled names are checked against the undefined-symbol list by the build, because a typo
 * in one of them is not a link error in this image: the generator invents a stub, and the probe would
 * stop the boot at its own instrument - the defect 455 found twice.
 *
 * Live only, and that is deliberate: 455's run is the demonstration that a boot which hangs at the
 * frontier never reaches `entry_epilogue`, so a key written into the final buffer is a key no run
 * this instrument exists for will ever print.
 */
extern void *_ZNK15IORegistryEntry12copyPropertyEPK8OSSymbol(void *, const void *);
extern uint32_t _ZNK7OSArray8getCountEv(void *);
extern uint32_t _ZNK7OSArray20getNextIndexOfObjectEPK15OSMetaClassBasej(void *, const void *, uint32_t);
extern void _ZNK8OSObject7releaseEv(void *);
extern void *gIOResourceMatchedKey;
extern void *gIOBSDKey;

__attribute__((noinline)) static void entry_registry_probe(uint32_t seq, uint32_t site)
{
    void *svc = _ZN9IOService18getResourceServiceEv();
    void *keys = 0;
    uint32_t state0 = 0, state1 = 0, count = 0, idx = 0xffffffffu;

    if (svc) {
        uint32_t *w = (uint32_t *)svc;

        state0 = w[9];                   /* __state[0], +36 - getState()'s own load */
        state1 = w[10];                  /* __state[1], +40 - registerService's second load */
    }
    if (svc && gIOResourceMatchedKey) {
        keys = _ZNK15IORegistryEntry12copyPropertyEPK8OSSymbol(svc, gIOResourceMatchedKey);
        if (keys) {
            count = _ZNK7OSArray8getCountEv(keys);
            idx = gIOBSDKey
                      ? _ZNK7OSArray20getNextIndexOfObjectEPK15OSMetaClassBasej(keys, gIOBSDKey, 0)
                      : 0xfffffffeu;
        }
    }
    g_reg_calls++;
    g_reg_ptr = (uint32_t)(uintptr_t)svc;
    g_reg_state0 = state0;
    g_reg_state1 = state1;
    g_reg_rm = (uint32_t)(uintptr_t)keys;
    g_reg_count = count;
    g_reg_idx = idx;
    entry_live_write("xnu_live_reg_seq", seq);
    entry_live_write("xnu_live_reg_site", site);
    entry_live_write("xnu_live_reg_ptr", g_reg_ptr);
    entry_live_write("xnu_live_reg_state0", state0);
    entry_live_write("xnu_live_reg_state1", state1);
    entry_live_write("xnu_live_reg_rm", g_reg_rm);
    entry_live_write("xnu_live_reg_count", count);
    entry_live_write("xnu_live_reg_idx", idx);
    entry_live_write("xnu_live_reg_calls", g_reg_calls);
    if (keys)
        _ZNK8OSObject7releaseEv(keys);
}

/*
 * One call per matching dictionary built by the four factory overloads in `entry_trace.c`. `name` is
 * the `const char *` for the two char* overloads - a literal in the image, so it resolves host-side -
 * and the `OSSymbol *` for the two `OSString *` ones, which is *not* dereferenced here: the site plus
 * the source line names it (`IOFindBSDRoot+0x28` passes `gIOResourcesKey`), and a read of an
 * unverified heap layout is a fault this instrument does not need to risk. `out` is the dictionary,
 * which is the pointer the match records carry, so builder and query pair up by value.
 */
void entry_note_dict(uint32_t site, uint32_t name, uint32_t table_in, uint32_t table_out)
{
    if (g_dict_calls < ENTRY_DICT_MAX) {
        g_dict_site[g_dict_calls] = site;
        g_dict_name[g_dict_calls] = name;
        g_dict_out[g_dict_calls] = table_out;
    }
    g_dict_calls++;
    g_dict_last_site = site;
    g_dict_last_name = name;
    g_dict_last_in = table_in;
    g_dict_last_out = table_out;
    entry_live_write("xnu_live_dict_site", site);
    entry_live_write("xnu_live_dict_name", name);
    entry_live_write("xnu_live_dict_in", table_in);
    entry_live_write("xnu_live_dict_out", table_out);
    entry_live_write("xnu_live_dict_seq", g_dict_calls);
}

/*
 * Experiment 457. One call per `IOCatalogue::findDrivers(IOService *, SInt32 *)` whose `service` is
 * the resource root, from the one wrapper in `entry_trace.c`. The argument that matters is the last:
 * `doServiceMatch` fills `resourceKeys` - and so sets the `IOResourceMatched` array the whole wait
 * turns on - only `if (keepGuessing && matches->getCount() && ...)` (`IOService.cpp:3724`), and
 * `matches` is exactly this call's return value. So the count *is* the measurement 456 could not
 * make, and the set pointer it is read from is kept beside it in case the count is right for the
 * wrong reason (a personality filed under `IOService` rather than `IOResources` would show up here
 * too, and `xnu_live_finddrv_svc` is what would say the reading was about the resource root at all).
 *
 * Live only, like every reading since 454: a boot that hangs at this frontier never reaches the
 * epilogue, so the five keys go to the live console in the order that makes the record readable -
 * ordinal, site, service, set, count.
 */
void entry_note_finddrivers(uint32_t site, uint32_t service, uint32_t set, uint32_t count)
{
    g_finddrv_calls++;
    g_finddrv_site = site;
    g_finddrv_svc = service;
    g_finddrv_set = set;
    g_finddrv_count = count;
    entry_live_write("xnu_live_finddrv_seq", g_finddrv_calls);
    entry_live_write("xnu_live_finddrv_site", site);
    entry_live_write("xnu_live_finddrv_svc", service);
    entry_live_write("xnu_live_finddrv_set", set);
    entry_live_write("xnu_live_finddrv_count", count);
}

/*
 * Experiment 453. One call per entry into the sleep family, from the six wrappers in `entry_trace.c`,
 * and the whole point is the first argument: `site` is the wrapper's own `lr`, so it names the
 * function that asked to sleep - the frame `entry_note_block` cannot reach, because the block it
 * records happens two calls below this one and the primitive in between is not the site.
 *
 * The five records per call go to the live console and only the last call's values are kept in
 * `.bss`, because a boot that hangs inside a sleep never reaches the epilogue: what has to survive is
 * the *sequence*, in order, on the channel that survives a hang.
 */
void entry_note_sleep(uint32_t ent, uint32_t site, uint32_t thread, uint32_t chan, uint32_t wmsg,
                      uint32_t pri, uint32_t tmo)
{
    g_sleep_calls++;
    if (ent < ENTRY_SLEEP_MAX)
        g_sleep_count[ent]++;
    g_sleep_last_ent = ent;
    g_sleep_last_site = site;
    g_sleep_last_thread = thread;
    g_sleep_last_chan = chan;
    g_sleep_last_wmsg = wmsg;
    g_sleep_last_pri = pri;
    g_sleep_last_tmo = tmo;
    entry_live_write("xnu_live_sleep_ent", ent);
    entry_live_write("xnu_live_sleep_site", site);
    entry_live_write("xnu_live_sleep_thr", thread);
    entry_live_write("xnu_live_sleep_chan", chan);
    entry_live_write("xnu_live_sleep_wmsg", wmsg);
    entry_live_write("xnu_live_sleep_pri", pri);
    entry_live_write("xnu_live_sleep_tmo", tmo);
}

void entry_note_vmwait(uint32_t caller)
{
    g_vmwait_caller = caller;
    g_vmwait_count++;
}

/*
 * Experiment 447. `ml_get_max_cpus` returns, so this one is called *before* the real function and
 * must keep the first caller rather than the last: the first call is the one that blocks, because
 * `ml_init_max_cpus` is the only writer of the flag and a call that found it set would not have
 * blocked. Keeping the last would name the site of the call that *returned*, which is the one site
 * that cannot be the one the run ended in.
 */
void entry_note_maxcpus(uint32_t caller)
{
    if (g_maxcpus_count == 0)
        g_maxcpus_caller = caller;
    g_maxcpus_count++;
    entry_live_write("xnu_live_maxcpus_caller", caller);
}

void entry_note_initmax_cpus(uint32_t caller, uint32_t max_cpus)
{
    if (g_initmax_cpus_count == 0) {
        g_initmax_cpus_caller = caller;
        g_initmax_cpus_arg = max_cpus;
    }
    g_initmax_cpus_count++;
    entry_live_write("xnu_live_initmax_cpus_caller", caller);
    entry_live_write("xnu_live_initmax_cpus_arg", max_cpus);
}

/*
 * Experiment 448's six. The first two record everything they are handed; the last four keep the
 * **first non-zero return** and the site it came from, because for all four zero means success and
 * the question is whether any call ever failed. A slot of 0 with a non-zero count is "called, never
 * failed"; a count of 0 is "never called", which is a different reading.
 */
void entry_note_dtalloc(uint32_t caller, uint32_t arg, uint32_t ret)
{
    if (g_dtalloc_count == 0) {
        g_dtalloc_caller = caller;
        g_dtalloc_arg = arg;
        g_dtalloc_ret = ret;
    }
    g_dtalloc_count++;
    entry_live_write("xnu_live_dtalloc_arg", arg);
    entry_live_write("xnu_live_dtalloc_ret", ret);
}

void entry_note_workloop(uint32_t caller, uint32_t ret)
{
    if (g_workloop_count == 0) {
        g_workloop_caller = caller;
        g_workloop_ret = ret;
    }
    g_workloop_count++;
    entry_live_write("xnu_live_workloop_ret", ret);
}

/*
 * One helper for the four guards, so the "first failure wins" rule is written once rather than four
 * times - the class of defect this project has paid for repeatedly is one value with several
 * definitions, and four copies of one rule is four chances to differ.
 */
void entry_note_guard(uint32_t *bad, uint32_t *bad_caller, uint32_t *count,
                      uint32_t caller, uint32_t ret)
{
    if (ret != 0 && *bad == 0) {
        *bad = ret;
        *bad_caller = caller;
    }
    (*count)++;
}

void entry_note_rlock(uint32_t caller, uint32_t ret)
{
    entry_note_guard(&g_rlock_bad, &g_rlock_caller, &g_rlock_count, caller, ret);
}

void entry_note_slock(uint32_t caller, uint32_t ret)
{
    entry_note_guard(&g_slock_bad, &g_slock_caller, &g_slock_count, caller, ret);
}

void entry_note_cgate(uint32_t caller, uint32_t ret)
{
    entry_note_guard(&g_cgate_bad, &g_cgate_caller, &g_cgate_count, caller, ret);
}

void entry_note_kthread(uint32_t cont, uint32_t caller, uint32_t ret)
{
    if (g_kthread_count < 4) {
        g_kthread_cont[g_kthread_count] = cont;
        g_kthread_site[g_kthread_count] = caller;
    }
    entry_note_guard(&g_kthread_bad, &g_kthread_caller, &g_kthread_count, caller, ret);
    entry_live_write("xnu_live_kthread_cont", cont);
    entry_live_write("xnu_live_kthread_ret", ret);
}

/*
 * Experiment 449's five. `postctor`, `catinit` and `allocname` have one call site each on this boot's
 * path, so they keep the **first** call; `unser` (three sites) and `pub2` (nine) keep a short ring of
 * the first three or four, because "the first call" is a claim about ordering and a ring does not have
 * to make it. Every one of them also counts, so "called once, returned nothing" stays distinguishable
 * from "never called".
 */
void entry_note_postctor(uint32_t caller)
{
    if (g_postctor_count == 0)
        g_postctor_caller = caller;
    g_postctor_count++;
}

void entry_note_catinit(uint32_t caller)
{
    if (g_catinit_count == 0)
        g_catinit_caller = caller;
    g_catinit_count++;
}

void entry_note_unser(uint32_t caller, uint32_t ret)
{
    if (g_unser_count < 3) {
        g_unser_caller[g_unser_count] = caller;
        g_unser_ret[g_unser_count] = ret;
    }
    g_unser_count++;
    entry_live_write("xnu_live_unser_caller", caller);
    entry_live_write("xnu_live_unser_ret", ret);
}

void entry_note_allocname(uint32_t name)
{
    if (g_alloc_count == 0)
        g_alloc_name = name;
    g_alloc_count++;
    entry_live_write("xnu_live_alloc_name", name);
}

void entry_note_pub2(uint32_t caller, uint32_t key)
{
    if (g_pub2_count < 4) {
        g_pub2_caller[g_pub2_count] = caller;
        g_pub2_key[g_pub2_count] = key;
    }
    g_pub2_count++;
    entry_live_write("xnu_live_pub2_key", key);
}

/*
 * ------------------------------------------------- 461: the plane, the path, the property, the device
 *
 * 459 stopped inside `IOFindBSDRoot` with the tree's `RAMDisk` property apparently absent, and it
 * left a gap exactly one link wide. The tree is not in doubt - the payload's blob answers XNU's own
 * reader on the host, node by node and word by word (`tools/host_dt_check.sh`) - and neither is
 * `IODeviceTreeAlloc`'s call, which 448's records show running with the tree's own physical address
 * (`0x806e0000`) and returning a nub. What is not measured is the object in the middle: the IODT
 * *plane*, the registry tree `IORegistryEntry::fromPath` walks and `getProperty` reads.
 *
 * The source says the chain has exactly three NULLs in it (`iokit/bsddev/IOKitBSDInit.cpp:440-450`):
 *
 *     if ((regEntry = IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane))) {
 *         data = (OSData *) regEntry->getProperty("RAMDisk");
 *         if (data) { ramdParms = data->getBytesNoCopy();
 *                     mdevadd(-1, ml_static_ptovirt(ramdParms[0]) >> 12, ramdParms[1] >> 12, 0); }
 *     }
 *
 * so `mdevadd` not being called means one of `fromPath`, `getProperty` or `getBytesNoCopy` answered
 * NULL - and 459's log cannot say which, because it has no reading from any of the three. These four
 * functions are that reading.
 *
 * `entry_note_dtplane` is taken from the `IODeviceTreeAlloc` wrapper once the real function has
 * returned, which is the only moment the plane exists and the tree is complete: the plane is
 * `makePlane`'d on that function's first statement (`IODeviceTreeSupport.cpp:120`) and the tree is
 * attached to the registry root as its last (`:216`). `map_ret` is the wrapper's own
 * `fromPath("/chosen/memory-map", gIODTPlane)` - the OS's own reader, called by the instrument
 * rather than inferred, and the one reading that separates "the plane does not have the node" from
 * "the node does not have the property".
 *
 * `entry_note_dtpath` is every `fromPath` call the image makes, wrapped. The caller is the important
 * half: the OS's own call lives at `IOKitBSDInit.cpp:440`, and a record from it whose return is 0 is
 * `fromPath`'s answer to the OS, not to the instrument. `w0`/`w1`/`w2` are the path's first twelve
 * bytes as words, so `"/chosen/memo"` is readable in the log without a pointer that could be wrong
 * by the time it is read.
 *
 * `entry_note_dtprop` is XNU's own `getProperty`, called by the instrument with the entry `fromPath`
 * returned. It is *not* a wrapper and cannot be one: `getProperty` is virtual, and `--wrap` renames
 * an undefined reference while a vtable entry is a defined one in the same object - 455's negative
 * result, which cost it eleven wrappers' worth of silence. Calling `IORegistryEntry`'s own
 * definition by its mangled name is the same code the vtable slot holds (`IOService` does not
 * override it), so the answer is the answer the OS would have got; `obj` non-zero means the property
 * *is* in the plane, and then `getBytesNoCopy` on it gives the two words the OS would have handed
 * `mdevadd`. A non-zero `obj` with the OS still not calling `mdevadd` would move the gap inside
 * `getProperty`'s own lookup, which is a different experiment.
 *
 * `entry_note_mdevadd`/`entry_note_mdevlookup` are the two calls at the end of the chain. 459
 * established that `mdevadd` is not called - it prints on all five of its outcomes
 * (`bsd/dev/memdev.c:560-635`) and none is in the log - so what the wrapper adds is not *whether* but
 * *what*: its arguments are the two words the OS read, `unsigned long long` in r1:r2 and `unsigned
 * int` in r3 by the AAPCS, which is why the C prototype here spells the widths out rather than
 * taking `uint32_t` for everything.
 */
void entry_note_dtplane(uint32_t plane, uint32_t dt_top, uint32_t root, uint32_t map_ret)
{
    g_dtplane = plane;
    g_dtplane_dt_top = dt_top;
    g_dtplane_root = root;
    g_dtplane_map_ret = map_ret;
    g_dtplane_count++;
    entry_live_write("xnu_live_dtplane", plane);
    entry_live_write("xnu_live_dtplane_root", root);
    entry_live_write("xnu_live_dtplane_map", map_ret);
}

void entry_note_dtpath(uint32_t caller, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t plane,
                       uint32_t ret)
{
    g_dtpath_count++;
    g_dtpath_caller = caller;
    g_dtpath_w0 = w0;
    g_dtpath_w1 = w1;
    g_dtpath_w2 = w2;
    g_dtpath_plane = plane;
    g_dtpath_ret = ret;
    if (ret == 0u) {
        g_dtpath_null++;
    }
    entry_live_write("xnu_live_path_caller", caller);
    entry_live_write("xnu_live_path_w0", w0);
    entry_live_write("xnu_live_path_w1", w1);
    entry_live_write("xnu_live_path_w2", w2);
    entry_live_write("xnu_live_path_plane", plane);
    entry_live_write("xnu_live_path_ret", ret);
}

void entry_note_dtprop(uint32_t entry, uint32_t key0, uint32_t key1, uint32_t obj, uint32_t w0,
                       uint32_t w1, uint32_t bytes)
{
    g_dtprop_calls++;
    if (obj != 0u) {
        g_dtprop_hits++;
    }
    g_dtprop_entry = entry;
    g_dtprop_key0 = key0;
    g_dtprop_key1 = key1;
    g_dtprop_obj = obj;
    g_dtprop_w0 = w0;
    g_dtprop_w1 = w1;
    g_dtprop_bytes = bytes;
    entry_live_write("xnu_live_prop_entry", entry);
    entry_live_write("xnu_live_prop_key0", key0);
    entry_live_write("xnu_live_prop_key1", key1);
    entry_live_write("xnu_live_prop_obj", obj);
    entry_live_write("xnu_live_prop_w0", w0);
    entry_live_write("xnu_live_prop_w1", w1);
    entry_live_write("xnu_live_prop_bytes", bytes);
}

void entry_note_mdevadd(uint32_t caller, uint32_t devid, uint32_t base, uint32_t size,
                        uint32_t phys, uint32_t ret)
{
    g_mdevadd_calls++;
    g_mdevadd_caller = caller;
    g_mdevadd_devid = devid;
    g_mdevadd_base = base;
    g_mdevadd_size = size;
    g_mdevadd_phys = phys;
    g_mdevadd_ret = ret;
    entry_live_write("xnu_live_mdevadd_caller", caller);
    entry_live_write("xnu_live_mdevadd_devid", devid);
    entry_live_write("xnu_live_mdevadd_base", base);
    entry_live_write("xnu_live_mdevadd_size", size);
    entry_live_write("xnu_live_mdevadd_phys", phys);
    entry_live_write("xnu_live_mdevadd_ret", ret);
}

void entry_note_mdevlookup(uint32_t caller, uint32_t devid, uint32_t ret)
{
    g_mdevlookup_calls++;
    g_mdevlookup_caller = caller;
    g_mdevlookup_devid = devid;
    g_mdevlookup_ret = ret;
    entry_live_write("xnu_live_mdevlookup_caller", caller);
    entry_live_write("xnu_live_mdevlookup_devid", devid);
    entry_live_write("xnu_live_mdevlookup_ret", ret);
}

/*
 * 471. The exit reason the exec path created, which is the frontier's *name*: the code is one of
 * `EXEC_EXIT_REASON_*` (`bsd/sys/reason.h:222-233`), or `CODESIGNING_EXIT_REASON_*` (`:190-193`)
 * when the namespace says so, and the caller is the `bl os_reason_create` that chose it - so the
 * pair answers "which of the eight `badtoolate` sites" without a guessing game about which failure
 * the console's silence hides.
 *
 * `entry_live_write` as well as the slots, for 459's reason: a run that never reaches the epilogue
 * still says how many reasons were created and what the last one was, because the live records are
 * written to the ram console as they happen.
 */
void entry_note_osreason(uint32_t caller, uint32_t ns, uint32_t code, uint32_t ret)
{
    g_osr_calls++;
    g_osr_caller = caller;
    g_osr_ns = ns;
    g_osr_code = code;
    g_osr_ret = ret;
    entry_live_write("xnu_live_osr_caller", caller);
    entry_live_write("xnu_live_osr_ns", ns);
    entry_live_write("xnu_live_osr_code", code);
    entry_live_write("xnu_live_osr_ret", ret);
}

void entry_note_loadmachfile(uint32_t caller, uint32_t header, uint32_t ret)
{
    g_lmf_calls++;
    g_lmf_caller = caller;
    g_lmf_header = header;
    g_lmf_ret = ret;
    entry_live_write("xnu_live_lmf_caller", caller);
    entry_live_write("xnu_live_lmf_header", header);
    entry_live_write("xnu_live_lmf_ret", ret);
}

/*
 * 462. The walk from the registry root, read at two moments in one boot - see `entry_probe_walk` in
 * `entry_trace.c` for what each number is and which failure of `fromPath`'s first component it
 * separates. `t1` selects the slot pair; the live records are written for every reading so that a
 * run which never reaches the epilogue still says how many readings there were and in what order.
 */
void entry_note_dtwalk(uint32_t t1, uint32_t root, uint32_t count, uint32_t first, uint32_t set,
                       uint32_t kids, uint32_t control)
{
    g_walk_probes++;
    if (t1 != 0u) {
        g_walk_t1_root = root;
        g_walk_t1_count = count;
        g_walk_t1_first = first;
        g_walk_t1_set = set;
        g_walk_t1_kids = kids;
        g_walk_t1_control = control;
    } else {
        g_walk_t2_root = root;
        g_walk_t2_count = count;
        g_walk_t2_first = first;
        g_walk_t2_set = set;
        g_walk_t2_kids = kids;
        g_walk_t2_control = control;
    }
    entry_live_write("xnu_live_walk_seq", g_walk_probes);
    entry_live_write("xnu_live_walk_root", root);
    entry_live_write("xnu_live_walk_count", count);
    entry_live_write("xnu_live_walk_first", first);
    entry_live_write("xnu_live_walk_set", set);
    entry_live_write("xnu_live_walk_kids", kids);
    entry_live_write("xnu_live_walk_control", control);
}

/*
 * 463. Four notes, one per link of the wait's predicate chain, and every one of them writes live first
 * for 462's reason: this boot ends *inside* the thing being measured, so the run that matters is the
 * one that never reaches `entry_epilogue`. See the block above `g_wmatch_calls` for what each number
 * is and which failure it separates.
 *
 * The order the records appear in the log is the order the links run in, and it is the reading: on one
 * wait the sequence is `wmatch_seq`/`_ent`/`_caller`/`_dict`/`_to_lo`/`_to_hi`, then `wsvc_seq`/`_p4`/
 * `_p0`, then `wcls_seq`/`_sym`/`_meta`/`_rsvc`, then one `winst_seq`/`_inst`/`_state` per instance,
 * then `wcls_seen`/`_shown`. A run whose last record is `wmatch_ent = 0` never got as far as calling
 * the predicate; a run whose last record is a `winst_state` died inside the walk. The two are
 * different logs, which is the property 268 spent a measurement on.
 */
void entry_note_wmatch(uint32_t ent, uint32_t caller, uint32_t dict, uint32_t to_lo, uint32_t to_hi,
                       uint32_t ret)
{
    g_wmatch_calls++;
    g_wmatch_last_ent = ent;
    g_wmatch_last_caller = caller;
    g_wmatch_last_dict = dict;
    g_wmatch_last_to_lo = to_lo;
    g_wmatch_last_to_hi = to_hi;
    g_wmatch_last_ret = ret;
    entry_live_write("xnu_live_wmatch_seq", g_wmatch_calls);
    entry_live_write("xnu_live_wmatch_ent", ent);
    entry_live_write("xnu_live_wmatch_caller", caller);
    entry_live_write("xnu_live_wmatch_dict", dict);
    entry_live_write("xnu_live_wmatch_to_lo", to_lo);
    entry_live_write("xnu_live_wmatch_to_hi", to_hi);
    entry_live_write("xnu_live_wmatch_ret", ret);
}

void entry_note_wsvc(uint32_t dict, uint32_t sym, uint32_t p4, uint32_t p0)
{
    g_wsvc_calls++;
    g_wsvc_last_dict = dict;
    g_wsvc_last_sym = sym;
    g_wsvc_last_p4 = p4;
    g_wsvc_last_p0 = p0;
    entry_live_write("xnu_live_wsvc_seq", g_wsvc_calls);
    entry_live_write("xnu_live_wsvc_dict", dict);
    entry_live_write("xnu_live_wsvc_sym", sym);
    entry_live_write("xnu_live_wsvc_p4", p4);
    entry_live_write("xnu_live_wsvc_p0", p0);
}

void entry_note_wcls_begin(uint32_t sym, uint32_t meta, uint32_t rsvc, uint32_t name0,
                           uint32_t name1)
{
    g_wls_seen = 0u;
    g_wcls_calls++;
    g_wcls_last_sym = sym;
    g_wcls_last_meta = meta;
    g_wcls_last_rsvc = rsvc;
    g_wcls_last_name0 = name0;
    g_wcls_last_name1 = name1;
    entry_live_write("xnu_live_wcls_seq", g_wcls_calls);
    entry_live_write("xnu_live_wcls_sym", sym);
    entry_live_write("xnu_live_wcls_meta", meta);
    entry_live_write("xnu_live_wcls_rsvc", rsvc);
    entry_live_write("xnu_live_wcls_name0", name0);
    entry_live_write("xnu_live_wcls_name1", name1);
}

/*
 * One instance from the walk, with `__state[0]` read through `IOService::getState()`.
 *
 * The count is taken for **every** instance and only the first `ENTRY_WCLS_MAX` are recorded, which is
 * what keeps a walk over a large set from filling the console: `wcls_seen` carries the total, so a
 * report with `shown < seen` says the walk was longer than the record and not that it was shorter.
 */
void entry_note_winst(uint32_t inst, uint32_t state)
{
    uint32_t seq = g_wls_seen;

    g_wls_seen++;
    if (seq >= ENTRY_WCLS_MAX)
        return;
    g_wcls_inst[seq] = inst;
    g_wcls_state[seq] = state;
    entry_live_write("xnu_live_winst_seq", seq);
    entry_live_write("xnu_live_winst_inst", inst);
    entry_live_write("xnu_live_winst_state", state);
}

void entry_note_wcls_end(void)
{
    g_wcls_seen = g_wls_seen;
    g_wcls_shown = (g_wls_seen < ENTRY_WCLS_MAX) ? g_wls_seen : ENTRY_WCLS_MAX;
    entry_live_write("xnu_live_wcls_seen", g_wcls_seen);
    entry_live_write("xnu_live_wcls_shown", g_wcls_shown);
}
#endif /* STAGE90_ENTRY_TRACE */

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
 *
 * **Compiled out by experiment 201**, which links `osfmk_kern_debug.o` for `panic_init` - and this
 * is the first retired stand-in in this sequence whose replacement is *not* equivalent to it. The
 * message above is a diagnosis this project chose; XNU's own `panic` is
 * `panic_trap_to_debugger(...)`, and the first thing on its path that this image does not have is
 * `PEHaltRestart(kPEPanicBegin)` in `iokit_Kernel_PlatformExpert.o`. So a panic from here on
 * reports `stub_hit=PEHaltRestart`, which does not say that a panic happened.
 *
 * That cost is paid deliberately and it is paid once: the bespoke message was a stand-in for an
 * image that had no panic at all, and an image that runs XNU's own code should panic the way XNU
 * panics. It is kept here, under its switch, so that it can be brought back by one build variable
 * if the trade turns out to run the other way - which is the same reason every other retired
 * stand-in in this file is kept rather than deleted.
 */
#ifndef STAGE90_ENTRY_REAL_PANIC
void panic(const char *fmt, ...);
void panic(const char *fmt, ...)
{
    (void)fmt;
    entry_epilogue("panic() - XNU rejected something; see which call precedes this line");
}
#endif /* !STAGE90_ENTRY_REAL_PANIC */

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
 *
 * **Experiment 244 adds the second argument, and it is the one thing the name cannot say.** From
 * 243's stop onwards the frontier is a stub *somewhere*, and experiment 206's lesson is that
 * `stub_hit=<symbol>` names a symbol and never a caller - 243's own log said "real arm_init reached
 * a symbol this image does not provide" about `kmem_init`, which is four frames below `arm_init`
 * (`arm_init` -> `machine_startup` -> `kernel_bootstrap` -> `vm_mem_bootstrap` -> `kmem_init`). The
 * value is `lr` as the stub was entered with it, which for a `bl` is the address of the instruction
 * after it - so the call site is `caller - 4`, and `tools/host_resolve_entry_addr.sh` resolves it
 * that way against this image and prints `function+0xNN`.
 *
 * It is read with `__builtin_return_address(0)` **inside the generated one-liner**, where it is a
 * read of `lr` before the function has done anything: the compilation of both shapes this build
 * produces was checked with the build's own flags and the disassembly says `mov r1, lr` ahead of the
 * tail call (and, for the shape with calls before it, `mov r5, lr` in the prologue). A stub is still
 * 16 bytes rather than 12 - which is the whole cost of this, times 559 of them.
 */

/*
 * **465: the live channel, for a caller that does not know how this file was compiled.**
 * `entry_live_write` is inside the `STAGE90_ENTRY_TRACE` guard, and `entry_stub_hit` is not
 * (`build_entry.sh` compiles the trace only when `STAGE90_ENTRY_TRACE=1`, which is the canonical
 * build but not the only one). This wrapper is defined outside that guard, so the pthread table's
 * own body - a platform object, compiled by `tools/build_xnu_arm_kernel.sh` with flags this file
 * does not choose - can call it in either configuration and the trace-off build still links. With
 * the trace off there is no live channel and the call is a no-op, which is the truthful answer:
 * there is nothing to write the record to, and `entry_kv`'s buffer copy is still there.
 */
void entry_note_live(const char *key, uint32_t value)
{
#ifdef STAGE90_ENTRY_TRACE
    entry_live_write(key, value);
#else
    (void)key;
    (void)value;
#endif
}

void entry_stub_hit(const char *name, uint32_t caller)
{
    static const char prefix[] = " stub_hit=";

    /*
     * **465: the terminal record goes to the live channel first, and this is 461's lesson one
     * channel over.** 464's run stopped with `kv_written = kv_in_dram = 0x2000` - the 8 KB buffer
     * full, 32896 writes refused - and the only reason the log names the stop at all is that a
     * *record-sized* write is refused at `g_kv_len + 40 >= ENTRY_KV_BUF` while this function's own
     * guard is `g_kv_len + 12 < ENTRY_KV_BUF`: eleven characters of the name fitted in the last
     * bytes that no `entry_kv` record could reach, and the name that survived is
     * ` stub_hit=stage90_pth`, which is not a symbol. The caller survived only because it is in
     * `.bss` - the two `entry_kv` records for it were refused. So the one record that names a stop
     * was the one record a full buffer could cut, which is 461's trap failure exactly.
     *
     * The name is written here **twice, in two forms**, because the live channel carries 32-bit
     * values and not strings: as a pointer into the image (which `nm`/`tools/read_elf_string.py`
     * resolve from the ELF, the way `t268_lckgrp_name` is resolved) and as the first eight bytes of
     * the string itself, so a log with no ELF beside it names the stop on its own. The sequence
     * number is what makes a *second* stop distinguishable from a repeat of the first - 464's run
     * had no such counter, and "one `stub_hit`" was a count taken by reading the log by eye.
     *
     * These are terminal records: nothing later can be lost by writing them early, and nothing
     * later can be *gained* by writing them late.
     */
    g_stub_hit_count++;
    entry_note_live("xnu_live_stub_hit_seq", g_stub_hit_count);
    entry_note_live("xnu_live_stub_hit_name_ptr", (uint32_t)(uintptr_t)name);
    entry_note_live("xnu_live_stub_hit_name_w0",
                    entry_image_ptr((uintptr_t)name) ? entry_word_at((uintptr_t)name) : 0u);
    entry_note_live("xnu_live_stub_hit_name_w1",
                    entry_image_ptr((uintptr_t)name + 4u)
                        ? entry_word_at((uintptr_t)name + 4u) : 0u);
    entry_note_live("xnu_live_stub_hit_caller", caller);

    /*
     * The buffer copy is kept as the *dump's* record - the epilogue prints the whole buffer, and a
     * run whose live channel was capped wants the name in the dump too - and its bound is
     * `+ 2u`, not `+ 1u`. **The `+ 1u` was an off-by-one that 464's run paid for out of the
     * kernel's own data**: the loop could fill to `g_kv_len = 8191`, the `'\n'` took the length to
     * exactly `ENTRY_KV_BUF` (8192), and `g_kv_buf[g_kv_len] = '\0'` therefore stored at
     * `&g_kv_buf + 8192` = `0x80558000`, which `nm -n` puts at the first byte of the kernel's
     * `ExceptionVectorsTable` (an 8-word table of exception handler addresses that `start.s` fills
     * in during boot, so it is not a zero byte that was overwritten). Reserving both the `'\n'` and
     * the `'\0'` in the loop's own bound keeps the last write inside the array.
     */
    if (g_kv_len + sizeof prefix + 1u < ENTRY_KV_BUF) {
        for (const char *p = prefix; *p != '\0'; p++) {
            g_kv_buf[g_kv_len++] = *p;
        }
        while (*name != '\0' && g_kv_len + 2u < ENTRY_KV_BUF) {
            g_kv_buf[g_kv_len++] = *name++;
        }
        g_kv_buf[g_kv_len++] = '\n';
        g_kv_buf[g_kv_len] = '\0';
    }

    /* `" " + "xnu_entry_stub_caller" (21) + "=0x"`, so the first hex digit is at `g_kv_len + 25`.
     * The `26` this line carried through experiment 273 was a miscount of the key's length - 22 for
     * a 21-character name - and it is visible in that run's own report: `_w0` reads `0x65303030`,
     * four bytes of `000e138`, for a value whose digits are `8000e138`. So `_w0`/`_w1` have always
     * covered digits *two* through nine, which is where 271's corruption was anyway (`a` and `c`,
     * digits four and five of `800ac0b4`), and nothing either experiment concluded depended on the
     * first digit. Corrected here; a run that shows `_w0` beginning `0x38` has the corrected
     * window. */
    g_stub_caller_digits = g_kv_len + 25u;
    g_stub_caller = caller;
    /* 465: whether the two records below will be written at all. The test is `entry_kv_into`'s own
     * refusal test (`len + 40 >= max`), so it cannot disagree with it, and `_in_buf = 0` is what
     * tells the report that `_digits` is an offset into a record that is not there - and in 464's
     * run an offset past the buffer's end, which is how `_w0`/`_w1` came to print bytes of the
     * kernel's `ExceptionVectorsTable`. */
    g_stub_caller_in_buf = (g_kv_len + 40u < ENTRY_KV_BUF) ? 1u : 0u;
    entry_kv("xnu_entry_stub_caller", caller);
    /* The same value through the same call site, one call later: two records in the same machine
     * state say whether a bias is systematic per invocation or a per-call hazard. */
    entry_kv("xnu_entry_stub_caller_a", caller);

    entry_epilogue("a symbol this image does not provide was called");
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
 *
 * **Compiled out by experiment 199**, which links `osfmk_kern_printf.o` for `printf_init`. The
 * argument above - that the object costs 5607 bytes and 24 obligations to obtain four - is still
 * true and is no longer the deciding fact: `printf_init` is `osfmk/arm/arm_init.c:328`, it is the
 * next statement the device reaches, and it lives in this object and nowhere else. The four bytes
 * were the whole question when this stand-in was written (experiment 189); they are not when the
 * object is the step. Kept under its own switch so the decision is one variable, the way every
 * other probe and stand-in here is.
 */
#ifndef STAGE90_ENTRY_REAL_KPRINTF
void _consume_kprintf_args(int a, ...)
{
    (void)a;
}
#endif /* !STAGE90_ENTRY_REAL_KPRINTF */

/*
 * ------------------------------------------------------------------ the C++ runtime's `__cxa_atexit`
 *
 * **Experiment 330.** Every `.cpp` in this image that has a static object needing destruction ends
 * its `_GLOBAL__sub_I_<file>.cpp` with a **tail branch into `__cxa_atexit`** - the Itanium ABI's
 * static-destructor registration - and experiment 329's run is what made that reachable: XNU's own
 * `OSRuntimeInitializeCPP` now finds the `.init_array` table (`__DATA,__mod_init_func`), calls the
 * first of its six entries, and stops there:
 *
 *     _GLOBAL__sub_I_OSKext.cpp:
 *         ...
 *         e1a01004   mov  r1, r4          ; arg  = the object
 *         e3002000   movw r2, #0          ; dso  = &__dso_handle
 *         e8bd4010   pop  {r4, lr}
 *         eafffffe   b    __cxa_atexit    ; <- the stub 329 stopped in
 *
 * **It is defined nowhere in XNU** - `grep -rn __cxa_atexit external/xnu-upstream/` returns nothing
 * at all, and `__dso_handle` with it - so this is the one stop in the whole walk that no link can
 * move: there is no object to grow the image with.
 *
 * **And the flag is not this step, which is a measurement rather than a preference.** Experiment 330
 * compiled all 83 C++ files with each candidate and read the objects back (`tools/
 * build_xnu_arm_kernel.sh` grew `XNU_KERNEL_EXTRA_CXXFLAGS` for exactly this comparison, mirroring
 * the define hook above):
 *
 *   - `-fno-use-cxa-atexit` - the flag experiment 329 predicted - only *renames* the call: `b atexit`,
 *     and a kernel has no `atexit` either, so the stop would move one name and no distance.
 *   - `-fapple-kext` removes the call entirely, and it is what Apple's own rules pass
 *     (`makedefs/MakeInc.def:371`, `CXXFLAGS_GEN = -fapple-kext`). But it is not one call's worth of
 *     change: it also moves vtable emission to the key function's translation unit, so `OSCollection.o`,
 *     `OSDictionary.o`, `OSObject.o`, `OSKext.o` and `OSSymbol.o` acquire references to `_ZTV8OSObject`,
 *     `_ZTV12OSCollection` and `_ZTV8OSString` - the last of which is defined by `OSString.cpp`, an
 *     object this image does not link, so it would arrive as a *storage stand-in* for a vtable. It also
 *     grows the linked C++ text by about 13 KB (OSKext.o alone 74436 -> 83272), which moves the 16 KB
 *     boundary and every address above it and re-baselines the ledger's numbers for 324-329.
 *
 * So the flag is a step of its own, with its own measurement, and the step that moves *this* frontier
 * is the smaller one: define the two symbols. The semantics are exactly right rather than convenient -
 * **a kernel never exits**, so a static destructor is never run and the registration is a successful
 * no-op; `0` is what the real `__cxa_atexit` returns on success. This is the same shape as
 * `_consume_kprintf_args` above ("the real body is nothing to do") and as `printf` in an earlier
 * experiment: a small real definition where the alternative is linking an object this image has no
 * other reason to carry.
 *
 * `__dso_handle` is defined as its own address, which is what `crtbegin`'s definition is; it is passed
 * as a *value* (the initializers load its address into r2) and nothing in this image reads its
 * contents, so its size is the only property that could matter, and four bytes is what a `B` symbol in
 * the kernel objects is.
 */

/*
 * `__cxa_atexit(void (*func)(void *), void *arg, void *dso_handle)` - the signature read off the
 * call sites in the linked objects, not assumed: r0 is the destructor (in `_GLOBAL__sub_I_OSKext.cpp`
 * an address in the same object's `.text`), r1 the object, r2 the `__dso_handle` address. Registering
 * nothing and reporting success is the whole function.
 */
int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle)
{
    (void)func;
    (void)arg;
    (void)dso_handle;
    return 0;
}

void *__dso_handle = &__dso_handle;

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

    entry_stub_hit("pmap_bootstrap", (uint32_t)(uintptr_t)__builtin_return_address(0));
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
extern const char *debugger_panic_str;
extern const char *debugger_message;
extern unsigned long debugger_panic_caller;
/*
 * XNU's panic state. `debugger_panic_str`, `debugger_message` and `debugger_panic_caller` are `B`
 * objects in the image, defined by `osfmk_kern_debug.o`, which is already linked - so declaring them
 * costs nothing and they cannot go stale the way a hard-coded address would. **Experiments 237 and
 * 238 measured all three as zero**, which is the answer the caveat predicted: `handle_debugger_trap`
 * sets them from the CPU's debugger context and then restores them to NULL before returning
 * (`debug.c:905` then `debug.c:947`), and it runs before `DebuggerTrapWithState`, so the cache is
 * always clear by the time the trap fires. They are still reported, because a non-zero value here
 * would be news.
 *
 * So the message is read from where it cannot have been cleared - **the registers**, which the
 * vector trampoline does not touch. The trampoline (`entry_vectors.s`) loads SP from a literal and
 * branches to the handler without pushing anything, so the handler's first statement runs with the
 * trapping instruction's registers intact. Not `r0`-`r3`: `panic_trap_to_debugger` moves its first
 * four arguments into callee-saved registers at `+0x10` and they stay there, because
 * `DebuggerSaveState` pushes and restores `r4`-`r9` and `sl` and neither function touches them
 * again:
 *
 *   r9   = `panic_format_str`  - the panic message.
 *   r8   = `panic_args`        - a `va_list *`: the pointer `panic()` passed, not the list. The
 *                                list itself lives in `panic`'s frame, which is still live.
 *   r4   = the low half of `db_panic_options`, `sl` the high half. **Not the caller.** The key was
 *                                named `xnu_entry_trap_sl_caller` through experiments 236 to 239 and
 *                                the name was wrong; experiment 240 renamed it
 *                                `xnu_entry_trap_sl_options_hi`, so the record of the earlier runs
 *                                and the key the current image prints differ by name and not by
 *                                value.
 *   r5   = `panic_caller`      - the way `panic()` got here, i.e. the instruction after its `bl`.
 *
 * **Experiment 238 read the message through `r9` and got it:**
 *
 *   w0=0x72667a22 w1=0x203a6565 w2=0x65657266 w3=0x20676e69 w4=0x61766e69 w5=0x2064696c
 *   -> "zfree: freeing invalid
 *
 * which is `zalloc.c:1208`, the `is_sane_zone_element` check inside `free_to_zone`. That is a
 * `%p`/`%s` panic, so the two things it names are the first two entries of the `va_list` - **and
 * experiment 239 measured that the read below is one dereference short.** `r8` is `va_list *`, so
 * the word at `r8` is the list's `__ap`, a pointer to the first argument, and the word after it is
 * whatever follows the struct in `panic`'s frame. The run reported `arg0=0x0029be94` and
 * `arg1=0x00000020`, and `0x0029be94 - 0x0029be88 = 12` is exactly `panic`'s own layout: the list
 * is at its `sp+16`, its `__ap` is `sp+28` where the three varargs were stored, and 12 bytes is the
 * distance between them. The element and the zone name are one dereference further on, which is
 * what experiment 240 reads.
 *
 * `r0`-`r3` are not read either: they would hold `DebuggerTrapWithState`'s first four arguments -
 * `db_panic_str` among them, which is the same string as `r9` - but they are clobbered inside
 * `DebuggerSaveState` before the trap. `r9` and `r10` survive everything, and the disassembly is
 * the check that they do: this function's prologue is `strd r4, [sp, #-24]!`, `strd r6, [sp, #8]`,
 * `str r8, [sp, #16]`, so it writes `r4` (`mov r4, lr`) and `r5` (`mrs r5, SPSR`) before its first
 * statement and never writes `r9` or `r10` - while `r6`, `r7` and `r8` keep their values only until
 * the compiler reuses them, which is why `r6`, `r7` and `r8` are worth reading out of the frame
 * *the prologue saved them into* and not out of the registers. Experiment 240 does that, and four
 * of the five values it will read are already known.
 *
 * Every word read at a pointer is read only if that pointer is inside the entry image, because a
 * wild pointer here would fault inside the fault handler and say nothing at all. Words are read a
 * byte at a time rather than with an `ldr`: an unaligned `ldr` is allowed on ARMv7 only while
 * SCTLR.A is clear, and this code has no business assuming which mapping mode it is running under.
 *
 * Experiment 240 reads the element and the zone name, and it does **not** widen this guard to do
 * it - deliberately. The element is expected at `0x40400000`, XNU's `virtual_space_start` in this
 * image, which is outside the image and may well be unmapped; a data abort inside the abort handler
 * says nothing at all. So the element is read as a **value** - out of the `va_list`, which lives on
 * the (mapped) boot stack - and never dereferenced. Every address actually dereferenced here is
 * still inside the image, and the guard keeps it that way.
 *
 * ## Why `free_to_zone` panics at all in this image
 *
 * `is_sane_zone_ptr` tests three things: alignment (`zalloc.c:1058`), then `pmap_kernel_va`
 * (`:1063`), then - only when the zone is `collectable && !allows_foreign` - the zone-map range
 * (`:1070-1077`). That is the **source** order; the **built** order is `pmap_kernel_va` first, then
 * alignment, because the address tests are pure and the kernel-address test folds to a constant the
 * compiler is free to hoist. Both statements are true, of different artifacts, and only the built
 * order says anything about which test fired. **The test that fires is `pmap_kernel_va`, and it is
 * false for every address this image uses** - experiment 239's finding, and arithmetic on the boot
 * args rather than a measurement of XNU:
 *
 *   `entry.ld` links the image at `ENTRY_BASE` and `stage90`'s `xnu_entry_jump.c:141` hands `_start`
 *   `virtBase = physBase = ENTRY_BASE`, which was 0x00200000 through experiment 240. From that,
 *   `arm_vm_init.c:496` computes
 *   `vm_kernel_slide = gVirtBase - 0x80000000 = 0x80200000`, and `arm_vm_init.c:505` computes
 *   `virtual_space_start = (gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000 = 0x40400000`, with
 *   `MEM_SIZE_MAX = 0x40000000` (`arm_vm_init.c:134`). `pmap_kernel_va` wants
 *   `[0x80000000, 0xFFFEFFFF]` (`osfmk/arm/pmap.h:371`). 0x40400000 is not in it.
 *
 * So the first `free_to_zone` on the boot path - `vm_map_init+0x260`'s
 * `zcram(vm_map_zone, map_data, map_data_size)`, cramming the page `vm_map_steal_memory` stole
 * at `virtual_space_start` - panics, and the element it hands the check is in the 0x4040xxxx
 * chunk. Any later free would do the same. The fix is not another object; it is a base at or above
 * 0x80000000, which is where the device's RAM starts (`RAM_PHYS_BASE`, `stage90.h:21`).
 *
 * ## The base, and what experiment 241 changed about the arithmetic
 *
 * Everything above was true of an image linked at 0x00200000, and experiment 241 moved the base to
 * **0x80000000**. The two derived values become:
 *
 *   `vm_kernel_slide = gVirtBase - 0x80000000` = **0** - which is what this field means on a real
 *   device and what it was invented for; the underflow to 0x80200000 was this image's, not XNU's.
 *
 *   `virtual_space_start = (gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000` = **0xC0000000**,
 *   inside `pmap_kernel_va`'s `[0x80000000, 0xFFFEFFFF]` - so `is_sane_zone_ptr`'s first test now
 *   passes for the addresses XNU's own allocator hands out, and the zfree panic on the boot path
 *   is gone.
 *
 * What is *not* claimed here is that the boot proceeds. `zone_init` still never runs in this image
 * (the three stubs still stand in front of it), the pmap still bootstraps from a base it was never
 * run at, and the frontier method resumes with whatever the next run's `stub_hit=` or exception
 * names. The claim this change supports is the narrow one: the base is no longer the reason a
 * mandatory-path free cannot pass, and `vm_kernel_slide` is no longer reported as 0x80200000.
 */
extern uint32_t zone_map_max_address;
extern uint32_t zone_map_min_address;

static uint32_t
entry_word_at(uintptr_t p)
{
    return (uint32_t)*(const volatile unsigned char *)(p)
         | ((uint32_t)*(const volatile unsigned char *)(p + 1u) << 8)
         | ((uint32_t)*(const volatile unsigned char *)(p + 2u) << 16)
         | ((uint32_t)*(const volatile unsigned char *)(p + 3u) << 24);
}

/*
 * A pointer worth dereferencing: inside the entry image, where every string in it lives.
 *
 * The bounds are the **linker's**, not constants. This function used to test `[0x00200000,
 * 0x04000000)` - two magic numbers that had to be edited whenever the base or the image grew - and
 * experiment 241 moved the base, which would have turned every guarded read in this file into a
 * skipped one *silently*, because a guard that says "not worth dereferencing" and a guard that is
 * wrong about where the image is look identical in a log. `__entry_text_start` is the first symbol
 * `entry.ld` defines and `__entry_image_end` is its end, so the range is the image's real extent
 * whatever the base is. It is the same reason the layout above the image is derived rather than
 * written down twice.
 */
static int
entry_image_ptr(uintptr_t p)
{
    return (p >= (uintptr_t)__entry_text_start) && (p < (uintptr_t)__entry_image_end);
}

/*
 * ------------------------------------------------------------------ the second guard, and the
 * address the first one is wrong about
 *
 * `entry_image_ptr` above answers "is this inside the image". It is the right question for a string
 * or a table *this image* owns, and it was the right question for the panic's arguments too - while
 * they were inside the image. **They are not any more, and the way it stopped being right is a
 * number nobody would look at twice.**
 *
 * `panic()` keeps its `va_list` in its own frame, so `r8` - the trapping context's `r8`, which
 * `DebuggerSaveState` preserves - is a pointer into the stack `panic` is running on. Experiment 239
 * measured that stack at `0x0029be88` with the image linked at 0x00200000: inside
 * `[__entry_text_start, __entry_image_end)`, so the guard passed and the whole chain was read -
 * `ap = 0x0029be94`, `element = 0x40401f20`, `zonename = 0x0028b1b6`, all three in experiment 240's
 * log. Experiment 241 moved the base to 0x80000000, and `arm_vm_init.c:505` derives
 * `virtual_space_start = (gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000`, so the kernel's map
 * moved from 0x40400000 to **0xC0000000** and the bootstrap thread's stack moved with it. Two runs
 * since have read `r_args` at `0xc80abeb0` (362) and `0xc2113d80` (440) - outside the image, so the
 * guard said no, `ap` stayed 0, and `xnu_entry_panic_element` and `xnu_entry_panic_zonename` were
 * printed as `0x00000000`.
 *
 * **Zero is what makes this a measurement defect and not a missing reading.** A panic that names no
 * arguments and an instrument that refused to look at them produce the same two keys, so two
 * experiments recorded the zeros as a property of the panic - one of them writing, in its own doc,
 * that the `va_list` read "is refused by the image guard", which is true and reads like the guard
 * working.
 *
 * The guard's *purpose* is worth keeping: a wild pointer dereferenced inside the fault handler
 * faults again, and a data abort inside `fleh_undef` costs the whole report. What replaces it is
 * narrower and does not need to know where the image or the kernel map is, because the address is
 * not a guess: `r_args` is `panic`'s live frame address and the CPU is executing on that stack, so
 * the page holding it is mapped. The three words that follow it are `panic`'s own frame - the
 * `va_list` at `sp+16`, its `__ap` at `sp+28`, the first two spilled varargs at `sp+32` and `sp+36`
 * (experiment 239's arithmetic: `ap - r_args = 12`) - so the whole chain is 20 bytes wide and sits
 * behind `r_args`.
 *
 * The rule is therefore: **read only inside `[r_args, r_args + 32)` and only if that window lies in
 * one page.** The window's own size is what says the reads cannot walk off into a page the stack
 * does not own; the single-page test is what says the one page they do use is the one the CPU is
 * running on. Both are derived - 32 from the frame layout, 4096 from the page size - and neither is
 * a base or an image size that a later experiment can move.
 *
 * The refusal is still *visible*, which is the part the old guard got wrong - and **474 is the run that
 * shows the sentence that used to stand here was false.** It read "zero there means the reads below did
 * not happen", and `entry_panic_args_page(0)` returns 1: zero is 4-aligned and the window `[0, 32)`
 * lies in one page, so an *accepted* NULL published the same `0 & ~0xFFF` as a refusal and the key
 * could not tell the two apart. `xnu_entry_panic_args_ok` is that decision as a key of its own (1 =
 * accepted, 0 = refused), and the page key keeps its name and its value beside it. The deeper
 * correction is in the paragraph above: this guard tests *shape*, and the sentence that made it read
 * like a *mapping* test - "the address is not a guess: `r_args` is `panic`'s live frame address" - has a
 * premise 474 falsified, because the pointer can come from a user-mode `udf`, where the CPU is not on
 * that stack at all. The zero test is a necessary condition and not that proof; a page-table walk, or a
 * probe read under a recoverable abort, is what would be, and neither is here.
 *
 * `xnu_entry_panic_ap_delta` is the control, and it is the reason this can be believed rather than
 * hoped for: the frame layout is a compiler decision and no promise makes it 12. Reporting
 * `ap - r_args` means a layout that moved shows up as the number it moved by, next to the two
 * arguments whose meaning depends on it.
 */
#define ENTRY_PANIC_ARG_BYTES 32u

static int
entry_panic_args_page(uintptr_t args)
{
    /*
     * 475: zero is not a frame, and both tests below are trivially true for it. This is the one
     * comparison 474's run asks for: `args = 0` means no panic frame was recorded - the `udf` is not
     * `panic`'s - and the read it used to permit was of address 0, inside the fault handler, which is
     * the fault this handler must not take (269). The result is `xnu_entry_panic_args_ok` in the log
     * rather than a convention about a page number.
     */
    if (args == 0u) {
        return 0;
    }
    if ((args & 0x3u) != 0u) {
        return 0;
    }
    /* One page, both ends: the window may not straddle a boundary. */
    return (args & ~(uintptr_t)0xFFFu) == ((args + (ENTRY_PANIC_ARG_BYTES - 1u)) & ~(uintptr_t)0xFFFu);
}

static int
entry_panic_arg_word(uintptr_t args, uintptr_t p, uint32_t n)
{
    if (p < args || ((p - args) + (uintptr_t)n) > (uintptr_t)ENTRY_PANIC_ARG_BYTES) {
        return 0;
    }
    return 1;
}

/*
 * One word at an address the *kernel* uses, or `0` and no read.
 *
 * The bound is `__entry_text_start`, which is the base `entry.ld` links this image at and the same
 * number `xnu_entry_jump.c` hands `_start` as both `physBase` and `virtBase`. So "at or above the
 * base the kernel was linked at" is derived from the linker rather than written down, and it is the
 * weakest statement available: every address read below comes from `boot_args->deviceTreeP` or from
 * `DTLookupEntry`'s own root, and neither can be under it.
 *
 * The residual risk is a pointer above that base which is unmapped, and it is accepted for the same
 * reason the panic-argument reads accept it: everything above this point is already in `g_kv_buf`,
 * `entry_epilogue` writes the whole buffer out, and a nested fault still runs that epilogue - so a
 * bad address costs the rest of the keys and not the report.
 */
static int
entry_kernel_ptr(uintptr_t p)
{
    return ((p & 0x3u) == 0u) && (p >= (uintptr_t)__entry_text_start);
}

static uint32_t
entry_kernel_word(uintptr_t p)
{
    return entry_kernel_ptr(p) ? entry_word_at(p) : 0u;
}

/*
 * ------------------------------------------------------- the tree, replayed on the device's bytes
 *
 * 442 established two things and left one address between them. The walk **had** the blob — its root
 * read `0x80900000` with 4 properties, 0x15 children and a first property named `"name"` — and the
 * address it died on, `0x8090cae0`, is **not** one `next_prop` can produce over the blob's own bytes
 * (`tools/xnu_dt_walk.py --landings`: 603 landings, every one of them a node header, a property
 * header or the tree's end, the highest exactly the tree's `0x7358`, and none at `0xcae0`). Both
 * readings are about the file. Neither is about the memory the walk was reading, and the host dump
 * cannot see the difference.
 *
 * So this replays XNU's walk on the device's own bytes and hashes them in the same pass:
 *
 *   **the replay** is a walk that ends where the host's walk ends (0x7358, 26 nodes, 629 properties)
 *   or records the offset where it stopped and which of XNU's conditions stopped it. A walk that
 *   ends at the same offset having seen the same counts **is** the host's walk, on the same bytes;
 *
 *   **the hash** is what makes "the same bytes" a statement rather than an inference, because a
 *   replay can agree on structure while a value inside a property differs, and a `length` is a value
 *   inside a property;
 *
 *   and the two together are what separates the two explanations 442 left: equal structure and equal
 *   hash and the bytes are the file's, so the panicking walk was handed a pointer off this tree and
 *   the frontier is the **caller**; anything else and the first chunk that differs bounds where the
 *   **writer** worked.
 *
 * The hash is FNV-1a 32 — offset basis 2166136261, prime 16777619 — over exactly the bytes the
 * replay consumed, `[root, root + end)`, and then over each eighth of that same range: a whole-range
 * mismatch says *that* the bytes differ and the chunk hashes say *where*, and both come out of one
 * pass. `tools/xnu_dt_walk.py --fnv` computes the identical numbers over the blob, so the comparison
 * is a diff of two blocks of text rather than a number checked by eye.
 *
 * Three choices worth stating, because each of them is a way this could have been wrong:
 *
 *   **The reads are bounded by the linear map, and the bound is derived** — see the block below.
 *
 *   **The overflow test is on absolute addresses, not offsets.** XNU's check is
 *   `os_add3_overflow(prop, prop->length, sizeof(DeviceTreeNodeProperty))` on the pointer.
 *   `0xff000000` added to an offset of `0x1234` does not carry and added to `0x80901234` does, so an
 *   offset-space test would walk straight past 442's own panic and report a clean tree.
 *
 *   **The property loop is the host tool's, step for step** — the advance runs for every property,
 *   the overflow test only from the second on, because `DTInitPropertyIterator` sets the first
 *   property as `entry + 1` and `next_prop` is never called for it. A replay that used its own
 *   arithmetic would be comparing the host against a second implementation of the walk instead of
 *   against the device's memory.
 */
#define ENTRY_DT_REPLAY_CAP 0x4000u      /* nodes + properties; the blob's tree has 655 */
#define ENTRY_DT_REPLAY_DEPTH 16u        /* the blob's tree is 3 deep */
#define ENTRY_DT_MEM_SIZE_MAX 0x40000000ul   /* `arm_vm_init.c`'s MEM_SIZE_MAX */

#define ENTRY_DT_STOP_NONE     0u        /* the walk finished */
#define ENTRY_DT_STOP_OVERFLOW 1u        /* XNU's os_add3_overflow: the panic, at `stop` */
#define ENTRY_DT_STOP_CAP      2u        /* the step cap */
#define ENTRY_DT_STOP_DEPTH    3u        /* the depth cap */
#define ENTRY_DT_STOP_RANGE    4u        /* a read below gVirtBase or above the linear map */

#define ENTRY_FNV_BASIS 2166136261u
#define ENTRY_FNV_PRIME 16777619u

/*
 * The reads are bounded by the linear map, and the bound is derived rather than written down.
 *
 * The host tool can refuse a landing past `len(blob)`; the device cannot, because it does not have a
 * `len(blob)` — but it does not need one. Within `[gVirtBase, gVirtBase + MEM_SIZE_MAX)` every
 * address is XNU's own linear map of physical memory and therefore readable, which is the same fact
 * that lets this file read the kernel's bootstrap stack at `0xc2013000`. `MEM_SIZE_MAX` is
 * `0x40000000` in `osfmk/arm/arm_vm_init.c` — the same constant `:505` turns into
 * `virtual_space_start`, which is why 441's bootstrap stack moved to `0xc0000000` and above.
 *
 * A bound here has to exist for one case the overflow check does not cover: the **first** property of
 * a node is set by `DTInitPropertyIterator` as `entry + 1` and never passes through `next_prop`, so
 * on the host the `limit` is what refuses a first property whose `length` is enormous, and XNU's own
 * overflow check only sees it one iteration later. Outside the map the replay stops with
 * `ENTRY_DT_STOP_RANGE`, and `xnu_entry_dt_map_base` beside it says which range it was using — so a
 * refusal is a reading rather than a silence.
 */
extern unsigned long gVirtBase;

struct entry_dt_replay {
    uint32_t nodes;
    uint32_t props;
    uint32_t steps;
    uint32_t end;                        /* where the walk finished, relative to `root` */
    uint32_t stop;                       /* the offset a condition fired at */
    uint32_t stop_kind;
    uint32_t hash;                       /* FNV-1a over [root, root + end) */
    uint32_t chunk_hash[8];
    uint32_t chunk_bytes;                /* ceil(end / 8), the chunk width both sides use */
    uint32_t map_base;                   /* gVirtBase, as the range check read it */
    /*
     * The trace, which is what the counts cannot say. `node_n` counts every node visited and the
     * three arrays hold the first `ENTRY_DT_TRACE_N` of them with the header words **as read**; the
     * ring holds the last `ENTRY_DT_TRACE_N` property reads as (header offset, length as read), in
     * the order they happened.
     *
     * The totals bound where the device's walk diverges from the host's; the trace names it. A node
     * whose recorded `nProperties` is not the host's says the clobber is that node's header; a ring
     * entry whose offset is not a property header in the host's tree, or whose length is not the
     * host's length there, says the clobber is that header - and either way the offset is exact.
     */
    uint32_t node_n;
    uint32_t node_off[16];
    uint32_t node_props[16];
    uint32_t node_child[16];
    uint32_t ring_n;
    uint32_t ring_off[16];
    uint32_t ring_len[16];
};

#define ENTRY_DT_TRACE_N 16u

static uint32_t
entry_dt_fnv(uint32_t h, uint32_t byte)
{
    h ^= byte & 0xFFu;
    return h * ENTRY_FNV_PRIME;
}

static int
entry_dt_addr_ok(uintptr_t p)
{
    return (p >= (uintptr_t)gVirtBase) && ((p - (uintptr_t)gVirtBase) < ENTRY_DT_MEM_SIZE_MAX);
}

static uint32_t
entry_dt_replay_node(uintptr_t base, uint32_t off, uint32_t depth, struct entry_dt_replay *r)
{
    uint32_t nprops, nchildren, p, i;

    if (r->stop_kind != ENTRY_DT_STOP_NONE) {
        return off;
    }
    if (depth > ENTRY_DT_REPLAY_DEPTH) {
        r->stop_kind = ENTRY_DT_STOP_DEPTH;
        r->stop = off;
        return off;
    }

    if (++r->steps > ENTRY_DT_REPLAY_CAP) {
        r->stop_kind = ENTRY_DT_STOP_CAP;
        r->stop = off;
        return off;
    }
    if (!entry_dt_addr_ok(base + off + 7u)) {
        r->stop_kind = ENTRY_DT_STOP_RANGE;
        r->stop = off;
        return off;
    }

    nprops = entry_word_at(base + off);
    nchildren = entry_word_at(base + off + 4u);
    r->nodes++;
    if (r->node_n < ENTRY_DT_TRACE_N) {
        r->node_off[r->node_n] = off;
        r->node_props[r->node_n] = nprops;
        r->node_child[r->node_n] = nchildren;
    }
    r->node_n++;

    p = off + 8u;
    for (i = 0u; i < nprops; i++) {
        uintptr_t a, t;
        uint32_t length;

        if (r->stop_kind != ENTRY_DT_STOP_NONE) {
            return p;
        }
        if (++r->steps > ENTRY_DT_REPLAY_CAP) {
            r->stop_kind = ENTRY_DT_STOP_CAP;
            r->stop = p;
            return p;
        }
        if (!entry_dt_addr_ok(base + p + 35u)) {
            r->stop_kind = ENTRY_DT_STOP_RANGE;
            r->stop = p;
            return p;
        }

        r->props++;
        length = entry_word_at(base + p + 32u);

        /* Recorded before the check, so the pair that kills the walk is in the ring. */
        r->ring_off[r->ring_n & (ENTRY_DT_TRACE_N - 1u)] = p;
        r->ring_len[r->ring_n & (ENTRY_DT_TRACE_N - 1u)] = length;
        r->ring_n++;

        a = base + p;
        t = a + length;
        if ((i > 0u) && ((t < a) || ((t + 36u) < t))) {
            r->stop_kind = ENTRY_DT_STOP_OVERFLOW;
            r->stop = p;
            return p;
        }

        p = p + 36u + ((length + 3u) & ~3u);
    }

    for (i = 0u; i < nchildren; i++) {
        p = entry_dt_replay_node(base, p, depth + 1u, r);
        if (r->stop_kind != ENTRY_DT_STOP_NONE) {
            return p;
        }
    }

    return p;
}

/*
 * FNV-1a over `[base, base + n)`, whole and in eighths, in one pass and with no division: the chunk
 * width is `ceil(n / 8)` and the last chunk ends exactly at `n`. `tools/xnu_dt_walk.py --fnv` uses
 * the same width, which is the only thing that has to agree for the two blocks to be comparable.
 */
static void
entry_dt_hash(uintptr_t base, uint32_t n, struct entry_dt_replay *r)
{
    uint32_t c, off;

    r->chunk_bytes = (n + 7u) >> 3;
    r->hash = ENTRY_FNV_BASIS;
    for (c = 0u; c < 8u; c++) {
        r->chunk_hash[c] = ENTRY_FNV_BASIS;
    }

    off = 0u;
    for (c = 0u; c < 8u; c++) {
        uint32_t end = (c + 1u) * r->chunk_bytes;

        if (end > n) {
            end = n;
        }
        for (; off < end; off++) {
            uint32_t b = (uint32_t)*(const volatile unsigned char *)(base + off);

            r->hash = entry_dt_fnv(r->hash, b);
            r->chunk_hash[c] = entry_dt_fnv(r->chunk_hash[c], b);
        }
    }
}

/*
 * XNU's own accessor for the root of the tree it is walking.
 *
 * `DTRootNode` is `static` in `pexpert/gen/device_tree.c`, so it cannot be named from here, and an
 * `nm` literal for its address is exactly the kind of second definition of one value this file has
 * spent three experiments removing. `DTLookupEntry(NULL, "/", &root)` reads it without either: with
 * `searchPoint == NULL` the function takes `DTRootNode` (`:226-229`) and the path `"/"` returns it on
 * the second statement (`*cp` is 0 after the separator, `:230-235`). One call, no walk, no
 * allocation, no kalloc.
 */
extern int DTLookupEntry(const void *searchPoint, const char *pathName, void **foundEntry);

/* 475: the mode decision, kept as a reading - 477 makes the vector page do the branching, and this
 * global is what says whether the split fired. See the block in `fleh_undef`. */
static uint32_t g_undef_user;
static uint32_t g_undef_user_seq;

void fleh_undef(void)
{
    uintptr_t frame;
    uint32_t lr_undef, spsr;
    uint32_t r_fmt, r_args, r_sl;
    uint32_t ap, element, zone_name;
    uint32_t i;
    int args_ok;
    uint32_t root;
    uint32_t c;
    struct entry_dt_replay replay;

    __asm__ volatile ("mov %0, sp" : "=r"(frame));

    __asm__ volatile ("mov %0, lr" : "=r"(lr_undef));
    __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));

    /*
     * 461: the count, then every key below through `entry_panic_kv` rather than `entry_kv`.
     *
     * This is the trap's report and it must not share a buffer with the trace: 459's run had the
     * tracer's 8192 bytes full at 8170 when this handler ran, so all 47 of the keys that follow were
     * written into a full buffer and dropped, and the run ended without naming the instruction it
     * had stopped on (`xnu_entry_kv_dropped = 0x66bb` is that silence, counted). The buffer this
     * writes to is `g_panic_buf`, sized for the whole record, and the epilogue prints it under a
     * heading of its own.
     */
    g_panic_entered++;

    entry_panic_kv("xnu_entry_undef_lr", lr_undef);
    entry_panic_kv("xnu_entry_undef_pc", lr_undef - 4);
    entry_panic_kv("xnu_entry_undef_spsr", spsr);

    /*
     * ------------------------------------------------- 477: the mode is a reading, not a branch
     *
     * **This function is now reached by the kernel's `udf` only, and the test below is what says so
     * rather than what decides it.** 475 branched here: a user-mode `udf` was recorded on the live
     * channel and then handed to Apple's `locore_fleh_undef` by a `bl`. 476's run measured what that
     * costs, and it cost the boot:
     *
     *   - the fifth abort is a **user-mode instruction fetch of `0x80006d04`**, with `cpsr = 0x10`
     *     and `lr = 0`, `IFSR = 0xd` (permission fault, section - a user fetch of a supervisor-only
     *     section);
     *   - `0x80006d04` is the `bl locore_fleh_undef` *in this file*, and the reason the resumed PC is
     *     that address and not the `bl`'s return address is Apple's own first instruction:
     *     `locore_fleh_undef` does `mrs sp, SPSR`, `tst sp, #32`, `subeq lr, lr, #4`
     *     (`0x8001402c`), and `undef_from_user` stores that `lr` into `SS_PC`. On a vector entry `lr`
     *     is `pc + 4` and the subtraction recovers the trapping instruction; on a `bl` from C it is
     *     this image's return address, one past the `bl` itself.
     *
     * So the kernel resumed process 1 *at this image's own instruction*, in user mode, and the
     * prefetch abort that followed was the instrument's fault and not the kernel's. **A user `udf`
     * cannot be reached by a call at all** - `lr` is the one register Apple's entry depends on - and
     * the split now happens in the vector page, before any C code runs: `vec_tramp_1` in
     * `entry_vectors.s` tests the interrupted mode with `mrs sp, spsr` (the banked register, so
     * nothing of the interrupted context is touched) and branches to `locore_fleh_undef` for user
     * mode and to this function for kernel mode.
     *
     * **The mode is still recorded, and now it is a falsifier.** This handler should never see a user
     * `udf` again; if `xnu_entry_undef_user` reads 1 in a later run, the trampoline's test did not
     * fire (a vector page that is not this file's, an `SPSR` this image does not expect) and the run
     * says so rather than proceeding into the panic walk as if the frame were `panic`'s. What it does
     * *not* do is forward: the walk below is written for `panic`'s frame and 474 measured what it does
     * with a user's, so a user mode here reports and stops, which is the least that can be said about
     * a state this image is not supposed to be in.
     *
     * **The reading the forward used to produce now comes from `sleh_undef` itself** -
     * `entry_trace.c`'s `__wrap_sleh_undef`, which sees every undefined instruction the kernel is
     * about to handle, user or kernel, with the frame in hand, and writes the same four live keys
     * (`xnu_live_undef_user_seq`, `_pc`, `_lr`, `_spsr`). It is 467's arrangement for `sleh_abort`
     * applied to the other trap, and it does not need the handler to be this image's.
     */
    g_undef_user = ((spsr & STAGE90_PSR_MODE_MASK) == STAGE90_PSR_USER_MODE) ? 1u : 0u;
    entry_panic_kv("xnu_entry_undef_user", g_undef_user);
    if (g_undef_user != 0u) {
        g_undef_user_seq++;
        entry_live_write("xnu_live_undef_user_seq", g_undef_user_seq);
        entry_epilogue("exception: undefined instruction in user mode reached this handler, so the"
                       " vector page's slot-1 split did not fire");
    }

    entry_panic_kv("xnu_entry_panic_str", (uint32_t)(uintptr_t)debugger_panic_str);
    entry_panic_kv("xnu_entry_panic_caller", (uint32_t)debugger_panic_caller);
    entry_panic_kv("xnu_entry_panic_message", (uint32_t)(uintptr_t)debugger_message);

    __asm__ volatile ("mov %0, r9" : "=r"(r_fmt));
    __asm__ volatile ("mov %0, r8" : "=r"(r_args));
    __asm__ volatile ("mov %0, r10" : "=r"(r_sl));

    entry_panic_kv("xnu_entry_trap_r9_fmt", r_fmt);
    entry_panic_kv("xnu_entry_trap_r8_args", r_args);
    /*
     * **`sl` is not the caller, and the old key name said it was.** `panic_trap_to_debugger` loads
     * `r4` and `sl` from `[sp+56]` and `[sp+60]` and hands both to `DebuggerTrapWithState` as the
     * two halves of the 64-bit `db_panic_options` - `stm sp, {r4, sl}` at the call - and the caller
     * is `r5`, loaded from `[sp+64]` and stored at `[sp+12]`. So this key is the options mask's
     * high word, and zero here says the mask `panic()` passed is zero. The caller is read out of
     * the frame below, which is the only way to get it: `fleh_undef`'s own prologue destroys `r5`
     * with `mrs r5, SPSR`.
     */
    entry_panic_kv("xnu_entry_trap_sl_options_hi", r_sl);

    entry_panic_kv("xnu_entry_zone_map_min", zone_map_min_address);
    entry_panic_kv("xnu_entry_zone_map_max", zone_map_max_address);
    /*
     * **Both read zero on the device, and that is not a missing object.** `zone_init` is the only
     * writer of either (`zalloc.c:2958-2959`, against the zero-initialized declarations at
     * `zalloc.c:340-341`), its only caller is `vm_mem_bootstrap+0x204`, and the path to that call
     * goes through two stubs first - `kmem_init` at `+0x074` and `kext_alloc_init` at `+0x1a4` -
     * while `zone_init`'s own first call is `kmem_suballoc`, also a stub. A stub ends the run, so
     * `zone_init` has never executed in this image. The image's `zone_init` really does store them
     * (`str r1, [r0]` at `+0x94` and `str r2, [r0]` at `+0xac`), so a non-zero reading was possible
     * and the zero is the boot's answer.
     *
     * **Experiment 244 makes `kmem_init` real and `kmem_suballoc` with it** - both are in
     * `osfmk_vm_vm_kern.o`, so the first of the two stubs above is gone and `zone_init`'s own first
     * call is answered too. What is left in the path is `kext_alloc_init`, whose object is not
     * linked yet. The two readings above are therefore expected to stay zero for one more run, and
     * the run after that is the one that could change them.
     *
     * (The stub sizes in this comment read "12-byte" before 244 and "16-byte" after it: the
     * generated stubs gained a `mov r1, lr` for the caller argument. The count of stubs on the path,
     * which is what the sentence is about, did not change.)
     *
     * What that means is in `is_sane_zone_ptr` (`zalloc.c:1071-1077`): with
     * `zone_map_min_address == zone_map_max_address == 0` the range test cannot pass for any
     * non-zero element. But in this image it is not the range test that fires - it is the one
     * *before* it, `pmap_kernel_va`, which is `[0x80000000, 0xFFFEFFFF]` at compile time and false
     * for every address this image uses. See the module comment above `fleh_undef`, and experiment
     * 239 for the arithmetic.
     */

    /*
     * **The frame, in words - and the offsets this comment used to carry were another image's.**
     * `fleh_undef`'s prologue in the image that ran experiment 362 is
     *
     *   80002cf4: strd r4, [sp, #-24]!     ; r4 at +0, r5 at +4
     *   80002cf8: strd r6, [sp, #8]        ; r6 at +8, r7 at +12
     *   80002cfc: str r8, [sp, #16]
     *   80002d00: str lr, [sp, #20]        ; <- the sixth store, added since the table below was written
     *   80002d04: mov r4, sp               ; the captured `sp` is the frame base
     *   80002d08: mov r5, lr               ; not `mrs r5, SPSR`: that is `r6` two instructions later
     *
     * so the block is the trapping context's `r4`, `r5`, `r6`, `r7`, `r8` and **`lr`**, and the words
     * after them are this function's own uninitialized stack. What the keys mean, therefore:
     *
     *   frame_r4   +0   `db_panic_options`'s low half (0 from `IOPanicPlatform::start`)
     *   frame_r5   +4   `db_proceed_on_sync_failure` (1 from `panic`) - *not* the panic caller
     *   frame_r6   +8   `ctx`
     *   frame_r7   +12  `reason`
     *   frame_r8   +16  `panic_args`, a `va_list *`
     *   frame_r9   +20  the trapping context's `lr`, i.e. `undef_lr` over again
     *   frame_sl   +24  uninitialized stack - the name is kept for log continuity and the value is junk
     *   frame_lr   +28  uninitialized stack, likewise
     *
     * **The check is the values agreeing with each other, and in experiment 362 they did**: `frame_r9 =
     * 0x8002dd8c` equal to `undef_lr`, `frame_r8 = 0xc80abeb0` equal to the live `trap_r8_args`,
     * `frame_r4 = 0` the options mask, `frame_r5 = 1` `db_proceed_on_sync_failure`. The same run shows
     * the cost of the stale names: `frame_sl` read `0x253`, and a reader who trusted the old table
     * would have called that the options mask's high half while `trap_sl_options_hi` read 0. The caller
     * of `panic` is *not* recoverable from this block in the current image - it was `frame_r5` when the
     * prologue had five stores - and the trap's `r9` (the panic format) is what names the call site
     * instead: in 362 it resolved to the literal `IOPanicPlatform::start` loads into `r0`.
     *
     * A key that comes back with the wrong value is a shift in the block, and `frame_sp` plus the names
     * say which shift - but only while the names match the prologue. Reading the block is safe for the
     * same reason everything else here is read: this is the boot stack, inside the image, and it is
     * mapped. Only the *values* are reported; nothing in the block is dereferenced.
     */
    entry_panic_kv("xnu_entry_frame_sp", (uint32_t)frame);
    {
        static const char *const fr[8] = {
            "xnu_entry_frame_r4", "xnu_entry_frame_r5", "xnu_entry_frame_r6",
            "xnu_entry_frame_r7", "xnu_entry_frame_r8", "xnu_entry_frame_r9",
            "xnu_entry_frame_sl", "xnu_entry_frame_lr",
        };

        for (i = 0u; i < 8u; i++) {
            entry_panic_kv(fr[i], entry_word_at(frame + (i * 4u)));
        }
    }

    /*
     * **The two arguments, one dereference further on than experiment 239 read them.** `r_args` is
     * a `va_list *`, so the word at it is the list's `__ap` - a pointer to the first vararg - and
     * not the first argument. Experiment 239 measured that word as `0x0029be94`, twelve bytes past
     * `r_args`, which is exactly where `panic`'s frame puts `__ap` (the list at `sp+16`, the three
     * spilled varargs at `sp+28`). So the first vararg is at `ap` and the second four bytes later:
     *
     *   arg0 = *ap        for the panic that found this, `prop`  - a `DeviceTreeNodeProperty *`
     *   arg1 = *(ap + 4)  the second one, `prop->length`
     *
     * **The element is reported and never dereferenced**, which is why `entry_image_ptr` is not
     * widened for it: `0x40400000` is outside the image and may be unmapped, and a data abort
     * inside the abort handler says nothing at all. `ap` and `zone_name` are both expected inside
     * the image, so the guard that is already here covers every read that happens.
     *
     * **That paragraph was written for `free_to_zone`, and it is the second thing 441 corrects.**
     * The two keys below are named for the panic that first used them - a zone element and a zone
     * name - and for any other panic they hold whatever its first two arguments are. For the one
     * that stopped experiment 440 they are `prop` and `prop->length`, so **the answer was in the
     * log all along, under two names that said it was about zones.** `xnu_entry_panic_arg0` and
     * `xnu_entry_panic_arg1` are therefore logged first and under the generic names, and the two
     * old keys are kept after them, with the values they always had, because three experiments
     * quoted the pair and renaming a key is a discontinuity a reader has to be told about.
     *
     * The reads are gated by `entry_panic_*` rather than `entry_image_ptr`; see the guard's comment
     * above for why the image bound is the wrong question for the stack these live on, and for what
     * `xnu_entry_panic_args_page` means when it is zero.
     */
    ap = 0u;
    element = 0u;
    zone_name = 0u;
    args_ok = entry_panic_args_page((uintptr_t)r_args);
    /* 475: the decision, so that a refusal and an accepted NULL cannot publish the same number. */
    entry_panic_kv("xnu_entry_panic_args_ok", args_ok ? 1u : 0u);
    entry_panic_kv("xnu_entry_panic_args_page",
             args_ok ? (uint32_t)((uintptr_t)r_args & ~(uintptr_t)0xFFFu) : 0u);
    if (entry_image_ptr((uintptr_t)r_args) ||
        (args_ok && entry_panic_arg_word((uintptr_t)r_args, (uintptr_t)r_args, 4u))) {
        ap = entry_word_at((uintptr_t)r_args);
    }
    entry_panic_kv("xnu_entry_panic_ap", ap);
    entry_panic_kv("xnu_entry_panic_ap_delta",
             (ap >= r_args) ? (ap - r_args) : 0u);
    if (args_ok && entry_panic_arg_word((uintptr_t)r_args, (uintptr_t)ap, 8u)) {
        element = entry_word_at((uintptr_t)ap);
        zone_name = entry_word_at((uintptr_t)ap + 4u);
    }
    entry_panic_kv("xnu_entry_panic_arg0", element);
    entry_panic_kv("xnu_entry_panic_arg1", zone_name);
    entry_panic_kv("xnu_entry_panic_element", element);
    entry_panic_kv("xnu_entry_panic_zonename", zone_name);
    if (entry_image_ptr((uintptr_t)zone_name)) {
        entry_panic_kv("xnu_entry_zone_name_w0", entry_word_at((uintptr_t)zone_name));
        entry_panic_kv("xnu_entry_zone_name_w1", entry_word_at((uintptr_t)zone_name + 4u));
    }

    /*
     * ------------------------------------------------------------------ what the walk was handed
     *
     * 441 measured the panic's two operands and they came back `prop = 0x8090cae0`,
     * `length = 0xff000000` — a pointer 0x5788 bytes past the end of a 0x7358-byte tree, into the
     * part of the buffer `xnu_entry_copy_device_tree` never wrote. That says the walk left the tree.
     * It does not say **whether it left a correct tree, or started outside one**, because both leave
     * the same pointer at the end: one is a walk that drifted inside a node, the other is a walk
     * handed a root that is not the tree. 440's doc chose between them by *reachability* — it named
     * `DTIterateProperties` because that function has exactly one caller in the image — and `prop`
     * does not settle it either, because `next_prop`'s four inlined call sites all hand it the same
     * kind of pointer.
     *
     * **The one number that separates them is what `DTInit` was given.**
     */
    root = 0u;
    (void)DTLookupEntry((const void *)0, "/", (void **)&root);
    entry_panic_kv("xnu_entry_dt_root", root);
    entry_panic_kv("xnu_entry_dt_root_nprops", entry_kernel_word((uintptr_t)root));
    entry_panic_kv("xnu_entry_dt_root_nchildren", entry_kernel_word((uintptr_t)root + 4u));
    /*
     * A root of 0x80900000 whose own two words read 4 and 0x15 is the blob's root: the same two
     * numbers `tools/xnu_dt_walk.py --verbose` prints for offset 0, and the same two the payload
     * logged as `apple_dt_root_props` / `apple_dt_root_children`. Anything else and the walk was
     * handed a different tree, which is the whole answer.
     *
     * The four words of the root's first property header are the check on top of the counts, because
     * counts can coincide: the blob's first property name is a C string this project wrote, so these
     * sixteen bytes are a byte-for-byte comparison against the host dump's own first header.
     */
    entry_panic_kv("xnu_entry_dt_first_prop_w0", entry_kernel_word((uintptr_t)root + 8u));
    entry_panic_kv("xnu_entry_dt_first_prop_w1", entry_kernel_word((uintptr_t)root + 12u));
    entry_panic_kv("xnu_entry_dt_first_prop_w2", entry_kernel_word((uintptr_t)root + 16u));
    entry_panic_kv("xnu_entry_dt_first_prop_w3", entry_kernel_word((uintptr_t)root + 20u));

    /*
     * And the 36 bytes at `prop` itself — read **because they are known mapped**, not because a bound
     * allows it. `next_prop` read `prop->length` at `prop + 32` before it panicked, so
     * `prop .. prop + 35` has just been read by the kernel; the thirty-two below are a subset.
     *
     * A printable name followed by a small length is a property header that something walked to by
     * mistake - which is the drift case, and the name says which property. Anything else is the
     * arithmetic garbage of a pointer that has been adding lengths to itself for 0x5788 bytes, and
     * then the drift started earlier than this address and the root above says where.
     *
     * `entry_kernel_ptr` is still the gate, for the one case the argument above does not cover: an
     * `element` of zero, which is what a refused argument read leaves, and which would make the first
     * of these a read of address 0. The refusal is not silent - `xnu_entry_panic_arg0` is logged
     * above and is the same number.
     */
    if (entry_kernel_ptr((uintptr_t)element)) {
        static const char *const pw[8] = {
            "xnu_entry_panic_prop_w0", "xnu_entry_panic_prop_w1",
            "xnu_entry_panic_prop_w2", "xnu_entry_panic_prop_w3",
            "xnu_entry_panic_prop_w4", "xnu_entry_panic_prop_w5",
            "xnu_entry_panic_prop_w6", "xnu_entry_panic_prop_w7",
        };

        for (i = 0u; i < 8u; i++) {
            entry_panic_kv(pw[i], entry_word_at((uintptr_t)element + (i * 4u)));
        }
    }

    /*
     * ------------------------------------------------- the tree, replayed on the device's own bytes
     *
     * The two questions 442 left are about the *memory* and not about the file, so the answer has to
     * be taken here: a walk over the bytes at `root`, with XNU's formula, and a hash of the bytes it
     * consumed. The keys are printed unconditionally, zeros included, because a walk that did not run
     * has to be distinguishable from one that ran and found nothing — `xnu_entry_dt_root` is logged
     * above and is the gate's own report.
     *
     * `xnu_entry_dt_replay_end` is where the walk stopped, so it is the tree's length **only** when
     * `xnu_entry_dt_replay_stop_kind` is 0. The host's values to compare against come from
     * `tools/xnu_dt_walk.py --fnv`: 26 nodes, 629 properties, end `0x7358`, and the same nine hashes
     * over the same range.
     */
    replay.nodes = 0u;
    replay.props = 0u;
    replay.steps = 0u;
    replay.end = 0u;
    replay.stop = 0u;
    replay.stop_kind = ENTRY_DT_STOP_NONE;
    replay.hash = 0u;
    replay.chunk_bytes = 0u;
    replay.map_base = (uint32_t)gVirtBase;
    replay.node_n = 0u;
    replay.ring_n = 0u;
    for (c = 0u; c < 8u; c++) {
        replay.chunk_hash[c] = 0u;
    }
    for (c = 0u; c < ENTRY_DT_TRACE_N; c++) {
        replay.node_off[c] = 0u;
        replay.node_props[c] = 0u;
        replay.node_child[c] = 0u;
        replay.ring_off[c] = 0u;
        replay.ring_len[c] = 0u;
    }

    if (entry_kernel_ptr((uintptr_t)root)) {
        replay.end = entry_dt_replay_node((uintptr_t)root, 0u, 0u, &replay);
        entry_dt_hash((uintptr_t)root, replay.end, &replay);
    }

    entry_panic_kv("xnu_entry_dt_map_base", replay.map_base);
    entry_panic_kv("xnu_entry_dt_replay_nodes", replay.nodes);
    entry_panic_kv("xnu_entry_dt_replay_props", replay.props);
    entry_panic_kv("xnu_entry_dt_replay_steps", replay.steps);
    entry_panic_kv("xnu_entry_dt_replay_end", replay.end);
    entry_panic_kv("xnu_entry_dt_replay_stop", replay.stop);
    entry_panic_kv("xnu_entry_dt_replay_stop_kind", replay.stop_kind);
    entry_panic_kv("xnu_entry_dt_checksum", replay.hash);
    entry_panic_kv("xnu_entry_dt_chunk_bytes", replay.chunk_bytes);
    {
        static const char *const ck[8] = {
            "xnu_entry_dt_chunk0", "xnu_entry_dt_chunk1",
            "xnu_entry_dt_chunk2", "xnu_entry_dt_chunk3",
            "xnu_entry_dt_chunk4", "xnu_entry_dt_chunk5",
            "xnu_entry_dt_chunk6", "xnu_entry_dt_chunk7",
        };

        for (c = 0u; c < 8u; c++) {
            entry_panic_kv(ck[c], replay.chunk_hash[c]);
        }
    }

    /*
     * The trace: the first sixteen nodes with their header words as read, and the last sixteen
     * property reads as (offset, length). Both are compared against the tables `tools/xnu_dt_walk.py
     * --verbose` prints; the totals above bound where the two walks part and this names it.
     */
    entry_panic_kv("xnu_entry_dt_node_count", replay.node_n);
    {
        static const char *const no[ENTRY_DT_TRACE_N] = {
            "xnu_entry_dt_node0_off", "xnu_entry_dt_node1_off", "xnu_entry_dt_node2_off",
            "xnu_entry_dt_node3_off", "xnu_entry_dt_node4_off", "xnu_entry_dt_node5_off",
            "xnu_entry_dt_node6_off", "xnu_entry_dt_node7_off", "xnu_entry_dt_node8_off",
            "xnu_entry_dt_node9_off", "xnu_entry_dt_node10_off", "xnu_entry_dt_node11_off",
            "xnu_entry_dt_node12_off", "xnu_entry_dt_node13_off", "xnu_entry_dt_node14_off",
            "xnu_entry_dt_node15_off",
        };
        static const char *const np[ENTRY_DT_TRACE_N] = {
            "xnu_entry_dt_node0_props", "xnu_entry_dt_node1_props", "xnu_entry_dt_node2_props",
            "xnu_entry_dt_node3_props", "xnu_entry_dt_node4_props", "xnu_entry_dt_node5_props",
            "xnu_entry_dt_node6_props", "xnu_entry_dt_node7_props", "xnu_entry_dt_node8_props",
            "xnu_entry_dt_node9_props", "xnu_entry_dt_node10_props", "xnu_entry_dt_node11_props",
            "xnu_entry_dt_node12_props", "xnu_entry_dt_node13_props", "xnu_entry_dt_node14_props",
            "xnu_entry_dt_node15_props",
        };
        static const char *const nc[ENTRY_DT_TRACE_N] = {
            "xnu_entry_dt_node0_child", "xnu_entry_dt_node1_child", "xnu_entry_dt_node2_child",
            "xnu_entry_dt_node3_child", "xnu_entry_dt_node4_child", "xnu_entry_dt_node5_child",
            "xnu_entry_dt_node6_child", "xnu_entry_dt_node7_child", "xnu_entry_dt_node8_child",
            "xnu_entry_dt_node9_child", "xnu_entry_dt_node10_child", "xnu_entry_dt_node11_child",
            "xnu_entry_dt_node12_child", "xnu_entry_dt_node13_child", "xnu_entry_dt_node14_child",
            "xnu_entry_dt_node15_child",
        };

        for (c = 0u; c < ENTRY_DT_TRACE_N; c++) {
            entry_panic_kv(no[c], replay.node_off[c]);
            entry_panic_kv(np[c], replay.node_props[c]);
            entry_panic_kv(nc[c], replay.node_child[c]);
        }
    }
    entry_panic_kv("xnu_entry_dt_ring_count", replay.ring_n);
    {
        static const char *const ro[ENTRY_DT_TRACE_N] = {
            "xnu_entry_dt_ring0_off", "xnu_entry_dt_ring1_off", "xnu_entry_dt_ring2_off",
            "xnu_entry_dt_ring3_off", "xnu_entry_dt_ring4_off", "xnu_entry_dt_ring5_off",
            "xnu_entry_dt_ring6_off", "xnu_entry_dt_ring7_off", "xnu_entry_dt_ring8_off",
            "xnu_entry_dt_ring9_off", "xnu_entry_dt_ring10_off", "xnu_entry_dt_ring11_off",
            "xnu_entry_dt_ring12_off", "xnu_entry_dt_ring13_off", "xnu_entry_dt_ring14_off",
            "xnu_entry_dt_ring15_off",
        };
        static const char *const rl[ENTRY_DT_TRACE_N] = {
            "xnu_entry_dt_ring0_len", "xnu_entry_dt_ring1_len", "xnu_entry_dt_ring2_len",
            "xnu_entry_dt_ring3_len", "xnu_entry_dt_ring4_len", "xnu_entry_dt_ring5_len",
            "xnu_entry_dt_ring6_len", "xnu_entry_dt_ring7_len", "xnu_entry_dt_ring8_len",
            "xnu_entry_dt_ring9_len", "xnu_entry_dt_ring10_len", "xnu_entry_dt_ring11_len",
            "xnu_entry_dt_ring12_len", "xnu_entry_dt_ring13_len", "xnu_entry_dt_ring14_len",
            "xnu_entry_dt_ring15_len",
        };

        /*
         * Oldest of the ring first: index `(ring_n + c) & 15` is the c-th oldest read, and with
         * fewer than sixteen reads the ring is not yet wrapped and the offsets are still in order.
         */
        for (c = 0u; c < ENTRY_DT_TRACE_N; c++) {
            uint32_t k = (replay.ring_n + c) & (ENTRY_DT_TRACE_N - 1u);

            entry_panic_kv(ro[c], replay.ring_off[k]);
            entry_panic_kv(rl[c], replay.ring_len[k]);
        }
    }

    entry_epilogue("exception: undefined instruction");
}
/*
 * **479: this handler is no longer installed, and it is kept for the same reason the two below it
 * are.** `entry_vectors.s`'s slot 2 is Apple's `locore_fleh_swi`, because process 1's first
 * instruction is now a `svc` (`entry_ramdisk.s`'s five words, Unix syscall 20) and a handler whose only
 * behaviour is to stop the run would stop the boot at that instruction and measure nothing else about
 * it. The one line this function has always been - record that an exception happened, and not even
 * where - is exactly what a syscall must not hit: `fleh_swi` takes no register readings, so neither
 * the syscall vector in `r12` nor the caller would be in the record. The syscall path's own report
 * therefore comes from *inside* Apple's path instead: `--wrap=getpid` puts `entry_trace.c`'s wrapper in
 * `sysent[20].sy_call`, where it sees the answer the kernel wrote into the process's own return slot -
 * see `entry_note_getpid` for the readings and `tools/check_sysent_table.py` for the check that the
 * wrapper really is in the slot.
 *
 * The build check asserts that this address is **not** in the vector table's slot 2, because the
 * failure it guards is silent: a slot that quietly kept this handler looks exactly like a `svc` this
 * image reported and left, and the run's stop would be a perfectly plausible one.
 */
void fleh_swi(void) { entry_epilogue("exception: svc/swi"); }

/*
 * ------------------------------------------------------------------ the abort handlers, named
 *
 * Experiment 241 ended at `exception: data abort` with a decodable `DFSR` (a write, to a level-2
 * page mapping that is not writable) and no way to say *which* instruction - and `DFAR` alone
 * cannot say it, because two very different things are consistent with it: the protection code
 * creating the read-only mapping, and a later write into a region it had already protected. Both
 * are real, linked code in this image.
 *
 * So the abort handlers now report the same three kinds of thing `fleh_undef` does:
 *
 *   **the instruction** - `lr_abt` and `pc_abt`. `LR_abt` is set by the CPU for the abort's *own*
 *   class: for a data abort it is the faulting instruction plus 8, for a prefetch abort plus 4, and
 *   both are the architecture's definitions rather than a guess. `pc_abt` is what to look up in the
 *   disassembly, and the word *at* `pc_abt` is reported too, so a `pc_abt` that is not an
 *   instruction is visible as one.
 *
 *   **the mapping state** - `TTBR0`, `TTBR1`, `TTBCR` and `SCTLR`. These say which page tables were
 *   live and, with `SCTLR`'s `TEX`/`XP`/`AFE` bits, whether the walk that produced this fault is the
 *   one `_start` left or one XNU's pmap built. `TTBR0 == topOfKernelData` means the fault is inside
 *   `_start`'s own tables - which, since those map this window with writable sections, would make a
 *   page permission fault impossible and therefore points at the pmap having taken them over.
 *
 *   **the boundaries XNU computed** - `cpu_ttep`, `avail_start`, `gPhysBase`, `mem_size`. Exp-241's
 *   fault address is 3 MB above the base, inside the range
 *   `arm_vm_init.c:512-530`'s "2 MB + 3 MB per 256 MB segment" pre-initialization loop exists to
 *   cover; these four are the numbers that say whether the address really is in the available range
 *   or above it.
 *
 * Every one of these is read the same way the rest of this file reads: a register, or a word in the
 * image, which is mapped because the handlers could not have run otherwise.
 *
 * Experiment 242 answered that question from the same two registers - `lr_abt - 8` was
 * `pmap_init_pte_static_page`'s first PTE store, into a page-table page that routine had itself
 * filled with read-only entries - and named a cause that is *not* an object and *not* XNU's pmap:
 * this image's Mach-O has no `__PRELINK_TEXT`, so `arm_vm_prot_init`'s call
 * `RWNX(segPRELINKTEXTB + segSizePRELINKTEXT, end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT), ...)`
 * became `RWNX(0, end_kern)` - a 1024-iteration loop taking a page-table page from `avail_start`
 * each time, until one landed on a page the protection pass had already made read-only.
 *
 * **So experiment 243 adds the three lives that call is computed from** - `end_kern`,
 * `segPRELINKTEXTB` and `segSizePRELINKTEXT` - and the header grew the empty `__PRELINK_TEXT`
 * segment that makes the second of them a real address (`entry_macho.s`). The values to expect are
 * in the build's own report and in `tools/host_entry_macho_check.sh`, which now fails on any header
 * for which that range is not the round-up slop of the image's last page:
 *
 *   host         getsegdatafromheader("__PRELINK_TEXT") -> 0x800da248, size 0
 *                getlastaddr() -> 0x800da248, so end_kern = round_page(...) = 0x800db000
 *                the call is RWNX(0x800da248, 0xdb8) - [0x800da248, 0x800db000)
 *
 * so the device should report exactly those three numbers if it aborts again, which would also be
 * the evidence that the header change reached the code. If it does not abort, the keys are never
 * printed and the *absence* of the fault is the result - which is why they are here rather than a
 * claim in a document.
 */
extern uint32_t cpu_ttep;
extern uint32_t avail_start;
extern uint32_t gPhysBase;
extern uint32_t mem_size;
extern uint32_t end_kern;
extern uint32_t segPRELINKTEXTB;
extern uint32_t segSizePRELINKTEXT;

/*
 * **476: this handler is no longer in the vector table.** The prefetch slot now carries Apple's
 * `locore_fleh_prefabt` (`entry_vectors.s`), which reads the fault, calls
 * `sleh_abort(regs, T_PREFETCH_ABT)` and - when the page is paged in - returns through the mode's own
 * `thread_bootstrap_return`, which **retries the fetch**. That is 467's change one slot over, and the
 * reason is 475's run: it entered user mode, had its `udf` handled by the kernel, and then stopped on
 * a prefetch abort that this handler could only record - and the record was refused by the full trace
 * channel, so the run has a stop and no address.
 *
 * **The function is kept, and the build check asserts it is not installed.** Kept because
 * `fleh_dataabt` below has been uninstalled since 467 and is kept for the same reason: the body
 * documents which registers identify a fault of this class and what the page-table state was, and a
 * future step that needs a handler back wants it beside its sibling rather than in a git history.
 * Asserted-not-installed because the failure this replaces is *silent*: a slot that quietly kept the
 * instrument's handler looks exactly like a kernel that refused to service the fault.
 *
 * **What its eleven keys no longer say, named rather than left to be missed.** The live record the
 * kernel's own path produces carries five of the nine numbers (`pc`/`lr`/`cpsr` and the fault pair,
 * read from the instruction-side coprocessor registers by `entry_trace.c`'s wrapper) plus the thread;
 * **TTBR0, TTBR1, TTBCR and SCTLR are not in it**, and 241/242 needed them to tell which page tables
 * were live. A step that needs the mapping state must add it to the wrapper - the keys
 * `xnu_entry_prefetch_abort_*` are now produced by no path at all.
 */
void fleh_prefabt(void)
{
    uint32_t ifar, ifsr, lr_abt, spsr, ttbr0, ttbr1, ttbcr, sctlr;

    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    __asm__ volatile ("mov %0, lr" : "=r"(lr_abt));
    __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ttbr1));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr));
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));

    entry_kv("xnu_entry_prefetch_abort_ifar", ifar);
    entry_kv("xnu_entry_prefetch_abort_ifsr", ifsr);
    entry_kv("xnu_entry_prefetch_abort_lr", lr_abt);
    /* LR_abt - 4 for a prefetch abort: the instruction that could not be fetched. */
    entry_kv("xnu_entry_prefetch_abort_pc", lr_abt - 4u);
    entry_kv("xnu_entry_prefetch_abort_spsr", spsr);
    entry_kv("xnu_entry_prefetch_abort_ttbr0", ttbr0);
    entry_kv("xnu_entry_prefetch_abort_ttbr1", ttbr1);
    entry_kv("xnu_entry_prefetch_abort_ttbcr", ttbcr);
    entry_kv("xnu_entry_prefetch_abort_sctlr", sctlr);
    entry_epilogue("exception: prefetch abort");
}

void fleh_dataabt(void)
{
    uint32_t dfar, dfsr, lr_abt, spsr, ttbr0, ttbr1, ttbcr, sctlr;
    uint32_t pc_abt, insn, sp_now;

    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile ("mov %0, lr" : "=r"(lr_abt));
    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile ("mov %0, sp" : "=r"(sp_now));
    pc_abt = lr_abt - 8u;
    insn = 0u;
    if (entry_image_ptr((uintptr_t)pc_abt)) {
        insn = entry_word_at((uintptr_t)pc_abt);
    }

    /*
     * Everything the handler knows goes into `.bss`, and the handler then goes straight to the
     * epilogue. It makes no `entry_kv` call at all, and that is the whole point of the shape.
     *
     * Experiment 269's run reported `xnu_entry_kv_dropped` = 17 - the number of `entry_kv` calls this
     * function used to make - and a results buffer of 0x1fe4 bytes that was three hundred and
     * thirteen copies of `xnu_entry_data_abort_dfar` with no value after any of them. That is a
     * storm: one of the handler's own records faulted, which re-entered the handler, which wrote
     * another record, which faulted. The measured first entry says `dfsr = 0x05` (translation fault,
     * section, read) and `pc` in the middle of `entry_kv`'s value write, with `dfar` = `first_kv_len
     * + 11` - i.e. the address that store would use if the buffer's base register were zero, which
     * nothing in that function can produce.
     *
     * The 269 canon (first) run's first fault was a *store* (`strb r1, [r3, #11]`) reported with a
     * status bit saying *read*; this run's is a read at an address that must be readable because the
     * same `.rodata` is what every key in the report is read from. Both are exactly what an
     * imprecise abort looks like on memory the preflight maps Strongly-ordered: DFAR and LR are
     * unpredictable for it, so the two numbers the handler reports are stale rather than wrong about
     * whatever actually faulted. Chasing that further is worth an experiment of its own; what is not
     * worth anything is a reporting path that can fault, because a fault in it is not one bad line,
     * it is three hundred.
     *
     * So: record, then leave. The epilogue writes all of it with `entry_write_kv` straight to the
     * ram console after the mmu is off, which is the one reporting path here with a record of
     * working - including for the numbers below, which is how this run's first fault was read at all.
     */
    if (g_abort_entries == 0u) {
        __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));
        __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
        __asm__ volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ttbr1));
        __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr));
        __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
        g_first_abort_dfar = dfar;
        g_first_abort_pc = pc_abt;
        g_first_abort_kv_len = g_kv_len;
        g_first_abort_dfsr = dfsr;
        g_first_abort_lr = lr_abt;
        g_first_abort_insn = insn;
        g_first_abort_step = g_kv_step;
        g_first_abort_step_addr = g_kv_step_addr;
        g_first_abort_kvbuf = (uint32_t)(uintptr_t)g_kv_buf;
        g_first_abort_sp = sp_now;
        g_first_abort_spsr = spsr;
        g_first_abort_ttbr0 = ttbr0;
        g_first_abort_ttbr1 = ttbr1;
        g_first_abort_ttbcr = ttbcr;
        g_first_abort_sctlr = sctlr;
        g_first_abort_cpu_ttep = cpu_ttep;
        g_first_abort_avail_start = avail_start;
        g_first_abort_gphysbase = gPhysBase;
        g_first_abort_mem_size = mem_size;
        g_first_abort_end_kern = end_kern;
        g_first_abort_prelink_b = segPRELINKTEXTB;
        g_first_abort_prelink_size = segSizePRELINKTEXT;
        g_first_abort_hex = (uint32_t)(uintptr_t)g_hex;
        g_first_abort_hex_page = (uint32_t)((uintptr_t)g_hex & ~0xfffu);
        g_first_abort_hex_used = g_kv_hex_in_use;
        g_first_abort_hex_arg = g_kv_hex_arg;
        g_abort_entries = 1u;
        entry_epilogue("exception: data abort");
    }

    /* A second entry means the epilogue's own path faulted. Count it and leave again; there is
     * nothing left to report through, and a loop here is a loop that never ends. */
    g_abort_entries++;
    entry_epilogue("a data abort inside the data-abort handler");
}

void fleh_addrexc(void) { entry_epilogue("exception: address exception"); }
void fleh_irq(void)
{
    /*
     * The one bit of work this vector does before reporting, and it is a request rather than the
     * reading itself: the GIC is at 0xf9002000, XNU's page tables map only [physBase, physBase +
     * memSize), and the MMU does not come off until `entry_epilogue` is inside. So the flag is set
     * here and the read happens there.
     */
    g_irq_report_pending = 1u;
    entry_epilogue("exception: irq");
}
void fleh_decirq(void) { entry_epilogue("exception: decrementer irq"); }
