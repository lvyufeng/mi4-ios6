# Experiment 380 — `osfmk/ipc/ipc_kmsg.c`: the object that empties the frame 379 stopped in, and a prediction that retired itself

**Step:** link one object, `osfmk/ipc/ipc_kmsg.c` (`osfmk_ipc_ipc_kmsg.o`) — the pool's only definer
of 379's stop `ipc_kmsg_dequeue` and of the two other stubs in the frame it stopped in. It is inserted
into the entry link between `bsd_kern_sys_reason.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`.
Nothing else changes.

**Prediction:** *13 resolved (12 function, 1 storage) / 15 added (15 function) — 742 → **744**
undefined, 644 → **647** function, 98 → **97** storage*; `realstubs.o` `.text` **0x3CA8**, its
`.rodata.str1.4` **0x366E** and its `.bss` **0x2604**; the object's `.text` **0x80180618** (0x3BD0),
its `.rodata.str1.1` **0x801AA789** (0xA5), its `.rodata` **0x801AA830** (0x14) and its
`__DATA,__data` **0x801CA380** (0x30); `MSM8974PlatformExpert.o` `.text` **0x801841E8**; rtabi
**0x80184340**; `realstubs.o` `.text` **0x80184394**; the `initcode` pad **0x4**; text size
**1764896 (0x1AEE20)**; `.data` **0x801B0000 (0x1A3B0)**; `.sysctl_set` **0x801CA3B0 (0x158)**;
`.init_array` **0x801CA508 (0x90)**; `.bss` **0x801CA5C0 .. 0x80203618** with the object's 0x8 at
**0x80200FBC** and `realstubs.o`'s slot at **0x80201000 (0x2604)**; args **0x80205000**,
`topOfKernelData` **0x80400000**, headroom **2083304**, image **1877400 (0x1CA598)**. And for the next
stop — **the prediction 379 wrote was wrong and was corrected before this build**: the frame cannot
stop at all, and the walk returns into one of two callers, whose continuations give
**`ipc_notify_send_once` at key `0x800d82C0`**, **`ipc_notify_dead_name` at key `0x800d8330`**,
**`io_free` at key `0x800d8380`**, or **no nameable stop**.

**Result:** **all three counts exact** (744 / 647 / 97); **every section-level number exact**, including
the whole `.bss` chain the previous step missed (`0x801CA5C0 .. 0x80203618`, `realstubs.o`'s slot
`0x80201000`, `__bss_end`, the 0x2C → 0x24 fill, `.bss` printed size 0x39058), text size 1764896, image
1877400, args 0x80205000, `topOfKernelData` 0x80400000 and headroom 2083304 — and the run's own markers
agree to the byte. The one thing that did not come out as written is the mergeable `.rodata` run: the
addresses inside it are 0x18 low and then 0x4 low, and the chunk the shared string came out of is not
the one the prediction said. Neither reached the section's end, because `initcode`'s pad absorbed both.
And **the stop is prediction (c): `stub_hit=io_free` at key `0x800d8380`** = `ipc_port_destroy +
0x35C` — which also settles *which caller* the frame returned into, the one thing the previous step's
record said no log could settle.

## The object

```
== osfmk_ipc_ipc_kmsg.o
   54 definitions, 73 references
   resolved (13: 12 function, 1 storage)
      ipc_kmsg_alloc                                             object T, stand-in was func T
      ipc_kmsg_copyout_object                                    object T, stand-in was func T
      ipc_kmsg_copyout_size                                      object T, stand-in was func T
      ipc_kmsg_delayed_destroy                                   object T, stand-in was func T
      ipc_kmsg_dequeue                                           object T, stand-in was func T
      ipc_kmsg_destroy                                           object T, stand-in was func T
      ipc_kmsg_enqueue_qos                                       object T, stand-in was func T
      ipc_kmsg_free                                              object T, stand-in was func T
      ipc_kmsg_override_qos                                      object T, stand-in was func T
      ipc_kmsg_queue_next                                        object T, stand-in was func T
      ipc_kmsg_reap_delayed                                      object T, stand-in was func T
      ipc_kmsg_rmqueue                                           object T, stand-in was func T
      ipc_kmsg_zone                                              object B, stand-in was data B 0x4
   added (15: 15 function, 0 storage)
      ipc_entries_hold  ipc_entry_claim  ipc_entry_dealloc  ipc_entry_grow_table
      ipc_notify_port_deleted  ipc_object_copyin_from_kernel  ipc_object_copyin_type
      ipc_object_copyout_dest  ipc_object_destroy  ipc_object_destroy_dest
      ipc_right_copyin  ipc_right_copyin_check  ipc_right_copyin_two  ipc_right_copyout
      ipc_right_reverse
   of the 73 references, 58 are already satisfied
```

Twelve function records retire at once — the largest single retirement since the walk reached
`ipc_kmsg.c`'s seven names at 279 — and the fifteen it adds are the pointer-rights face of IPC, all of
them already compiled in the pool.

**The one retired storage record is `ipc_kmsg_zone` (`pool B 4`)** — the first storage record to retire
since 374, and the first change to `realstubs.o`'s `.bss` in six steps. Its slot table is a sum of
per-record `align64(size)` terms, so it gives up exactly `align64(4)` = 0x40 — *provided* the retired
record is not the last one, whose term the table takes raw. `ipc_kmsg_zone` is record 21 of 98, so it
is not, and the arithmetic is a clean subtraction rather than a rule change.

The three synthetic sizes, each recomputed from its rule and each checked against 379's map before
being used:

```
realstubs.o .text           644 x 0x18 = 0x3C60  ->  647 x 0x18 = **0x3CA8**
realstubs.o .rodata.str1.4  0x3622 - 0x104 + 0x150 = **0x366E**
realstubs.o .bss            0x2644 - 0x40 = **0x2604**
```

The middle one is the delicate one, because its rule is *not* a plain sum: it is `Σ align4(len+1)`
over all but the last name plus the last name's raw `len+1`, so a retirement or an addition is only a
clean per-name term while the **last** name stays last. It does — the generated list is C names then
`_Z`-mangled names, the last name is `_ZN9IODTNVRAMC1Ev`, and no mangled name is retired or added — so
the delta is exactly the twelve retired terms (0x104) and the fifteen added ones (0x150). Read as a
plain `Σ align4(len+1)` the same 647-name list gives 0x3670, 2 bytes more, so this is a term the rule
has to get right rather than a rounding. Measured: **0x366E**, exact.

## The layout: the cursor rule, not the shift rule

379's `.bss` miss had a second life in the tooling. Every layout prediction in this series had been
computed with

    address = align_up(old_address + shift, alignment)

which double-counts an input's old padding: `old_address` is already aligned, so adding a shift to it
and re-aligning adds the old pad a second time. The map settles which rule the linker uses. Walking
each section with `address = align_up(cursor, alignment)` — the linker's rule, applied to the *cursor*
rather than to an already-aligned address — reproduces the map at **every one of the 366 rows** of
`.data`, `.sysctl_set`, `.init_array` and `.bss`, 0 mismatches. And where the two forms disagree, 379's
own measurement decides: with a 0x40 shift, `align_up(old_addr + 0x40, 0x40)` puts `realstubs.o`'s
`.bss` at 0x801FD000, while the map says **0x801FCFC0**, which is `align64` of the *new* cursor
0x801FCF94. So the cursor is what is carried, and 380's prediction is built that way.

One refinement is needed for sections that are `SHF_MERGE`: `osfmk_ipc_ipc_kmsg.o`'s `.rodata.str1.1`
is one, and there the map prints each input's upper-bound size, so four rows in the image print an
address *below* the cursor (`osfmk_arm_loose_ends.o` by 0x14, `bsd_kern_kern_ktrace.o` by 0x4,
`iokit_Kernel_IOStartIOKit.o` by 0x7, `osfmk_device_iokit_rpc.o` by 0x14). Those rows are anchored to
the map's own address plus the shift, and the tail is anchored so the drift cannot grow.

## The merge run: measured by rebuilding 379 and diffing the two maps

The prediction named `osfmk_kern_locks.o`'s `.rodata.str1.1` as the chunk that would *give up* the
0x14 of the string `"overflow detected"` (which already sits at 0x8018D802, at the very end of that
chunk, and is at 0x8018D802 + 0x3C18 = 0x8019141A in the new image), and gave the new object's chunk
its full 0xA5. Rather than argue about which copy the linker drops, 379's link was rebuilt — its object
taken out of the list, `OUT` pointed at a scratch directory — and the two maps diffed. It reproduces
379's published numbers exactly (text 1749280, image 1860968, `.bss` 0x801C6580..0x801FF618, headroom
2099688, 742 / 644 / 98), which is what makes it a 379 map and not a 380 map with holes.

**Exactly two pre-existing rows in the whole `.text` section changed size:**

```
xnu_arm_entry_realstubs.o   .text            0x3C60 -> 0x3CA8  (+0x48)
xnu_arm_entry_realstubs.o   .rodata.str1.4   0x3622 -> 0x366E  (+0x4C)
```

**No earlier `.rodata.str1.1` chunk shrank at all.** Every row from `osfmk_arm_arm_init.o` to
`bsd_kern_sys_reason.o` has the same delta, **0x3C18** (= the new object's `.text` 0x3BD0 + the 0x48
`realstubs.o`'s `.text` grew by), and every row after the insertion point has **0x3CBC**. So the merge
saving in this case is entirely inside the new object's own chunk — 0xA5 as a sum of its four strings,
**0x91** as placed — and `osfmk_kern_locks.o` keeps its full **0x10B**.

That is the *opposite* direction from 377, which measured five earlier chunks shrinking by a total of
0x18 (plus 0xC of fills, so 0x24) when `kern_malloc.o` and `Tests.cpp` were added. Both are measured;
they are not one rule. What *is* a rule is the **net**: the placed size is the object's own sum minus
the bytes of every string the image already had (here 0xA5 − 0x14 = 0x91), and that net is what the
tail sees. **Which chunk's printed size carries the saving is a measurement per case, not a
prediction** — and since the map prints each input's upper-bound size for a `SHF_MERGE` section, a
cursor walk over those rows can be wrong by a few bytes per case without affecting the net.

So the run's numbers against the prediction:

| row | predicted | measured |
|---|---|---|
| `osfmk_kern_locks.o` `.rodata.str1.1` | 0x80191323 (0xF7) | 0x80191323 (**0x10B**) |
| `bsd_kern_sys_reason.o` `.rodata.str1.1` | 0x801AA749 (0x40) | 0x801AA761 (0x40) |
| `osfmk_ipc_ipc_kmsg.o` `.rodata.str1.1` | 0x801AA789 (0xA5) | 0x801AA7A1 (**0x91**) |
| `osfmk_ipc_ipc_kmsg.o` `.rodata` | 0x801AA830 (0x14) | 0x801AA834 (0x14) |
| `MSM8974PlatformExpert.o` `.rodata` | 0x801AA844 | 0x801AA848 |
| `xnu_arm_entry_macho.o` `.rodata.macho` | 0x801AAC9C | 0x801AACA0 |
| `xnu_arm_entry_realstubs.o` `.rodata.str1.4` | 0x801AADE8 (0x366E) | 0x801AADEC (**0x366E**) |
| `pexpert_arm_pe_init.o` `__TEXT,__const` | 0x801AE458 | 0x801AE45C |
| `bsd_kern_kern_memorystatus.o` `__TEXT, initcode` | 0x801AE460 (pad 0x4) | 0x801AE460 (**pad 0x0**) |

The addresses between `bsd_kern_sys_reason.o` and the tail are **0x18 low and then 0x4 low**, and the
two terms are separable: 0x14 of it is the wrong chunk attribution (the prediction subtracted the 0x14
from `osfmk_kern_locks.o`, which the map now shows untouched), and 0x4 of it is the mergeable rows'
advances — a cursor walk over rows whose printed addresses overlap cannot be exactly right, and the one
pair this step has to look at is `osfmk_kern_kpc_common.o` (0x4) and `bsd_kern_kern_ktrace.o` (0xD7)
printing the *same* address, where a printed size is not an advance.

**And both halves were absorbed, so neither reached the section.** `initcode`'s pad went 0x4 → 0x0 and
put `initcode` at the predicted **0x801AE460** anyway, leaving the content end at **0x801AEE10** and
the text size at **0x1AEE20**. That is the third step running where the section's end comes out exact
while a row inside it does not, and here it is not a coincidence worth leaving unremarked: the
mergeable run is *before* the pad, and the pad takes whatever the run leaves over.

## What the build measured

```
== pass 1: which symbols do XNU's own objects need? ==
  744 symbol(s) undefined
  stubs: 647 function(s), 97 storage
entry base   0x80000000
entry point  0x80000074
text size    1764896 bytes (.text)
image bytes  1877400
bss          0x801ca5c0 .. 0x80203618 (233560 bytes, zeroed by the payload)
layout       args +2117632, topOfKernelData +4194304, tree +6291456, window 8388608
headroom     2083304 bytes below topOfKernelData
```

`xnu_arm_entry_undef.txt` reads 744 lines; `xnu_arm_entry_stubnames.txt` reads 647 `func` and 97 `data`
records — the predicted **744 / 647 / 97**, exact. The map's rows agree with the predicted table
everywhere except the eight lines above, and the lower chain — `.data` 0x801B0000 (0x1A3B0),
`__DATA,__data` 0x801CA380 (0x30) for the new object, `.sysctl_set` 0x801CA3B0 (0x158), `.init_array`
0x801CA508 (0x90), `.bss` 0x801CA5C0, the object's `.bss` 0x80200FBC (0x8),
`MSM8974PlatformExpert.o` 0x80200FC4, `xnu_arm_entry_macho.o` 0x80200FDC (0x0), `realstubs.o`
0x80201000 (0x2604), the fill 0x2C → **0x24**, content end 0x80203604, `__bss_end` **0x80203618**,
printed `.bss` size **0x39058**, content span **0x39044** — is exact, each term as written. Three of
those are rules rather than numbers: the printed `.bss` size falls by 0x40 while the content span falls
by 0x40 too (the object's +0x8, `realstubs.o`'s −0x40 and the fill's 0x2C → 0x24 sum to −0x40); the
pad moves by exactly the inserted 0x8, which is 379's pad rule read from its other side; and `args`
stays the page the bucket gives, because `align_up(0x203618, 0x1000)` and `align_up(0x203658, 0x1000)`
are the same 0x204000.

## The stop: the prediction that retired itself, and the caller the log could not name

379's block predicted that with `ipc_kmsg_dequeue` real the next stub the walk could reach would be
`ipc_kmsg_delayed_destroy` at key `0x800dad44` if the port's message queue were non-empty. That
prediction was **empty**: `ipc_kmsg_delayed_destroy` is one of the twelve records *this* object
retires, so the branch it named is removed by the step that names it. Disassembled,
`ipc_mqueue_destroy_locked` is 0xa0 bytes, `0x800dacdc`..`0x800dad7c`, with exactly three stub calls,
all three inside the loop and all three retired here:

```
800dad2c  caller 800dad30  ->  ipc_kmsg_dequeue
800dad40  caller 800dad44  ->  ipc_kmsg_delayed_destroy
800dad50  caller 800dad54  ->  ipc_kmsg_dequeue
```

and everything after the loop is real — `strh r7, [r4, #44]`, `waitq_invalidate_locked` (0x800a8d14, a
four-instruction leaf that ends `bx lr`), `waitq_clear_prepost_locked` (0x800a9274), `pop {r4, r5, r6,
r7, fp, pc}`. So the frame cannot stop in *either* queue state, and the run confirms it: the walk
returns.

What it then reaches is decided by which of the frame's two callers it returns into, and 379's record
said that was unreadable because the frame had been entered through `thread_block`. The disassembly
says otherwise for both:

* **`ipc_port_clear_receiver + 0x60`** (`bl` 0x800d7c78): `imq_unlock`, `return reap_messages`. Its
  only stub call is `ipc_pset_remove_from_all` at `+0x24` (0x800d7c3c), which *precedes* the call the
  run is in and is guarded by `ip_in_pset != 0`, so no stop is left in it, and its own caller is not in
  the image. No nameable stop.
* **`ipc_port_destroy + 0x1C4`** (`bl` 0x800d81e4, the `pdrequest == IP_NULL` arm — which this run
  took, since the `ipc_notify_port_destroyed` stub at 0x800d8134 precedes the call and was not hit):
  the walk continues at 0x800d81e8 through `waitq_unlock`, the `io_bits & 0x8000` test and
  `lck_spin_unlock` to the common tail at 0x800d828c, where the stub calls left in address order are
  `ipc_kmsg_free` (`0x800d828C`) and `ipc_kmsg_reap_delayed` (`0x800d82CC`) — both retired by this same
  link — and then `ipc_notify_send_once` (**`0x800d82C0`**, guarded by `ip_nsrequest != IP_NULL`),
  `ipc_notify_dead_name` (**`0x800d8330`**, in the `ip_dead_names` scan) and `io_free`
  (**`0x800d8380`**, guarded by the last reference).

**The run reports `stub_hit=io_free` at `xnu_entry_stub_caller_v=0x800d8380`** — the `bl` at
0x800d837c, `ipc_port_destroy + 0x35C`. So it is prediction **(c)**, on branch (b) of the caller
question, and the whole continuation ran as written: `ipc_kmsg_free` and `ipc_kmsg_reap_delayed` are
real because this link retired them, `ipc_notify_send_once` was skipped (`ip_nsrequest` is NULL for
this port), `ipc_mqueue_deinit` is real, the `ip_dead_names` scan found nothing, `ipc_kobject_destroy`
is real, and `io_free` at the `io_references == 1` test is what this image still lacks. The stub's
caller words spell the key back: `stub_caller_w0=0x64303038` = `"800d"`, `w1=0x30383338` = `"8380"`.

This is the first time the walk's next frame has been named from a stop whose caller is *not on the
stack*. 379 recorded that its frontier was one frame wide "by construction" because the frame had been
reached through a context switch, and the correction shows the construction is not that strong: the
return address is missing, but the caller's own *continuation* is in the image, so a stop can be
predicted from the code the thread is about to run even when the stack cannot be read.

## The next object

`io_free` is not IOKit's. It is `osfmk/ipc/ipc_object.c`'s `io_free(otype, object)` — XNU's own object
release — and the pool's only definer of that name is **`osfmk_ipc_ipc_object.o`**, so that is 381:

```
24 definitions, 37 references
resolved (12: 11 function, 1 storage) / 8 added (8 function, 0 storage)
  -> 740 undefined, 644 function, 96 storage
```

The retired storage record is `ipc_object_zones` (`B 8`), so `realstubs.o`'s `.bss` gives up
`align64(8)` = 0x40 — the same term as this step's — and again the record is not the last, so the slot
table is a plain subtraction. Of the eleven retired functions, **five are names this step obliged** —
`ipc_object_copyin_type`, `ipc_object_copyin_from_kernel`, `ipc_object_copyout_dest`,
`ipc_object_destroy`, `ipc_object_destroy_dest`, which is 379's own new stubs coming good one experiment
later, the same shape as 279's — and the other six have been waiting: `io_free`, `ipc_object_alloc`,
`ipc_object_alloc_name`, `ipc_object_copyin`, `ipc_object_copyout`, `ipc_object_translate`. And the
eight it *adds* —
`ipc_entry_alloc`, `ipc_entry_alloc_name`, `ipc_entry_get`, `ipc_entry_modified`, `ipc_right_inuse`,
`ipc_right_lookup_two_write`, `ipc_right_lookup_write`, `ipc_right_rename` — make 381 the first step in
this series where linking an object creates new stubs rather than only retiring them.

**Prediction.** `io_free`'s own body reaches no new stub: of its three statements, `ipc_port_finalize`
(0x800d9668) is real and has no stub call of its own, and `io_lock_destroy` and `zfree` are macros in
`ipc_object.h` and `kern/zalloc.h`. So the frame 380 stopped in *also* cannot stop, the walk returns
into the rest of `ipc_port_destroy` and out through the importance-inheritance epilogue (both real), and
the next stop is wherever the resumed thread goes — which is the one thing this prediction cannot name,
because after two returns the frames above are the scheduler's. What is new is that the step's own
additions are now *in* the image as stubs, so a stop at one of the eight would be the first time a step's
own new names came back as the frontier.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4896768 bytes, sha256
`a492fb87fd6a1ad369025a8f79289747bf32094a75f9b61a12541396bd41d73b`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading
of either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`,
`xnu_entry_abort_entries=0x00000000` with `abort_first_pc=0` and `abort_first_dfar=0`,
`checksum=0x90406605`, `xnu_entry_why=0x80188824` (the instrument's own staging reason, 379's
0x80184C0C plus this step's 0x3C18 shift), no `panic` and no `exception:` line. Log 301619 bytes, 3975
lines, last line `No errors detected`. The device came back to Android on its own.

The step's own code is the message-queue walk of a port teardown: it reads and writes only kernel
memory it was handed, and the only newly linked body on the path is `ipc_kmsg_dequeue` and its
neighbours, which walk a linked list this kernel built. The recovery nets are unchanged — the payload's
`platform_reboot` on the normal path, the software dead-man on a silent boot, and the hardware watchdog
under both.
