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
    entry_kv("t268_vm_page_wait_caller", (uint32_t)(uintptr_t)__builtin_return_address(0));
    return __real_vm_page_wait(wait_type);
}

/* -------------------------------------------------------------------- thread_block */
/*
 * `void thread_block(thread_continue_t continuation)`. Reporting here rather than returning is the
 * point: this boot has one thread, so a block is not a delay, it is the end of the run. The
 * epilogue's cache work and the teardown are what get the record out.
 */
void __real_thread_block(void *continuation);

void __wrap_thread_block(void *continuation)
{
    entry_kv("t268_thread_block_caller", (uint32_t)(uintptr_t)__builtin_return_address(0));
    entry_kv("t268_thread_block_continuation", (uint32_t)(uintptr_t)continuation);
    entry_epilogue("t268: thread_block was called");
}
