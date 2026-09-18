# Experiment 291 — `osfmk_kern_ipc_tt.o`, and the pad replaced by a pin

**Step:** link the object that defines `ipc_task_init` — `osfmk/kern/ipc_tt.c`, `osfmk_kern_ipc_tt.o`
— the name the 290 run stopped at. The largest step since 288: 12676 bytes of `.text`, 60
definitions, 53 references, 37 stubs retired and 2 obliged.
**Prediction:** that the frontier moves to `mac_exc_create_label` at `ipc_task_init + 0x0dc` —
**whichever branch of `if (parent == TASK_NULL)` the function takes**, because the `CONFIG_MACF`
exception-action loop precedes both arms.
**Result:** exactly that. **`stub_hit=mac_exc_create_label` at `xnu_entry_stub_caller=0x800c8d1c`** —
after the build had first *refused the step*, because the instrument that has guarded these writes
since 281 had finally run out of room.

## The object, measured

```
12676 bytes of .text, 82 of rodata.str1.1, 48 of __DATA,__data, 60 definitions, 53 references.

resolved  37   the fifteen convert_* names, ipc_task_init/enable/disable/reset/terminate, the four
               ipc_thread_*, mach_ports_lookup/_register, port_name_to_thread,
               space_deallocate, space_inspect_deallocate, the five task_* and five thread_*
               _special_port/_exception_ports - every one already a stub in this image, so this
               object retires 37 names, the largest retirement of the walk by count
added      2   mac_exc_free_action_label, mac_exc_inherit_action_label - both functions, both from
               the same unlinked security/mac_exc.c
```

Eight of the 53 references are already stubs (`io_free`, `ipc_object_copyin`,
`ipc_object_translate`, five `mac_exc_*`) and 43 are already real, so `added` is 2 and not 45.

`ipc_task_init` sits at **object offset 0**, so unlike 284 the object offsets are function offsets
and no adjustment is needed.

## The prediction is branch-independent

Every `bl` in `ipc_task_init`'s 0x2e8 bytes, in address order, with its status in the image:

```
+0x020  ipc_space_create          real    +0x034  panic                  real
+0x04c  ipc_port_alloc_special    real    +0x064  panic                  real
+0x06c  ipc_port_alloc_special    real    +0x084  panic                  real
+0x09c  lck_mtx_init              real    +0x0c4  ipc_port_make_send     real
+0x0d8  mac_exc_create_label      ***STUB***   <- the stop, return address +0x0dc
+0x0e4 .. +0x1a4   the other twelve unrolled MACF iterations
+0x1b8  lck_mtx_lock              real
+0x1c0 .. +0x264   the parent != TASK_NULL arm: eight ipc_port_copy_send, one
                   mac_exc_inherit_action_label, lck_mtx_unlock
+0x2a8  host_priv_self            real    +0x2b8  host_get_special_port  real
```

The `CONFIG_MACF` loop — `for (i = FIRST_EXCEPTION; i < EXC_TYPES_COUNT; i++)`, thirteen iterations,
fully unrolled — sits above `if (parent == TASK_NULL)` in the source and above both arms in the text.
**So the stop is at +0x0d8 whichever branch runs**, which is the first prediction of this walk that
does not depend on a branch. What the parent value decides is which arm runs *after* the stop, and
that is the next step's business.

Eight real calls have to return before the stop, and three of them have a stub inside them. Each
guard was read rather than assumed — the method 285–290 settled on:

| call | the stub inside | why it is not reached |
|---|---|---|
| `ipc_space_create` | `zfree` | in the `if (table == IE_NULL)` arm — only when `kalloc` is out of memory |
| `ipc_table_alloc` → `kalloc_canblock` | `OSKextGetAllocationSiteForCaller` | `ipc_table_alloc` is `return kalloc(size)`, i.e. `VM_ALLOC_SITE_STATIC(0, 0)` — **flags 0** — and `VM_TAG_BT` is set only by the `*_tag_bt` macros (kalloc.h:100-139), so `vm_tag_bt` is not entered |
| `kmem_alloc_flags` | `trace_backtrace` at +0x50 | `ldr r0,[0x8012a1ac] / cmp r0,#0 / beq` — the word is `log_leaks`, in `.bss` (zeroed) and set only by the sysctl of that name |
| `lck_spin_unlock` → `hw_lock_unlock` → `_enable_preemption` | `ast_taken_kernel` | behind `thread->machine.CpuDatap->cpu_pending_ast & AST_URGENT` (locks_arm.c:319); nothing has posted an AST this early |
| `zfree` (not on this path at all) | `trace_backtrace`, `btlog_add_entry`, `OSBacktrace`, `btlog_remove_entries_for_element` | the zone-logging paths, which 287 already measured as needing a `zlog` boot-arg this payload does not carry |

The falsifiers were named in advance and none fired: `ast_taken_kernel`, `log_leaks` being non-zero,
`OSKextGetAllocationSiteForCaller`, or anything out of `zfree`.

## The build refused the step, and was right

`verify_pad` stopped it:

```
FAIL: entry_skip_pad is at 0x800023d4, which puts the branch itself on or above
      0x8000235c, one of the addresses XNU writes
```

Thirty-seven retired stubs moved `&ResetHandlerData - &ExceptionLowVectorsBase` from 290's 0x2448 to
**0x2358** — 0x7c bytes *below* the 512-byte pad's start — so XNU's two writes were about to land in
`entry_epilogue`'s own code for the third time. The check 288 built against exactly this caught it
with **no device run spent**: the first defect in this project stopped by a build *before* it cost a
single silent run.

**And it is a treadmill.** The difference is a difference between the addresses of two *generated stub
bodies*, so it moves by 0x18 for every stub name entering or leaving the alphabetically-ordered stub
object between "E" and "R". The five values measured so far:

```
281  0x2404     288  0x24a8     289  0x24c0     290  0x2448     291  0x2358
```

A pad can be re-aimed at each of them; the next step moves it again. So the pad was neither widened
nor moved — **the value was given one definition**, in `entry.ld`, which is the same fix 288 applied to
the *check* now applied to the *value*:

```ld
    ExceptionLowVectorsBase = ENTRY_BASE;                  /* the image base */
    ResetHandlerData = __entry_reset_handler_data - 4;     /* a reserved .bss slot */
```

with 16 bytes reserved in `.bss` at `__entry_reset_handler_data`. `cpu.c` computes
`gPhysBase + (&ResetHandlerData.boot_args - &ExceptionLowVectorsBase)`, so with these two values the
two writes come out as the first two words of that slot. Two consequences, both measured in the
linked image rather than argued:

- the writes go to `.bss` — zeroed by the payload, never executed, read by nothing;
- `cpu.c:565`'s `bcopy(&ExceptionLowVectorsBase, LowExceptionVectorsAddr, 0x90)` and the page-long copy
  after it become **a page copied onto itself** through two mappings of the same physical page. Before
  this step, with `ExceptionLowVectorsBase` being a stub address, they took 4096 bytes of stub bodies
  over page zero — over start.s's own code. That had been latent since the beginning: nothing was
  copying the vectors anywhere.

And because these are linker-script symbols and pass 1 links with the same script, **the generator
never sees them as undefined and never stubs them** — one definition of each, where before there were
two (a stub body and a written-down number).

`verify_pad` now checks the two write addresses against the reserved slot, still checks that
`ExceptionLowVectorsBase` is the image base, and still checks that the pad is a real skipped range —
the pad stays as a second line of defence, which is how a region that once absorbed XNU's writes
should end up.

## The build

```
                      predicted        measured
undefined             827              827
function stubs        733              733
storage               94               94
image                 1198104          1198104
__bss_start           0x80123c08       0x80123c08
text                  ~1093000         1093400
```

All three counts and `__bss_start` exact. The image grew by **+48 bytes — exactly the object's
`__DATA,__data`** — because `ipc_tt.o`'s 12.5 KB of read-only content fitted inside the 15268 bytes
of slack, so `.data` did not move. bss end 0x8015a408 → 0x8015a458 (+16 for the reserved slot, +64 of
alignment inside the stub object's storage), headroom 1727480 → 1727400.

```
entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
XNU writes 0x8015a448 and 0x8015a44c (ResetHandlerData - ExceptionLowVectorsBase = 0x15a444),
both inside the reserved slot at 0x8015a448
```

## The run

```
stub_hit=mac_exc_create_label        xnu_entry_stub_caller=0x800c8d1c
```

`0x800c8d1c` is `ipc_task_init + 0x0dc`: the function links at 0x800c8c40 and the image's own
instruction stream has `bl mac_exc_create_label` at 0x800c8d18.

So one run measured the longest chain this walk has had to clear — eight real calls, including two
`ipc_port_alloc_special`s, `ipc_space_create` with `kalloc_canblock` underneath it and
`ipc_port_make_send` — and that none of the three guards was open.

**The instrument change is measured in the same run, and that is the more important half:** the report
arrived normally with XNU's writes aimed at the reserved `.bss` slot instead of at a skipped pad —
`kernel_entry ok`, `loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`. The
treadmill is over: those two addresses are now a property of the link script rather than of where the
stub object's bodies happen to sit.

Preflight clean, log 301121 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 292 — `security/mac_exc.c` for `mac_exc_create_label` and its
`mac_exc_associate_action_label` partner, the two names this step added and the thirteen unrolled loop
iterations that call them. After that, `ipc_task_init`'s remaining work (the `parent == TASK_NULL` arm
is real, the other arm is eight `ipc_port_copy_send`s) and then the nine further stub calls
`task_create_internal` makes at +0x300 and up.
