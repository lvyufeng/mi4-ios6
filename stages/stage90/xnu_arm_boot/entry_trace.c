/*
 * A tracer for a hang, built only when STAGE90_ENTRY_TRACE=1.
 *
 * Why this exists. Experiment 268 linked `osfmk/ipc/ipc_table.o` - three function stubs replaced by
 * real code, with both of its references (`kalloc_canblock`, `kfree`) already real since experiment
 * 250 - and the run went silent: the payload's own ladder completes, the jump line is written, and
 * then nothing at all. No `stub_hit`, no `exception: <vector>`, no second boot's worth of output.
 * Both other ways a run can end *are* visible in this log (a missing symbol reports
 * `stub_hit=<name>`; a fault reports its vector, its `DFAR`/`DFSR` and the instruction, as
 * experiments 236-242 established), so silence means the CPU never reached reporting code: a loop,
 * a spin, or a block that never wakes.
 *
 * The frontier method names the next missing *symbol*, and there is no missing symbol here: the code
 * that fails is real XNU code the image has carried since 250 and has never executed. So the
 * question is not which object to link next but *where inside the real code the machine stopped*.
 *
 * ------------------------------------------------------------------ what this instrument records
 *
 * `entry_write_kv` cannot be called on the path under test: it writes to the ram_console at VA
 * 0xde500000, and XNU's boot tables map only `[physBase, physBase + memSize)` - 8 MB - so that
 * address has no translation while XNU's MMU is on. The first build of this instrument did exactly
 * that and turned the silence into `exception: data abort`, `dfar=0xde500000`, `pc=0x80002148`
 * (inside `entry_write_kv` itself): the instrument's own fault, reported perfectly by the image's
 * handler. Everything the image needs to say during the run is stashed in `g_kv_buf` in `.bss` and
 * written out by the epilogue, after the teardown - which is the design `entry_stubs.c` already
 * documents - and a *hang* runs no epilogue at all, so a stash is not enough either.
 *
 * What the first version got wrong, and what this one does instead. That version wrapped
 * `kalloc_canblock` and forced `canblock` to FALSE, on the reasoning that a zone which cannot satisfy
 * the request would then return NULL, and a NULL handed to the next store would abort and be
 * reported. It did produce a report - and the report was *about the forcing*, not about the run:
 *
 *   - forced FALSE, the first `kalloc` of the whole boot takes `zalloc.c:3318`'s branch
 *     (`(addr == 0) && (!canblock || nopagewait) && ...`), which sets `zone->async_pending`, and
 *     calls `thread_call_enter(&call_async_alloc)`. That reaches `_pending_call_enqueue` and
 *     `enqueue_tail(&thread_call_groups[0].pending_queue, ...)` - and that queue is still zero,
 *     because `thread_call_initialize()` has never run in this boot. Measured: XNU's real panic
 *     `"Invalid queue element pointers for %p: next %p prev %p"` (`queue.h:241`) with
 *     `elt = 0x800f6b58` = `thread_call_groups + 8` = `thread_call_groups[0].pending_queue`, whose
 *     offset was read out of `thread_call_initialize`'s own inlined setup (`str r0, [r6, #8]`/`#12`
 *     for `queue_init`) rather than assumed. `thread_call_groups` is a file-local symbol, so only
 *     `thread_call.o` can name that address.
 *   - not forced, that branch is not taken at all: `canblock == TRUE` skips it and enters
 *     `while ((addr == 0) && canblock)` - the zone *expansion* loop - which is where the plain run
 *     goes instead.
 *
 * So the two branches are different destinations and the forcing chose one of them. This version
 * observes the run as it is: `canblock` is passed straight through, and each wrapper records what it
 * was called with.
 *
 * What the first run of *this* version got wrong, and what it cost. It ran and reported correctly -
 * and then stopped mid-record, because `entry_kv` drops records once `g_kv_buf` is nearly full and
 * said nothing about it. The log's last complete line was `t268_kma_size=0x00001000`, and
 * `xnu_entry_kv_written` read 0x7f6 = 2038 against a 2048-byte buffer: full. The trace therefore
 * covered the first six `kalloc` calls and nine `kernel_memory_allocate` calls of the boot and
 * nothing after them - and since `ipc_table_init`'s two `kalloc`s are the *last* two calls before
 * the frontier, the absence of a record for them read exactly like "those calls never happened". The
 * untraced run stops at the very next call after them, so they had. That is the project's "a
 * measurement can be the thing that is wrong" defect in its purest form: a truncated buffer
 * producing a *negative* result that looks like a positive finding - and it was compounded by the
 * search being made for `0x00000200`, a size this instrument had never seen, because the 512 in the
 * expectation came from reading `ipc_table.c` too quickly and the *measurement* was never checked
 * against the source until the records existed. Both keys are 0x100.
 *
 * Two changes answer it, both in `entry_stubs.c`: the buffer is 8192 bytes (269 grew it 2048 ->
 * 8192, the same move experiments 237 and 240 made for the same reason), and a dropped record now
 * increments a counter the epilogue reports as `xnu_entry_kv_dropped`, so the failure mode is a
 * number in the log. This file's own contribution is the `_ret` and `_actual` records above: with
 * room to spare, "did the allocation succeed, and what was it served with" can be answered directly
 * instead of inferred from the call site.
 *
 * ------------------------------------------------------------------ the four things it reports
 *
 *   - `kalloc_canblock`: the requested size, the size it was actually served with, `canblock` as
 *     given, the caller, **the returned pointer**, and `vm_page_free_count` read at that moment. The
 *     return value is the one added after experiment 268's first traced run: the frontier reaches
 *     `ipc_voucher_init` past `ipc_table_init`'s two `kalloc` calls, and whether those returned
 *     memory or NULL is the difference between an ipc table that exists and one that is a null
 *     pointer the boot will carry until something dereferences it. `vm_page_free_count` is the
 *     reason the instrument is interesting at all: `zalloc_internal` expands a zone by calling
 *     `kernel_memory_allocate(zone_map, ..., KMA_KOBJECT|KMA_NOPAGEWAIT)`, and with `KMA_NOPAGEWAIT`
 *     a shortage of free pages is *not* waited for - it is returned as `KERN_RESOURCE_SHORTAGE`
 *     (`kern_return.h:98` = 6) - after which `zalloc_internal` calls `VM_PAGE_WAIT()`, which is
 *     `vm_page_wait(THREAD_UNINT)` (`vm_page.h:1499`) and ends in `thread_block`. With one thread
 *     and no scheduler, that is forever: the silent hang this whole instrument exists to name.
 *   - `kernel_memory_allocate`: the size, the flags, and **the return value**. This is the datum that
 *     says *why* the expansion did not happen - `KERN_RESOURCE_SHORTAGE` (6) is "no free pages right
 *     now", `KERN_NO_SPACE` (3) is "the map has no room", and anything else is a different story.
 *   - `vm_page_wait`: its caller, which is `zalloc_internal` if the plain run stops where the code
 *     says it does.
 *   - `thread_block`: its caller and its continuation, reported *and terminal* - the epilogue runs
 *     instead of the block, so a block that would have hung becomes a line in the log.
 *
 * `lck_grp_alloc_init` is wrapped as well, and only for its caller: it is the code that makes the
 * boot's first `kalloc` call cheap to identify (`kalloc(264)` for a `struct lck_grp`), and the
 * wrapper's `__builtin_return_address(0)` is the *outer* site - `kalloc_init`, `OSMalloc_init`,
 * `stackshot_init`, `cs_init`, `kdbg_lock_init`, `prng_cpu_init` or `bsd_init`, whichever the boot
 * reaches first - which is what says where in the boot the machine actually is.
 *
 * All five are `--wrap`s: the linked image keeps XNU's own definitions and the linker routes calls
 * through these, so nothing about the traced image's code differs from the stage's except the calls
 * that are recorded. See `build_entry.sh`'s `TRACE_LDFLAGS`.
 */

#include <stdint.h>

/* entry_stubs.c. Records into `g_kv_buf`, which only an epilogue writes out - see above. */
extern void entry_kv(const char *key, uint32_t value);
extern void entry_epilogue(const char *why) __attribute__((noreturn));

/*
 * `entry_stubs.c`'s terminal record, outside `g_kv_buf`. 446's change: a report written as a run's
 * last act must not depend on a collector that keeps the oldest records - it carries its numbers to
 * the epilogue instead, which prints them beside the abort slots.
 */
extern void entry_epilogue_block(const char *why, uint32_t caller,
                                 uint32_t continuation) __attribute__((noreturn));
extern void entry_note_vmwait(uint32_t caller);

/*
 * 447's two, and they are the *non*-terminal pair: `ml_get_max_cpus` blocks and returns, so the
 * wrapper records and calls through, and the block that ends the run still happens inside the real
 * function - which is what makes the site it names the site of the block and not of a return.
 */
extern void entry_note_maxcpus(uint32_t caller);
extern void entry_note_initmax_cpus(uint32_t caller, uint32_t max_cpus);

/*
 * 448's six, the chain `StartIOKit` -> `IOPlatformExpertDevice::initWithArgs` -> `IOWorkLoop::init`.
 * Two of the calls in that chain are virtual and therefore out of `--wrap`'s reach - `initWithArgs`
 * (`[vtable+0x340]`) and `registerService` (`IOService`'s slot) - because `--wrap` renames an
 * *undefined reference*, and a vtable entry against a symbol defined in the same link resolves
 * directly. `new IOPlatformExpertDevice` is **not** one of them, and treating it as one is the
 * prediction error 448's measurement corrected: `StartIOKit+0x104..+0x10c` is `mov r0,#0x60` /
 * `bl OSObject::operator new` / `bl IOPlatformExpertDevice::IOPlatformExpertDevice()`, a direct
 * allocation and constructor for a class known at compile time. So the chain is read through the
 * **direct** calls it contains, each of which is decisive on its own: `IODeviceTreeAlloc` has exactly
 * one call site in the image (inside `initWithArgs`), and `initWithArgs`'s own return value is
 * `IOWorkLoop::workLoop`'s, by the disassembly's arithmetic.
 */
extern void entry_note_dtalloc(uint32_t caller, uint32_t arg, uint32_t ret);
extern void entry_note_workloop(uint32_t caller, uint32_t ret);
extern void entry_note_rlock(uint32_t caller, uint32_t ret);
extern void entry_note_slock(uint32_t caller, uint32_t ret);
extern void entry_note_cgate(uint32_t caller, uint32_t ret);
extern void entry_note_kthread(uint32_t cont, uint32_t caller, uint32_t ret);

/* `iokit/Kernel/IODeviceTreeSupport.cpp:96`, called from `initWithArgs` and nowhere else. */
void *__real__Z17IODeviceTreeAllocPv(void *dtTop);

void *__wrap__Z17IODeviceTreeAllocPv(void *dtTop)
{
    void *r = __real__Z17IODeviceTreeAllocPv(dtTop);

    entry_note_dtalloc((uint32_t)(uintptr_t)__builtin_return_address(0),
                       (uint32_t)(uintptr_t)dtTop, (uint32_t)(uintptr_t)r);
    return r;
}

/*
 * `iokit/Kernel/IOWorkLoop.cpp:179`, a `static` member so there is no `this` to carry. Its return
 * value is `IOPlatformExpertDevice::initWithArgs`'s: `0x80160890` is `bl IOWorkLoop::workLoop`
 * followed by `cmp r0,#0` / `movne r5,#1`, and `r5` is what that function returns.
 */
void *__real__ZN10IOWorkLoop8workLoopEv(void);

void *__wrap__ZN10IOWorkLoop8workLoopEv(void)
{
    void *r = __real__ZN10IOWorkLoop8workLoopEv();

    entry_note_workloop((uint32_t)(uintptr_t)__builtin_return_address(0),
                        (uint32_t)(uintptr_t)r);
    return r;
}

/*
 * The four guards inside `IOWorkLoop::init` (`iokit/Kernel/IOWorkLoop.cpp:117`) that can plausibly
 * fail on a boot this young. Each is a direct call whose result the function tests on its way to
 * `return false`, and all four answer 0 on success.
 */
void *__real_IORecursiveLockAlloc(void);

void *__wrap_IORecursiveLockAlloc(void)
{
    void *r = __real_IORecursiveLockAlloc();

    entry_note_rlock((uint32_t)(uintptr_t)__builtin_return_address(0), (uint32_t)(uintptr_t)r);
    return r;
}

void *__real_IOSimpleLockAlloc(void);

void *__wrap_IOSimpleLockAlloc(void)
{
    void *r = __real_IOSimpleLockAlloc();

    entry_note_slock((uint32_t)(uintptr_t)__builtin_return_address(0), (uint32_t)(uintptr_t)r);
    return r;
}

/* `static IOCommandGate *commandGate(OSObject *owner, Action action = 0)` - two arguments. */
void *__real__ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E(void *owner, void *action);

void *__wrap__ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E(void *owner, void *action)
{
    void *r = __real__ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E(owner, action);

    entry_note_cgate((uint32_t)(uintptr_t)__builtin_return_address(0), (uint32_t)(uintptr_t)r);
    return r;
}

/*
 * `kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *)` - the work loop's own
 * thread, and `IOWorkLoop::init`'s last guard. 449 records the continuation of each of the first four
 * calls as well, because a count of two says a matching thread *could* have been created and the entry
 * pointer says which thread it was: `_IOServiceJob::pingConfig` (inlined `_IOConfigThread::configThread`)
 * is the only creator of the async service-matching thread, and `IOWorkLoop::init` is the other.
 */
uint32_t __real_kernel_thread_start(void *continuation, void *parameter, void *new_thread);

uint32_t __wrap_kernel_thread_start(void *continuation, void *parameter, void *new_thread)
{
    uint32_t kr = __real_kernel_thread_start(continuation, parameter, new_thread);

    entry_note_kthread((uint32_t)(uintptr_t)continuation,
                       (uint32_t)(uintptr_t)__builtin_return_address(0), kr);
    return kr;
}

/*
 * 449's five: the catalogue, and the last unmeasured step between a nub that came up and a driver that
 * never started. Four of the five exist to *bracket* the fifth - `OSUnserialize`'s return over this
 * project's `gIOKernelConfigTables` - because a single reading with no counts around it cannot
 * distinguish "returned nothing" from "never called".
 */
extern void entry_note_postctor(uint32_t caller);
extern void entry_note_catinit(uint32_t caller);
extern void entry_note_unser(uint32_t caller, uint32_t ret);
extern void entry_note_allocname(uint32_t name);
extern void entry_note_pub2(uint32_t caller, uint32_t key);

/*
 * `libsa/lastkernelconstructor.c`'s only statement, and the image's only reference to it is a **tail
 * branch** from `last_kernel_constructor` (`b 0x8011bc4c` in 448's image). `--wrap` works on the
 * relocation rather than on the instruction, so a `b` is caught exactly as a `bl` is - which is what
 * makes this the one direct reading of whether the `.init_array` walk reached the entry that is
 * supposed to run after all the others.
 */
void __real_iokit_post_constructor_init(void);

void __wrap_iokit_post_constructor_init(void)
{
    entry_note_postctor((uint32_t)(uintptr_t)__builtin_return_address(0));
    __real_iokit_post_constructor_init();
}

/* `iokit/Kernel/IOCatalogue.cpp:92`, one call site, inside `iokit_post_constructor_init`. */
void __real__ZN11IOCatalogue10initializeEv(void);

void __wrap__ZN11IOCatalogue10initializeEv(void)
{
    entry_note_catinit((uint32_t)(uintptr_t)__builtin_return_address(0));
    __real__ZN11IOCatalogue10initializeEv();
}

/*
 * `iokit/Kernel/IOCatalogue.cpp:98`, one call site, inside `IOCatalogue::initialize`. Its return is
 * the parsed array - or NULL, which with `assert` compiled out leaves the catalogue existing and
 * empty, i.e. nothing matches, the fallback never runs, and no panic is reported.
 */
void *__real__Z13OSUnserializePKcPP8OSString(const char *inString, void *errorString);

void *__wrap__Z13OSUnserializePKcPP8OSString(const char *inString, void *errorString)
{
    void *r = __real__Z13OSUnserializePKcPP8OSString(inString, errorString);

    entry_note_unser((uint32_t)(uintptr_t)__builtin_return_address(0), (uint32_t)(uintptr_t)r);
    return r;
}

/* `libkern/c++/OSMetaClass.cpp`, the instantiation-by-name that matching ends in. */
void *__real__ZN11OSMetaClass18allocClassWithNameEPK8OSSymbol(void *name);

void *__wrap__ZN11OSMetaClass18allocClassWithNameEPK8OSSymbol(void *name)
{
    void *r = __real__ZN11OSMetaClass18allocClassWithNameEPK8OSSymbol(name);

    entry_note_allocname((uint32_t)(uintptr_t)name);
    return r;
}

/*
 * `iokit/Kernel/IOService.cpp:3532`, the statement between `MSM8974PlatformExpert::start`'s guard and
 * its `ml_init_max_cpus(1)`. Any record at all - and `"IORTC"` especially - refutes 447's deduction
 * rather than confirming it, which is why it is here.
 */
void __real__ZN9IOService15publishResourceEPKcP8OSObject(const char *key, void *value);

void __wrap__ZN9IOService15publishResourceEPKcP8OSObject(const char *key, void *value)
{
    entry_note_pub2((uint32_t)(uintptr_t)__builtin_return_address(0), (uint32_t)(uintptr_t)key);
    __real__ZN9IOService15publishResourceEPKcP8OSObject(key, value);
}

/*
 * XNU's own page-accounting global (`vm_page.h`), read rather than declared by this file's own idea
 * of it. It is a `B` symbol in the image, so if the object that owns it is not linked the read gets
 * the generated storage stand-in and reports 0 - which is a different claim ("no free pages") from
 * the one a missing object makes, so the reading is worth taking together with the
 * `kernel_memory_allocate` return value below, which cannot be a stand-in.
 *
 * `vm_page_free_target` was recorded alongside it in 268's first traced run and is no longer: it
 * read 0 in all six samples, so the record cost 35 bytes of a buffer that turned out to be the
 * binding constraint. `vm_page_free_count` read 0x3ba (954) in all six, free pages exist, and the
 * target being 0 says only that the paging thresholds are set up later than this point.
 */
extern uint32_t vm_page_free_count;

/* ------------------------------------------------------------------ kalloc_canblock */
/*
 * `void *kalloc_canblock(size_t *psize, boolean_t canblock, vm_allocation_site_t *site)`. The size
 * is passed *by pointer*, so the same pointer goes straight through and `canblock` is untouched:
 * this version changes no argument.
 *
 * Two sizes are recorded, and both are needed because `*psize` is written by the callee. XNU sets
 * it to the size the allocation actually is - `z->elem_size` for a zone allocation (`kalloc.c:714`)
 * or `round_page(size)` for a large one - so a single reading is ambiguous in exactly the way that
 * matters when the question is "did the call the code shows happen, happen". Readings taken before
 * and after the call are the request and the answer:
 *
 *   - `t268_kalloc_size`: `*psize` read *before* the call = what the caller asked for. This is the
 *     key that identifies the call site, and it is the key 268's earlier traced run recorded; a
 *     288-byte request appears here as 288.
 *   - `t268_kalloc_actual`: `*psize` read *after* the call = what XNU made of it. A 288-byte request
 *     appears here as 512, the zone it lands in. The two keys together say whether a request was
 *     served by the zone it names, and a difference is not an error - it is the zone ladder.
 *
 * For the calls this instrument was pointed at, `ipc_table_init`'s two, both keys read 0x100:
 * `sizeof(struct ipc_table_size) * ipc_table_entries_size` and the same expression over
 * `ipc_table_requests_size` are each 4 * 64 = 256 bytes (`ipc_table.c:124,138`, with
 * `CONFIG_IPC_TABLE_ENTRIES_STEPS=64` from this project's own generated config), and 256 is a
 * kalloc zone size, so nothing was rounded. The two returned blocks came back adjacent, which is
 * the second half of the evidence that both are real 256-byte allocations.
 */
void *__real_kalloc_canblock(uint32_t *psize, uint32_t canblock, void *site);

void *__wrap_kalloc_canblock(uint32_t *psize, uint32_t canblock, void *site)
{
    uint32_t requested = *psize;
    void *r = __real_kalloc_canblock(psize, canblock, site);

    entry_kv("t268_kalloc_size", requested);
    entry_kv("t268_kalloc_actual", *psize);
    entry_kv("t268_kalloc_canblock", canblock);
    entry_kv("t268_kalloc_caller", (uint32_t)(uintptr_t)__builtin_return_address(0));
    entry_kv("t268_kalloc_ret", (uint32_t)(uintptr_t)r);
    entry_kv("t268_vm_page_free", vm_page_free_count);
    return r;
}

/* --------------------------------------------------------------- lck_grp_alloc_init */
/*
 * The boot's first `kalloc` is this function's `kalloc(264)` for a `struct lck_grp`, and the site
 * that matters is the one *above* it: `__builtin_return_address(0)` here is the caller of
 * `lck_grp_alloc_init`, which says where in the boot the machine is. There are seven call sites in
 * the image (`kalloc_init`, `OSMalloc_init`, `stackshot_init`, `cs_init`, `kdbg_lock_init`,
 * `prng_cpu_init`, and three in `bsd_init`), and they are spread from `vm_mem_bootstrap` to
 * `bsd_init`, so the answer is not a detail.
 */
void *__real_lck_grp_alloc_init(const char *grp_name, void *attr);

void *__wrap_lck_grp_alloc_init(const char *grp_name, void *attr)
{
    entry_kv("t268_lckgrp_caller", (uint32_t)(uintptr_t)__builtin_return_address(0));
    entry_kv("t268_lckgrp_name", (uint32_t)(uintptr_t)grp_name);
    return __real_lck_grp_alloc_init(grp_name, attr);
}

/* ------------------------------------------------------------ kernel_memory_allocate */
/*
 * `kern_return_t kernel_memory_allocate(vm_map_t map, vm_offset_t *addr, vm_size_t size,
 * vm_offset_t mask, int flags, vm_tag_t tag)`. The return value is the measurement; the wrapper
 * calls through and records what came back, so unlike a "did it get called" wrapper this one
 * observes a *result*.
 */
uint32_t __real_kernel_memory_allocate(void *map, uint32_t *addr, uint32_t size, uint32_t mask,
                                       int flags, int tag);

uint32_t __wrap_kernel_memory_allocate(void *map, uint32_t *addr, uint32_t size, uint32_t mask,
                                       int flags, int tag)
{
    uint32_t kr = __real_kernel_memory_allocate(map, addr, size, mask, flags, tag);

    entry_kv("t268_kma_size", size);
    entry_kv("t268_kma_flags", (uint32_t)flags);
    entry_kv("t268_kma_ret", kr);
    entry_kv("t268_kma_caller", (uint32_t)(uintptr_t)__builtin_return_address(0));
    return kr;
}

/* -------------------------------------------------------------------- vm_page_wait */
/*
 * `boolean_t vm_page_wait(wait_result_t wait_type)` - what `VM_PAGE_WAIT()` expands to. Recording
 * its caller names the site that decided to wait, which is one frame above where `thread_block`
 * would be reported from.
 */
uint32_t __real_vm_page_wait(uint32_t wait_type);

uint32_t __wrap_vm_page_wait(uint32_t wait_type)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);

    entry_kv("t268_vm_page_wait_caller", caller);
    entry_note_vmwait(caller);
    return __real_vm_page_wait(wait_type);
}

/* -------------------------------------------------------------------- thread_block */
/*
 * `void thread_block(thread_continue_t continuation)`. Reporting here rather than returning is the
 * point: this boot has one thread, so a block is not a delay, it is the end of the run. The
 * epilogue's cache work and the teardown are what get the record out.
 *
 * **The record goes through `entry_epilogue_block`, not through `entry_kv`.** Experiment 445 wrote
 * these two numbers with `entry_kv` and they are the two numbers the report could not show: a record
 * written as the *last* act of a run is the first one a full buffer refuses, and 445's buffer refused
 * 16269 of them. So they travel as arguments to the epilogue and land in the `.bss` slots the
 * epilogue prints outside the dump.
 */
void __real_thread_block(void *continuation);

void __wrap_thread_block(void *continuation)
{
    entry_epilogue_block("t268: thread_block was called",
                         (uint32_t)(uintptr_t)__builtin_return_address(0),
                         (uint32_t)(uintptr_t)continuation);
}

/* ------------------------------------------------------- ml_get_max_cpus / ml_init_max_cpus */
/*
 * Experiment 447. 446 named the frame the run ends in - `ml_get_max_cpus`'s own `thread_block` - and
 * the six `bl ml_get_max_cpus` sites in the image are `vm_page_init_local_q`,
 * `waitq_alloc_prepost_reservation`, `commpage_populate`, `mcache_init`, `mbinit` and
 * `sysctl_mib_init`; a terminal wrapper cannot tell them apart, because the call it reports is the
 * one *inside* the function it wrapped.
 *
 * These two are the frame above. Neither is terminal, and neither changes its argument or its result:
 * the caller's return address goes into a `.bss` slot the epilogue prints outside the buffer, and the
 * real function runs exactly as it would have. `ml_get_max_cpus` keeps the **first** caller (the call
 * that blocks is the first one to find the flag unset); `ml_init_max_cpus` keeps the first caller and
 * the number announced, and its count is the reading that says whether the writer ran at all - 0 would
 * mean the platform expert's `start` never reached experiment 405's `ml_init_max_cpus(1)`, which is
 * the one thing that can leave `ml_get_max_cpus` waiting forever on this machine.
 */
uint32_t __real_ml_get_max_cpus(void);

uint32_t __wrap_ml_get_max_cpus(void)
{
    entry_note_maxcpus((uint32_t)(uintptr_t)__builtin_return_address(0));
    return __real_ml_get_max_cpus();
}

void __real_ml_init_max_cpus(uint32_t max_cpus);

void __wrap_ml_init_max_cpus(uint32_t max_cpus)
{
    entry_note_initmax_cpus((uint32_t)(uintptr_t)__builtin_return_address(0), max_cpus);
    __real_ml_init_max_cpus(max_cpus);
}
