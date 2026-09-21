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
 *     `vm_page_wait(THREAD_UNINT)` (`vm_page.h:1499`) and ends in `thread_block`. 268 read that as a
 *     silent hang - "with one thread and no scheduler, a block is forever" - and 449 corrected the
 *     premise: the boot has two kernel threads by the time it blocks and the scheduler is real, so a
 *     block is a switch. The sentence survives here because it is why the wrapper existed; 450 is
 *     where it stops being the wrapper's behaviour.
 *   - `kernel_memory_allocate`: the size, the flags, and **the return value**. This is the datum that
 *     says *why* the expansion did not happen - `KERN_RESOURCE_SHORTAGE` (6) is "no free pages right
 *     now", `KERN_NO_SPACE` (3) is "the map has no room", and anything else is a different story.
 *   - `vm_page_wait`: its caller, which is `zalloc_internal` if the plain run stops where the code
 *     says it does.
 *   - `thread_block`: its caller and its continuation, **non-terminally since 450** - the wrapper
 *     records and calls through, so a block that used to end the run now switches the CPU the way
 *     XNU intends. The slots carry the first eight `(caller, continuation)` pairs, how many blocks
 *     there were and how many returned, because with the halt retired the question is no longer
 *     *where* the first block is but what the boot does *after* it.
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

/* 476: `STAGE90_T_PREFETCH_ABT`/`STAGE90_T_DATA_ABT`, the two abort classes that reach the wrapper
 * below. This file used to need nothing from that header - it took `regs` as an opaque pointer and
 * never indexed it, which is still true - but the number that says *which* fault pair is the fault
 * pair is the same number `entry_stubs.c`'s comment reasons about, so it is written once. */
#include "entry_saved_state.h"

/* entry_stubs.c. Records into `g_kv_buf`, which only an epilogue writes out - see above. */
extern void entry_kv(const char *key, uint32_t value);
extern void entry_epilogue(const char *why) __attribute__((noreturn));

/*
 * `entry_stubs.c`'s terminal record, outside `g_kv_buf`. 446's change: a report written as a run's
 * last act must not depend on a collector that keeps the oldest records - it carries its numbers to
 * the epilogue instead, which prints them beside the abort slots. Since 450 nothing calls it: the
 * block it was written for is no longer terminal, and 450's `entry_note_block` pair replaces it.
 */
extern void entry_epilogue_block(const char *why, uint32_t caller,
                                 uint32_t continuation) __attribute__((noreturn));
extern void entry_note_vmwait(uint32_t caller);

/*
 * 450's pair, one on each side of the real `thread_block`: the first records the site and the
 * continuation, the second whether the block ever came back. See `entry_stubs.c` for why a
 * non-terminal block needs both and why the ring is eight deep. 453 added the thread to the first,
 * which is the value `entry_thread()` returns below.
 */
extern uint32_t entry_note_block(uint32_t caller, uint32_t continuation, uint32_t thread,
                                 uint32_t now);
/* 478: the second argument is `thread_block`'s own return value, `self->wait_result`, and the third
 * is the index `entry_note_block` wrote this block into - passed through so that a block's entry and
 * its return carry the same number even when the block in between never came back. */
extern void entry_note_block_return(uint32_t caller, uint32_t result, uint32_t seq);

/* 479: what the kernel answered process 1's first syscall with. The second argument is `getpid`'s own
 * return value (0 for a call that worked) and the third is the word it wrote into the caller's
 * `retval` - `p->p_pid` - which is the process's identity rather than a status. */
extern void entry_note_getpid(uint32_t caller, uint32_t error, uint32_t value);

/* 480: what the kernel was *handed* for process 1's second syscall. `args` is the eight words the
 * munger left in `uap` - `r0..r5`, `r6`, `r8` in that order, see the wrapper below - and `value` is
 * `*retval`, the address `mmap` entered into the process's map. The eight words are a pointer rather
 * than eight arguments because they are one claim about one layout, and a reading of them is worth
 * more together than apart. */
extern void entry_note_mmap(uint32_t caller, const uint32_t *args, uint32_t error, uint32_t value);

/* 503: what process 1 asked the kernel's clock for, and how long the kernel took to answer. The four
 * inputs are the call's own arguments and its call site; `error` and `retval` are what it returned;
 * `before`/`after` are the two `mach_absolute_time` readings the wrapper takes around the real call,
 * so their difference is the interval the kernel kept this thread parked. See the wrapper below. */
extern void entry_note_poll(uint32_t caller, uint32_t fds, uint32_t nfds, uint32_t timeout_ms,
                            uint32_t error, uint32_t retval, uint32_t before, uint32_t after);
extern void entry_note_open(uint32_t caller, uint32_t path, uint32_t flags, uint32_t mode,
                            uint32_t error, uint32_t fd);
extern void entry_note_read(uint32_t caller, uint32_t fd, uint32_t buf, uint32_t nbytes,
                            uint32_t error, uint32_t lo, uint32_t hi,
                            uint32_t word_before, uint32_t word_after,
                            uint32_t copy_before, uint32_t copy_after);

/* 505: three, and they are the step's whole instrument. `fork`'s is the pid the kernel made plus the
 * argument word it did *not* read; `exit`'s is published before the call because the call does not
 * return, with a second entry point for the record that would only exist if it did; and `sigchld`'s
 * is the one inter-process record, taken from a function downstream of the branch that ended 478's
 * run. See the wrappers below. */
extern void entry_note_fork(uint32_t caller, uint32_t uap0, uint32_t error, uint32_t lo, uint32_t hi);
extern void entry_note_exit(uint32_t caller, uint32_t pid, uint32_t rval);
extern void entry_note_exit_returned(uint32_t pid);
extern void entry_note_sigchld(uint32_t from, uint32_t to, uint32_t signum, uint32_t psignal_calls);
extern void entry_note_sigchld_returned(uint32_t from, uint32_t to);

/* The count `--wrap=psignal` keeps of *all* signals, SIGCHLD or not - the filter's own denominator,
 * defined in `entry_stubs.c` beside the record it qualifies. */
extern uint32_t g_psignal_calls;

/* 508: the pair around `wait4`, and the two records are one reading split in two on purpose - the
 * first is written *before* the kernel's own call runs and the second after it returns, so the log's
 * own order says whether the parent was parked inside the call while the child finished dying. The
 * first returns the call's sequence number, which the second carries, because a `wait4` can block and
 * be run more than once by the same process: the pair has to be associable without assuming order.
 * See the two bodies in `entry_stubs.c`. */
extern uint32_t entry_note_wait(uint32_t caller, uint32_t who, uint32_t pid, uint32_t status_ptr,
                                uint32_t options, uint32_t rusage);
extern void entry_note_wait_returned(uint32_t seq, uint32_t error, uint32_t ret,
                                     uint32_t copy_error, uint32_t status, uint32_t ticks);

/* 481: the kernel's end of the timer chain - the deadline `timer_resync_deadlines` chose and the
 * decrementer value the real `setPop` computed for it, recorded beside what the writer in
 * `entry_timebase.c` was handed. See the wrapper below. */
extern void entry_timebase_note_setpop(uint64_t deadline, uint32_t returned);

/* 484: the arm-er, named at the entry to the field it writes. The three sources are the two
 * `timer_call_enter` forms this kernel reaches and the quantum timer's own entry point; the numbers
 * are the ones `entry_timebase.c` documents above its state, and `check_timer_sources.py` asserts that
 * the wrappers below pass exactly `1`, `2` and `3`, that the note function reads all three, and that
 * the family's fourth member - `timer_call_enter1`, whose only callers in this tree are SFI and dtrace
 * and which is therefore not wrapped - still has none. */
extern void entry_timebase_note_timer_setup(uint32_t call, uint32_t func, uint32_t param0);
extern void entry_timebase_note_timer_enter(uint32_t source, uint32_t call, uint64_t deadline,
                                            uint32_t flags);
extern void entry_timebase_note_quantum_expire(uint32_t thread);

/*
 * 453's one, called by the six sleep wrappers below with the frame they were entered from. `ent` is
 * the entry point's id in the table in `entry_stubs.c` - the fifth argument means something
 * different for each, so the id is what says which reading `tmo` is.
 */
extern void entry_note_sleep(uint32_t ent, uint32_t site, uint32_t thread, uint32_t chan,
                             uint32_t wmsg, uint32_t pri, uint32_t tmo);

/*
 * 454's one, called by the two IOKit deadline wrappers. `dl_lo`/`dl_hi` and `now` are the two ends of
 * the comparison the wait will make; see `entry_stubs.c`.
 */
extern void entry_note_iolock(uint32_t ent, uint32_t site, uint32_t thread, uint32_t lock,
                              uint32_t event, uint32_t inter, uint32_t dl_lo, uint32_t dl_hi,
                              uint32_t now);

/*
 * Experiment 453. The current thread, in one instruction: on ARMv7 `current_thread()` is a read of
 * TPIDRPRW (`osfmk/arm/cpu_data.h:58`), and `cswitch.s` writes that register on every context switch
 * - so a read taken anywhere between two switches is the thread running there, with no frame pointer
 * and no per-CPU lookup needed. This is the same read the kernel's own macro does, which is why it is
 * safe to take it from a wrapper: it cannot fault, it reads no memory, and it holds no lock.
 */
static inline uint32_t entry_thread(void)
{
    uint32_t t;

    __asm__ volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r"(t));
    return t;
}

/*
 * Experiment 454. The counter `ml_get_timebase` reads - `mrrc p15, 0, lo, hi, c14`, which is the ARM
 * generic timer's virtual count on this target and the third instruction pair of that function's own
 * body (`0x8000f380`), behind a `mach_absolute_time` that is a four-byte tail branch to it. Read
 * directly rather than by calling `mach_absolute_time` for the same reason the thread is read
 * directly: the wrapper then depends on one instruction and no memory, and it cannot be affected by
 * anything the boot has or has not initialised - which is the point, because whether this counter is
 * *moving* is the question. Only the low word is kept (`now` in the report): at 19.2 MHz it wraps
 * every 3.7 minutes and the waits here are seconds apart, while the high word would cost a second
 * record per call to say nothing.
 */
static inline uint32_t entry_counter(void)
{
    uint32_t lo, hi;

    __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return lo;
}

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

/* 461's plane reading, defined with the rest of 461's instruments at the end of this file - the
 * declaration is here because this wrapper is the one place it is taken. */
void entry_probe_plane(void *dtTop, void *root);

void *__wrap__Z17IODeviceTreeAllocPv(void *dtTop)
{
    void *r = __real__Z17IODeviceTreeAllocPv(dtTop);

    entry_note_dtalloc((uint32_t)(uintptr_t)__builtin_return_address(0),
                       (uint32_t)(uintptr_t)dtTop, (uint32_t)(uintptr_t)r);
    /*
     * 461: and immediately, while the plane is complete and nothing else has touched it.
     *
     * This is the first moment the plane exists at all: `makePlane` is `IODeviceTreeAlloc`'s first
     * statement and the tree is attached to the registry root as its last, so this call is the only
     * place in the boot where the IODT plane can be read without also reading whatever BSD init did
     * to it afterwards. 459's frontier is *inside* that afterwards, so the reading has to be taken
     * before it to be a reading of the plane rather than of the run.
     */
    entry_probe_plane(dtTop, r);
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
 * 455's three, called by the six wrappers near the end of this file. `entry_note_match` is called
 * *after* the real `copyExistingServices` so that the record carries its answer, and `entry_note_mpass`
 * gets the `this` the matcher was asked on, which is what tells the resource-root query apart from the
 * plane search. `entry_note_dict` records the dictionary a factory built, and its return value is the
 * pointer the match records carry.
 */
extern void entry_note_match(uint32_t site, uint32_t dict, uint32_t in_state, uint32_t options,
                             uint32_t result);
extern void entry_note_mpass(uint32_t site, uint32_t dict, uint32_t options, uint32_t result,
                             uint32_t self);
extern void entry_note_dict(uint32_t site, uint32_t name, uint32_t table_in, uint32_t table_out);
/* 457's one, called by the wrapper at the end of this file, and the reason it is a *separate*
 * function from `entry_note_dict`'s family is that its argument is not a dictionary but a set: the
 * count it carries is the reading `doServiceMatch` branches on. 492 adds two more arguments and no
 * more call sites - the generation the real call stored through the caller's out-parameter, and
 * whether the service is the resource root, which is 457's filter moved from the writer to the
 * reader. */
extern void entry_note_finddrivers(uint32_t site, uint32_t service, uint32_t set, uint32_t count,
                                   uint32_t gen, uint32_t res);

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
 * `void thread_block(thread_continue_t continuation)`.
 *
 * **Experiment 450 retired the halt.** From 268 to 449 this wrapper was *terminal*: it recorded the
 * caller and the continuation and ran `entry_epilogue_block` instead of calling through, on the
 * reasoning that this boot has one thread and a block is therefore the end of the run. 449 measured
 * what that reasoning cost: the first `thread_block` is `ml_get_max_cpus+0x3c`, and the matching
 * thread `registerService` had already created (`_IOConfigThread::main`, from `pingConfig`) was
 * waiting to be dispatched at exactly that instruction. So the terminal report did not name the end
 * of the boot, it *made* the end of the boot - and 446/447/448 each built a frontier on top of it
 * ("the flag's writer never ran, so `start` was never entered"). The untraced runs say the same
 * thing from the other side: 439 and 440, built without this file, reached `bsd_init+0x870` and
 * `bsd_init+0x880`, which is only reachable if the block in `ml_get_max_cpus` was passed and
 * `ml_init_max_cpus` - `MSM8974PlatformExpert::start`'s third statement - ran.
 *
 * So the wrapper calls through, and the reading moves to the `.bss` slots: the sequence of blocks,
 * how many returned, and the pairs around them. `entry_epilogue_block` stays in `entry_stubs.c` for
 * the record of what the halt was and for a later step that needs a terminal stop on purpose.
 *
 * One thing it does *not* do is report live. `entry_write_kv` writes the ram console at VA
 * 0xde500000, and that address has no translation while XNU's own page tables are live (the first
 * build of this instrument turned exactly that into `exception: data abort`, `dfar=0xde500000`,
 * `pc=inside entry_write_kv`), so a boot that now hangs reports nothing at all - which is why this
 * step's run is expected to stop at a *stub* or a *fault*, and why the recovery nets are the answer
 * to the other case. A live channel from inside XNU is its own step; the candidates are named in
 * `docs/experiments/experiment-450-*.md`.
 *
 * ----- 478: the return value, which is the one thing this wrapper used to throw away
 *
 * **`thread_block` returns `self->wait_result`** (`osfmk/kern/sched_prim.c`, the last statement of
 * `thread_block_reason`), and 476's and 477's runs both stop inside `ipc_mqueue_receive` **one
 * statement after a block returned there**: that function's tail is `ipc_mqueue_receive_results(wresult)`
 * and its `default:` arm is `panic("ipc_mqueue_receive_results: strange wait_result")`
 * (`osfmk/ipc/ipc_mqueue.c:871`). The switch accepts exactly `THREAD_AWAKENED` (0), `THREAD_TIMED_OUT`
 * (1), `THREAD_INTERRUPTED` (2) and `THREAD_RESTART` (3), so *the value this function returns is the
 * panic's argument*, and until now the wrapper called through and ignored it - the run could name the
 * site and not the number.
 *
 * So the value is captured (and returned unchanged, which is the whole ABI of a `--wrap`) and handed
 * to `entry_note_block_return`, which writes it live for every block and keeps a first-of-each-kind
 * record for the ones the receive path cannot accept. **The reading it decides**: `10`
 * (`THREAD_NOT_WAITING`, `osfmk/kern/kern_types.h:81`) means the wait was refused before it started,
 * and `0xFFFFFFFF` (`THREAD_WAITING`) is what a thread that blocked and was never woken still holds -
 * it is also what `thread.c:250` initialises every thread's `wait_result` to - which is the reading
 * the missing timer and interrupt controller would explain.
 */
int __real_thread_block(void *continuation);

int __wrap_thread_block(void *continuation)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uint32_t seq;
    int result;

    seq = entry_note_block(caller, (uint32_t)(uintptr_t)continuation, entry_thread(),
                           entry_counter());
    result = __real_thread_block(continuation);
    entry_note_block_return(caller, (uint32_t)result, seq);
    return result;
}

/* ------------------------------------------------------- the process's first syscall (479) */
/*
 * 478's run left process 1 with one requirement - it must keep running - and `entry_ramdisk.s`'s
 * program meets it with `svc #0x80` asking for Unix syscall 20, `getpid`. That call reaches
 * `sysent[20].sy_call`, and `--wrap=getpid` puts this function in the slot: `ld`'s `--wrap` renames an
 * address reference exactly as it renames a call (458's finding about `cons_ops[1].putc`, 463's about
 * `getState`), and the reference here is `bsd/kern/init_sysent.c`'s initialiser. That is why the
 * reachability census below in `build_entry.sh` counts `getpid` as *by address*, and why
 * `tools/check_sysent_table.py` reads the word back out of the linked image instead of trusting the
 * link line.
 *
 * **The declaration is the ABI, and it has to be the caller's.** `unix_syscall` calls the slot as
 * `(*(callp->sy_call))(proc, &uthread->uu_arg[0], &uthread->uu_rval[0])`
 * (`bsd/dev/arm/systemcalls.c:174`), so the wrapper takes the process, the argument buffer and the
 * return-value pair - and it returns `getpid`'s own `int` **unchanged**. A `void` shape would end in a
 * tail call and leave the caller's `r0` holding whatever ran last, which is exactly the defect 478
 * found in this file's own `__wrap_thread_block`; the fixture's `cmp r0, #1` is what makes that defect
 * loud rather than silent, and the value here is what makes it readable.
 *
 * **What it records is the answer the kernel wrote for the user**, not a status of its own: `*retval`
 * is `p->p_pid`, the identity `bsd_utaskbootstrap` gave this process
 * (`initproc = proc_find(1)`, `bsd/kern/bsd_init.c:1147`), and the error is the syscall's own return.
 * Neither is touched. `entry_note_getpid` writes the first call in full and then only powers of two,
 * because this loop runs as fast as the CPU allows: the live channel's record cap is a *bound*
 * the boot's own report depends on, and 461's defect was the report path's buffer being the tracer's
 * and full when the report was written.
 */
int __real_getpid(void *proc, void *uap, int *retval);

int __wrap_getpid(void *proc, void *uap, int *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int error = __real_getpid(proc, uap, retval);

    entry_note_getpid(caller, (uint32_t)error,
                      (retval != 0) ? (uint32_t)*retval : 0xFFFFFFFFu);
    return error;
}

/* ---------------------------------------------------- the process's own memory (480) */
/*
 * 479 gave process 1 a syscall whose answer is a register. 480 gives it one whose effect is on the
 * machine - `mmap` of one anonymous page, then a read of it and a write of it - and this wrapper
 * exists for the **arguments**, not for the return. The return is already legible from the fixture: an
 * address that is not a page faults on the load, an errno takes the `bcs`. The arguments are a claim
 * about the ABI that no run could otherwise falsify.
 *
 * **The claim, and why it needs an instrument.** `arm_get_syscall_args`
 * (`bsd/dev/arm/systemcalls.c:337`) is the *munging* one on this target - the file's
 * `#if __arm__ && (__BIGGEST_ALIGNMENT__ > 4)` is on, because the configuration is
 * `CPU_SUBTYPE_ARM_V7K` - so before the call `arm_get_u32_syscall_args` runs
 * `sysent[197].sy_arg_munge32`, which is `munge_wwwwwl`. With `r12 != 0` that takes kDirect
 * (`SS_TO_STYLE`, `bsd/dev/arm/munge.c:54`) and reduces to `munge_wlll`, which is `munge_wll`'s
 * `memcpy(args, regs, 6 * sizeof(uint32_t))` followed by `uu_args[6] = ss->r[6]` and
 * `uu_args[7] = ss->r[8]` (`munge.c:347-373`). `regs` is the saved state, and `struct arm_saved_state`
 * begins with `uint32_t r[13]` (`osfmk/mach/arm/thread_status.h:223`), so the eight words in `uap` are
 * **r0..r5 in order, then r6 and r8** - and the generated `struct mmap_args`
 * (`out/xnu_generated/bsd/sys/sysproto.h:671`) is `addr, len, prot, flags, fd`, five words, followed
 * by an 8-byte-aligned `off_t pos` at word six. **Word five is therefore the struct's padding**: the
 * munger writes `r5` into it and `mmap` never reads it, which is why the fixture puts `0x5a5a` there.
 * A wrapper that found that marker in word four, or in word six, would say the munge is not the one
 * this build believes - and the syscall would have worked anyway, because `MAP_ANON` makes `fd` and
 * `pos` inoperative. **A swapped argument is invisible in the call's own behaviour and visible only
 * here**, which is what makes this wrapper the reading rather than a restatement.
 *
 * **The words are read before the call, not after.** They are the caller's buffer
 * (`uthread->uu_arg`) and `mmap` reads them; reading afterwards would be a claim about what the
 * syscall left there rather than about what it was handed, and the two are the same only as long as
 * nothing in `mmap` writes `uap`. Captured after the call are the two things the call decides: its
 * `int` return, which is the errno (`0` on the run this is for), and `*retval`, which is
 * `_SYSCALL_RET_ADDR_T`'s `uu_rval[0]` - the address - written by `arm_prepare_u32_syscall_return`
 * (`systemcalls.c:293`).
 *
 * **It is called exactly once**, because `entry_ramdisk.s` calls `mmap` outside its loop. A call per
 * iteration would enter a page per iteration and end with `mmap` returning ENOMEM, which the fixture's
 * `bcs` would then take to `udf #1` - a leak that ends in the panic 478 measured.
 * `entry_note_mmap` writes every word live and counts, and the count is a reading of that decision
 * rather than a guard against a storm.
 *
 * The declaration is `sysproto.h`'s `int mmap(struct proc *, struct mmap_args *, user_addr_t *)` in
 * the types this file can spell: on this target `user_addr_t` is `u_int32_t` (`bsd/arm/types.h:82`),
 * so the third argument is a `uint32_t *` and `*retval` is the address. The kernel's indirect call is
 * `(*(callp->sy_call)) (proc, &uthread->uu_arg[0], &(uthread->uu_rval[0]))` (`systemcalls.c:174`),
 * which is that shape.
 */
int __real_mmap(void *proc, void *uap, uint32_t *retval);

int __wrap_mmap(void *proc, void *uap, uint32_t *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t args[8];
    unsigned i;
    int error;

    /* The eight words as the munger left them, before anything else can touch them. A `uap` of 0 is
     * recorded as eight 0xFFFFFFFF words rather than dereferenced: the kernel never calls the slot
     * that way, and an instrument that faults on a state it was told cannot happen costs the run. */
    for (i = 0u; i < 8u; i++)
        args[i] = (given != 0) ? given[i] : 0xFFFFFFFFu;

    error = __real_mmap(proc, uap, retval);

    entry_note_mmap(caller, args, (uint32_t)error,
                    (retval != 0) ? *retval : 0xFFFFFFFFu);
    return error;
}

/* ------------------------------------------------------ the deadline a process asks for (503) */
/*
 * **This is the first syscall in this walk whose point is that it *blocks*.** 479's `getpid` and 480's
 * `mmap` both answer immediately; `poll` with no descriptors sleeps until a deadline the kernel
 * computes, and the deadline is the object. `entry_ramdisk.s` makes two of them, five milliseconds
 * apart from forty, so that the answer has a shape rather than a value.
 *
 * The chain, and every link of it is a reading this run takes:
 *
 *     svc #0x80 (r12 = 230)  -> sysent[230].sy_call, which this wrapper occupies
 *     poll                   -> poll_nocancel -> kqueue_scan        (`kern_event.c:1666`, `:6100`)
 *     waitq_assert_wait64_leeway -> timer_call_enter_with_leeway on `thread->wait_timer`
 *                                                                (`osfmk/kern/waitq.c:2565`)
 *     thread_block_parameter -> the thread parks
 *     ... the virtual timer's interrupt 483 armed:
 *          rtclock_intr -> timer_intr -> timer_queue_expire     (`osfmk/arm/rtclock.c`,
 *                                                                `osfmk/arm/arm_timer.c`)
 *          -> thread_timer_expire -> clear_wait_internal(THREAD_TIMED_OUT)
 *                                                                (`osfmk/kern/sched_prim.c:639`)
 *     kqueue_scan's switch   -> EWOULDBLOCK, which `poll` maps to 0  (`sys_generic.c:1816-1820`)
 *
 * **The two clock readings are taken around the real call and nowhere else.** `entry_counter()` is
 * `mrrc p15, 0, lo, hi, c14` - the same counter `mach_absolute_time` reads on this target (454's
 * note above) - so `after - before` is the interval the kernel held this thread, measured with the
 * kernel's own clock and not with anything this image chose. Both ends are published as well as their
 * difference: the low word wraps every 3.7 minutes at 19.2 MHz and a difference taken across a wrap
 * would be a small positive number that looked like a fast return, whereas the pair says which
 * happened (`after` below `before` is the wrap and nothing else).
 *
 * **The declaration is the ABI and it is `sysproto.h`'s.** `out/xnu_generated/bsd/sys/sysproto.h:2483`
 * is `int poll(struct proc *, struct poll_args *, int *);`, `struct poll_args` is
 * `user_addr_t fds; u_int nfds; int timeout` (`:778-782`) and `unix_syscall` calls the slot as
 * `(*(callp->sy_call))(proc, &uthread->uu_arg[0], &uthread->uu_rval[0])`
 * (`bsd/dev/arm/systemcalls.c:174`), so the three words of `uap` are exactly those three fields, in
 * that order, marshalled by the slot's own munger. **That munger is the claim the arguments rest on**:
 * entry 230's is `munge_www` (`out/xnu_generated/init_sysent.c:1317`), which copies `r0..r2` and
 * nothing else - so the fixture's three register writes are the three arguments and there is no
 * padding word here to measure, unlike `mmap`'s. `tools/check_sysent_table.py` reads that word back
 * out of the linked image, and `tools/host_ramdisk_macho_check.py` reads the same two numbers and the
 * three register writes out of the fixture.
 *
 * **The wrapper returns the syscall's own `int` unchanged**, which is the defect 478 found in this
 * file's own `__wrap_thread_block`: a `void` shape ends in a tail call and leaves the caller's `r0`
 * holding whatever ran last, and here that would be the *fixture's* answer - it compares `r0` against
 * nothing, but the spin loop after it compares `r0` against the pid, so a wrong `r0` would be read as
 * the kernel answering the wrong process.
 *
 * `uap` of 0 is recorded as three `0xFFFFFFFF` words rather than dereferenced, for the reason
 * `__wrap_mmap` gives: the kernel never calls a slot that way, and an instrument that faults on a
 * state it was told cannot happen costs the run.
 */
int __real_poll(void *proc, void *uap, int *retval);

int __wrap_poll(void *proc, void *uap, int *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t fds = 0xFFFFFFFFu, nfds = 0xFFFFFFFFu, timeout = 0xFFFFFFFFu;
    uint32_t before, after;
    int error;

    if (given != 0) {
        fds = given[0];
        nfds = given[1];
        timeout = given[2];
    }

    before = entry_counter();
    error = __real_poll(proc, uap, retval);
    after = entry_counter();

    entry_note_poll(caller, fds, nfds, timeout, (uint32_t)error,
                    (retval != 0) ? (uint32_t)*retval : 0xFFFFFFFFu, before, after);
    return error;
}

/* ---------------------------------------------------- the first call a driver answers (504) */
/*
 * **`open` and `read`, and what makes this pair a step rather than two more syscall records.** Every
 * `svc` this fixture has made until now ended in the *kernel's* own machinery: `getpid` in the proc
 * table, `mmap` in the pmap, `poll` in the timer queue. `open("/dev/rmd0")` ends in `bsd/dev/memdev.c`
 * - in a `bdevsw`/`cdevsw` entry a driver installed before process 1 existed - and the `read` after it
 * ends in that driver's `uiomove64`. So the records below are the first in this walk whose *outcome*
 * depends on a device, which is why the pair is instrumented as two ends of one reading.
 *
 * **The declarations are `sysproto.h`'s, and the two slots' mungers are `munge_www`.**
 * `out/xnu_generated/bsd/sys/sysproto.h` is `int open(struct proc *, struct open_args *, int *);` and
 * `int read(struct proc *, struct read_args *, user_ssize_t *);` - so `open`'s `uap` is
 * `{user_addr_t path; int flags; int mode}` and `read`'s is `{int fd; user_addr_t cbuf; user_size_t
 * nbyte}`, three 4-byte words each on this 32-bit target, which is why both slots name `munge_www` and
 * why the fixture's three register writes per call are its three arguments with no padding word.
 * (`read`'s *return* is 64-bit, which does not touch the munger - that is generated from the argument
 * types - but it does mean the return path writes `save_r1` as well as `save_r0`, which is why the
 * fixture keeps its page address in `r9`.) `tools/check_sysent_table.py` reads both munger words back
 * out of the linked image.
 *
 * **`read` reads the *buffer* either side of the call, and that is the step's measurement.** The
 * fixture's `cbuf` is the page 480's `mmap` returned, into whose first word the fixture's own store
 * wrote the mapping's address - so `before` is a number this image can predict from the *other* keys
 * and `after` is what the driver wrote there. The value is the fixture's own `MH_MAGIC`: the RAM disk
 * these pages are is the file the kernel loaded the program from.
 *
 * **And that read is `copyin_word`, not a dereference, because 504's first run faulted on the
 * dereference and the fault is the finding.** Run A wrote `word_before = out[0]` - a plain kernel
 * load of the user address - and the kernel took a data abort at exactly that instruction
 * (`pc = __wrap_read+0xb8`, `DFSR = 0x5` = translation fault/section, `DFAR = 0x00102000` = the
 * fixture's own buffer). Nothing had armed `thread->recover`, so the handler had no recovery address
 * to point `pc` at: `xnu_live_sleh_seen` ran to its cap of 64 with `pc`/`far` repeating, and the run
 * has **no `xnu_live_read_*` key at all** - the load is one instruction before `bl __real_read`, so the
 * `read` never reached `mdevrw` and the record that would have said so is written after the call that
 * never happened.
 *
 * The mechanism is Apple's own and the image states it: a kernel-mode load of a user address is only
 * valid inside the copy paths. `COPYIO_HEADER(r0, L_copyin_word_fault)`, `COPYIO_SET_RECOVER()`,
 * `COPYIO_MAP_USER()` and the single `ldr`/`ldrd` between `COPYIO_UNMAP_USER()` and
 * `COPYIO_RESTORE_RECOVER()` (`osfmk/arm/machine_routines_asm.s:732-746`) are the whole of
 * `copyin_word`, and `COPYIO_MAP_USER` (`:548-561`) writes the thread's *user* page-table walk table
 * and ASID into `TTBR0`/`CONTEXTIDR` while `COPYIO_UNMAP_USER` (`:601-610`) puts the kernel's back.
 * The linked image is what says that pair is compiled in - `__ARM_USER_PROTECT__` is a `#define` and
 * the four `mcr p15, 0, ..., c2, c0, 0`/`c13, c0, 1` instructions are in this build's `copyout` at
 * `0x80015d8c-0x80015da0` and `0x80015df0-0x80015e00`, which is where dispatch's own `copyin_word`
 * `0x80015e14` is written the same way. **So a wrapper that skips all of that is not copying user
 * memory, it is dereferencing an address that happens to belong to a process** - which is why run A's
 * fault could not be serviced and why the retry repeated instead of returning `EFAULT`.
 *
 * `copyin_word(user_addr, uint64_t *kernel_addr, vm_size_t nbytes)` is the kernel's own answer to
 * precisely this (`osfmk/kern/misc_protos.h:101`): "Move an aligned 32 or 64-bit word from user space
 * to kernel space using a single read instruction ... think `*kernel_addr = *(uint32_t *)user_addr`".
 * It is the same call XNU's own `kern_event.c:2095`, `sys_ulock.c:455` and `thread_policy.c:2367` use
 * to read one user word, and its three argument widths are this target's: `user_addr_t` and
 * `user_size_t` are `u_int32_t` with `__arm64__` undefined (`bsd/arm/types.h:82-83`), so all three are
 * 4-byte words, and the kernel destination is 8 bytes wide because the same call serves a 64-bit read
 * - the value is 0-extended into it, so the low word is the word. Two of its own rules are visible in
 * the record rather than assumed: `nbytes` must be 4 or 8 and the user address must be aligned to it
 * (`:725-730`, `L_copyin_invalid` at `:746` returning `EINVAL`), and both are true of the fixture's
 * buffer, which is the page `mmap` returned. It is *not* in `TRACE_LDFLAGS`: it is called directly,
 * and `build_entry.sh` requires it to be defined by this image rather than stubbed for it.
 *
 * `copy_before`/`copy_after` are the two calls' returns, so the record says which of the two readings
 * is a reading: `0` with a word beside it, or the `EFAULT`/`EINVAL` that means the instrument could
 * not read the user's page at all - which is the case run A could not even print.
 *
 * **Neither wrapper branches on its answer and neither changes it.** Both return the syscall's own
 * `int` unchanged, for the reason `__wrap_poll` gives: this file's `__wrap_thread_block` once ended in
 * a tail call and left `r0` holding whatever ran last.
 */
int __real_open(void *proc, void *uap, int *retval);
int __real_read(void *proc, void *uap, void *retval);

/* `copyin_word`'s declaration, written here rather than taken from `<kern/misc_protos.h>` because
 * this file includes no XNU header: it is the ABI above, with this target's `user_addr_t` spelled as
 * the 4-byte word `bsd/arm/types.h` defines it to be. The kernel buffer is `uint64_t` because
 * `copyin_word` requires an 8-byte destination and zero-extends a 32-bit read into it. */
extern int copyin_word(const uint32_t user_addr, uint64_t *kernel_addr, uint32_t nbytes);

int __wrap_open(void *proc, void *uap, int *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t path = 0xFFFFFFFFu, flags = 0xFFFFFFFFu, mode = 0xFFFFFFFFu;
    uint32_t fd = 0xFFFFFFFFu;
    int error;

    if (given != 0) {
        path = given[0];
        flags = given[1];
        mode = given[2];
    }

    error = __real_open(proc, uap, retval);
    if (retval != 0)
        fd = (uint32_t)*retval;

    entry_note_open(caller, path, flags, mode, (uint32_t)error, fd);
    return error;
}

int __wrap_read(void *proc, void *uap, void *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t fd = 0xFFFFFFFFu, buf = 0xFFFFFFFFu, nbytes = 0xFFFFFFFFu;
    uint32_t lo = 0xFFFFFFFFu, hi = 0xFFFFFFFFu;
    uint32_t word_before = 0xFFFFFFFFu, word_after = 0xFFFFFFFFu;
    uint32_t copy_before = 0xFFFFFFFFu, copy_after = 0xFFFFFFFFu;
    int error;

    if (given != 0) {
        fd = given[0];
        buf = given[1];
        nbytes = given[2];
    }

    /*
     * The two reads of the user's buffer, both through the kernel's own copy path, both taken with
     * the length the fixture asked for. `word` is a `uint64_t` because `copyin_word` requires one and
     * zero-extends a 32-bit read into it; only the low word is published, which is the word.
     */
    if (buf != 0u) {
        uint64_t word = 0;

        copy_before = (uint32_t)copyin_word(buf, &word, 4u);
        if (copy_before == 0u)
            word_before = (uint32_t)word;
    }

    error = __real_read(proc, uap, retval);

    if (buf != 0u) {
        uint64_t word = 0;

        copy_after = (uint32_t)copyin_word(buf, &word, 4u);
        if (copy_after == 0u)
            word_after = (uint32_t)word;
    }
    if (retval != 0) {
        const uint32_t *words = (const uint32_t *)retval;
        lo = words[0];
        hi = words[1];
    }

    entry_note_read(caller, fd, buf, nbytes, (uint32_t)error, lo, hi,
                    word_before, word_after, copy_before, copy_after);
    return error;
}

/* ---------------------------------------------- the second process, and the death that is not fatal (505) */
/*
 * **504's four calls are all the kernel doing work for *one* process. These three are the kernel doing
 * work for a process that did not exist before the run started.**
 *
 * `entry_ramdisk.s` grows nine instructions: `mov r0, #0` / `mov r12, #SYS_FORK` / `svc #0x80`, then
 * `cmp r0, #0` / `beq entry_child` / `b spin`, and at `entry_child` the three of `exit`. Every record
 * below is a number that has to agree with the fixture's branch for the step to mean what it says, and
 * none of the three wrappers can be predicted from the others.
 *
 * ------------------------------------------------------------------ `fork`: the pid the OS made
 *
 * `2 AUE_FORK ALL { int fork(void) }` reaches `sysent[2].sy_call` - `bsd/kern/kern_fork.c:872` - and
 * the slot takes **no argument**: `sy_narg` is 0 and `sy_arg_munge32` is NULL
 * (`out/xnu_generated/init_sysent.c`'s own line for 2), so `arm_get_syscall_args` is never called for
 * this index and `uu_arg[0]` is whatever the *previous* syscall left there. That is why this wrapper
 * publishes `uap0` beside the return: the fixture's `mov r0, #0` is a word the kernel does not read,
 * and the word in the buffer it does not read is the control `open`'s path address, which the `open`
 * record published a moment earlier. The two readings together are what rule out "the child's zero
 * came from its argument buffer" - which would be the only other way a single call could return twice.
 *
 * The two halves of the return are `retval[0]` and `retval[1]`: `fork` writes `retval[0] =
 * child_proc->p_pid` and sets `retval[1] = 0` as "flag parent return for user space"
 * (`kern_fork.c:878`, `:885`), and `arm_prepare_u32_syscall_return` copies both into `save_r0` and
 * `save_r1` (`bsd/dev/arm/systemcalls.c:293`). So `ret_hi = 0` is a prediction, and the pid is the
 * **first pid this machine has ever made for a request from user mode**: process 1 came out of
 * `kernproc` (`bsd/kern/bsd_init.c:1147`), so `nprocs` has been 1 since the boot began and 2 is the
 * only number this can be.
 *
 * ------------------------------------------------------------------ `exit`: the death, published before it happens
 *
 * `1 AUE_EXIT ALL { void exit(int rval) }` reaches `sysent[1].sy_call`, and that function is
 * `__attribute__((noreturn))` (`bsd/kern/kern_exit.c:677`) - it ends in `thread_exception_return()`
 * and the thread is marked terminated, so **a record written after `__real_exit` would never be
 * written**. This wrapper therefore publishes *before* the call, which is the opposite of every other
 * wrapper in this file and is a property of the callee rather than a choice: the alternative is a
 * record whose absence cannot be told apart from a call that was never made.
 *
 * What it publishes is the caller's own identity - `proc_pid(proc)`, the process's pid read out of the
 * `proc` the dispatcher handed the slot - and the fixture's `rval` as the munger marshalled it
 * (`munge_w`, one 4-byte word). The pid is the step's load-bearing number: the same 2 the `fork`
 * record published, read at a different point of the kernel's call graph, from the *child's* side.
 *
 * ------------------------------------------------------------------ `psignal`: the OS telling pid 1
 *
 * The third record is the one that says the death was *processed* rather than merely requested, and
 * it is deliberately not in the exit path's own object. `proc_prepareexit`
 * (`kern_exit.c:838-850`) - the function whose first statement is the `if (p == initproc)
 * launchd_crashed_panic(...)` that ended 478's run - **cannot be wrapped**: it is defined in
 * `kern_exit.c` and called from `kern_exit.c`, so the reference is resolved inside its own object and
 * `--wrap` never sees it (455's lesson, and the census in `build_entry.sh` classifies such a name as
 * `same-object`). What can be wrapped is what the non-init arm *reaches afterwards*:
 * `psignal(pp, SIGCHLD)` at `kern_exit.c:1443`, in `proc_exit`, which runs on the child's thread once
 * the last thread of the task terminates - **downstream of the branch that panics**, so a SIGCHLD
 * record for this run is the proof that the branch was not taken, and `pp` is the parent, i.e. pid 1.
 *
 * `psignal` is defined in `kern_sig.c` and called from `kern_exit.c`, so this one *is* wrappable, and
 * it is the first record in this walk that is **about two processes at once**: `xnu_live_sigchld_from`
 * is `current_proc()`'s pid - the dying child, 2 - and `xnu_live_sigchld_to` is the argument's, the
 * parent, 1. The signal number is `SIGCHLD` = 20 (`bsd/sys/signal.h:108`), and the filter on it is
 * deliberate: `psignal` is the boot's general signal path, so the wrapper publishes **only** the
 * SIGCHLD deliveries and counts the rest, which is what `xnu_live_psignal_calls` reports - a record of
 * every signal the boot sends would be a record of the boot, and the claim here is about one.
 *
 * The guard on `current_proc()` is not decoration: a signal sent from a task with no BSD process
 * behind it would make that pointer 0, and `proc_pid(0)` is a dereference of a null pointer - 504's
 * run A is the standing reminder of what an instrument that faults on a state it did not expect costs
 * the run.
 */
#define STAGE90_SIGCHLD 20u

/* `int proc_pid(proc_t)`, written with this file's `void *` spelling of a XNU pointer type for the
 * reason `copyin_word`'s declaration gives: no XNU header is included here, and every pointer this
 * target has is 4 bytes. `proc_pid` is a real function of the image (there is no `--wrap` on it), and
 * `build_entry.sh`'s `xnu_entry_505` clause requires it to be defined by this image rather than
 * stubbed for it - a stand-in would make both pids facts about the stand-in. */
extern int proc_pid(void *proc);
extern void *current_proc(void);

int __real_fork(void *proc, void *uap, int *retval);
int __wrap_fork(void *proc, void *uap, int *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t uap0 = (given != 0) ? given[0] : 0xFFFFFFFFu;
    uint32_t lo = 0xFFFFFFFFu, hi = 0xFFFFFFFFu;
    int error = __real_fork(proc, uap, retval);

    if (retval != 0) {
        const uint32_t *words = (const uint32_t *)retval;

        lo = words[0];
        hi = words[1];
    }

    entry_note_fork(caller, uap0, (uint32_t)error, lo, hi);
    return error;
}

/* `exit` is `void` and noreturn in the kernel; the declaration here is the ABI the dispatcher uses -
 * `(*(callp->sy_call))(proc, &uthread->uu_arg[0], &uthread->uu_rval[0])`,
 * `bsd/dev/arm/systemcalls.c:174` - and the `int` the wrapper returns is never used, because
 * `sysent[1].sy_return_type` is `_SYSCALL_RET_NONE` (`arm_prepare_u32_syscall_return` writes 0 for
 * it). It is spelled `int` rather than `void` so that the wrapper is not a tail call: 478's defect. */
void __real_exit(void *proc, void *uap, int *retval);
int __wrap_exit(void *proc, void *uap, int *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t rval = (given != 0) ? given[0] : 0xFFFFFFFFu;
    uint32_t pid = (uint32_t)proc_pid(proc);

    /* Before the call, because the call does not come back - see the header. */
    entry_note_exit(caller, pid, rval);

    __real_exit(proc, uap, retval);

    /* Not reached in any run this step is for. It is written anyway, and what it would mean is
     * written down: a record after this line says `exit` returned to its caller. */
    entry_note_exit_returned(pid);
    return 0;
}

void __real_psignal(void *proc, int signum);
void __wrap_psignal(void *proc, int signum)
{
    uint32_t to;

    g_psignal_calls++;
    if ((uint32_t)signum == STAGE90_SIGCHLD) {
        void *me = current_proc();
        uint32_t from = (me != 0) ? (uint32_t)proc_pid(me) : 0xFFFFFFFFu;

        to = (proc != 0) ? (uint32_t)proc_pid(proc) : 0xFFFFFFFFu;
        entry_note_sigchld(from, to, (uint32_t)signum, g_psignal_calls);
        __real_psignal(proc, signum);
        entry_note_sigchld_returned(from, to);
        return;
    }

    __real_psignal(proc, signum);
}

/* ---------------------------------------------------- the parent asks for its child (508) */
/*
 * **`wait4`, and it is the third of the three calls a Unix process's life is: `fork`, `exit`, `wait`.**
 * 505 made the process and 507 watched the kernel tell its parent it was gone; what is missing at the
 * end of 507's run is the parent *asking*, and the answer that only this call can produce - the word
 * the kernel composed out of the child's exit status. The child is a zombie nothing has reaped, so
 * this is also the step that measures the free side of the corpse path 505's run stopped in.
 *
 * **The four words are read raw and published, for the reason 505's `uap0` is.** `sysent[7]`'s munger
 * is `munge_wwww` (`out/xnu_generated/init_sysent.c`) - four words, contiguously - and this file
 * includes no XNU header, so `struct wait4_args` is spelled here as what the object says it is: the
 * four words at byte offsets 0, 4, 8 and 12 that `wait4_nocancel` loads with `ldr r0, [r6]`,
 * `ldr r1, [r6, #4]`, `ldrb r0, [r6, #8]` and `ldr r1, [r6, #12]`. Publishing all four rather than
 * the two the record names is deliberate: the record's own *values* are then the evidence that the
 * marshalling is what the fixture's header derives, and a `wait4` whose arguments had moved would
 * show up as a word in the wrong key rather than as a silent difference.
 *
 * **`_who` is the *waiter*, and it is the other side of 507's record.** `proc_pid(proc)` on the
 * process the dispatcher handed the slot is 1 - the same process 507's `xnu_live_sigchld_to` named as
 * the recipient - and `_pid` is the argument: the child, 2. So one line says who asked, about whom,
 * with what, and a second says what came back.
 *
 * **The word behind the pointer is read back through the kernel's own copy path, after the call.**
 * 504's rule, for 504's reason: a plain load of a user address is a data abort in the wrapper if the
 * fixture's page is not mapped when this runs, and `copyin_word` answers `EFAULT` instead - so
 * `_copy_error` is the record that says whether `_status` is a reading of the user's page or of
 * nothing. Reading it *after* the call is the whole point: before it, the word is whatever the
 * fixture's own read left there (this file's magic), and the step's claim is about the word the
 * kernel wrote through the pointer.
 *
 * **`_ticks` is `entry_counter()` at the two ends of the real call, and it is the third kind of
 * reading in this step.** The child was made microseconds before the parent asks, so the first scan
 * may find it still exiting; the source then sleeps on `(caddr_t)q` and the child's own `proc_exit`
 * wakes it (`wakeup((caddr_t)pp)`, `kern_exit.c:1446-1448`, under the same list lock). A non-trivial
 * count here is that block measured - the same counter 503's `xnu_live_poll_ticks` publishes, whose
 * scale is known from those two calls - and a handful of ticks is the other answer: the child was
 * already a zombie and nothing parked.
 *
 * **The declaration is `sysproto.h`'s**: `int wait4(struct proc *, struct wait4_args *, int *);` -
 * `retval` is an `int *` and not a `user_ssize_t *` like `read`'s, so the return path writes `r0`
 * alone. `tools/check_sysent_table.py` checks the slot's munger and its two counts, and
 * `tools/host_ramdisk_macho_check.py` checks the four registers against the master's prototype; this
 * wrapper is the third side of that agreement, and it is the only one that sees the kernel's answer.
 */
int __real_wait4(void *proc, void *uap, int *retval);
int __wrap_wait4(void *proc, void *uap, int *retval)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint32_t *given = (const uint32_t *)uap;
    uint32_t pid = 0xFFFFFFFFu, status_ptr = 0u, options = 0xFFFFFFFFu, rusage = 0xFFFFFFFFu;
    uint32_t who = (uint32_t)proc_pid(proc);
    uint32_t seq, before, after, copy_error = 0xFFFFFFFFu;
    uint64_t word = 0;
    int error;

    if (given != 0) {
        pid = given[0];
        status_ptr = given[1];
        options = given[2];
        rusage = given[3];
    }

    seq = entry_note_wait(caller, who, pid, status_ptr, options, rusage);
    before = entry_counter();
    error = __real_wait4(proc, uap, retval);
    after = entry_counter();

    if (status_ptr != 0u) {
        copy_error = (uint32_t)copyin_word(status_ptr, &word, 4u);
    }

    entry_note_wait_returned(seq, (uint32_t)error,
                             (retval != 0) ? (uint32_t)retval[0] : 0xFFFFFFFFu,
                             copy_error, (uint32_t)word, after - before);
    return error;
}

/* ---------------------------------------------------- the kernel's own timer deadline (481) */
/*
 * **481's reading is a chain with two ends, and this is the kernel's end of it.**
 *
 * `setPop` (`osfmk/arm/rtclock.c:344`) is where the machine-independent timer queue becomes a
 * machine-dependent one: `timer_resync_deadlines` (`osfmk/arm/arm_timer.c`) picks the nearest of the
 * three deadlines `struct cpu_data` holds and calls it, and `setPop` converts that absolute time into
 * a decrementer countdown (`deadline_to_decrementer`) and hands it to `ml_set_decrementer`. So the
 * wrapper's `time` is the deadline the kernel chose and its return value is the only number in the
 * system that says what the kernel believes it programmed.
 *
 * The other end is `entry_timebase.c`'s `stage90_tbd_set_decrementer`, which records the value it was
 * *handed*. **The two are the same number and that is the reading** - one is computed by the kernel's
 * own arithmetic from an absolute deadline and the other is what arrived in the writer's register, so
 * a chain of two independent records joins the timer queue to the hardware. Before 481 that second
 * end did not exist: `ml_set_decrementer` stored the value into `cpu_data` and programmed nothing,
 * because `CPU_SET_DECREMENTER_FUNC` was NULL for the whole boot (see `entry_timebase.c`).
 *
 * **Why the return value and not a second computation.** Recomputing `deadline_to_decrementer` here
 * would make the instrument agree with itself. `__real_setPop` is called and its return recorded, so
 * the number in the log is the kernel's own.
 *
 * `setPop` is called from `timer_resync_deadlines`, which is called from `timer_intr` on every timer
 * interrupt, from `timer_set_deadline`, and from `arm_init`. That is a bounded set of sites but an
 * unbounded number of calls, so this records the first call's three numbers and then only powers of
 * two - the same channel budget discipline as `xnu_live_getpid_count` and `xnu_live_mmap_*`.
 *
 * **The return type is `int` because Apple's is** (`int setPop(uint64_t time)`,
 * `osfmk/arm/rtclock.c:343`). It is one register either way, so this is not a behaviour - it is that
 * `__wrap_`/`__real_` are a *linker* substitution of one symbol for another, and a declaration that
 * disagreed with the definition would be this project's "one value, two definitions" applied to a
 * function's *type*, where the compiler cannot see the disagreement because the definition is in
 * another object. The bits are handed to the record as an unsigned word because that is what the
 * writer receives (`ml_set_decrementer(uint32_t dec_value)`), and the two ends are compared as bits
 * rather than as values.
 */
int __real_setPop(uint64_t time);

int __wrap_setPop(uint64_t time)
{
    int decr = __real_setPop(time);

    entry_timebase_note_setpop(time, (uint32_t)decr);
    return decr;
}

/* ------------------------------------------------- 484: the arm-er, in four forms and one table */
/*
 * The census in `entry_timebase.c` counts *what* the kernel armed. These wrappers say *who* asked,
 * and the reason that has to be a separate instrument is in `entry_timebase.c`'s comment above the
 * four source numbers: `setPop` receives an absolute time, not its provenance, and the same number
 * means "the scheduler is preempting a thread that is running" or "a thread is waiting for a deadline"
 * depending on which of the three fields in `struct cpu_data` it came out of.
 *
 * **The prototypes below are transcriptions, and the transcription is the fragile part.** None of
 * these files can include `osfmk/kern/timer_call.h` - it needs the kernel's include path, the same
 * reason `entry_timebase.c` spells `tbd_ops_t` out - so each of the six declarations here is a claim
 * about Apple's, and `tools/check_timer_sources.py` asserts that claim against the header for argument
 * *count*, argument *width* and return width. The widths matter more than they look: on AAPCS a
 * `uint64_t` occupies an even-numbered register *pair*, so a prototype that spelled `deadline` as
 * `uint32_t` would shift every later argument by one register and the log would carry a `flags` word
 * read out of the middle of a deadline - `__wrap_mdevadd`'s comment above records the same hazard
 * costing this project a run once already. `timer_call_param_t` is `void *` (`timer_call.h:66`) and
 * `timer_call_t` is `struct timer_call *`, both one word here, which is why they are spelled as
 * pointers rather than as integers.
 *
 * **What each wrapper records, and the one it deliberately does not.** Sources 1, 2 and 3 are the
 * three ways a timer queue entry is made; source 4 is `timer_call_quantum_timer_enter`, which is the
 * only caller of `quantum_timer_set_deadline` (`timer_call.c:732`; the cancel path at `:762` passes
 * zero and so never becomes a deadline). All four pass the *requested* deadline, not the field it will
 * end up in - the field is written inside the real function. The `flags` word is recorded because
 * `TIMER_CALL_LOCAL` is one of its bits and a local timer never reaches `timer_resync_deadlines` at
 * all, so a reading of the flags is what separates "this timer was armed and ignored" from "this timer
 * was armed and became the decrementer".
 *
 * **`timer_call_setup` is published in full and unconditionally**, which is the opposite of the
 * budget discipline the counters use: it is called once per timer (about fifteen times in the whole
 * kernel) and its second argument is the callback, so the sequence of these records *is* the table
 * that lets the `call` pointers in the enter records be resolved to names. A sampled version of it
 * would be a table with holes in it.
 *
 * **`thread_quantum_expire` is the fifth wrapper and the one with the interesting risk.** Its only
 * reference in the tree is `processor.c:163`, where the *address* is handed to `timer_call_setup` -
 * so this is not a call site but a function pointer, and whether `--wrap` reaches it is a property of
 * the link rather than of the source. It does: `--wrap` redirects an undefined *reference*, and
 * taking a symbol's address is one. The consequence is worth writing down because it changes what the
 * setup table holds - the func word published for the quantum timer will be `__wrap_thread_quantum_expire`'s
 * address and not `thread_quantum_expire`'s, which is why `check_timer_sources.py` reads that pair out
 * of the linked image instead of assuming either address.
 */
int __real_timer_call_enter(void *call, uint64_t deadline, uint32_t flags);

int __wrap_timer_call_enter(void *call, uint64_t deadline, uint32_t flags)
{
    int r = __real_timer_call_enter(call, deadline, flags);

    entry_timebase_note_timer_enter(1u, (uint32_t)(uintptr_t)call, deadline, flags);
    return r;
}

/* **There is no `__wrap_timer_call_enter1` here, and its absence is the first build's finding.**
 * `timer_call_enter1` is the family's second member and its callers in this tree are `sfi.c`'s seven
 * and `dtrace_glue.c`'s three - Selective Forced Idle, which is not compiled for ARM, and dtrace,
 * which is not in this configuration. So the wrap was written, the build refused it, and rightly:
 * `build_entry.sh`'s reachability check found no branch to the wrapper anywhere in the linked image,
 * because a `--wrap` on a name nothing references produces a wrapper nothing can call. Adding it to
 * that check's `never-called` list would have made the refusal quiet for every later step, so the
 * wrapper is gone and what replaced it is a claim in `tools/check_timer_sources.py`: it reads this
 * tree's callers of `timer_call_enter1` and refuses the build if the family ever gains one here, which
 * is the event that would make this omission wrong. */
int __real_timer_call_enter_with_leeway(void *call, void *param1, uint64_t deadline,
                                        uint64_t leeway, uint32_t flags, uint32_t ratelimited);

int __wrap_timer_call_enter_with_leeway(void *call, void *param1, uint64_t deadline,
                                        uint64_t leeway, uint32_t flags, uint32_t ratelimited)
{
    int r = __real_timer_call_enter_with_leeway(call, param1, deadline, leeway, flags, ratelimited);

    entry_timebase_note_timer_enter(2u, (uint32_t)(uintptr_t)call, deadline, flags);
    return r;
}

int __real_timer_call_quantum_timer_enter(void *call, void *param1, uint64_t deadline,
                                          uint64_t ctime);

int __wrap_timer_call_quantum_timer_enter(void *call, void *param1, uint64_t deadline,
                                          uint64_t ctime)
{
    int r = __real_timer_call_quantum_timer_enter(call, param1, deadline, ctime);

    /* `flags` is sent as zero rather than read: this entry point is the one that does *not* take a
     * flags argument - it hard-codes `TIMER_CALL_SYS_CRITICAL | TIMER_CALL_LOCAL` for itself
     * (`timer_call.c:715`) - so a recorded zero here is the absence of the argument and not a reading
     * of it, and the note function's own documentation says so. */
    entry_timebase_note_timer_enter(3u, (uint32_t)(uintptr_t)call, deadline, 0u);
    return r;
}

void __real_timer_call_setup(void *call, void *func, void *param0);

void __wrap_timer_call_setup(void *call, void *func, void *param0)
{
    __real_timer_call_setup(call, func, param0);

    entry_timebase_note_timer_setup((uint32_t)(uintptr_t)call, (uint32_t)(uintptr_t)func,
                                    (uint32_t)(uintptr_t)param0);
}

void __real_thread_quantum_expire(void *param0, void *param1);

void __wrap_thread_quantum_expire(void *param0, void *param1)
{
    __real_thread_quantum_expire(param0, param1);

    /* `param1` is the thread and `param0` the processor: `priority.c:99` names the two
     * `processor = p0; thread = p1;` in its first two statements, and `processor.c:163` sets the timer
     * up with the processor as `param0`, so the thread arrives in the second argument. */
    entry_timebase_note_quantum_expire((uint32_t)(uintptr_t)param1);
}

/* ------------------------------------------------------------ the IOKit deadline sleeps (454) */
/*
 * 453 ended with a negative - the boot thread's `lck_mtx_sleep_deadline` wait came through none of the
 * seven sleep entries - and with the image's own answer to what it did come through:
 * `lck_mtx_sleep_deadline` has exactly three callers there (`_sleep+0x21c`, `IOLockSleepDeadline+0x20`,
 * `IORecursiveLockSleepDeadline+0x38`), so wrapping these two wraps the whole remaining path, exactly
 * as wrapping the seven wrapped the whole sleep family. Both are global, both are
 * `int (lock, event, AbsoluteTime deadline, UInt32 interType)`, and the disassembly fixes the ABI: each
 * stores the `{r2, r3}` pair - the `AbsoluteTime` - straight to the stack for the call below and loads
 * `interType` from `[sp, #16]` (or `[sp, #24]`, with one more saved register), so a `uint64_t` third
 * parameter is the same function as far as the calling convention is concerned, and nothing here
 * changes an argument or a return value.
 *
 * What the wrapper adds is the *other* end of the wait: the deadline it was given, and the counter it
 * will be compared against (`entry_counter()`). `assert_wait_deadline` arms the thread's own timer
 * with that deadline (`osfmk/kern/locks.c:892`), and a thread timer is delivered by a timer interrupt,
 * of which this boot has none - so the pair `(deadline, now)` is what tells a boot that is waiting for
 * a timer that cannot exist from one waiting for a publisher that never came.
 */
int __real_IOLockSleepDeadline(void *lock, void *event, uint64_t deadline, uint32_t interType);
int __wrap_IOLockSleepDeadline(void *lock, void *event, uint64_t deadline, uint32_t interType)
{
    entry_note_iolock(0u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                      (uint32_t)(uintptr_t)lock, (uint32_t)(uintptr_t)event, interType,
                      (uint32_t)deadline, (uint32_t)(deadline >> 32), entry_counter());
    return __real_IOLockSleepDeadline(lock, event, deadline, interType);
}

int __real_IORecursiveLockSleepDeadline(void *lock, void *event, uint64_t deadline,
                                        uint32_t interType);
int __wrap_IORecursiveLockSleepDeadline(void *lock, void *event, uint64_t deadline,
                                        uint32_t interType)
{
    entry_note_iolock(1u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                      (uint32_t)(uintptr_t)lock, (uint32_t)(uintptr_t)event, interType,
                      (uint32_t)deadline, (uint32_t)(deadline >> 32), entry_counter());
    return __real_IORecursiveLockSleepDeadline(lock, event, deadline, interType);
}

/* --------------------------------------------------------------- the sleep family (453) */
/*
 * `_sleep` is `static` in `bsd/kern/kern_synch.c` and `lck_mtx_lock_contended` is a local symbol
 * (`nm` says `t`), so neither can be wrapped however the link is spelled - `--wrap` renames an
 * undefined reference, and a call inside the defining object, or to a local symbol, is neither. What
 * *can* be wrapped is the seven global entries into `_sleep`, and a `grep -n '_sleep('` over that
 * file finds exactly seven call sites beside `_sleep`'s own definition (151) and the two calls
 * inside it (199 is `lck_mtx_sleep`, which is not `_sleep` at all): 311, 328, 346, 357, 371, 386 and
 * 397. All seven are global, and all seven are in the image as `T` - `sleep`, `msleep`, `msleep0`,
 * `msleep1`, `tsleep`, `tsleep0`, `tsleep1`. Wrapping them is therefore complete for the whole
 * family, and each wrapper's own `lr` is the frame `entry_note_block` cannot reach: the function that
 * asked to sleep, one call above `_sleep`.
 *
 * Nothing here changes an argument or a return value. The `panic` `lck_mtx_sleep_deadline` contains
 * is on its own validation path, two calls below this one, and is reached exactly as before.
 *
 * The argument spellings are the source's own (`bsd/kern/kern_synch.c`), which is what keeps the
 * wrappers ABI-identical: `msleep` takes a `struct timespec *` while `msleep0`, `tsleep` and
 * `tsleep0` take an `int` timeout in seconds, and `msleep1`/`tsleep1` a `u_int64_t` deadline - so
 * they are declared as such rather than all as `void *`. The last argument of `msleep0`, `tsleep0`
 * and `tsleep1` is a continuation and is *not* recorded: it is a code address only for the sleeps
 * that ask to be resumed, and `chan`/`wmsg` already name the wait.
 */
int __real_sleep(void *chan, int pri);
int __wrap_sleep(void *chan, int pri)
{
    entry_note_sleep(0u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, 0u, (uint32_t)pri, 0u);
    return __real_sleep(chan, pri);
}

int __real_msleep(void *chan, void *mtx, int pri, const char *wmsg, void *ts);
int __wrap_msleep(void *chan, void *mtx, int pri, const char *wmsg, void *ts)
{
    entry_note_sleep(1u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, (uint32_t)(uintptr_t)wmsg, (uint32_t)pri,
                     (uint32_t)(uintptr_t)ts);
    return __real_msleep(chan, mtx, pri, wmsg, ts);
}

int __real_msleep0(void *chan, void *mtx, int pri, const char *wmsg, int timo, void *continuation);
int __wrap_msleep0(void *chan, void *mtx, int pri, const char *wmsg, int timo, void *continuation)
{
    entry_note_sleep(2u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, (uint32_t)(uintptr_t)wmsg, (uint32_t)pri,
                     (uint32_t)timo);
    return __real_msleep0(chan, mtx, pri, wmsg, timo, continuation);
}

/*
 * `msleep1` and `tsleep1` take the deadline as a `u_int64_t`, which on this ABI is a register pair -
 * so the wrapper's last parameter is `uint64_t` and the report carries its **low half**, which is
 * what a `.bss` word can hold. Read it with that in mind: a deadline whose low half is 0 and whose
 * high half is not would be 2^32 ticks away rather than "no deadline", and `mach_absolute_time`'s
 * counter on a 19.2 MHz timebase would have to run for years to get there, so a 0 in this slot is a
 * 0 in the argument.
 *
 * `tsleep0` is *not* one of those: the source passes `int timo`, a timeout in seconds, and computes
 * the deadline itself (`kern_synch.c:375-386`). Its shape is `msleep0`'s, not `msleep1`'s, and the
 * first draft of this step got that wrong from the name - which is why the table in `entry_stubs.c`
 * lists each id's fifth argument beside its source lines rather than by analogy.
 */
int __real_msleep1(void *chan, void *mtx, int pri, const char *wmsg, uint64_t abstime);
int __wrap_msleep1(void *chan, void *mtx, int pri, const char *wmsg, uint64_t abstime)
{
    entry_note_sleep(3u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, (uint32_t)(uintptr_t)wmsg, (uint32_t)pri,
                     (uint32_t)abstime);
    return __real_msleep1(chan, mtx, pri, wmsg, abstime);
}

int __real_tsleep(void *chan, int pri, const char *wmsg, int timo);
int __wrap_tsleep(void *chan, int pri, const char *wmsg, int timo)
{
    entry_note_sleep(4u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, (uint32_t)(uintptr_t)wmsg, (uint32_t)pri,
                     (uint32_t)timo);
    return __real_tsleep(chan, pri, wmsg, timo);
}

int __real_tsleep0(void *chan, int pri, const char *wmsg, int timo, void *continuation);
int __wrap_tsleep0(void *chan, int pri, const char *wmsg, int timo, void *continuation)
{
    entry_note_sleep(5u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, (uint32_t)(uintptr_t)wmsg, (uint32_t)pri,
                     (uint32_t)timo);
    return __real_tsleep0(chan, pri, wmsg, timo, continuation);
}

int __real_tsleep1(void *chan, int pri, const char *wmsg, uint64_t abstime, void *continuation);
int __wrap_tsleep1(void *chan, int pri, const char *wmsg, uint64_t abstime, void *continuation)
{
    entry_note_sleep(6u, (uint32_t)(uintptr_t)__builtin_return_address(0), entry_thread(),
                     (uint32_t)(uintptr_t)chan, (uint32_t)(uintptr_t)wmsg, (uint32_t)pri,
                     (uint32_t)abstime);
    return __real_tsleep1(chan, pri, wmsg, abstime, continuation);
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

/* ------------------------------------------------------------ the registry query (455) */
/*
 * Six wrappers. The first two are the step: `copyExistingServices` is what `waitForMatchingService`
 * asks before it sleeps, and its return value is the answer - a `bl` from six places in the image,
 * one of them `waitForMatchingService+0x3c`. `matchPassive` is the verdict the fast path asks the
 * resource root for; it is non-virtual too (`bl` from six places, two inside `copyExistingServices`)
 * and its `this` is the filter that matters - the plane search calls it on *candidates*, never on
 * `gIOResources`, which is why the filter is a pointer comparison and not a call site (which the next
 * relink would move).
 *
 * The other four are the dictionary factories. A match record on its own says *that* a dictionary
 * found nothing; these say *what was asked for*, because each returns the `OSDictionary *` it built
 * and that pointer pairs the two records by value. `IOFindBSDRoot` builds its dictionary with the
 * `OSString *` overload (`serviceMatching(gIOResourcesKey)`, `IOKitBSDInit.cpp:377`) and
 * `IOKitInitializeTime` with the `const char *` one (`resourceMatching("IORTC")`,
 * `IOStartIOKit.cpp:74`), so the two overloads are both on this path and both are wrapped.
 *
 * Nothing here changes an argument or a return value, and `copyExistingServices` is the first wrapper
 * in this file whose recorded value *is* the measurement: the filesystem's answer of "nothing
 * matched" is the thing being measured, not a side effect of it.
 */
void *__real__ZN9IOService20copyExistingServicesEP12OSDictionarymm(void *matching, uint32_t in_state,
                                                                   uint32_t options);
void *__wrap__ZN9IOService20copyExistingServicesEP12OSDictionarymm(void *matching, uint32_t in_state,
                                                                   uint32_t options)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *result = __real__ZN9IOService20copyExistingServicesEP12OSDictionarymm(matching, in_state,
                                                                               options);

    entry_note_match(site, (uint32_t)(uintptr_t)matching, in_state, options,
                     (uint32_t)(uintptr_t)result);
    return result;
}

int __real__ZN9IOService12matchPassiveEP12OSDictionaryj(void *self, void *table, uint32_t options);
int __wrap__ZN9IOService12matchPassiveEP12OSDictionaryj(void *self, void *table, uint32_t options)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int result = __real__ZN9IOService12matchPassiveEP12OSDictionaryj(self, table, options);

    entry_note_mpass(site, (uint32_t)(uintptr_t)table, options, (uint32_t)result,
                     (uint32_t)(uintptr_t)self);
    return result;
}

void *__real__ZN9IOService15serviceMatchingEPK8OSStringP12OSDictionary(const void *name,
                                                                       void *table);
void *__wrap__ZN9IOService15serviceMatchingEPK8OSStringP12OSDictionary(const void *name, void *table)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *result = __real__ZN9IOService15serviceMatchingEPK8OSStringP12OSDictionary(name, table);

    entry_note_dict(site, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)table,
                    (uint32_t)(uintptr_t)result);
    return result;
}

void *__real__ZN9IOService15serviceMatchingEPKcP12OSDictionary(const char *name, void *table);
void *__wrap__ZN9IOService15serviceMatchingEPKcP12OSDictionary(const char *name, void *table)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *result = __real__ZN9IOService15serviceMatchingEPKcP12OSDictionary(name, table);

    entry_note_dict(site, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)table,
                    (uint32_t)(uintptr_t)result);
    return result;
}

void *__real__ZN9IOService16resourceMatchingEPK8OSStringP12OSDictionary(const void *name,
                                                                        void *table);
void *__wrap__ZN9IOService16resourceMatchingEPK8OSStringP12OSDictionary(const void *name, void *table)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *result = __real__ZN9IOService16resourceMatchingEPK8OSStringP12OSDictionary(name, table);

    entry_note_dict(site, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)table,
                    (uint32_t)(uintptr_t)result);
    return result;
}

void *__real__ZN9IOService16resourceMatchingEPKcP12OSDictionary(const char *name, void *table);
void *__wrap__ZN9IOService16resourceMatchingEPKcP12OSDictionary(const char *name, void *table)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *result = __real__ZN9IOService16resourceMatchingEPKcP12OSDictionary(name, table);

    entry_note_dict(site, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)table,
                    (uint32_t)(uintptr_t)result);
    return result;
}

/* ------------------------------------------------------------ the candidate set (457) */
/*
 * One wrapper, and it is the reading 456's doc named as the one the next experiment that is not the
 * timer had to take: **what `gIOCatalogue->findDrivers(gIOResources)` returns on this boot.**
 *
 * The call is in `IOService::doServiceMatch` and its result *is* `matches`, which the same function
 * branches on one screen later (`IOService.cpp:3688` then `:3724`) - so the count is not a proxy for
 * the decision, it is the decision's operand. `findDrivers` is non-virtual, defined in
 * `IOCatalogue.cpp` and called from `IOService.cpp`, so a `--wrap=` reaches it: the image's whole
 * call graph has one `bl` to it, at `0x80134378`, inside `doServiceMatch`.
 *
 * **492 removes the filter and makes the record total, because the filter was a claim about the
 * question the goal asks.** 457 recorded only the call whose `service` is `gIOResources`, taken from
 * `IOService::getResourceService()`, on the ground that the run would otherwise "spend five records
 * per registered service and the frontier's own record would be somewhere inside a stream of
 * `IOWorkLoop`s". That was the right trade for one reading and the wrong shape for the goal's:
 * *which* services the catalogue answered and how many candidates each one got *is* the second half
 * of "把基础驱动跑起来", and a filter that keeps one service cannot say how many others got none. The
 * run is one record per `doServiceMatch` iteration - two per registration, so about sixty for this
 * machine's thirty services - against a 2 MB ram console that 491's run filled to 533 KB, so the
 * stream is affordable and the reading it carries is the count of *registrations*, which is what the
 * `_seq` slot has been documented as since 457.
 *
 * The resource root is not lost by removing the filter: every record carries a `res` flag, tested
 * with the same `getResourceService()` call the filter used, so 457's reading is the subset of
 * records whose `res` is 1 - a filter the *reader* applies to a total stream rather than one the
 * *writer* applies to it, which is the same move as naming `_matchpass` instead of `_matched`: a
 * record that says what it is beats a record that was selected for a purpose nobody can check.
 *
 * Nothing is changed: the real function runs with the same arguments and its result is returned
 * unchanged. The count is `0xffffffff` when `findDrivers` returned nothing at all, which cannot be
 * confused with a real empty set - the reason `entry_note_finddrivers`'s sentinel is the extreme
 * value rather than 0, since `0` is exactly what an empty candidate set looks like. The generation
 * is the value the real call stored through the caller's own out-parameter (`*generationCount =
 * getGenerationCount()`, `IOCatalogue.cpp:230`), read here because it is the catalogue's own answer
 * to "have personalities been added since matching started" - the other conjunct of the loop's
 * `keepGuessing` (with `reRegistered`) and therefore the reason a second call happens at all.
 */
extern void *_ZN9IOService18getResourceServiceEv(void);
extern uint32_t _ZNK12OSOrderedSet8getCountEv(const void *set);

void *__real__ZN11IOCatalogue11findDriversEP9IOServicePl(void *self, void *service, void *generation);
void *__wrap__ZN11IOCatalogue11findDriversEP9IOServicePl(void *self, void *service, void *generation)
{
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *result = __real__ZN11IOCatalogue11findDriversEP9IOServicePl(self, service, generation);

    if (service != 0) {
        uint32_t count = result ? _ZNK12OSOrderedSet8getCountEv(result) : 0xffffffffu;
        uint32_t gen = generation ? *(volatile uint32_t *)generation : 0u;
        uint32_t res = (service == _ZN9IOService18getResourceServiceEv()) ? 1u : 0u;

        entry_note_finddrivers(site, (uint32_t)(uintptr_t)service, (uint32_t)(uintptr_t)result,
                               count, gen, res);
    }

    return result;
}

/* ------------------------------------------------------- the OS's own console (458) */
/*
 * The two sinks the OS's console text leaves through, wrapped so that the text is captured instead
 * of discarded. The derivation is in `entry_stubs.c`'s 458 block, under `entry_os_console_char`, and
 * the two readings it rests on are in this image rather than in the source: `cons_ops_index` selects
 * the ops-table entry the ring's drain calls (`0` = `_serial_putc`, `1` = `vcputc`, whose reference
 * is the table's *address*, rewritten by `--wrap` exactly as a call is - `build_entry.sh` checks
 * that the image's `cons_ops[1].putc` holds `__wrap_vcputc`), and both sinks return immediately on
 * their own local gates (`uart_initted`, `gc_initialized`), which is why the OS's output has never
 * been visible anywhere.
 *
 * **The serial side is wrapped one level further down than the first design had it, and the reason
 * is a reading, not a preference.** That design wrapped `serial_putc`; the image then showed three
 * branches to `__wrap_serial_putc` (`_serial_putc`'s tail call, and kdp's two `pal_serial_*`), and
 * `PE_init_kprintf` (`pexpert/arm/pe_kprintf.c:34-42`, `80031ad0`) storing
 * `0x80031af8 = serial_putc` **unwrapped** into `PE_kputc` - a `movw`/`movt` pair against a symbol
 * its own object defines, which is exactly the same-object case `--wrap` cannot reach. `PE_kputc` is
 * the sink `kprintf` and the panic banner print through, so with `serial_putc` wrapped the one text
 * this instrument most needs on the next steps - a panic - would have been the one it missed.
 * `uart_putc` (`pexpert/arm/pe_serial.c:813`, in `pexpert_arm_pe_serial.o`) is cross-object from
 * every caller, is the single function every serial route ends in, and carries the `uart_initted`
 * gate the text dies at - so wrapping it captures `_serial_putc`, kdp's two, *and* the pointer
 * `PE_init_kprintf` stored, with no path counted twice.
 *
 * Nothing is changed: the wrapper appends to the ram console and then calls the real sink with the
 * same arguments. `vcputc`'s signature is the source's own three `int`s
 * (`osfmk/console/video_console.c`, `vcputc(__unused int l, __unused int u, int c)`) and
 * `uart_putc`'s is `void uart_putc(char c)` (`pexpert/arm/pe_serial.c:813`), which is what
 * `serial_putc` calls it with.
 */
void entry_os_console_char(int ch, uint32_t which);

void __real_vcputc(int l, int u, int c);
void __wrap_vcputc(int l, int u, int c)
{
    entry_os_console_char(c, 1u);
    __real_vcputc(l, u, c);
}

void __real_uart_putc(char c);
void __wrap_uart_putc(char c)
{
    entry_os_console_char((int)(unsigned char)c, 2u);
    __real_uart_putc(c);
}

/* ----------------------------------------------------- the IODT plane, and the root-device chain (461) */
/*
 * 459 stopped in `IOFindBSDRoot` with the tree's `RAMDisk` property apparently gone, and it left a
 * gap one link wide: the payload's blob is proved right on the host by XNU's own reader and
 * `IODeviceTreeAlloc` is proved to have run with the tree's own physical address, so what was never
 * measured is the object in between - the IODT *plane*, the registry tree that
 * `IORegistryEntry::fromPath` walks. The derivation of the four readings below, and of why
 * `getProperty` cannot be wrapped and has to be *called* instead, is in `entry_stubs.c` above
 * `entry_note_dtplane`. What this file adds is where each reading is taken.
 *
 * The three call sites, in the order the boot reaches them:
 *
 *   1. `entry_probe_plane`, from the `IODeviceTreeAlloc` wrapper above, the moment the plane
 *      exists. It reads `gIODTPlane` and asks the plane for `/chosen/memory-map` through XNU's own
 *      `fromPath`, then reads the property off whatever came back. This is the instrument's own
 *      call and not the OS's, which is the point: it is a reading of the plane *as built*, before
 *      BSD init has done anything, so a failure here is upstream of 459's frontier.
 *   2. `__wrap_...fromPath`, every call the image makes. The OS's own call for `/chosen/memory-map`
 *      is one of them (`IOKitBSDInit.cpp:440`), and its record carries the caller, so "the OS asked
 *      and got nothing" and "the OS never asked" are different lines in the log.
 *   3. `__wrap_mdevadd`/`__wrap_mdevlookup`, the two calls the property is supposed to lead to.
 *
 * `getProperty` is **virtual**, and a vtable entry is a reference to a symbol defined in the same
 * object - which `--wrap` does not rename, as 455 measured at a cost of eleven silent wrappers. So
 * it is called here by its mangled name, with `this` in r0 exactly as the vtable slot would pass it:
 * `_ZNK15IORegistryEntry11getPropertyEPKc` is `IORegistryEntry`'s own definition of
 * `getProperty(const char *) const` (`IORegistryEntry.cpp:631`), no subclass overrides it, and the
 * body is the same code the OS's call reaches - `OSSymbol::withCString` (which finds the interned
 * symbol `MakeReferenceTable` created with `withCStringNoCopy`), the virtual `getProperty(symbol)`,
 * and the release. `build_entry.sh` checks that both mangled names are *defined in this image*
 * rather than invented as stubs by the generator, which is the shape of 455's typo defect: a
 * misspelled name links, because the generator synthesises a stand-in for anything undefined.
 */
extern void entry_note_dtplane(uint32_t plane, uint32_t dt_top, uint32_t root, uint32_t map_ret);
extern void entry_note_dtpath(uint32_t caller, uint32_t w0, uint32_t w1, uint32_t w2,
                              uint32_t plane, uint32_t ret);
extern void entry_note_dtprop(uint32_t entry, uint32_t key0, uint32_t key1, uint32_t obj,
                              uint32_t w0, uint32_t w1, uint32_t bytes);
extern void entry_note_mdevadd(uint32_t caller, uint32_t devid, uint32_t base, uint32_t size,
                               uint32_t phys, uint32_t ret);
extern void entry_note_mdevlookup(uint32_t caller, uint32_t devid, uint32_t ret);
extern void entry_note_dtwalk(uint32_t t1, uint32_t root, uint32_t count, uint32_t first,
                              uint32_t set, uint32_t kids, uint32_t class0, uint32_t class1,
                              uint32_t control);

/* `IODeviceTreeSupport.cpp:64`, `.bss`, non-zero exactly when `makePlane` succeeded. */
extern const void *gIODTPlane;

/* XNU's own readers. These are *declarations* of XNU's functions under their mangled names, not
 * wrappers: nothing in this file changes what they do. */
extern void *entry_xnu_get_property(const void *self, const char *key)
    __asm__("_ZNK15IORegistryEntry11getPropertyEPKc");
extern void *entry_xnu_get_bytes(const void *self)
    __asm__("_ZNK6OSData14getBytesNoCopyEv");

/*
 * 462's four, the walk `fromPath` takes from the registry root. `getRegistryRoot` is a static member
 * (`IORegistryEntry.h:232`) and the other three are `const` members of `IORegistryEntry`
 * (`IORegistryEntry.cpp:1505`, `:1518`, `:1536`); all four are the same code the OS's own walk runs,
 * which is what makes them safe to call from here - `fromPath` itself calls three of them, so a
 * table these would fault on is a table the OS's own call would have faulted on first.
 */
extern void *entry_xnu_registry_root(void)
    __asm__("_ZN15IORegistryEntry15getRegistryRootEv");
extern void *entry_xnu_child_entry(const void *self, const void *plane)
    __asm__("_ZNK15IORegistryEntry13getChildEntryEPK15IORegistryPlane");
extern void *entry_xnu_child_set(const void *self, const void *plane)
    __asm__("_ZNK15IORegistryEntry20getChildSetReferenceEPK15IORegistryPlane");
extern unsigned int entry_xnu_child_count(const void *self, const void *plane)
    __asm__("_ZNK15IORegistryEntry13getChildCountEPK15IORegistryPlane");

void *__real__ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_(
        const char *path, const void *plane, char *buf, int *len, void *from);

/*
 * Four bytes of a string as a word, little-endian, so a path or a key is *readable in the log*
 * without a pointer that may be stale by the time anyone looks at it. The caller guarantees the four
 * bytes are inside the literal: every path here is longer than twelve characters and every key here
 * is `"RAMDisk"`, whose eighth byte is its terminator. Nothing is dereferenced - the argument is a
 * `.rodata` string in this image, and this reads it while the image's own tables are live, on the
 * path the wrapper was entered on.
 */
static uint32_t entry_str_word(const char *s, unsigned off)
{
    uint32_t w = 0u;

    for (unsigned i = 0u; i < 4u; i++) {
        w |= (uint32_t)(unsigned char)s[off + i] << (i * 8u);
    }
    return w;
}

static int entry_str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/*
 * Ask the plane for the property the OS is about to ask it for.
 *
 * `entry` is what `fromPath` returned, so the property table being read is the one the OS's own call
 * will read four statements later. Both answers are recorded in one record - the object and, when
 * there is one, the two words the object holds and `getBytesNoCopy` would have handed `mdevadd` -
 * because the second is only meaningful when the first is non-zero, and one record cannot be read as
 * a pair with a record that was never written. Reading the two words is safe for the same reason
 * every other read on this path is: the OS dereferences exactly this pointer on the next line of its
 * own code, so if it is bad the run was already over.
 *
 * The entry is *not* released. `fromPath` retains what it returns (`IORegistryEntry.cpp:1320`) and
 * the OS's own call releases it at `:449`; this one leaves the count one higher. The node is a
 * permanent member of the device-tree registry - nothing frees it before shutdown - and releasing it
 * would mean calling `OSObject::release` by mangled name, i.e. another name to get wrong, for no
 * reading.
 */
static void entry_probe_property(void *entry)
{
    static const char key[] = "RAMDisk";
    void *obj = entry_xnu_get_property(entry, key);
    void *bytes = obj ? entry_xnu_get_bytes(obj) : 0;
    uint32_t w0 = 0u, w1 = 0u;

    if (bytes) {
        w0 = ((const uint32_t *)bytes)[0];
        w1 = ((const uint32_t *)bytes)[1];
    }
    entry_note_dtprop((uint32_t)(uintptr_t)entry, entry_str_word(key, 0u), entry_str_word(key, 4u),
                      (uint32_t)(uintptr_t)obj, w0, w1, (uint32_t)(uintptr_t)bytes);
}

/*
 * 486: the *class* of an entry, by name - and the way there is through the vtable.
 *
 * 485 established that the entry the OS's own walk starts from is one object inside the
 * `IODeviceTreeAlloc` wrapper and a different one at the first `fromPath` of `bsd_init`, with the same
 * children and the same child set. The class name is what turns that pair of addresses into a sentence:
 * a device tree root is `new IOService` (`IODeviceTreeSupport.cpp:359`), and the entry that adopts its
 * children is the platform expert device `StartIOKit` makes (`new IOPlatformExpertDevice`,
 * `IOStartIOKit.cpp:156`).
 *
 * **`getMetaClass` is virtual, and this step's first run is why that matters.** A virtual reached by its
 * mangled name - the road 461 took to `IOService::getProperty`, which is not overridden away from its
 * base - calls the *base* implementation: `OSObject::getMetaClass` is
 * `{ return &gMetaClass; }` (`libkern/c++/OSObject.cpp:57-58`), so run A printed `OSObject` for the tree
 * root *and* for the entry that adopted it, a correct answer to a question nobody asked. The call has to
 * go through the object's own vtable, which is what C++ would have compiled, and the slot is the ABI's:
 * the primary vtable is `[offset-to-top][typeinfo][fn0][fn1]...`, and in this image `_ZTV8OSObject` and
 * `_ZTV9IOService` both hold their **own** `getMetaClass` at index 9 - two classes, one slot, which is
 * what makes the index a number read from the image rather than a choice of this file.
 * `tools/check_registry_adoption.py` reads both vtables out of the linked image, requires that index to
 * equal `STAGE90_VTABLE_META_CLASS`, requires the preamble word to be the zero of a primary vtable, and
 * fails the build if the mangled *base* name is called directly anywhere in this file. Apple's own
 * `getClassName(OSObject *)` (`OSObject.cpp:84-88`) is the same two calls in C++.
 *
 * The second call, `getClassName`, is **non-virtual** (`OSMetaClass.h`), and it is already declared in
 * this file for the metaclass walk of an earlier step (`entry_xnu_metaclass_name`, below) and used from
 * here rather than declared a second time: two declarations of one call are this project's oldest defect
 * class, and a selftest that mutates one of them would leave the other standing and report a blind spot
 * that is not there (240). It returns the class's own `const char *`, which lives as long as the class
 * does - the whole boot - so copying eight bytes of it into a record is safe.
 *
 * Zero words mean no name could be read: a NULL object, a NULL vtable, a NULL slot or a NULL name. That
 * is a record of its own, and it is deliberately not spelled as a name, so "the class is empty" and "the
 * class was not read" cannot be confused.
 *
 * ------------------------------------------------------------------- 486's run B: the object, not the slot
 *
 * **The ungated version of this call faulted on the device, and the fault is a finding.** Run B called
 * `vtable[9](obj)` on the entry the walk starts from and the kernel took a data abort at the *first*
 * instruction of `OSMetaClass::getClassName` - `ldr r0, [r0, #12]` - with `DFSR = 0x5` (section
 * translation fault) and `DFAR = 0x0d`, i.e. the metaclass pointer it was handed was **1**. So the call
 * at slot 9 returned 1: whatever object the walk was holding, its first word is not a vtable of this
 * image, because **218 vtables in the linked image hold a `getMetaClass` at index 9** (all of them except
 * `_ZTV8OSSymbol`, whose own `operator delete` sits there, and one holding `__cxa_pure_virtual`), and both
 * `OSObject::getMetaClass` and `IOService::getMetaClass` in this image are two instructions returning a
 * `.bss` address (`0x80574390` and `0x80574570`), never 1. The abort could not be serviced: the handler
 * was entered 64 times and `xnu_live_sleh_at_back` was written **zero** times, against run A's twelve,
 * and the run's own records stop at that call - the census, the `IODeviceTreeAlloc` plane record and the
 * OS's two `fromPath` records are all missing from the log because the boot never got there.
 *
 * So the read now has a **precondition it publishes and checks before it calls**, rather than a
 * dereference of whatever the registry happens to hand it. An object of a class in this image has a
 * primary vtable at index 9 - offset-to-top 0, no RTTI pointer - and the function there is in this
 * image's text, below `__bss_start`. Both are properties the image itself states: the 218 preambles are
 * 0/0, and a code address in this link is `[0x80000000, __bss_start)`. When the precondition does not
 * hold, `xnu_live_cls_took` is 0 and the six numbers are published anyway - the object, its first word,
 * that word's two preamble words, slot 9 and the metaclass - which is what names the object instead of
 * calling it: a slot-9 word that is not any class's `getMetaClass` is readable in the log beside the
 * pointer it came from, and the boot goes on to the records this experiment is actually for.
 *
 * This is the same rule 485's `entry_note_dtwalk` and the 474 frame check follow: a number the instrument
 * cannot use is published, not dereferenced.
 *
 * ------------------------------------------------------- 486's run C: the guard reads the right words
 *
 * **Run C's guard refused every object, and the numbers it published say why - which is the finding.**
 * Every call came back `cls_took = 0` with `pre0`/`pre1` non-zero, and the three objects' words name
 * their own classes without any call being made at all:
 *
 *   call 1, the tree root:       `vptr = 0x804999ac`, `pre0 = 0x8013cfb0`, `pre1 = 0x8013cfb4`
 *   calls 2..N, the adopter:     `vptr = 0x8049f990`, `pre0 = 0x801754a8`, `pre1 = 0x801754ac`
 *
 * and `nm` on the same image says `pre0`/`pre1` are **the class's own destructors**:
 * `0x8013cfb0 = IOService::~IOService()` with `0x8013cfb4 = IOService::~IOService()` (complete object
 * deleting), `0x801754a8 = IOPlatformExpertDevice::~IOPlatformExpertDevice()` with `0x801754ac` the
 * deleting form. So the object's first word is **the vtable's first function slot** - the Itanium ABI's
 * vptr, which points *past* the two-word preamble - and the two words the guard read as a preamble are
 * that vtable's slots 0 and 1. `0x804999ac` is `_ZTV9IOService` **+ 8** and `0x8049f990` is
 * `_ZTV22IOPlatformExpertDevice` **+ 8**, and `nm` names both classes: **the object the walk starts from
 * inside the `IODeviceTreeAlloc` wrapper is an `IOService`, and the object at the later moments is an
 * `IOPlatformExpertDevice`** - the two classes 485's question needed, and exactly the pair the prediction
 * named.
 *
 * The same arithmetic explains run B's fault. `getMetaClass` is at the **vtable's** index 9, which is the
 * object's table index **9 − 2 = 7**; index 9 of the object's table is `OSObject::taggedRetain(const
 * void *)` (`0x80133d38`, the value run C published as `fn` and the same address run B called). That
 * function returns nothing, so the metaclass the instrument passed on was its leftover `r0` - and the log
 * says that value was 1. **The tool that was wrong is the check**: `claim_slot` read
 * `_ZTV8OSObject`/`_ZTV9IOService` from the image, found each class's own `getMetaClass` at index 9 of
 * the *label*, and required the code's `#define` to equal 9 - while the code indexed the *object's*
 * table. One index, two tables, nothing comparing them: this project's oldest defect, in the one place
 * the whole step depended on it. The preamble constant below is now a second `#define`, the code
 * subtracts it, and the check reads both from the image and requires the arithmetic.
 *
 * Nothing is called through slot 7 unless the two words *before* the vptr are zero - they are, for both
 * classes: `_ZTV9IOService[0]/[1]` and `_ZTV22IOPlatformExpertDevice[0]/[1]` are `0/0`, which is what a
 * primary vtable with no RTTI looks like and what makes the offset-to-top position a *reading*.
 */
#define STAGE90_VTABLE_PREAMBLE 2u

/* The vtable's own index of the class's `getMetaClass` (`[offset-to-top][typeinfo][slot 0]...`), and the
 * object's table index is this minus `STAGE90_VTABLE_PREAMBLE`. Read out of the linked image by
 * `tools/check_registry_adoption.py`, which requires both vtables to hold their own `getMetaClass` here
 * and requires the preamble words to be zero. */
#define STAGE90_VTABLE_META_CLASS 9u

/* This image's own bounds: `pmap_kernel_va` starts at 0x80000000, and `entry.ld` puts `__bss_start` at
 * the first byte of `.bss`, i.e. past the whole of `.text` and `.data`. A vtable slot is a code address
 * only if it is inside them. */
#define STAGE90_IMAGE_BASE 0x80000000u
extern uint32_t __bss_start;

typedef const void *(*entry_meta_class_fn)(const void *);

/* Declared here because both are defined further down this file: `entry_xnu_metaclass_name` with the
 * mangled name of Apple's `OSMetaClass::getClassName` in the block that explains it, and `entry_str8`
 * beside the other string readers. One declaration each - the defect this project keeps paying for is
 * two declarations of one call. */
extern const char *entry_xnu_metaclass_name(const void *meta);
static void entry_str8(const char *s, uint32_t *w0, uint32_t *w1);

extern void entry_note_class(uint32_t site, uint32_t obj, uint32_t vptr, uint32_t pre0, uint32_t pre1,
                             uint32_t fn, uint32_t meta, uint32_t took);

/* The three callers, spelled so the log says which one a record belongs to rather than leaving the
 * reader to count records. `entry_class_words` is one function and these are its three sites. */
#define STAGE90_CLS_WALK     1u
#define STAGE90_CLS_ROOT     2u
#define STAGE90_CLS_RECORDED 3u
/* 487: the tree's own children, and the second plane's. Two more sites rather than one, because the
 * four records answer four different questions - "what is the tree root", "what is the registry slot",
 * "what is each node of the tree" and "what is attached to the service root" - and a shared site would
 * make the log unable to say which of them a class word came from. */
#define STAGE90_CLS_CHILD    4u
#define STAGE90_CLS_SVCCHILD 5u
/* 491: the third census's rows, both levels. One site and not two, because the two levels answer one
 * question ("what is attached below the platform expert") and the record already carries `depth`; a
 * second site would be a second name for a distinction the table already makes. */
#define STAGE90_CLS_PEXCHILD 6u

static const void *entry_object_meta(uint32_t site, const void *obj)
{
    const void *vptr;
    const void *meta = 0;
    entry_meta_class_fn fn;
    uint32_t pre0 = 0u, pre1 = 0u, fnw = 0u, took = 0u;

    if (obj == 0) {
        entry_note_class(site, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
        return 0;
    }

    vptr = *(const void *const *)obj;
    if (vptr != 0) {
        /* `vptr[-2]`/`vptr[-1]` are the vtable's offset-to-top and typeinfo, and the slot is the
         * vtable's index less that preamble - see the arithmetic at the head of this block. */
        pre0 = ((const uint32_t *)vptr)[-(int)STAGE90_VTABLE_PREAMBLE];
        pre1 = ((const uint32_t *)vptr)[1 - (int)STAGE90_VTABLE_PREAMBLE];
        fn = ((const entry_meta_class_fn *)vptr)[STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE];
        fnw = (uint32_t)(uintptr_t)fn;

        if (pre0 == 0u && pre1 == 0u && fnw >= STAGE90_IMAGE_BASE &&
            fnw < (uint32_t)(uintptr_t)&__bss_start) {
            took = 1u;
            meta = fn(obj);
        }
    }

    entry_note_class(site, (uint32_t)(uintptr_t)obj, (uint32_t)(uintptr_t)vptr, pre0, pre1, fnw,
                     (uint32_t)(uintptr_t)meta, took);
    return meta;
}

static void entry_class_words(uint32_t site, const void *obj, uint32_t *w0, uint32_t *w1)
{
    *w0 = 0u;
    *w1 = 0u;
    if (obj != 0) {
        const void *meta = entry_object_meta(site, obj);
        if (meta != 0) {
            const char *name = entry_xnu_metaclass_name(meta);
            if (name != 0)
                entry_str8(name, w0, w1);
        }
    }
}

/*
 * The walk itself, read at the two moments the boot has them (462).
 *
 * 461 measured the two ends of this and left the middle open: the instrument's own
 * `fromPath("/chosen/memory-map", gIODTPlane)` returned the node when it was called from inside the
 * `IODeviceTreeAlloc` wrapper - i.e. *before* `IOPlatformExpertDevice::initWithArgs` initializes from
 * that root and before the platform expert is started - and the OS's own two `fromPath` calls in
 * `IOFindBSDRoot` returned 0 in the same boot. Something between those two moments changed the
 * registry, and `fromPath`'s own code says the change can only be in one of four places: the meta
 * root, the entry the walk starts from, that entry's child set, or its children's names.
 *
 * `fromPath` (`IORegistryEntry.cpp:1232-1325`) starts at
 * `gRegistryRoot->getChildEntry(plane)`, names the first component against
 * `entry->getChildFromComponent`, and its failure at the *first* component leaves no other candidate.
 * So this reads the same four objects by the same calls:
 *
 *   `root`   `IORegistryEntry::getRegistryRoot()`, the object `fromPath` walks from.
 *   `count`  its `getChildCount(plane)`, how many entries the walk can start at.
 *   `first`  its `getChildEntry(plane)`, the entry the walk *does* start at - and the one number
 *            that says whether the registry changed (`getChildEntry` is `copyChildEntry` plus a
 *            release, `:1536-1546`, so the entry is retained by the registry, not by this call).
 *   `set`    that entry's `getChildSetReference(plane)`: **NULL means the child-set key is not in
 *            its registry table at all**, which is a different failure from a child set that is
 *            present and holds no matching name, and `kids` is the second half of that pair.
 *   `kids`   that entry's `getChildCount(plane)`: 0 with a non-NULL `set` is "the names do not
 *            match"; 0 with `set` NULL is "the child set is gone".
 *   `class`  (486) that entry's *runtime class name*, eight bytes of it, so the reading names its own
 *            object at every moment instead of being a pointer the reader has to match up by hand.
 *   `control` `fromPath(path, plane, 0, 0, 0)` through `__real_fromPath` - the same call the OS is
 *            about to make, by the instrument, at the same moment. A control that answers while the
 *            OS's own call does not would say the two calls differ; both answering the same way says
 *            the registry is what changed.
 *
 * `t1` marks the reading taken at the earlier moment (the probe below, once); every later reading
 * overwrites the `t2` slots, so the report carries the first and the last of them and the live
 * `xnu_live_walk_seq` records carry their order.
 *
 * The control's entry is **not** released, for 461's reason: releasing it would mean calling
 * `OSObject::release` by mangled name, i.e. another name to get wrong, for no reading. At most two
 * entries are leaked for the life of the boot.
 */
static void entry_probe_walk(const void *plane, const char *path, uint32_t t1)
{
    void *root = 0;
    void *first = 0;
    void *set = 0;
    void *control = 0;
    unsigned int count = 0u;
    unsigned int kids = 0u;
    uint32_t class0 = 0u, class1 = 0u;

    if (plane == 0) {
        return;
    }

    root = entry_xnu_registry_root();
    if (root != 0) {
        count = entry_xnu_child_count(root, plane);
        first = entry_xnu_child_entry(root, plane);
    }
    if (first != 0) {
        set = entry_xnu_child_set(first, plane);
        kids = entry_xnu_child_count(first, plane);
        entry_class_words(STAGE90_CLS_WALK, first, &class0, &class1);
    }
    if (path != 0) {
        control = __real__ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_(
                      path, plane, 0, 0, 0);
    }

    entry_note_dtwalk(t1, (uint32_t)(uintptr_t)root, (uint32_t)count, (uint32_t)(uintptr_t)first,
                      (uint32_t)(uintptr_t)set, (uint32_t)kids, class0, class1,
                      (uint32_t)(uintptr_t)control);
}

/*
 * The plane, as built. Called from the `IODeviceTreeAlloc` wrapper and from nowhere else.
 *
 * `map_ret` is this file's own `fromPath("/chosen/memory-map", gIODTPlane)`, the OS's reader called
 * by the instrument. A zero `xnu_entry_dtplane_map` with a non-zero `xnu_entry_dtplane` says the
 * plane exists and does not answer for the node - which is the reading that separates "the registry
 * tree is wrong" from "the OS never asked".
 */
void entry_probe_plane(void *dtTop, void *root)
{
    const void *plane = gIODTPlane;
    void *map = 0;

    if (plane != 0) {
        entry_probe_walk(plane, 0, 1u);
        map = __real__ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_(
                  "/chosen/memory-map", plane, 0, 0, 0);
    }
    entry_note_dtplane((uint32_t)(uintptr_t)plane, (uint32_t)(uintptr_t)dtTop,
                       (uint32_t)(uintptr_t)root, (uint32_t)(uintptr_t)map);
    if (map != 0) {
        entry_probe_property(map);
    }
}

void *__wrap__ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_(
        const char *path, const void *plane, char *buf, int *len, void *from)
{
    void *r;

    /*
     * 462: read the registry *before* the OS's own call runs, so what is recorded is the state that
     * call is about to see rather than what it left behind - and with the OS's own path as the
     * control, so the instrument's call and the OS's differ in nothing at all at that instant.
     */
    entry_probe_walk(plane, path, 0u);

    r = __real__ZN15IORegistryEntry8fromPathEPKcPK15IORegistryPlanePcPiPS_(
            path, plane, buf, len, from);

    /*
     * The path words are read only when there is a path. `fromPath` handles a NULL path itself
     * (`IORegistryEntry.cpp:1250`) and returns 0, so a NULL here is a real call the OS made, and it
     * is recorded as three zero words rather than as a fault in the instrument.
     */
    if (path != 0) {
        entry_note_dtpath((uint32_t)(uintptr_t)__builtin_return_address(0),
                          entry_str_word(path, 0u), entry_str_word(path, 4u),
                          entry_str_word(path, 8u), (uint32_t)(uintptr_t)plane,
                          (uint32_t)(uintptr_t)r);
    } else {
        entry_note_dtpath((uint32_t)(uintptr_t)__builtin_return_address(0), 0u, 0u, 0u,
                          (uint32_t)(uintptr_t)plane, (uint32_t)(uintptr_t)r);
    }

    /*
     * And when the OS's own call is the memory-map one, read the property off what it got - before
     * it does, so the reading is of the same object and not of whatever its own read left behind.
     */
    if (r != 0 && path != 0 && entry_str_eq(path, "/chosen/memory-map")) {
        entry_probe_property(r);
    }
    return r;
}

/*
 * `bsd/dev/memdev.c:561`, and its arguments are the measurement: the first is the device id the OS
 * chose (`-1`, "pick one"), the second and third are the RAM disk's base and size *in pages*, and
 * the fourth is `phys`. The widths are the source's own - `int`, `uint64_t`, `unsigned int`, `int` -
 * because the AAPCS puts the 64-bit one in an aligned register pair (r1:r2) and the fourth argument
 * on the stack, so a prototype that spelled everything `uint32_t` would read the low half of the
 * base as the size and report a device that was never asked for.
 */
int __real_mdevadd(int devid, unsigned long long base, unsigned int size, int phys);
int __wrap_mdevadd(int devid, unsigned long long base, unsigned int size, int phys)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int r = __real_mdevadd(devid, base, size, phys);

    entry_note_mdevadd(caller, (uint32_t)devid, (uint32_t)base, (uint32_t)size, (uint32_t)phys,
                       (uint32_t)r);
    return r;
}

/* `bsd/dev/memdev.c:635`: `mdevlookup(0)` is the call whose negative return reaches the panic. */
int __real_mdevlookup(int devid);
int __wrap_mdevlookup(int devid)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int r = __real_mdevlookup(devid);

    entry_note_mdevlookup(caller, (uint32_t)devid, (uint32_t)r);
    return r;
}

/* ------------------------------------------------- the wait's own predicate (463) */
/*
 * 462 ended with a proof and two candidates. The proof: `IOSecureBSDRoot` (`IOKitBSDInit.cpp:664`) is
 * the first statement after `bsd_init`'s mount loop breaks, so reaching it *is* `vfs_mountroot()`
 * returning 0 - and the boot reaches it, prints `BSD root: md0, major 1, minor 0` and stops there.
 * The candidates, both consistent with the source, neither ruled out: the registry set that
 * `OSMetaClass::applyToInstancesOfClassName` walks, and the
 * `inState == (service->__state[0] & inState)` bit test `IOService::instanceMatch` applies to every
 * instance that walk yields.
 *
 * `waitForMatchingService` names its own predicate in its first four lines:
 *
 *     LOCKWRITENOTIFY();
 *     result = (IOService *) copyExistingServices( matching, kIOServiceMatchedState, kIONotifyOnce );
 *     if (result) break;
 *     notify = IOService::setNotification( gIOMatchedNotification, matching, ... );
 *
 * and `copyExistingServices` (`IOService.cpp:4264`) is where the two candidates live, in this order:
 *
 *     1. `obj = matching->getObject(gIOProviderClassKey)` - the dictionary's class name.
 *     2. `str = OSDynamicCast(OSString, obj)`; if that holds,
 *        `OSMetaClass::applyToInstancesOfClassName(OSSymbol::withString(str), instanceMatch, &ctx)`
 *        and *nothing at all* happens if the registry has no metaclass under that name or that
 *        metaclass's `reserved->instances` set is empty - `applyToInstancesOfClassName` returns
 *        before it calls the applier once (`OSMetaClass.cpp:919-936`).
 *     3. `IOService::instanceMatch` (`IOService.cpp:4222`), the applier, which rejects an instance
 *        first on `state == (state & service->__state[0])` and only then on `matchInternal`.
 *     4. `ctx->done == ctx->count` - with `kIONotifyOnce` the *only* way the service is returned
 *        instead of an `OSSet`.
 *
 * Five readings separate all of that, and each is XNU's own function called with the OS's own values:
 *
 *   `p4`/`p0`  the predicate itself, twice. The first is the wait's own call, argument for argument
 *              (`inState = kIOServiceMatchedState = 0x4`). The second is the *same call* with
 *              `inState = 0`, which disables exactly one clause: `instanceMatch`'s
 *              `state == (state & __state[0])` is true for any state when `state` is 0, while the
 *              `matchInternal` clause below it is untouched. So `p4 = 0, p0 != 0` **is** the state
 *              bit, and `p4 = 0, p0 = 0` is somewhere upstream of it, with no reading of the states
 *              needed to say which mechanism it was.
 *   `sym`/`meta`  the dictionary's own `IOProviderClass` object, read out of the dictionary by
 *              `OSDictionary::getObject(const char *)` so that the walk below uses **the OS's own
 *              interned symbol** and not a string this file re-spells, and then
 *              `OSMetaClass::getMetaClassWithName(sym)`. `meta = 0` is "the name is not in the
 *              metaclass registry"; `sym = 0` is "the dictionary has no `IOProviderClass` key at
 *              all", which would send `copyExistingServices` down its `IOService::gMetaClass.apply-
 *              ToInstances` branch instead - the whole of `IOService` rather than one class.
 *   `seen`/`inst`/`state`  `applyToInstancesOfClassName` called by the instrument with **its own**
 *              applier, which counts every instance, records the first `ENTRY_WCLS_MAX` of them with
 *              `IOService::getState()` - `__state[0]` through the tree's only implementation of
 *              `getState` (`IOService.h:499`, one `ldr r0, [r0, #36]`, a fact `build_entry.sh` already
 *              checks) - and returns false so the walk is not stopped early. `seen = 0` with a
 *              non-zero `meta` is the reading that is *neither* of 462's two candidates: the metaclass
 *              exists and has no instances, and `OSMetaClass::addInstance` has exactly one caller in
 *              this tree, `IOService::registerService` (`IOService.cpp:3694`).
 *
 * **The instrument calls `copyExistingServices` without `gNotificationLock`**, and that is a
 * deviation from the function's own contract ("internal - call with gNotificationLock",
 * `IOService.cpp:4262`) which the real path honours: `waitForMatchingService` takes it two lines
 * above. It is taken deliberately and the reason is stated rather than implied. First, the lock cannot
 * be named here at all: `gNotificationLock` is `static` in `IOService.cpp:178`, so the only route
 * would be to abandon the measurement. Second, what the lock buys at that point is the atomicity of
 * *check then register* - `copyExistingServices` reads the instance registry, which is protected by
 * `sAllClassesLock`/`sInstancesLock` inside `applyToInstancesOfClassName` and by nothing to do with
 * `gNotificationLock` - and the probe does not register anything. Third, the probe cannot be
 * interleaved by another thread on this image, because it has no blocking point: there is no timer in
 * it (the step list still owes `ml_init_timebase`), so nothing preempts, and a thread switch needs an
 * explicit block, of which the two reads and the walk have none on a *failing* predicate. Fourth and
 * last, the probe's only side effect when the predicate *succeeds* is one `retain` on the object it
 * found - which `copyExistingServices` does itself, in the same place, for the same reason.
 *
 * The duplicate call does not change what the boot measures, and that is structural rather than
 * argued: the real call is made two instructions later with the same dictionary and the same
 * arguments, so any answer the probe gets the real call gets too. **The probe cannot make the wait
 * succeed** - it would have to change `__state[0]`, and nothing here writes it.
 */
extern void entry_note_wmatch(uint32_t ent, uint32_t caller, uint32_t dict, uint32_t to_lo,
                              uint32_t to_hi, uint32_t ret);
extern void entry_note_wsvc(uint32_t dict, uint32_t sym, uint32_t p4, uint32_t p0);
extern void entry_note_wcls_begin(uint32_t sym, uint32_t meta, uint32_t rsvc, uint32_t name0,
                                  uint32_t name1);
extern void entry_note_winst(uint32_t inst, uint32_t state);
extern void entry_note_wcls_end(void);

/* XNU's own functions under their mangled names - declarations, not wrappers: nothing in this file
 * changes what they do. `getState` is `virtual` (`IOService.h:499`) and `getObject(const char *)` and
 * `getMetaClassWithName` are members of classes whose vtable or static dispatch a C file cannot spell,
 * so being *called* by mangled name is the only way in - 455's measurement, 461's method.
 *
 * Every one of them is checked against the image's pass-1 undefined list by `build_entry.sh`: a
 * mangled name is a spelling, the generator synthesises a stand-in for anything undefined, and a
 * misspelling therefore links and then reports a hit that looks like a finding about the device. */
extern void *entry_xnu_dict_get_object(const void *dict, const char *key)
    __asm__("_ZNK12OSDictionary9getObjectEPKc");
extern void *entry_xnu_metaclass_with_name(const void *sym)
    __asm__("_ZN11OSMetaClass20getMetaClassWithNameEPK8OSSymbol");
extern unsigned int entry_xnu_get_state(const void *service)
    __asm__("_ZNK9IOService8getStateEv");
extern void *entry_xnu_get_resource_service(void)
    __asm__("_ZN9IOService18getResourceServiceEv");

/*
 * `OSMetaClass::getClassName()`, **non-virtual** (`OSMetaClass.h:1606`) and therefore safe to call on a
 * metaclass whose dynamic type this file does not know - which is what turns `meta`, a pointer, into
 * the name of a class *in the log*: a metaclass pointer alone is a number a reader has to go and look
 * up in the image with `nm`, and this project's rule since 461 is that a pointer in the log is worth
 * nothing unless the log also says what it is. The implementation is
 * `return className->getCStringNoCopy();` (`OSMetaClass.cpp:526`), so the string is the *interned*
 * symbol the registry was keyed by, and it is NULL only when the metaclass has no class name.
 */
extern const char *entry_xnu_metaclass_name(const void *meta)
    __asm__("_ZNK11OSMetaClass12getClassNameEv");

/*
 * `OSMetaClass::applyToInstancesOfClassName(const OSSymbol *, OSMetaClassInstanceApplierFunction,
 * void *)`, called by mangled name with **this file's** function as the applier - which is what makes
 * the walk a reading rather than a re-derivation: the set walked, the lock taken around it and the
 * recursion into nested subclass sets are XNU's, and all this adds is what to do with each instance.
 *
 * The applier's return type is `unsigned char` and not `int`, and that is the ABI rather than a style.
 * XNU's `OSMetaClassInstanceApplierFunction` is `bool (*)(const OSObject *, void *)`
 * (`OSMetaClass.h:828`), and XNU's `applyToInstances` assigns the call's result to a `bool` to decide
 * whether to stop. AAPCS returns a `bool` zero-extended in r0, which is what an `unsigned char` return
 * does as well and what an `int` return also does *for the two values this function returns* - so the
 * two are the same call here, and `unsigned char` is the one that says so without a caveat. Returning
 * `true` would stop the walk at the first instance, which is why the return is written as a literal
 * `0u` with this comment next to it.
 */
typedef unsigned char (*entry_applier_t)(const void *instance, void *context);
extern void entry_xnu_class_instances(const void *sym, entry_applier_t applier, void *context)
    __asm__("_ZN11OSMetaClass27applyToInstancesOfClassNameEPK8OSSymbolPFbPK8OSObjectPvES6_");

/* 455's wrapper, and this is the first step that reaches it: `copyExistingServices` is called *inside*
 * `IOService.cpp`, so the OS's own call can never be wrapped (455's negative result, measured), and it
 * is this file's reference to the mangled name that is undefined and therefore renamed - which is how
 * the two predicate records get their `in_state` and their answer for free. */
extern void *entry_xnu_copy_existing(const void *matching, uint32_t in_state, uint32_t options)
    __asm__("_ZN9IOService20copyExistingServicesEP12OSDictionarymm");

/* The dictionary key the OS's own matcher reads the class name from (`IOService.cpp:4285`). */
static const char entry_kProviderClassKey[] = "IOProviderClass";

/*
 * Up to eight bytes of a string as two words, **stopping at the terminator** - so, unlike
 * `entry_str_word` above, this one is safe on a string it did not size, which the class name is:
 * `getClassName` hands back an interned symbol's own storage, whose length this file cannot know, and
 * a four-byte read on a three-letter class name would read past it. The bytes after the terminator are
 * left zero, which makes a short name read as itself in the log rather than as a truncation.
 */
static void entry_str8(const char *s, uint32_t *w0, uint32_t *w1)
{
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    unsigned i;

    for (i = 0u; s != 0 && i < 8u && s[i] != '\0'; i++) {
        if (i < 4u) {
            lo |= (uint32_t)(unsigned char)s[i] << (i * 8u);
        } else {
            hi |= (uint32_t)(unsigned char)s[i] << ((i - 4u) * 8u);
        }
    }
    *w0 = lo;
    *w1 = hi;
}

static unsigned char entry_wcls_applier(const void *instance, void *context)
{
    (void)context;
    entry_note_winst((uint32_t)(uintptr_t)instance,
                     (uint32_t)entry_xnu_get_state(instance));
    return 0u;   /* false: keep walking - `true` would stop the walk at the first instance */
}

/*
 * The predicate and the class lookup, for one dictionary. Called once per `waitForMatchingService`
 * entry, before the real call, so every reading is of the state that call is about to see.
 *
 * The two `copyExistingServices` calls come first, in that order, because the pair is the step's
 * result and the walk below is what says *where* in the chain the failure sits. `p0`'s answer may be
 * non-NULL - that is the finding, not a fault - and when it is, the instance it found has been
 * retained once by XNU's own code path (*not* by this file), which is exactly what the real call does
 * with an instance it returns.
 */
static void entry_probe_wait(void *matching)
{
    void *sym;
    void *meta = 0;
    void *p4;
    void *p0;
    uint32_t name0 = 0u;
    uint32_t name1 = 0u;

    p4 = entry_xnu_copy_existing(matching, 4u /* kIOServiceMatchedState */, 1u /* kIONotifyOnce */);
    p0 = entry_xnu_copy_existing(matching, 0u, 1u);

    sym = entry_xnu_dict_get_object(matching, entry_kProviderClassKey);
    entry_note_wsvc((uint32_t)(uintptr_t)matching, (uint32_t)(uintptr_t)sym,
                    (uint32_t)(uintptr_t)p4, (uint32_t)(uintptr_t)p0);

    if (sym == 0) {
        return;
    }

    meta = entry_xnu_metaclass_with_name(sym);
    if (meta != 0) {
        entry_str8(entry_xnu_metaclass_name(meta), &name0, &name1);
    }
    entry_note_wcls_begin((uint32_t)(uintptr_t)sym, (uint32_t)(uintptr_t)meta,
                          (uint32_t)(uintptr_t)entry_xnu_get_resource_service(), name0, name1);
    if (meta != 0) {
        entry_xnu_class_instances(sym, entry_wcls_applier, 0);
    }
    entry_note_wcls_end();
}

/*
 * The wait. `IOService::waitForMatchingService(OSDictionary *, uint64_t)` is `IOService.h:792` - the
 * argument list and types are the source's own, so the AAPCS register assignment is the same on both
 * sides of the wrapper with nothing to spell differently: the pointer in r0, and the 64-bit timeout in
 * **r2:r3**, because r1 is consumed and an eight-byte argument takes an even-numbered pair. The image
 * confirms it rather than the rule being quoted: `IOSecureBSDRoot`'s call site builds
 * `movw r2, #0xac00; movt r2, #0xfc23; mov r3, #6`, i.e. `0x6fc23ac00` = 30000000000 ns = the 30 s of
 * `30ULL * kSecondScale`, and `waitForMatchingService` itself reads the pair from there.
 *
 * Two records per call and not one, for 454's reason: `ent = 0` is written before the real call and
 * `ent = 1` after it, and this boot's last live record is an `ent = 0` - which is how "the wait began
 * and never returned" becomes a line in the log instead of an absence of one.
 *
 * Nothing is changed: the real function runs with the same arguments and its result is returned as it
 * came. The probe runs *before* it and not after, because a reading taken after a call that does not
 * return is a reading that never happens.
 */
void *__real__ZN9IOService22waitForMatchingServiceEP12OSDictionaryy(void *matching, uint64_t timeout);
void *__wrap__ZN9IOService22waitForMatchingServiceEP12OSDictionaryy(void *matching, uint64_t timeout)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uint32_t to_lo = (uint32_t)timeout;
    uint32_t to_hi = (uint32_t)(timeout >> 32);
    void *r;

    entry_note_wmatch(0u, caller, (uint32_t)(uintptr_t)matching, to_lo, to_hi, 0u);
    if (matching != 0) {
        entry_probe_wait(matching);
    }

    r = __real__ZN9IOService22waitForMatchingServiceEP12OSDictionaryy(matching, timeout);

    entry_note_wmatch(1u, caller, (uint32_t)(uintptr_t)matching, to_lo, to_hi,
                      (uint32_t)(uintptr_t)r);
    return r;
}

/* --------------------------------------------------- 477: the trap's other half, wrapped */
/*
 * `void sleh_undef(struct arm_saved_state *regs, struct arm_vfpsaved_state *vfp_ss)` -
 * `osfmk/arm/trap.c:118`, the second level of XNU's undefined-instruction path, called once per `udf`
 * from `locore.s`'s `handle_undef`/`handle_undef2` (two `bl sleh_undef` sites in this image,
 * `0x800140b8` and `0x80014170`), with `regs` pointing at the thread's PCB frame the vector filled.
 *
 * **Why this wrapper exists at all: 475's handler was taken out of this path, and this is where the
 * reading went.** 475 reported a user-mode `udf` from this image's own `fleh_undef`, and 476's run
 * showed that entering Apple's handler *from C* is unsafe - `locore_fleh_undef` derives the saved PC
 * from `lr` (`subeq lr, lr, #4`), so a `bl` from this image resumes the user thread at the `bl`'s own
 * address, in user mode, where the fetch permission-faults and the kernel panics. 477 moves the
 * user/kernel split into the vector page (`entry_vectors.s`'s `vec_tramp_1`), which means this image's
 * handler no longer sees a user `udf` - and with it, no longer produces the milestone reading
 * `xnu_live_undef_pc = 0x10e0`. This wrapper is where that reading now comes from: the kernel's own
 * call, once per undefined instruction, with the frame in hand.
 *
 * It is 467's arrangement for `sleh_abort` applied to the other trap, and the two differences are
 * worth naming. **The frame is read, not the coprocessors**: an undefined instruction has no fault
 * pair to read (`IFSR`/`DFSR` are whatever the last abort left), so the numbers here are the frame's
 * own - `pc` (the instruction that trapped), `cpsr` (the mode it ran in, Apple's user/kernel test),
 * `lr` - at the offsets `entry_saved_state.h` holds and `check_saved_state_offsets.py` checks. And
 * **the record goes to the live channel and not to `g_panic_buf`**, because the interesting entry -
 * a user `udf` - is one after which `sleh_undef` may never return: its user arm ends in
 * `exception_triage(EXC_BAD_INSTRUCTION, ...)` and its kernel arm in `panic_context`, and 474 is the
 * experiment that showed a report which needs the epilogue is a report a hang can eat.
 *
 * **The cap is a count, because this path can be entered by a looping process.** `UNDEF_LIVE_MAX`
 * entries get the four detail keys; every entry advances `xnu_live_undef_seq`, and every *user* entry
 * advances `xnu_live_undef_user_seq`, which keeps 475's key meaning what it meant there ("how many
 * user `udf`s the kernel was asked to handle"). A number climbing without the detail keys reappearing
 * is a program re-executing an undefined instruction, which is a reading and not a missing one.
 */
#define UNDEF_LIVE_MAX 4u

static uint32_t g_undef_seq;
static uint32_t g_undef_user_seq_trace;

extern void entry_live_write(const char *key, uint32_t value);
extern void __real_sleh_undef(void *regs, void *vfp_ss);

void __wrap_sleh_undef(void *regs, void *vfp_ss)
{
    const uint32_t *frame = (const uint32_t *)regs;
    uint32_t pc = 0u, lr = 0u, cpsr = 0u;

    g_undef_seq++;
    entry_live_write("xnu_live_undef_seq", g_undef_seq);

    if (frame != 0) {
        pc = frame[STAGE90_SS_PC / 4];
        lr = frame[STAGE90_SS_LR / 4];
        cpsr = frame[STAGE90_SS_CPSR / 4];
    }
    if ((cpsr & STAGE90_PSR_MODE_MASK) == STAGE90_PSR_USER_MODE) {
        g_undef_user_seq_trace++;
        entry_live_write("xnu_live_undef_user_seq", g_undef_user_seq_trace);
    }
    if (g_undef_seq <= UNDEF_LIVE_MAX) {
        entry_live_write("xnu_live_undef_pc", pc);
        entry_live_write("xnu_live_undef_lr", lr);
        entry_live_write("xnu_live_undef_spsr", cpsr);
        entry_live_write("xnu_live_undef_user", (cpsr & STAGE90_PSR_MODE_MASK) == STAGE90_PSR_USER_MODE
                                                     ? 1u : 0u);
    }

    __real_sleh_undef(regs, vfp_ss);
}

/* --------------------------------------------------- 467: the kernel's own abort handler */
/*
 * `void sleh_abort(struct arm_saved_state *regs, int type)` - `osfmk/arm/trap.c:274`, the second
 * level of XNU's abort path, called once per abort from `locore.s:1140` (`dataabt_from_kernel`) and
 * from the three other abort entries. 467 is the step that gave slot 4 of the vector page to Apple's
 * own first-level handler (`locore_fleh_dataabt`, `entry_vectors.s`), so this call is where the
 * kernel *decides* what a data abort means: return - the page was paged in and the faulting
 * instruction is retried - or do not return, because it panicked or re-entered itself.
 *
 * **`regs` is taken as an opaque pointer, and the two fault numbers are read from cp15 instead of from
 * the frame.** Neither file on this side of the image has an XNU header (`<stdint.h>` is the whole
 * include list of both `entry_stubs.c` and this one), so `struct arm_saved_state`'s layout is not a
 * thing either can name, and a locally re-declared copy of it would be the "one value, two
 * definitions" hazard this project has paid for twenty-four times over. It is not needed: DFSR and
 * FAR are still what they were when the abort was taken - `locore.s` reads them with `mrc` and
 * nothing between that and this call writes either register - so the two numbers `sleh_abort` itself
 * is about to read are available without knowing anything about the frame. What that costs is the
 * faulting PC, which this record does not carry; a fault at the same address is the same fault, and
 * FAR is what says so.
 *
 * `type` was `T_DATA_ABT` (4, `osfmk/arm/trap.h:70` - not the 1 an earlier revision of this sentence
 * said) for every entry that could reach here, because the other abort vectors were still the
 * instrument's handlers. **476 makes that false in the way this wrapper was built for**: the prefetch
 * slot went to Apple's `locore_fleh_prefabt` the way 467 gave away the data slot, so `T_PREFETCH_ABT`
 * (3) now reaches here too - and with it a real defect in this function, which read DFSR and DFAR
 * (the *data* fault pair, `c5,c0,0`/`c6,c0,0`) for both classes. On a prefetch abort those two
 * registers are whatever the last data abort left there, and worse, the check that reads them back
 * would have gone false: the vector fills `SS_STATUS`/`SS_VADDR` from **IFSR/IFAR** on that path
 * (`locore.s`'s `prefabt_from_user`, `str r5,[sp,#68]` / `str r1,[sp,#72]`, from `c5,c0,1`/`c6,c0,2`),
 * so `xnu_live_sleh_frame_ok` would read 0 on every prefetch abort and a reader would conclude the
 * offsets had moved. **The class decides the pair, so the class selects it here** - one branch, and
 * the offset control goes back to meaning what it says.
 *
 * Nothing is changed: the real handler runs with the same arguments. The record is written before it,
 * so a handler that never returns has still reported what it was given, and `_back` afterwards is
 * what makes "returned" a separate reading from "entered".
 *
 * **474: the record takes the frame as well, and that is the whole of what was missing.** 473's run
 * is served four aborts and returns from all four, and then records a fifth and nothing else - an
 * abort whose `DFAR` the log never names, because `SLEH_LIVE_MAX` cut the record that would have
 * carried it. `DFSR`/`DFAR` from `cp15` are the two numbers this wrapper can read *without* knowing
 * anything about `struct arm_saved_state`; the third number it needs is `regs->pc`, the instruction
 * that faulted, and the fourth is `regs->cpsr`, which says whether that instruction was in user mode
 * or kernel mode. That is precisely the distinction 473's run could not make: a kernel `copyin` /
 * `copyout` of an unmapped user address and the process-1 thread faulting on its own account have the
 * same `DFSR` class and a `DFAR` that is a user address either way.
 *
 * The offsets are in `entry_saved_state.h`, one definition for the two files that need them, checked
 * against Apple's header, against this configuration's generated `assym.s`, and against
 * `ACT_PCBDATA_PC - ACT_PCBDATA`, by `tools/check_saved_state_offsets.py` in this build. **And they
 * are checked again at run time, on the device**: the vector stored the same `cp15` numbers into
 * `SS_STATUS`/`SS_VADDR`, so a frame read at the right offsets must report the `DFSR`/`DFAR` this
 * function's own `mrc`s just read - and entries 1 to 4 are aborts whose two numbers are already known
 * from 472's and 473's logs, which makes them a control rather than a sample. If the offsets are
 * wrong, `xnu_live_sleh_frame_ok` is 0 on those entries and the run says so *before* it names a
 * frontier.
 *
 * Nothing else changes: the pointer is passed, not dereferenced here, and the real handler still runs
 * with the same arguments.
 *
 * **490: the record takes `thread->recover` as well, and it is read *before* the real handler
 * because the real handler consumes it.** The word is Apple's recovery address for a fault-driven
 * copy (`entry_saved_state.h`'s `STAGE90_TH_RECOVER`): `copyin`/`copyout` arm it with an `adr` to
 * their own error label, `sleh_abort`'s second statement is `thread->recover = 0`, and when the page
 * cannot be paged in the handler points `regs->pc` at it so the copy returns `EFAULT` instead of the
 * instruction being retried forever. A non-zero value here therefore says, in the log, that the
 * fault was one the kernel had a plan for - which is the difference between a `far = 0` inside
 * `Lcopyin_wordwise_loop` being a frontier and being the designed path. Reading it *after*
 * `__real_sleh_abort` would report 0 on every entry, and a zero that means "nothing was armed" is
 * the same number as a zero that means "the handler already spent it" - which is the kind of thing
 * this project has had to retract before. `TPIDRPRW` is in hand for the `thread` field already, so
 * this is one load and no new register class.
 *
 * **And the word being armed is not the same reading as the word being spent, so the wrapper takes
 * the second one too.** `sleh_abort` reaches the recovery arm only when *both* `arm_fast_fault` and
 * `vm_fault` failed (`trap.c:446-461`), and the run's own first four aborts are exactly that
 * distinction: the two with the word armed are the two that go through `copyout` and the two without
 * it go through `memset` and `bcopy`, which have no such contract. Reading the word *alone* would
 * make one number out of two different outcomes - `copyout` faulting on a user page the kernel then
 * paged in and retried, and `copyout` faulting on a user page the kernel gave up on and converted
 * into an `EFAULT` return. The first of those is 488's and 489's own log (the console is silent
 * after `attempting to load /sbin/launchd`, so the exec's copies *succeeded*); the second is what a
 * `copyin` of a null pointer does. So the frame is read again **after** the call: the handler's only
 * way to take the arm is `regs->pc = (register_t)(recover & ~0x1)`, so a post-call `pc` equal to the
 * armed word with bit 0 cleared *is* the arm having been taken, and nothing else in `sleh_abort`
 * writes `pc`. One comparison, and the count it feeds states which of the two happened.
 *
 * The name of the counter matters as much as its value, which is why the armed-at-entry count is
 * published as `..._armed` and not as `..._recovered`: a key named for the outcome it does not
 * measure is the same defect as a comment that asserts a property nothing checks. `..._redirected`
 * is the one that says the copy returned `EFAULT`.
 */
extern void entry_note_sleh(uint32_t type, uint32_t dfsr, uint32_t dfar, uint32_t thread,
                            const uint32_t *frame, uint32_t recover);
extern void entry_note_sleh_back(uint32_t redirected);

void __real_sleh_abort(void *regs, int type);

void __wrap_sleh_abort(void *regs, int type)
{
    uint32_t fsr, far_, thread, recover, redirected;

    /*
     * **476: the class selects the coprocessor pair, because the class is what says which pair is the
     * fault pair.** `STAGE90_T_PREFETCH_ABT` (3) is the instruction side - IFSR `c5,c0,1`, IFAR
     * `c6,c0,2` - and everything else is the data side (`c5,c0,0`, `c6,c0,0`), which is what this
     * function read unconditionally until the prefetch slot became Apple's. The two constants and the
     * reasoning for them are in `entry_saved_state.h`; the short form is that reading the wrong pair
     * reports two stale numbers and turns `xnu_live_sleh_frame_ok` into a class indicator.
     */
    if ((uint32_t)type == STAGE90_T_PREFETCH_ABT) {
        __asm__ volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(fsr));
        __asm__ volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(far_));
    } else {
        __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(fsr));
        __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(far_));
    }
    __asm__ volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r"(thread));

    /* Before the real handler, which zeroes it (`trap.c:290-291`). See the comment above. */
    recover = ((const uint32_t *)(uintptr_t)thread)[STAGE90_TH_RECOVER / 4];

    entry_note_sleh((uint32_t)type, fsr, far_, thread, (const uint32_t *)regs, recover);

    __real_sleh_abort(regs, type);

    /*
     * Did the handler *spend* the word? `sleh_abort`'s only way to take the recovery arm writes
     * `regs->pc = (register_t)(recover & ~0x1)` (`trap.c:456-461`), so the frame's `pc` after the
     * call is the whole question - and it is asked here rather than inferred from the copy's return
     * value, which the wrapper never sees. The mask is Apple's: bit 0 of the armed word is the state
     * bit the handler moves into `PSR_TF` (`trap.c:459`), so the address it writes into `pc` is the
     * word with that bit cleared and the comparison has to clear it too.
     */
    redirected = 0u;
    if ((recover != 0u) && (regs != 0)) {
        if (((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_PC / 4] == (recover & ~0x1u))
            redirected = 1u;
    }

    entry_note_sleh_back(redirected);
}

/* ------------------------------------------------- what the exec said when it gave up (471) */
/*
 * Experiment 470's run is the first that reaches `load_init_program`: the OS console reads
 *
 *   Added memory device md0/rmd0 (02000000/0D000000) at 00000000804FD000 for 0000000000002000
 *   BSD root: md0, major 2, minor 0
 *   load_init_program: attempting to load /usr/local/sbin/launchd.development
 *   load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2
 *   load_init_program: attempting to load /sbin/launchd
 *
 * and then stops - **with no `failed loading /sbin/launchd` line**, which 467's zeros-image run did
 * print (`errno 8`). The image then reports one trap, in kernel mode, at the `udf` inside
 * `DebuggerTrapWithState` (`xnu_entry_undef_pc = 0x800364c0`, `spsr = 0x60000093`), and
 * `xnu_entry_panic_entered = 1` - exactly one entry, so there was no user-mode fault first and the
 * Mach-O never ran its first instruction.
 *
 * The name is in the log's one format-string reading, `xnu_entry_trap_r9_fmt = 0x804b291b`:
 * `"unexpected SIGKILL of %s %s with reason -- namespace %d code 0x%llx description %.800s"`, which
 * is `bsd/kern/kern_sig.c:2138` - `psignal_internal`'s `if (signum == SIGKILL && p == initproc)`
 * panic. So the boot did not fault and did not hit a stub: **it killed `initproc` and then panicked
 * about having done so.**
 *
 * That panic is downstream of the missing `failed loading` line, not the frontier. `kern_exec.c`'s
 * `badtoolate` label is reached from eight failures *after* the image has been activated, and it
 * ends with `error = 0` under the comment "We can't stop this system call at this point, so just
 * pretend we succeeded" - so `load_init_program` prints nothing whether the exec succeeded or
 * failed there, and the two are indistinguishable from the console alone.
 *
 * What *is* recorded, at every one of those eight sites and at the two inside `check_for_signature`,
 * is an exit reason: `exec_failure_reason = os_reason_create(OS_REASON_EXEC, <code>)`, where the
 * code is one of `EXEC_EXIT_REASON_*` (`bsd/sys/reason.h:222-233`, 1 = `BAD_MACHO` through 12 =
 * `UPX`). So the instrument is one wrap on `os_reason_create` - and it reads the **arguments**, not
 * the returned struct: `os_reason_create(uint32_t osr_namespace, uint64_t osr_code)` passes both in
 * registers, while reading them back off the `os_reason_t` would mean reproducing
 * `struct os_reason`'s layout, which begins with `decl_lck_mtx_data(, osr_lock)` and is therefore a
 * private-layout question this file has no business answering. The caller of the *last*
 * `os_reason_create` before the panic is the failing site, by the same argument every stub report
 * uses: a call names its call site.
 *
 * The second wrap is the direct reading rather than the inferred one. `load_machfile`'s
 * `load_return_t` is what separates "our Mach-O was refused" (`LOAD_BADARCH` 1, `LOAD_BADMACHO` 2,
 * `LOAD_FAILURE` 4, `LOAD_NOSPACE` 5) from "it loaded and something after it failed" - and
 * `EXEC_EXIT_REASON_BAD_MACHO` on the first wrap says the same thing only by way of the code. When
 * both readings agree the frontier is named twice over; when they disagree, the disagreement is the
 * finding.
 *
 * Both are non-terminal: the real function runs with the same arguments and the run continues. A
 * terminal instrument here would stop at the first exit reason the boot creates, which is not
 * necessarily the one that killed `initproc` - the 470 log's own `t268_*` records show how much work
 * happens between two adjacent calls.
 */
extern void entry_note_osreason(uint32_t caller, uint32_t ns, uint32_t code, uint32_t ret);
extern void entry_note_loadmachfile(uint32_t caller, uint32_t header, uint32_t ret);

void *__real_os_reason_create(uint32_t osr_namespace, uint64_t osr_code);
void *__wrap_os_reason_create(uint32_t osr_namespace, uint64_t osr_code)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *r = __real_os_reason_create(osr_namespace, osr_code);

    entry_note_osreason(caller, osr_namespace, (uint32_t)osr_code, (uint32_t)(uintptr_t)r);
    return r;
}

/* `load_machfile(struct image_params *, struct mach_header *, thread_t, vm_map_t *, load_result_t *)`
 * - five pointers, so no register-pair question and every argument is a word. The third is reported
 * rather than the second because `header` is `imgp->ip_header`, a pointer into the parser's own
 * scratch, while `thread` is the thread the load is being done for; and `mapp`/`result` are outputs
 * whose contents are the load's business.
 */
int __real_load_machfile(void *imgp, void *header, void *thread, void *mapp, void *result);
int __wrap_load_machfile(void *imgp, void *header, void *thread, void *mapp, void *result)
{
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int r = __real_load_machfile(imgp, header, thread, mapp, result);

    entry_note_loadmachfile(caller, (uint32_t)(uintptr_t)header, (uint32_t)r);
    return r;
}

/*
 * ================================================================================================
 * 485: the boot thread's tail, and the device tree the driver layer is handed
 * ================================================================================================
 *
 * `kernel_bootstrap_thread` (`osfmk/kern/startup.c:558`) is the kernel's own main thread. Its last
 * seven calls are Apple's, in Apple's order: `PE_init_iokit()` (`:544`), `bsd_init()` (`:628`), and
 * then `OSKextRemoveKextBootstrap()`, `kdebug_free_early_buf()`, `serial_keyboard_init()`,
 * `vm_page_init_local_q()`, `thread_bind(PROCESSOR_NULL)` and `vm_pageout()` (`:633-646`).
 *
 * Five of those are wrapped here, and *which* five is the design decision this step makes:
 *
 *   - `bsd_init` is not wrapped: it is where the boot's own console output comes from, so its
 *     completion is already readable in the text, and a wrapper on it would be a second, uncompared
 *     definition of "the boot got to BSD".
 *   - `thread_bind` is not wrapped either, and for the opposite reason: it is called from all over
 *     the kernel, and twice inside this very body - the earlier call binds the thread to `processor`
 *     (`startup.c:452`) and only the last one is the tail's - so a record of it would name no position
 *     and would fire away from the tail. `tools/check_boot_completion.py` fails the build if that
 *     stops being true in either direction (one caller, or a call that moves into the tail).
 *   - the remaining five are called from exactly one place in the whole kernel, this tail, so a
 *     record of one of them is a position. **`vm_pageout` entered is the end of the kernel's own
 *     boot**, and it cannot be entered unless `bsd_init` returned: `vm_pageout` never returns, so
 *     nothing after it runs, which is why the record is written *before* the wrapped call rather
 *     than after it - a wrapper that recorded on return would record nothing for the one call this
 *     step exists to prove.
 *
 * Each wrapper publishes its index as well as its address, because the index is Apple's order and
 * the address is only a pointer; the pair is what lets the log say *which* of the five ran without a
 * reader having to resolve an address first. The index is also what `tail_seen[]` counts, so a tail
 * that ran out of order or twice is a count rather than a last-value slot.
 */
extern void entry_note_boot_tail(uint32_t index, uint32_t site);

void __real_OSKextRemoveKextBootstrap(void);
void __wrap_OSKextRemoveKextBootstrap(void)
{
    entry_note_boot_tail(0u, (uint32_t)(uintptr_t)__real_OSKextRemoveKextBootstrap);
    __real_OSKextRemoveKextBootstrap();
}

void __real_kdebug_free_early_buf(void);
void __wrap_kdebug_free_early_buf(void)
{
    entry_note_boot_tail(1u, (uint32_t)(uintptr_t)__real_kdebug_free_early_buf);
    __real_kdebug_free_early_buf();
}

void __real_serial_keyboard_init(void);
void __wrap_serial_keyboard_init(void)
{
    entry_note_boot_tail(2u, (uint32_t)(uintptr_t)__real_serial_keyboard_init);
    __real_serial_keyboard_init();
}

void __real_vm_page_init_local_q(void);
void __wrap_vm_page_init_local_q(void)
{
    entry_note_boot_tail(3u, (uint32_t)(uintptr_t)__real_vm_page_init_local_q);
    __real_vm_page_init_local_q();
}

/*
 * The device tree census.
 *
 * **The root is read the way the OS reads it, and 461's root is published beside it.** The first run of
 * this step walked `g_dtplane_root` - 461's reading of what `IODeviceTreeAlloc` returned, which the
 * comment above used to call "the tree's root nub" - and found no children at `vm_pageout`, while the
 * same log's 462 walk of the *registry* root found a different entry with 21. So the census now starts
 * where the OS's own walk starts: `IORegistryEntry::getRegistryRoot()`, then that entry's child in
 * `gIODTPlane` (`getChildEntry`, the same accessor `fromPath` uses), and it publishes both pointers and
 * whether they agree. That is not a fallback: one of the two is the tree the driver layer is handed, and
 * a census that does not know which one it read is a census of an object nobody can name.
 *
 * The children are read with `getChildSetReference` - the OS's own non-virtual accessor,
 * `IORegistryEntry.h:824` - and each one's name with `getName(plane)` (`IORegistryEntry.h:1505`), so
 * every read here is a call the OS's own matching makes about the same objects. The count is taken
 * twice, `getChildCount(plane)` on the entry and `OSArray::getCount` on the set it returns (the header
 * shows the first is written in terms of the second), because a number that appears once is a number
 * with nothing to compare it against.
 *
 * **Nothing is released and nothing is retained.** `getChildSetReference` is documented as returning a
 * reference (*not* a copy), `getChildEntry` copies and releases inside itself, and `OSArray::getObject`
 * does not retain - so the census is a read of the live tree with no effect on its reference counts,
 * which matters because the object that reads the console is not entitled to change the object that owns
 * the drivers. 461's rule about `release` on a borrowed entry applies here too.
 *
 * The two state words are `__state[0]`/`__state[1]` at `+36`/`+40`. `__state` is a two-word array
 * (`IOService.h:328`), `__state[0]` holds the bits this census counts - `Registered` and `Matched`
 * are set there by `registerService` and by the match pipeline - and `__state[1]` holds the publish
 * and termination machinery (`kIOServiceNeedConfigState`, `kIOServiceSyncPubState`, the busy mask)
 * that the same calls maintain as they go. The first is read through `IOService::getState()` - the
 * tree's one implementation, which `build_entry.sh` pins by disassembly and by counting definitions -
 * and the second by offset, because there is no accessor for it. The offset is
 * `STAGE90_DTK_STATE0_OFF`, which is 455's layout reading of `getState`'s own `ldr r0, [r0, #36]`
 * plus the one word the array declares; `tools/check_boot_completion.py` compares it against the
 * number `build_entry.sh` fails the build on. A node of this tree is `new IOService`
 * (`IODeviceTreeSupport.cpp:359`), so both words are the service's own for every child.
 */
extern void entry_note_dtbegin(uint32_t plane, uint32_t recorded, uint32_t root, uint32_t same);
extern void entry_note_dtset(uint32_t set, uint32_t kids);
extern void entry_note_dtcount(uint32_t count);
extern void entry_note_dtnone(uint32_t why);
extern void entry_note_dtok(void);
extern void entry_note_dtchild(uint32_t seq, uint32_t child, uint32_t name0, uint32_t name1,
                               uint32_t state0, uint32_t state1);
extern void entry_note_dtclass(uint32_t obj, uint32_t class0, uint32_t class1, uint32_t kids,
                               uint32_t set);
extern void entry_note_dtrec(uint32_t obj, uint32_t class0, uint32_t class1, uint32_t kids,
                             uint32_t set);
extern uint32_t g_dtplane_root;
extern void entry_note_dtchildcls(uint32_t seq, uint32_t class0, uint32_t class1);
/* `entry_xnu_registry_root`, `entry_xnu_child_entry`, `entry_xnu_child_set` and `entry_xnu_child_count`
 * are declared by 462's block above, with the mangled names of the same four functions the OS's own
 * `fromPath` calls - one spelling of each in this file, which is what makes this census the same walk
 * the OS does. */
extern unsigned int entry_xnu_array_count(const void *set)
    __asm__("_ZNK7OSArray8getCountEv");
extern void *entry_xnu_array_object(const void *set, unsigned int index)
    __asm__("_ZNK7OSArray9getObjectEj");
extern const char *entry_xnu_entry_name(const void *self, const void *plane)
    __asm__("_ZNK15IORegistryEntry7getNameEPK15IORegistryPlane");
extern uint32_t entry_xnu_entry_state(const void *self)
    __asm__("_ZNK9IOService8getStateEv");

#define STAGE90_DTK_MAX 24u
#define STAGE90_DTK_STATE0_OFF 36u

/*
 * 487: this census returns the root it walked, because the second plane's census takes it as its
 * cross-check - `IOService::getServiceRoot()` is the same object by Apple's own construction
 * (`IOService.cpp:663-666`), so publishing the pair with a `same` flag is what keeps the two readings
 * from being two readings of two different things.
 */
void *entry_probe_dt_children(void)
{
    const void *plane = gIODTPlane;
    void *recorded = (void *)(uintptr_t)g_dtplane_root;
    void *root;
    void *set;
    unsigned int n, kids, i;
    uint32_t class0 = 0u, class1 = 0u;

    if (plane == 0) {
        entry_note_dtnone(0u);
        return 0;
    }

    root = entry_xnu_child_entry(entry_xnu_registry_root(), plane);
    if (root == 0) {
        entry_note_dtnone(1u);
        return 0;
    }

    entry_note_dtbegin((uint32_t)(uintptr_t)plane, (uint32_t)(uintptr_t)recorded,
                       (uint32_t)(uintptr_t)root, (root == recorded) ? 1u : 0u);

    kids = entry_xnu_child_count(root, plane);
    set = entry_xnu_child_set(root, plane);
    entry_note_dtset((uint32_t)(uintptr_t)set, (uint32_t)kids);

    /*
     * 486: the walked entry, named by its runtime class, with the children and child set it answers -
     * and beside it the entry `IODeviceTreeAlloc` returned, named the same way and asked for the same
     * two things *now*. The pair is the whole answer to 485's open question: if the two classes differ
     * and the second object's children and set are zero, then the registry slot holds one tree's
     * *adopter* and the tree root it was built from has had its child-set key taken away from it
     * (`IORegistryEntry::init`, `IORegistryEntry.cpp:356-380`) rather than there being two trees.
     */
    entry_class_words(STAGE90_CLS_ROOT, root, &class0, &class1);
    entry_note_dtclass((uint32_t)(uintptr_t)root, class0, class1, (uint32_t)kids,
                       (uint32_t)(uintptr_t)set);

    if (recorded != 0) {
        uint32_t rclass0 = 0u, rclass1 = 0u;
        unsigned int rkids = entry_xnu_child_count(recorded, plane);
        void *rset = entry_xnu_child_set(recorded, plane);

        entry_class_words(STAGE90_CLS_RECORDED, recorded, &rclass0, &rclass1);
        entry_note_dtrec((uint32_t)(uintptr_t)recorded, rclass0, rclass1, (uint32_t)rkids,
                         (uint32_t)(uintptr_t)rset);
    }

    /*
     * The root is returned whether or not the child set could be read: the second census's cross-check
     * is about *which object* the OS's own walk starts from, and that reading is complete before this
     * line. A `return 0` here would turn "the set was empty" into "there is no tree".
     */
    if (set == 0)
        return root;

    n = entry_xnu_array_count(set);
    entry_note_dtcount((uint32_t)n);

    for (i = 0u; i < n && i < STAGE90_DTK_MAX; i++) {
        void *child = entry_xnu_array_object(set, i);
        const char *name;
        uint32_t name0 = 0u, name1 = 0u;
        uint32_t state0 = 0u, state1 = 0u;
        uint32_t cclass0 = 0u, cclass1 = 0u;

        if (child == 0)
            continue;
        name = entry_xnu_entry_name(child, plane);
        if (name != 0) {
            entry_str8(name, &name0, &name1);
            entry_note_dtok();
        }
        state0 = entry_xnu_entry_state(child);
        state1 = *(volatile uint32_t *)((const char *)child + STAGE90_DTK_STATE0_OFF + 4u);
        entry_note_dtchild(i, (uint32_t)(uintptr_t)child, name0, name1, state0, state1);

        /*
         * 487: the child's *class*, which is the one reading 485 and 486 left unmeasured about these
         * 21 objects. A device-tree node is `new IOService` (`IODeviceTreeSupport.cpp:359`), and the
         * object Apple's driver layer puts in its place is `new IOPlatformDevice`
         * (`IOPlatformExpert.cpp:1283`, `IODTPlatformExpert::createNub`, called from `createNubs` at
         * `:1310`, which `configure` calls at `:1271` through `processTopLevel` at `:1314`). So the
         * name of the class at this object *is* the answer to "did the driver layer's nub pass run":
         * `IOService` means these are the tree's raw entries, `IOPlatformDevice` means the OS replaced
         * them with nubs and attached them for matching. The record is separate from `entry_note_dtchild`
         * rather than two arguments longer, because that record's shape is 485's claim and a step does
         * not re-open a previous step's claim to carry a new number.
         */
        entry_class_words(STAGE90_CLS_CHILD, child, &cclass0, &cclass1);
        entry_note_dtchildcls(i, cclass0, cclass1);
    }

    return root;
}

/*
 * ------------------------------------------------------------------- 487: the second plane
 *
 * **The IODT census above answers "what is in the tree"; this one answers "what is attached to the
 * thing that owns the tree", and the second is where a driver would appear.** The goal's second half
 * is "the basic drivers run", and by 485's own table every one of the 21 nodes is `Registered |
 * Matched | FirstPublish | FirstMatch` - so the question is no longer whether they are known to the
 * registry, it is whether anything is *attached* to a provider and therefore eligible for `start`.
 *
 * The two planes are separate registries over the same objects, and Apple's ARM boot uses both:
 * `IOService::attach( provider )` links a service to its provider in `gIOServicePlane`
 * (`IOService.cpp:639`), while the device tree's own parentage is `gIODTPlane`. `StartIOKit` makes the
 * platform expert device the *root of the service plane* with `rootNub->attach( 0 )`, whose
 * zero-provider branch is `gIOServiceRoot = this; attachToParent( getRegistryRoot(), gIOServicePlane )`
 * (`IOService.cpp:663-666`) - so `IOService::getServiceRoot()` is the object this census already read
 * as the tree's adopter, and its children in `gIOServicePlane` are the services attached to it.
 *
 * The reading is a census and not a trace for 455's reason, restated: `attach`, `start` and
 * `registerService` are all *virtual*, so none of them can be `--wrap`ped - a wrapper on the name
 * catches only a qualified direct call, and the OS's own machinery calls every one of them through
 * the object's vtable. What a virtual call *produces*, on the other hand, is a link in a plane and a
 * child set that any accessor can read. **A driver that was started left a child behind, and the
 * child set is the record.**
 *
 * The census is deliberately the same shape as the one above - the same four accessors, the same
 * state offset, the same per-child cap read from one `#define` - because the two numbers are meant to
 * be compared: two planes, two child counts, one object read twice. `tools/check_driver_plane_census.py`
 * fails the build unless the shapes are the same and the caps agree.
 *
 * Nothing is released and nothing is retained, exactly as above: every accessor here returns a
 * borrowed reference and `OSArray::getObject` does not retain.
 */
extern const void *gIOServicePlane;
extern void *entry_xnu_service_root(void)
    __asm__("_ZN9IOService14getServiceRootEv");
extern void entry_note_svcbegin(uint32_t plane, uint32_t root, uint32_t same);
extern void entry_note_svcnone(uint32_t why);
extern void entry_note_svcset(uint32_t set, uint32_t kids);
extern void entry_note_svccount(uint32_t count);
extern void entry_note_svcchild(uint32_t seq, uint32_t child, uint32_t name0, uint32_t name1,
                                uint32_t class0, uint32_t class1, uint32_t state0, uint32_t state1);

#define STAGE90_SVC_MAX 24u

void entry_probe_service_plane(const void *iokit_root)
{
    const void *plane = gIOServicePlane;
    void *root;
    void *set;
    unsigned int n, kids, i;

    if (plane == 0) {
        entry_note_svcnone(0u);
        return;
    }

    root = entry_xnu_service_root();
    if (root == 0) {
        entry_note_svcnone(1u);
        return;
    }

    /*
     * `same` is the cross-check, and it is the reason this function takes the IODT census's root as an
     * argument: Apple's own code says the two are one object (`attach(0)` sets the service root to
     * `this`), so a run where they differ means one of the two readings is of something else - and the
     * pair of pointers, published together, is what says which.
     */
    entry_note_svcbegin((uint32_t)(uintptr_t)plane, (uint32_t)(uintptr_t)root,
                        (root == iokit_root) ? 1u : 0u);

    kids = entry_xnu_child_count(root, plane);
    set = entry_xnu_child_set(root, plane);
    entry_note_svcset((uint32_t)(uintptr_t)set, (uint32_t)kids);

    if (set == 0)
        return;

    n = entry_xnu_array_count(set);
    entry_note_svccount((uint32_t)n);

    for (i = 0u; i < n && i < STAGE90_SVC_MAX; i++) {
        void *child = entry_xnu_array_object(set, i);
        const char *name;
        uint32_t name0 = 0u, name1 = 0u;
        uint32_t class0 = 0u, class1 = 0u;
        uint32_t state0 = 0u, state1 = 0u;

        if (child == 0)
            continue;
        name = entry_xnu_entry_name(child, plane);
        if (name != 0)
            entry_str8(name, &name0, &name1);
        entry_class_words(STAGE90_CLS_SVCCHILD, child, &class0, &class1);
        state0 = entry_xnu_entry_state(child);
        state1 = *(volatile uint32_t *)((const char *)child + STAGE90_DTK_STATE0_OFF + 4u);
        entry_note_svcchild(i, (uint32_t)(uintptr_t)child, name0, name1, class0, class1, state0, state1);
    }
}

/*
 * ------------------------------------------------- 491: the level nothing has read
 *
 * **The two censuses above read one plane each, at one level each, and the driver layer's own nodes are
 * one level below both of them.** 487's reading is that `attach(provider)` links a service to its
 * provider in `gIOServicePlane` and that `StartIOKit`'s `rootNub->attach(0)` makes the platform expert
 * device the root of that plane - so the root's children are "the services attached to it". The
 * platform expert's *driver* is one of those children and not the root, and `IODTPlatformExpert::
 * processTopLevel` is where the nubs are made: `createNubs( this, ... )` (`IOPlatformExpert.cpp:1360`,
 * `:1363`) with `this` being the expert, and `createNubs` does `nub->attach( parent )` followed by
 * `nub->registerService()` on every nub it builds (`:1310-1311`). So `createNubs`'s 21 `attach` calls
 * put the 21 nubs under the *expert*, and 487 - which walked the root - measured the level the nubs are
 * **not** at.
 *
 * 487's own owed list names the missing reading ("the platform expert instance's own service
 * children"), and 490's log is the first that could have shown it: the service census there read
 * `svc_kids = 2` with the two rows `MSM8974P` and `IOResources` on the *live* channel. Two is not 21,
 * and no build before this one could tell "the nub pass did not run" from "the nub pass ran one level
 * below where the census looked".
 *
 * The walk is two levels and it does not decide in advance which child is the expert: it records
 * **every** child of the root at `depth = 1`, and for each of the first `ENTRY_PEX_ROOT` of them every
 * child of *its* child set at `depth = 2`, each row carrying its own `kids_of` so that the object the
 * nubs hang from is the row with children rather than a pointer this file chose. `deep_l1` is how many
 * depth-1 rows were descended into, because a census that walked one of two must not read as a census
 * of the subtree.
 *
 * Every accessor is one the two censuses above already use, on the same kind of object: a child of a
 * service-plane child set was put there by `attachToParent`, so it is an `IOService` and answers all
 * four calls. The class read is the one that *calls* into the object (486's run B), and it is guarded
 * inside `entry_object_meta` by the vtable test that step added - a `took = 0` record is the reading
 * for an object whose first word is not a vtable of this image.
 */
extern void entry_note_pexbegin(uint32_t plane, uint32_t root, uint32_t set, uint32_t kids);
extern void entry_note_pexnone(uint32_t why);
extern void entry_note_pexdeep(uint32_t l1);
extern void entry_note_pexchild(uint32_t seq, uint32_t depth, uint32_t child, uint32_t name0,
                                uint32_t name1, uint32_t class0, uint32_t class1, uint32_t state0,
                                uint32_t state1, uint32_t kids_of);
extern void entry_note_pexend(void);

#define STAGE90_PEX_ROOT 8u
/* 491's cap was 24 and its run recorded 19 of the expert's children, because the inner loop's room
 * (`STAGE90_PEX_DEEP - STAGE90_PEX_ROOT` = 16) is what bounds the second level and the expert has
 * **26** service children: four `cpu@N` nubs, twenty-one top-level nubs and `IODTNVRAM`
 * (`processTopLevel`'s two `createNubs` calls and its own `dtNVRAM->attach( this )`). The ten rows
 * that fell off the end were the *tail* of that set, which is where the device nodes are - the very
 * rows a driver attached to one of them would appear under - so the cap is raised until the room
 * covers the set: 40 - 8 = 32 rows for a level the run counted 26 in. `ENTRY_PEX_DEEP` is the table's
 * own size and the two have to move together (`check_driver_plane_census.py` claim 4). */
#define STAGE90_PEX_DEEP 40u

void entry_probe_service_tree(void)
{
    const void *plane = gIOServicePlane;
    void *root;
    void *set;
    unsigned int n, i, j;
    unsigned int seq = 0u;
    unsigned int l1 = 0u;

    if (plane == 0) {
        entry_note_pexnone(0u);
        return;
    }

    root = entry_xnu_service_root();
    if (root == 0) {
        entry_note_pexnone(1u);
        return;
    }

    set = entry_xnu_child_set(root, plane);
    entry_note_pexbegin((uint32_t)(uintptr_t)plane, (uint32_t)(uintptr_t)root,
                        (uint32_t)(uintptr_t)set, (uint32_t)entry_xnu_child_count(root, plane));

    if (set == 0) {
        entry_note_pexnone(2u);
        entry_note_pexdeep(0u);
        entry_note_pexend();
        return;
    }

    n = entry_xnu_array_count(set);
    for (i = 0u; i < n && i < STAGE90_PEX_ROOT; i++) {
        void *child = entry_xnu_array_object(set, i);
        void *cset;
        unsigned int m;
        const char *name;
        uint32_t name0 = 0u, name1 = 0u, class0 = 0u, class1 = 0u;
        uint32_t state0 = 0u, state1 = 0u;
        unsigned int kids_of;

        if (child == 0)
            continue;
        kids_of = entry_xnu_child_count(child, plane);
        name = entry_xnu_entry_name(child, plane);
        if (name != 0)
            entry_str8(name, &name0, &name1);
        entry_class_words(STAGE90_CLS_PEXCHILD, child, &class0, &class1);
        state0 = entry_xnu_entry_state(child);
        state1 = *(volatile uint32_t *)((const char *)child + STAGE90_DTK_STATE0_OFF + 4u);
        entry_note_pexchild(seq++, 1u, (uint32_t)(uintptr_t)child, name0, name1, class0, class1,
                            state0, state1, (uint32_t)kids_of);

        cset = entry_xnu_child_set(child, plane);
        if (cset == 0)
            continue;
        m = entry_xnu_array_count(cset);
        l1++;
        for (j = 0u; j < m && j < (STAGE90_PEX_DEEP - STAGE90_PEX_ROOT); j++) {
            void *grand = entry_xnu_array_object(cset, j);
            uint32_t gname0 = 0u, gname1 = 0u, gclass0 = 0u, gclass1 = 0u;
            uint32_t gstate0 = 0u, gstate1 = 0u;
            const char *gname;

            if (grand == 0)
                continue;
            gname = entry_xnu_entry_name(grand, plane);
            if (gname != 0)
                entry_str8(gname, &gname0, &gname1);
            entry_class_words(STAGE90_CLS_PEXCHILD, grand, &gclass0, &gclass1);
            gstate0 = entry_xnu_entry_state(grand);
            gstate1 = *(volatile uint32_t *)((const char *)grand + STAGE90_DTK_STATE0_OFF + 4u);
            entry_note_pexchild(seq++, 2u, (uint32_t)(uintptr_t)grand, gname0, gname1, gclass0,
                                gclass1, gstate0, gstate1,
                                (uint32_t)entry_xnu_child_count(grand, plane));
        }
    }

    entry_note_pexdeep(l1);
    entry_note_pexend();
}

/*
 * `vm_pageout` is the fifth and last, and the census above runs inside its wrapper - at the one
 * moment that is downstream of everything the boot does and upstream of the loop that never ends.
 * Reading the registry there rather than earlier is the point: the platform expert's nub pass and
 * every matching attempt have already happened by then, so the table is the *settled* state of the
 * driver layer and not a snapshot of it mid-flight.
 *
 * 487 runs *both* censuses at that same moment and passes the first one's root to the second, so the
 * pair of planes is read as one state rather than as two moments.
 */
extern void *entry_probe_dt_children(void);
extern void entry_probe_service_plane(const void *iokit_root);
extern void entry_probe_service_tree(void);

void __real_vm_pageout(void);
void __wrap_vm_pageout(void)
{
    const void *iokit_root;

    entry_note_boot_tail(4u, (uint32_t)(uintptr_t)__real_vm_pageout);
    iokit_root = entry_probe_dt_children();
    entry_probe_service_plane(iokit_root);
    /*
     * 491: and then one level below the root that census just read, which is where `createNubs` put the
     * nubs. It runs after the service census and takes nothing from it, deliberately: the expert is a
     * child of that root *at runtime*, not by construction this file can name, so the walk finds it by
     * reading rather than by being handed it.
     */
    entry_probe_service_tree();
    __real_vm_pageout();
    /*
     * Nothing is recorded after the call and that is itself the reading: `vm_pageout` is followed by
     * Apple's own NOTREACHED marker (`startup.c:647`), so a record written here would mean the
     * function returned, which the kernel says cannot happen. The `tail_seen[4]` count written before
     * the call is the whole measurement.
     */
}
