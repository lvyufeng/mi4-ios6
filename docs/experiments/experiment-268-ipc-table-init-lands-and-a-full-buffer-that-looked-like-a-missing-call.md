# Experiment 268 — `ipc_table_init` Lands, and a Full Buffer That Looked Like a Missing Call

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

267's stop was `ipc_table_init`, and the object that defines it is `osfmk/ipc/ipc_table.c`
(manifest:521) — `osfmk_ipc_ipc_table.o`, 544 bytes of text, 80 of data, 8 of bss, ten definitions
and **exactly two references**, `kalloc_canblock` and `kfree`, both made real by 250. So this is the
258/260/263/266 shape: an object the manifest had been building all along, waiting for the image to
link it, and the link resolves and adds nothing.

Two of the ten definitions were already in hand from 266 — `ipc_table_alloc` and `ipc_table_free`,
thin wrappers over `kalloc`/`kfree` — and `ipc_table_init` is **two `kalloc_canblock` calls and two
`ipc_table_fill` loops**, with no `panic` in it: this build has `MACH_ASSERT` off, so the two
`assert(... != ITS_NULL)` lines compile out and the disassembly shows only the two `bl`s.

## The prediction and the result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003d
 xnu_entry_kv_in_dram=0x0000003d
 xnu_entry_kv_dropped=0x00000000
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_voucher_init
 xnu_entry_stub_caller=0x800abe28

No errors detected
```

`0x800abe28` resolves to `ipc_bootstrap+0x184`, whose `caller - 4` is `0x800abe24` —
`bl 800c3974 <ipc_voucher_init>`, **the call immediately after `bl ipc_table_init` at
`ipc_bootstrap+0x17C`**. Both are read straight out of the same ELF that made the run:

```
800abe1c:	bl	800ac31c <mig_init>
800abe20:	bl	800acbd8 <ipc_table_init>
800abe24:	bl	800c3974 <ipc_voucher_init>
800abe28:	bl	800c3734 <ipc_importance_init>
800abe2c:	bl	800c52ac <semaphore_init>
800abe30:	bl	800c4904 <mk_timer_init>
800abe34:	pop	{r4, r5, fp, lr}
```

so `ipc_table_init` returned to its caller, and the 64-entry doubling-fill over the entries table
and the requests table ran on the two allocations it had just made. The prediction — the **eleventh
step in a row** — held.

`kv_written == kv_in_dram == 0x3d` (61) with **`kv_dropped=0`**. `0x3d` is one byte more than 267's
`0x3b`: the fixed prefix is the same and `ipc_voucher_init` (17 characters) is three longer than
`ipc_table_init` (14), while the *stub's* trailing newline does not grow. `kv_dropped` is new in this
step and is what the second half of this document is about.

`failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

### The prediction was written in the previous step's coordinate system

267 recorded this same call sequence with the stop one call earlier: `xnu_entry_stub_caller=0x800abd84`
= `ipc_bootstrap+0x180`, whose `caller - 4` was `bl <ipc_table_init>` at **`ipc_bootstrap+0x17C`** —
the identical offset this step measures. Only the *base* moved: `ipc_bootstrap` was at `0x800abc04`
in 267's image and is at `0x800abca4` in 268's, because 268's link put `ipc_table.o` ahead of it.

That is why 268's written prediction, `xnu_entry_stub_caller=0x800abd88`, was the *right offset in the
wrong image*: `+0x184` from 267's base. It is a prediction of the form "the next `bl` after the one
that just returned", and an absolute address cannot carry across a step that grows the image. The
lesson is not that the prediction was wrong — the stop it named was exactly right — but that
**caller addresses must be resolved against the ELF that produced the run**, which is what the
`--elf` override on `tools/host_resolve_entry_addr.sh` is for, and why the offsets above are quoted
relative to the function.

## The trace: what it was for, and the wrong answer its first run gave

267's step had been recorded as ending in a **silent hang** — the payload's ladder completed, the
jump line was written, and then nothing, with no `stub_hit` and no fault. A diagnostic was built for
it (`entry_trace.c`, `STAGE90_ENTRY_TRACE=1`), because the frontier method names the next missing
*symbol* and there was no missing symbol: whatever stopped the machine was real XNU code that had
been in the image since 250 and had never executed.

The first finding is that **there was no hang.** Re-running the step with a payload that actually
contained it produced `stub_hit=ipc_voucher_init` on the first try. What had been captured was a
**stale embedded entry image**: the payload the device ran had been built before `ipc_table.o` was
in the entry image, so it stopped at `ipc_table_init` — and the log's tail
(`platform_reboot entered`, `hw_watchdog: forcing immediate bite`) was the payload's own recovery
path, not a hang. This is experiment 167's defect class, and the guard used here is to check the
payload *contains* the step: `strings -a out/stage90/stage90.bin | grep -c t268_kalloc_ret`.

The second finding is the trace's own first run, and it is the more instructive one. The run
reported:

```
 xnu_entry_kv_written=0x000007f6
 xnu_entry_kv_in_dram=0x000007f6
 ...
 t268_kma_size=0x00001000
 stub_hit=ipc_voucher_init
```

— ending **mid-record**, on a `t268_kma_size` with no matching caller, size or return. `0x7f6` is
2038, and `g_kv_buf` was **2048**. `entry_kv` returns without writing when the buffer is nearly full
and said nothing about it, so the trace covered the first six `kalloc` calls and nine
`kernel_memory_allocate` calls of the boot and nothing after them — and `ipc_table_init`'s two calls,
being the last two before the frontier, fell in the part that was dropped. Grepping the log for
`0x00000200` then returned 0, which read exactly like *"those calls never happened"*.

They happened. And the search had been made for `0x00000200` — a size this instrument had never
seen — because the "512 bytes per call" in 268's own written prediction came from reading
`ipc_table.c` too quickly and was never checked against `sizeof`. Two errors compounded: a
measurement that could not see the region of interest, and an expectation that would have been wrong
even if it could.

**Both were repaired, and the trace was run again.** `g_kv_buf` is 8192 bytes, `entry_kv` counts
every record it refuses and the epilogue reports it as `xnu_entry_kv_dropped`, and
`kalloc_canblock` now records the size **before** and **after** the call — the request and the answer
— because XNU writes the size it settled on back through `*psize` (`z->elem_size`, `kalloc.c:714`).
The second traced run then closed out the record set on the same stop:

```
 xnu_entry_kv_written=0x00000b99
 xnu_entry_kv_in_dram=0x00000b99
 xnu_entry_kv_dropped=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ipc_voucher_init
 xnu_entry_stub_caller=0x800abe28
```

2969 bytes of records, nothing dropped, and **the same stop and the same caller address as the
untraced stage run above** — the traced image is the stage image plus the wrappers. One layout
property had to be re-checked when the buffer grew: the exception handlers run on
`entry_vectors_stack`, whose top must stay at or below `g_kv_buf`'s first byte. It does —
`entry_vectors_stack_top` and `g_kv_len` are both `0x800f8f00`, `g_kv_buf` starts at `0x800f8f08`,
and everything after shifted up by the 6144 bytes the buffer gained. That adjacency is the linker
script's doing rather than a promise it makes, so it is checked with `nm -n` and not by the address
resolver.

The first traced run also carried the stop that made it confusing: `grep -c t268_kalloc_size=0x00000200`
= 0, and 268's own prediction of "512 bytes allocated per call" was never measured before it was
written. Both keys are `0x100`.

## The measurement the trace produced

With the whole record set present, the boot to `ipc_voucher_init` is 8 `kalloc_canblock` calls and
11 `kernel_memory_allocate` calls. Every caller resolved inside a function — that is the positive
control that the recorded return addresses are real, since a corrupted one lands in the middle of
nothing. The distinct kinds of call:

| call | size (request / actual) | count | caller | returned |
|---|---|---|---|---|
| `kalloc` | 264 / 288 | 3 | `lck_grp_alloc_init+0x2c` | `0xc0602ac0`, `0xc06029a0`, `0xc0602880` |
| `kalloc` | 256 / 256 | 2 | `ipc_table_init+0x34`, `+0x120` | `0xc060eb00`, `0xc060ec00` |
| `kalloc` | 4 / 8 | 3 | `lck_attr_alloc_init+0x24`, `lck_grp_attr_alloc_init+0x24` | `0xc0605810`, … |

| `kernel_memory_allocate` | size | flags | count | caller | returned |
|---|---|---|---|---|---|
| | 0x5000 | 0x06 | 1 | `zalloc_internal+0x3cc` | `KERN_SUCCESS` |
| | 0x1000 | 0x06 | 2 | `zalloc_internal+0x3cc` | `KERN_SUCCESS` |
| | 0x1000 | 0x06 | 1 | `vm_page_more_fictitious+0x80` | `KERN_SUCCESS` |
| | 0x2000 | 0x86 | 1 | `zalloc_internal+0x3cc` | `KERN_SUCCESS` |
| | 0x2000 | 0x06 | 3 | `zalloc_internal+0x3cc`, `waitq_bootstrap+0x114` | `KERN_SUCCESS` |
| | 0x1000 | 0x02 | 2 | `ltable_init+0x6c` | `KERN_SUCCESS` |
| | 0x2000 | 0x244 | 1 | `zone_init+0xec` | `KERN_SUCCESS` |

The callers above are quoted the way `tools/host_resolve_entry_addr.sh` reports them — the offset
that follows the call, which is the `bl` at `caller - 4`. Both images place these objects at the same
addresses: the traced run and the untraced stage run recorded the same caller for the same stop
(`0x800abe28`), so `entry_trace.o`'s extra text sits after the XNU objects and one ELF resolves
either run.

Three `lck_grp_alloc_init` calls appear, and the names they pass put three subsystems on the boot
path before the frontier: `0x80086750` → `"OSMalloc_tag"` (`kalloc.c:829`, in `kalloc_init`),
`0x800918f4` → `"KERNCS"` (in `cs_init`), `0x8009854c` → `"stackshot_subsys_lock"` (in
`stackshot_init`).

The four things this settles, none of which were readings before:

- **The ipc tables exist.** `kalloc(256)` twice, both non-NULL, returning blocks **256 bytes apart**
  (`0xc060eb00`, `0xc060ec00`) — two adjacent slots of the `kalloc.256` zone, and the same pair again
  one boot earlier (`0xc05e2900`, `0xc05e2a00`). 256 is right: `sizeof(struct ipc_table_size) * 64`
  = 4 × 64, for *both* tables, because `ipc_table.c:138` multiplies `ipc_table_requests_size` by
  `sizeof(struct ipc_table_size)` and not by the element `ipc_table_fill` writes into it.
- **`request == actual` for both**, so the zone ladder did nothing: `kalloc.256` serves a 256-byte
  request with 256 bytes. The 264 → 288 pair is the ladder doing its job, one line up the table.
- **No `vm_page_wait`, no `thread_block`.** Both wrappers exist and both recorded **zero** calls.
  Free pages were never short at this point: `vm_page_free_count` read `0x3ba` (954) at every
  `kalloc`, and every `kernel_memory_allocate` returned `KERN_SUCCESS`.
- **The expansion path was entered and succeeded.** `zalloc_internal+0x3c8` is the expansion
  `kernel_memory_allocate` — the one that takes `KMA_KOBJECT|KMA_NOPAGEWAIT` and, per the tracer's
  own reasoning, would return `KERN_RESOURCE_SHORTAGE` and then call `VM_PAGE_WAIT()` if pages were
  short. It ran five times and returned success five times. `vm_page_more_fictitious+0x7c` also ran,
  which is the other side of the same accounting.

## Finding: forcing `canblock` FALSE changed the destination

`entry_trace.c`'s first version wrapped `kalloc_canblock` and forced `canblock` to FALSE, on the
reasoning that a zone which cannot satisfy the request would return NULL and the next store into it
would abort and be reported. It did report — and the report was *about the forcing*:

- forced FALSE, the boot's first `kalloc` takes `zalloc.c:3318`'s branch
  (`(addr == 0) && (!canblock || nopagewait) && ...`), which sets `zone->async_pending` and calls
  `thread_call_enter(&call_async_alloc)`. That reaches `_pending_call_enqueue` and
  `enqueue_tail(&thread_call_groups[0].pending_queue, ...)` — and that queue is **still zero**,
  because `thread_call_initialize()` never runs in this boot: its only in-image caller is
  `kernel_bootstrap_thread`, reached from `kernel_bootstrap` through `thread_continue`, which this
  boot never performs. XNU's own panic fires:
  `"Invalid queue element pointers for %p: next %p prev %p"` (`queue.h:241`) with
  `elt = 0x800f6b58` = `thread_call_groups + 8` = `thread_call_groups[0].pending_queue`.
- not forced, that branch is not taken at all: `canblock == TRUE` skips it and enters
  `while ((addr == 0) && canblock)`, the expansion loop — which is where the real run goes, and
  where it succeeds.

So the two branches are different destinations, and a diagnostic that changes an argument has
changed the experiment. The version that produced everything above passes `canblock` straight
through.

## Cost

The step proper: resolved **3** (`ipc_table_init`, `ipc_table_alloc`, `ipc_table_free`), added
**0**; 863 → 860 undefined, 787 → 784 function stubs, storage unchanged at 76; text
904036 → 904484 (+448), image 1015440 → 1015520, bss end `0x8012a4c8` → `0x8012a508`.

The diagnostic, which is not the stage and is off by default: +160 bytes of text (904484 → 904644)
and the KV buffer's 6144-byte growth, taking bss end to `0x8012bd08`, the derived args offset to
+1232896, and headroom below `topOfKernelData` to 1917688 bytes. Payload text 1508766, `.bss`
644048.

## What is next

269's stop names **`ipc_voucher_init`**, and the object that defines it is
`osfmk/ipc/ipc_voucher.c` (manifest:522) — `osfmk_ipc_ipc_voucher.o`, **12335 bytes of text, 120 of
data, 2200 of bss**. That is by far the largest single step since 257, and the reason is that a
voucher is a first-class Mach object with its own hash table, zone, attribute machinery and MIG
subsystem.

`ipc_bootstrap` after it, in call order (`0x800abe28`–`0x800abe30`): `ipc_importance_init`
(`osfmk/ipc/ipc_importance.c`, manifest:511), `semaphore_init` (`osfmk/kern/sync_sema.c`,
manifest:587), `mk_timer_init` (`osfmk/kern/mk_timer.c`, manifest:572) — and then the function
returns. `host_notify_init` (`osfmk/kern/host_notify.c`, manifest:548) and `mac_policy_init`
(`0x8000dc84`) are `kernel_bootstrap`'s, not `ipc_bootstrap`'s, and come after.

## Reproduce

```bash
# the stage image, as it is judged - no trace
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)

# the trace, and the check that the payload contains it
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
strings -a out/stage90/stage90.bin | grep -c t268_kalloc_ret      # must be 1, not 0
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)

# the two ipc_table allocations, and the stop, resolved against the ELF that made the run
grep -a -B3 -A3 "t268_kalloc_caller=0x800acc0c" /tmp/cancro-last_kmsg.txt
tools/host_resolve_entry_addr.sh 0x800acc0c
tools/host_resolve_entry_addr.sh 0x800accf8
tools/host_resolve_entry_addr.sh 0x800abda8

# the layout property the larger buffer had to preserve
arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf | grep -E "entry_vectors_stack_top|g_kv_buf"
```
