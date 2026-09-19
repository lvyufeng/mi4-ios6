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

/* ------------------------------------------------------------------ bpfread_filtops */

/*
 * `bpfread_filtops` - the first stand-in in this file whose defining source the manifest does not
 * contain **at all**.
 *
 * `bsd/kern/kern_event.c:390` declares it `extern const struct filterops bpfread_filtops;` and
 * `:448` puts its *address* in the `kern_event_filtops[]` table at index `EVFILTID_BPFREAD`. It is
 * defined in `bsd/net/bpf.c:2216` as `SECURITY_READ_ONLY_EARLY(struct filterops)`, and bpf.c is
 * `optional bpfilter` in `bsd/conf/files:192` - a flag the device table this project builds from
 * does not select, so the file is not in `out/xnu_arm_manifest.txt` and no object in the pool
 * defines the name. (It is the same omission class as the ~126 `optional` sources the manifest
 * skips; here it is the first one a *step* has needed.)
 *
 * The size is not a guess, and it has two independent roads to it:
 *
 *   - **Measured over the pool.** Fifteen sibling filter tables are compiled here and every one of
 *     them is 0x28: `fs_filtops` (`bsd_vfs_vfs_subr.o`), `machport_filtops` (`osfmk_ipc_ipc_pset.o`),
 *     `soread_filtops`/`sowrite_filtops`/`sock_filtops`/`soexcept_filtops`
 *     (`bsd_kern_uipc_socket.o`), `pipe_rfiltops`/`pipe_wfiltops` (`bsd_kern_sys_pipe.o`),
 *     `sig_filtops`, `ptsd_kqops` (`bsd_kern_tty_ptmx.o`), `vnode_filtops`, `spec_filtops`,
 *     `fsevent_filtops`, `necp_fd_rfiltops`, `tty_filtops`. `nm -S --defined-only` reports
 *     `R fsevent_filtops 00000028` and the same for the rest.
 *   - **Derived from the type.** `struct filterops` (`bsd/sys/event.h:939-951`) is two `bool`s and
 *     nine function pointers: 2 + 2 padding + 9 * 4 = 40 = 0x28 on armv7, which is the number the
 *     pool measured.
 *
 * Zero is the right *value* here in a way it would not be for `EntropyData` above: this is a table
 * of function pointers that only `kern_event_init` indexes and only a `kqfilter` call on a BPF file
 * descriptor dereferences, and neither is on this walk's path - the run stops at `klist_init`,
 * inside `ipc_mqueue_init`, long before any of it. A reader of the linker map will find 40 bytes of
 * zero where XNU has a filter table, and this comment is why.
 */
uint8_t bpfread_filtops[0x28] __attribute__((aligned(4)));

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

void entry_kv(const char *key, uint32_t value)
{
    if (g_kv_len + 40u >= ENTRY_KV_BUF) {
        g_kv_dropped++;
        return;
    }
    g_kv_step = 1u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&g_kv_buf[g_kv_len];
    g_kv_buf[g_kv_len++] = ' ';
    g_kv_step = 2u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&g_kv_buf[g_kv_len];
    while (*key != '\0' && g_kv_len + 20u < ENTRY_KV_BUF) {
        g_kv_buf[g_kv_len++] = *key++;
    }
    g_kv_step = 3u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&g_kv_buf[g_kv_len];
    g_kv_buf[g_kv_len++] = '=';
    g_kv_buf[g_kv_len++] = '0';
    g_kv_buf[g_kv_len++] = 'x';
    g_kv_step = 4u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&g_kv_buf[g_kv_len];
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
        g_kv_buf[g_kv_len++] = (char)(d < 10u ? ('0' + d) : ('a' + (d - 10u)));
    }
    g_kv_step = 5u;
    g_kv_step_addr = (uint32_t)(uintptr_t)&g_kv_buf[g_kv_len + 11];
    g_kv_buf[g_kv_len++] = '\n';
    g_kv_buf[g_kv_len] = '\0';
    g_kv_step = 0u;
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
     * `entry_kv` ran then with XNU's caches and page tables live, and runs now without either.
     */
    entry_kv("xnu_entry_stub_caller_e", g_stub_caller);

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
        entry_kv("xnu_entry_irq_iar", *(volatile uint32_t *)(uintptr_t)0xf900200cu);
        entry_kv("xnu_entry_irq_ispendr0", *(volatile uint32_t *)(uintptr_t)0xf9000200u);
        entry_kv("xnu_entry_irq_isenabler0", *(volatile uint32_t *)(uintptr_t)0xf9000100u);
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
    entry_write_kv("xnu_entry_stub_caller_w0",
                   entry_image_ptr((uintptr_t)&g_kv_buf[g_stub_caller_digits])
                       ? entry_word_at((uintptr_t)&g_kv_buf[g_stub_caller_digits]) : 0u);
    entry_write_kv("xnu_entry_stub_caller_w1",
                   entry_image_ptr((uintptr_t)&g_kv_buf[g_stub_caller_digits + 4u])
                       ? entry_word_at((uintptr_t)&g_kv_buf[g_stub_caller_digits + 4u]) : 0u);
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
void entry_stub_hit(const char *name, uint32_t caller)
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

void fleh_undef(void)
{
    uintptr_t frame;
    uint32_t lr_undef, spsr;
    uint32_t r_fmt, r_args, r_sl;
    uint32_t ap, element, zone_name;
    uint32_t i;

    __asm__ volatile ("mov %0, sp" : "=r"(frame));

    __asm__ volatile ("mov %0, lr" : "=r"(lr_undef));
    __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));

    entry_kv("xnu_entry_undef_lr", lr_undef);
    entry_kv("xnu_entry_undef_pc", lr_undef - 4);
    entry_kv("xnu_entry_undef_spsr", spsr);

    entry_kv("xnu_entry_panic_str", (uint32_t)(uintptr_t)debugger_panic_str);
    entry_kv("xnu_entry_panic_caller", (uint32_t)debugger_panic_caller);
    entry_kv("xnu_entry_panic_message", (uint32_t)(uintptr_t)debugger_message);

    __asm__ volatile ("mov %0, r9" : "=r"(r_fmt));
    __asm__ volatile ("mov %0, r8" : "=r"(r_args));
    __asm__ volatile ("mov %0, r10" : "=r"(r_sl));

    entry_kv("xnu_entry_trap_r9_fmt", r_fmt);
    entry_kv("xnu_entry_trap_r8_args", r_args);
    /*
     * **`sl` is not the caller, and the old key name said it was.** `panic_trap_to_debugger` loads
     * `r4` and `sl` from `[sp+56]` and `[sp+60]` and hands both to `DebuggerTrapWithState` as the
     * two halves of the 64-bit `db_panic_options` - `stm sp, {r4, sl}` at the call - and the caller
     * is `r5`, loaded from `[sp+64]` and stored at `[sp+12]`. So this key is the options mask's
     * high word, and zero here says the mask `panic()` passed is zero. The caller is read out of
     * the frame below, which is the only way to get it: `fleh_undef`'s own prologue destroys `r5`
     * with `mrs r5, SPSR`.
     */
    entry_kv("xnu_entry_trap_sl_options_hi", r_sl);

    entry_kv("xnu_entry_zone_map_min", zone_map_min_address);
    entry_kv("xnu_entry_zone_map_max", zone_map_max_address);
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
     * **The frame, in words.** `fleh_undef`'s prologue is `strd r4, [sp, #-24]!`, `strd r6,
     * [sp, #8]`, `str r8, [sp, #16]`, so the trapping context's `r4`, `r5`, `r6`, `r7` and `r8` are
     * all still in this function's own frame - `r5` included, which is the point: it is
     * `db_panic_caller`, the address that names which call site panicked, and no live register
     * holds it any more because the prologue's second instruction is `mrs r5, SPSR`.
     *
     * The keys are named for the registers they are *predicted* to hold, which is the check: the
     * offset was read out of the built function's disassembly before this run, not guessed. `mov r5,
     * sp` lands immediately after the prologue's writeback, so the captured `sp` **is** the frame
     * base and the block is at +0, +4, +8, +12, +16, +20, +24, +28 in that prologue's order:
     *
     *   frame_r4   +0   0x00000000   the low half of `db_panic_options`
     *   frame_r5   +4   0x0026fe84   `free_to_zone+0x148`, the instruction after the `bl panic`
     *   frame_r6   +8   0x00000000   `ctx`
     *   frame_r7   +12  0x00000000   `reason`
     *   frame_r8   +16  0x0029be88   `panic_args`, which experiments 238 and 239 both measured live
     *   frame_r9   +20  == `trap_r9_fmt`   `panic_format_str`, and equality with the live read is
     *                                the prediction that holds: the literal's absolute address moves
     *                                with this image's text size, so no constant here is stable
     *                                across builds (it was 0x0028cc94 in the build this comment was
     *                                written against and 0x0028cc33 in the build that ran).
     *   frame_sl   +24  0x00000000   the options mask's high half
     *   frame_lr   +28  0x0022d40c   the instruction after the `udf`, i.e. `undef_lr` over again
     *
     * A key that comes back with the wrong value is a shift in the block, and `frame_sp` plus the
     * names say which shift. Reading the block is safe for the same reason everything else here is
     * read: this is the boot stack, inside the image, and it is mapped. Only the *values* are
     * reported; nothing in the block is dereferenced.
     */
    entry_kv("xnu_entry_frame_sp", (uint32_t)frame);
    {
        static const char *const fr[8] = {
            "xnu_entry_frame_r4", "xnu_entry_frame_r5", "xnu_entry_frame_r6",
            "xnu_entry_frame_r7", "xnu_entry_frame_r8", "xnu_entry_frame_r9",
            "xnu_entry_frame_sl", "xnu_entry_frame_lr",
        };

        for (i = 0u; i < 8u; i++) {
            entry_kv(fr[i], entry_word_at(frame + (i * 4u)));
        }
    }

    /*
     * **The two arguments, one dereference further on than experiment 239 read them.** `r_args` is
     * a `va_list *`, so the word at it is the list's `__ap` - a pointer to the first vararg - and
     * not the first argument. Experiment 239 measured that word as `0x0029be94`, twelve bytes past
     * `r_args`, which is exactly where `panic`'s frame puts `__ap` (the list at `sp+16`, the three
     * spilled varargs at `sp+28`). So the first vararg is at `ap` and the second four bytes later:
     *
     *   element   = *ap        expected in [0x40400000, 0x40401000)
     *   zone_name = *(ap + 4)  expected to be a pointer to the literal "maps"
     *
     * **The element is reported and never dereferenced**, which is why `entry_image_ptr` is not
     * widened for it: `0x40400000` is outside the image and may be unmapped, and a data abort
     * inside the abort handler says nothing at all. `ap` and `zone_name` are both expected inside
     * the image, so the guard that is already here covers every read that happens.
     */
    ap = 0u;
    element = 0u;
    zone_name = 0u;
    if (entry_image_ptr((uintptr_t)r_args)) {
        ap = entry_word_at((uintptr_t)r_args);
    }
    entry_kv("xnu_entry_panic_ap", ap);
    if (entry_image_ptr((uintptr_t)ap)) {
        element = entry_word_at((uintptr_t)ap);
        zone_name = entry_word_at((uintptr_t)ap + 4u);
    }
    entry_kv("xnu_entry_panic_element", element);
    entry_kv("xnu_entry_panic_zonename", zone_name);
    if (entry_image_ptr((uintptr_t)zone_name)) {
        entry_kv("xnu_entry_zone_name_w0", entry_word_at((uintptr_t)zone_name));
        entry_kv("xnu_entry_zone_name_w1", entry_word_at((uintptr_t)zone_name + 4u));
    }

    entry_epilogue("exception: undefined instruction");
}
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
