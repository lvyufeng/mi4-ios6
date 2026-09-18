# Experiment 301 — `machine.o`, `processor_up` runs real code, and the stop is the one name it calls that is not

**Step:** link `osfmk/kern/machine.c` (`osfmk_kern_machine.o`) — the object that defines `processor_up`,
where the 300 run stopped.
**Prediction:** `stub_hit=commpage_update_active_cpus`, `xnu_entry_stub_caller=0x800e6664` —
`processor_up + 0xac`, the return address of the `bl` at `processor_up + 0xa8`. That is, that
`processor_up`'s straight line runs real code and stops at the one call in it that is not real.
**Result:** `stub_hit=commpage_update_active_cpus` at `xnu_entry_stub_caller=0x800e6664`. Exact.

## Six resolved, one added, and two downstream stubs of which the path meets one

`osfmk/kern/machine.c` (manifest:569) was already built: `.text` 0x5F0, `.rodata.str1.1` 0xC7,
`.bss` 0x28.

**6 resolved** — five functions and one **storage** name:

| name | why it was a stub |
|---|---|
| `processor_up` | the 300 stop |
| `processor_assign` | referenced from `processor_start`/`processor_shutdown` paths |
| `processor_shutdown` | referenced from `processor_exit` |
| `host_get_boot_info` | a `host_priv` MIG routine |
| `host_reboot` | a `host_priv` MIG routine |
| `machine_info` (`B 0x28`) | storage; also why `__bss_end` moves if it is derived from sizes and not from slots |

**1 added**: `commpage_update_active_cpus` — a reference this image did not previously carry.

**The object carries two stub references, and the step's claim is which one the path meets.**
`arm-none-eabi-nm` on the object against the step's `xnu_arm_entry_stubnames.txt` gives exactly two:
`commpage_update_active_cpus` (called from `processor_up`) and `PEHaltRestart` (called from
`processor_doshutdown` and `host_reboot`, neither of which is on this path). A reading that counted
"stubs left in the object" would say two and predict nothing; a reading of the body says which one.

Also of the object's definitions, `ml_io_read`/`16`/`32`/`64`, `processor_doshutdown` and
`processor_offline` were **never stubs** — the 250/270 shape from both sides at once, an object that
answers the link and obliges it to almost nothing.

## The body had to be read, and reading it is where the step's trap was

`processor_up` is nine calls, and eight of them are real. Read off the image before the device was
touched:

| at | what runs | |
|---|---|---|
| 0x800e65c8 | `ml_set_interrupts_enabled(0)` (`splsched()`) | real |
| 0x800e65d4 | `init_ast_check(processor)` | real |
| 0x800e65e4 | `lck_spin_lock(&pset->pset_lock)` | real |
| 0x800e65e8–0x800e6648 | `++pset->online_processor_count`, `enqueue_tail(&pset->active_queue, processor)`, `processor->state = PROCESSOR_RUNNING` (6), `++pset->active_processor_count` | inlined |
| 0x800e664c | `sched_update_pset_load_average(pset)` | real |
| 0x800e665c | `hw_atomic_add(&processor_avail_count, 1)` — r0 = 0x80136f80 | real |
| **0x800e6660** | **`commpage_update_active_cpus()`** | ***the stop*** |
| 0x800e6668 | `lck_spin_unlock(&pset->pset_lock)` | real |
| 0x800e666c | `ml_cpu_up()` | real |

`enqueue_tail` is not a call in the built code — it is `__QUEUE_ELT_VALIDATE` plus four stores — and
the validator is **live** in this build (`osfmk/kern/queue.h:230`, inside the `XNU_KERNEL_PRIVATE`
arm), so the body carries three `panic` paths with the strings *"Invalid queue element pointers"* and
*"Invalid queue element linkage"*.

**The first reading of the disassembly made that a panic whenever the queue is empty:**

```
800e65ec:  ldrd  r8, [r7]        @ elt->next, elt->prev
800e65f4:  cmp   r8, #0
800e65fc:  beq   800e66a0        @ <panic>
```

A fresh processor set *is* empty, so this looked like a step that cannot possibly proceed. It is not.
These queues are **circular**: `queue_init` sets `next = prev = que`, so an empty `pset->active_queue`
has `next == prev == &active_queue`, non-zero, and the validator's two equality tests
(`next->prev == que`, `prev->next == que`) both hold — which is exactly the code at 0x800e6618:

```
800e6618:  cmp   r3, r7          @ prev->next == que ?
800e661c:  cmpeq r0, r7          @ next->prev == que ?
800e6620:  bne   800e6680        @ <panic>  - not taken
```

The `beq` at 0x800e65fc is the `elt_next == 0` case, which cannot happen for an initialised set. The
test that decided this was **one line of `queue_init`, not the disassembly**.

**A panic path in a body is not a prediction that it runs.** The distinguishing evidence for these
two is a data-structure invariant in a header, and the disassembly alone does not carry it.

## The build

| | predicted | measured |
|---|---|---|
| undefined / function / storage | 755 / 668 / 87 | **755 / 668 / 87** |
| `.data` | 0x80118000 | 0x80118000 |
| `__bss_start` | 0x80130a00 | 0x80130a00 |
| `__bss_end` | 0x80167798 | **0x80167798** (unchanged) |
| `.text` | 0x117120 → ~0x1177E8 | **0x1176C0** |
| image | 1247700 | **1247700** (unchanged) |

Three of the four layout lines landed exactly, and **`__bss_end` is unchanged for the second step
running** — by the same mechanism, not by luck. Measured in the map:

```
osfmk_kern_machine.o  .bss  0x801660a0  0x28
xnu_arm_entry_realstubs.o .bss 0x80166100 0x1684      (was 0x801660c0 / 0x16c4)
```

The object's 0x28 plus the fill it forces in front of the 64-aligned stand-in arrays grows
`realstubs.o`'s `.bss` **start** by 0x40; the retired `machine_info` stand-in shrinks its **size** by
0x40. Start + size is the same number. This is 299's rule ([[mi4-measurement-defects]], "a `.bss`
change is a slot change") applied in the direction that leaves the total alone.

### The `.text` miss, and a new kind of number

`.text` came in 0x128 below the estimate, and the whole decomposition is measurable in the map:

| | |
|---|---|
| `machine.o` `.text` | **+0x5F0** — placed 0x5F0 at 0x800e65b8 |
| `machine.o` `.rodata.str1.1` | **+0x3A** — *0xC7 in the object* |
| `realstubs.o` `.text` | **−0x60** — five function stubs retired, one added: −4 × 24 |
| `realstubs.o` `.rodata.str1.4` | **−0x3C** — five name strings retired, one added |
| section alignment | **+0x12** |
| | **+0x5A0** = the measured delta, to the byte |

Two errors, and only one of them is arithmetic. **Five stubs retire, not one** — `machine.o` defines
five names the image was stubbing, so `realstubs.o` loses 0x60 of `.text` rather than staying put.
And **`.rodata.str1.1` is a mergeable string section, so the linker relaxes it**: 0xC7 in the object
became 0x3A in the image, 141 bytes of common suffixes overlapped. The map prints both numbers side
by side — `0x3a … (size before relaxing) 0xc7` — for exactly this reason.

**A `.rodata.str*` size in an object is an upper bound, not a contribution.** That is a third kind of
number in this project's running list of them: a *total* read as a component (298, 300) and a
*component* read as a total are the same error; an *upper bound* read as a contribution is a
different one, and it is invisible in the object file.

## The run

```
stub_hit=commpage_update_active_cpus        xnu_entry_stub_caller=0x800e6664
xnu_entry_bss_start=0x80130a00              xnu_entry_bss_end=0x80167798   (both unchanged)
xnu_entry_bss_bytes=0x00036d98              xnu_entry_copied_bytes=0x001309d4
```

`0x800e6664` is `processor_up + 0xac` and `processor_up` is at 0x800e65b8 in the map, so the caller
key identifies the instruction, not just the function. What that measures, none of it asserted before
the run:

* `splsched()` ran, `init_ast_check` ran, the processor set's spinlock was taken.
* **`enqueue_tail` put the processor on the set's `active_queue`** — the inlined validator accepted
  an empty initialised circular queue, and the three panic paths were not taken.
* `processor->state = PROCESSOR_RUNNING`, the online and active counts were incremented,
  `sched_update_pset_load_average` ran, and `hw_atomic_add` incremented `processor_avail_count` at
  0x80136f80 — the *same variable* `commpage_update_active_cpus` reads to publish the count.
* The stop is the publication step. The processor is, at this instant, fully up in Mach's data
  structures and has not yet told the commpage.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`),
log 301128 bytes, one `stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10, `ro.product.device` = cancro).

**Next:** experiment 302 — `osfmk/arm/commpage/commpage.c` for `commpage_update_active_cpus`:

```c
void
commpage_update_active_cpus(void)
{
        if (!commPagePtr)
                return;
        *((uint8_t *)(_COMM_PAGE_ACTIVE_CPUS+_COMM_PAGE_RW_OFFSET)) = processor_avail_count;
}
```

Behind it `processor_up` ends with `lck_spin_unlock` and `ml_cpu_up` (both real), after which
`load_context` continues through `stack_alloc_try`, `sched_run_incr`, `thread_get_perfcontrol_class`,
`processor_state_update_explicit`, `mach_absolute_time` and two `timer_start`s — all real — into
**`machine_load_context`** (`osfmk/arm/cswitch.s`), which is a stub and **is the context switch
itself**. It does not return. That is the step where this walk stops being a walk.
