# Experiment 255 — The Ledger Is Live, the Allocation Completes, and the Frontier Asks for Something Not in the Tree

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`osfmk_kern_ledger.o` — **8324 bytes of text**, 35 references — the ledger subsystem. Resolved
**19**, added **1**:

```
resolved   ledger_credit  ledger_debit  ledger_dereference  ledger_disable_callback
           ledger_entry_add  ledger_entry_setactive  ledger_get_entry_info  ledger_get_limit
           ledger_get_period  ledger_init  ledger_instantiate  ledger_reference  ledger_rollup
           ledger_set_action  ledger_set_callback  ledger_set_limit  ledger_set_period
           ledger_template_complete  ledger_template_create
added      thread_block_reason
```

613 → **595** undefined, 532 → **514** function stubs, 81 storage stubs, text 692865 (+7872).

## The prediction: the earlier miss, corrected

Experiment 254's stop was the inlined `pmap_tt_ledger_credit` call inside `pmap_expand`, and its
prediction (for `__firehose_buffer_create`) had been one call too early because that allocation's
`kmem_alloc_flags` goes through the pmap. With the ledger linked, the two ledger calls are real, and
every direct call in `pmap_expand` after the stop (`ptd_alloc`, `lck_spin_lock`, `lck_spin_unlock`,
`pmap_tt_deallocate`) is real too — and so is every direct call in `pmap_enter_options`, the pmap's
own caller. So the allocation should complete and `oslog_init` should continue to the call it could
not reach in 254:

```
8002cd58  bl __firehose_buffer_create    <- STUB
```

**The prediction: `stub_hit=__firehose_buffer_create`, caller `oslog_init+0x70`** — the same name as
254's wrong answer, but for the opposite reason: in 254 it was the *next* call on a straight-line model
that skipped the one in between; here the one in between has been linked.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000045
 xnu_entry_kv_in_dram=0x00000045
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=__firehose_buffer_create
 xnu_entry_stub_caller=0x8002cd5c

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x45` (69 bytes
= 35 + 34, the name being 24 characters), `0x8002cd5c` resolves to `oslog_init+0x70`, and its
`caller - 4` is `8002cd58: bl 80094a30 <__firehose_buffer_create>`.

**And the size of the thing that did not fit is on the record**, because `oslog_init` was compiled with
the constants inline:

```
8002ccf4  mov  r0, #65536          ; size = FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT * FIREHOSE_CHUNK_SIZE
8002cd20  mov  r2, #73728          ; size + 2 * PAGE_SIZE  (0x12000)
8002cd24  mov  r3, #19             ; VM_KERN_MEMORY_LOG
8002cd10  ldr  r0, [..]            ; kernel_map
8002cd1c  mov  r1, r4              ; &kernel_firehose_addr
8002cd18  str  r1, [sp]  (r1 = 48) ; KMA_GUARD_FIRST | KMA_GUARD_LAST
```

So this run made a **73728-byte guarded allocation from `kernel_map`** — the first time this kernel has
ever allocated kernel memory for a purpose rather than to initialise itself. It is the first real
`kmem_alloc` in the sequence: it went into the pmap (254's `ledger_credit`), the pmap entered the
region, page tables were expanded for it, and the ledger accounted for it. `__bzero` then cleared the
buffer, and only then did `oslog_init` reach the missing symbol. The `panic("Failed to allocate memory
for firehose logging buffer")` at `+0x48` was not taken, so `kmem_alloc_flags` returned
`KERN_SUCCESS`.

## The finding: `__firehose_buffer_create` is not in this source tree

`bsd/kern/subr_log.c:874` calls it:

```c
	kernel_firehose_addr = (vm_offset_t)__firehose_buffer_create((size_t *) &size);
```

and it is **referenced there and nowhere defined**. It is not in `osfmk/`, not in `bsd/`, not in
`libkern/`. The firehose library ships as headers only:

```
$ ls external/xnu-4570.1.46/libkern/firehose/
chunk_private.h  firehose_types_private.h  ioctl_private.h  Makefile  private.h  tracepoint_private.h

$ cat external/xnu-4570.1.46/libkern/firehose/Makefile | grep KERNELFILES
KERNELFILES =
```

`KERNELFILES` is empty — the Makefile exports the headers and compiles nothing. Apple's firehose
implementation is a closed kernel library/kext; `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT`, which
`subr_log.c` uses, is not in the tree either (the compiled constant says it is 16).

**This is the first frontier symbol in the entire sequence whose implementation does not exist in the
source tree.** Every one of the ~250 previous steps was "link the object that defines it"; there is no
such object here. That makes 256 a decision rather than another link:

1. **Provide it from the entry image.** The entry image already supplies real definitions for symbols
   XNU expects from elsewhere (`arm_init`, `pmap_bootstrap`, the cache and MMU layer), so a
   `__firehose_buffer_create` in `entry_stubs.c` is the same kind of thing — but it cannot be a stub
   that stops the boot: `oslog_init` uses its return value as `kernel_firehose_addr`, and the kernel
   logs to that address from then on. A minimal real implementation (or a handoff that returns the
   already-allocated `kernel_firehose_addr` so the log buffer is simply never registered) is what the
   symbol needs.
2. **Look for the implementation elsewhere in the tree first** — `libkern/os/log.c` is in-tree and does
   know the firehose chunk layout (it memcpy's the boot chunk), so there may be enough structure there
   to write a minimal buffer registration against the shipped headers rather than invent one.

Which of the two is decided by reading the headers in `libkern/firehose/` and `libkern/os/firehose.h`
against `libkern/os/log.c`, and the decision belongs in experiment 256's own notes.

## Cost

| | exp-254 | now |
| --- | --- | --- |
| undefined | 613 | **595** (19 resolved, 1 added) |
| function stubs | 532 | **514** |
| storage stubs | 81 | **81** |
| entry text | 684993 B | **692865 B** (+7872) |
| entry image | 785648 B | **802176 B** (+16528) |
| entry `.bss` | 0x800bf5c0–0x800ee708 | **0x800c35c0–0x800f28c8** |
| layout | args +983040, headroom 1120504 B | **args +999424, headroom 1103672 B** |
| payload text | 1277866 B | **1294394 B** |

The image moved again (+16528), taking the `boot_args` offset to 999424 and the headroom below
`topOfKernelData` to **1103672 bytes**. The entry image has now grown from 736192 bytes (at 248) to
802176.

## Reproduce

```bash
grep -n 'OSFMK_KERN_LEDGER_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'
comm -23 <(sort /tmp/undef_254.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 19

# the prediction
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<oslog_init>:/{f=1} f{print} f&&/^$/{exit}'
./tools/xnu_entry_callwalk.py --root pmap_expand          # read ALL of it this time

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8002cd5c             # -> oslog_init+0x70

# the allocation that went through the pmap, from the compiled constants
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<oslog_init>:/{f=1} f{print} f&&/^$/{exit}' | head -14
sed -n '857,876p' external/xnu-4570.1.46/bsd/kern/subr_log.c

# the finding: the symbol has no implementation in the tree
grep -rn '__firehose_buffer_create' external/xnu-4570.1.46/          # only the one call site
grep KERNELFILES external/xnu-4570.1.46/libkern/firehose/Makefile    # empty
ls external/xnu-4570.1.46/libkern/firehose/
```
