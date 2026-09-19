# Experiment 381 — `osfmk/ipc/ipc_object.c`: the object that makes `io_free` real, an exactly-predicted layout, and a run that was silent

**Step:** link one object, `osfmk/ipc/ipc_object.c` (`osfmk_ipc_ipc_object.o`) — the pool's only
definer of 380's stop `io_free`. Inserted into the entry link between `osfmk_ipc_ipc_kmsg.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *12 resolved (11 function, 1 storage) / 8 added (8 function) — 744 → **740**
undefined, 647 → **644** function, 97 → **96** storage*; `realstubs.o` `.text` **0x3C60**, its
`.rodata.str1.4` **0x3622**, its `.bss` **0x25C4**; text **1768512 (0x1AFC40)**; `.data`
**0x801B0000**, `.sysctl_set` **0x801CA3B0 (0x158)**, `.init_array` **0x801CA508 (0x90)**; `.bss`
**0x801CA5C0 .. 0x802035D8** with `realstubs.o`'s slot at **0x80201000 (0x25C4)**; the fill before it
**0x24 → 0x1C**; args **0x80205000**, `topOfKernelData` **0x80400000**, headroom **2083368**, image
**1877400 (0x1CA598)**, `.bss` printed size **0x39018**. And for the next stop: **no stop inside
`ipc_port_destroy`'s own frame** — the walk returns into the task teardown, and the first stub on
that continuation is **`task_affinity_deallocate` at key `0x800c0fb8`** if the dying task has an
`affinity_space`, otherwise **`vm_purgeable_disown` at key `0x800c0fc0`**, the unconditional one.

**Measured:** **all three counts exact, every layout number exact, and the run silent.** The build
reproduced 740 / 644 / 96 and the whole section chain to the byte — text 1768512, image 1877400,
`.bss` 0x801CA5C0 .. 0x802035D8 (printed 0x39018), `realstubs.o`'s `.bss` 0x80201000 (0x25C4), the
fill 0x24 → 0x1C, headroom 2083368 — with one 0x4 residue inside the mergeable `.rodata` run that
the pad before `initcode` absorbed. The device ran the image, went through the payload's whole
ladder, jumped to XNU's `_start` — and **produced nothing at all**: no `stub_hit`, no `exception:`,
no epilogue report. Both stops this step predicted, and every other reporting stop, went unmeasured.
That is the signature experiment 280 measured once before, and the reading is the same: the next
run is a checkpoint bisection, not another prediction.

## The object

`osfmk/ipc/ipc_object.c` is 8140 bytes with **24 definitions** (18 `T`, 1 `B`, 5 `r`), **37
references**, **3564 bytes of `.text`** (0xDEC), `.bss` 0x8, `.rodata.str1.1` 169 bytes (0xA9),
`.rodata` 24 bytes.

Twelve of its names are stubs the image already carried, so linking it **retires** twelve records:
`io_free` (380's stop), `ipc_object_alloc`, `ipc_object_alloc_name`, `ipc_object_copyin`,
`ipc_object_copyin_from_kernel`, `ipc_object_copyin_type`, `ipc_object_copyout`,
`ipc_object_copyout_dest`, `ipc_object_destroy`, `ipc_object_destroy_dest`,
`ipc_object_translate`, and the storage record `ipc_object_zones` (`pool B 8`).

Eight names it **adds**, because its own bodies call them and the link did not have them before:
`ipc_entry_alloc`, `ipc_entry_alloc_name`, `ipc_entry_get`, `ipc_entry_modified`, `ipc_right_inuse`,
`ipc_right_lookup_two_write`, `ipc_right_lookup_write`, `ipc_right_rename` — the entry/rights face
of IPC that `ipc_object_copyin`/`ipc_object_copyout` are built on. **This is the first step in the
walk where linking one object adds stubs as well as retiring them**, which is why the object-op cost
of the step is 12 + 8 = 20 records touched rather than a pure subtraction.

`ipc_object_zones` is the second storage record to retire in two steps (380 retired
`ipc_kmsg_zone`), and the second change to `realstubs.o`'s `.bss`. Its slot table is a sum of
per-record `align64(size)` terms; the record is **22 of 97**, not the last, so the table gives up
exactly `align64(8)` = 0x40.

## The layout, by the linker's own rule

The three synthetic sizes were each recomputed from their rule against this step's own name list and
each **checked against 380's map first** — the rules reproduce 0x3CA8 / 0x366E / 0x2604 exactly:

    realstubs.o .text           647 x 0x18 = 0x3CA8  ->  644 x 0x18 = **0x3C60**
    realstubs.o .rodata.str1.4  0x366E - 0xF0 (11 names) + 0xA4 (8 names) = **0x3622**
    realstubs.o .bss            0x2604 - align64(8) = **0x25C4**

The middle one is worth a note: it lands exactly on 379's 0x3622, because the eleven names this step
retires are worth 0xF0 of `align4(len+1)` terms and the eight it adds are worth 0xA4, and the last
name in the list (`_ZN9IODTNVRAMC1Ev`) is neither retired nor added, so the unpadded last term is
untouched and the delta is a clean sum.

Everything below is the cursor walk 380 validated (**0 mismatches over all 366 rows** of `.data`,
`.sysctl_set`, `.init_array` and `.bss`) — `address = align_up(cursor, alignment); cursor = address +
size` — and **every number came out exact**:

    text size                    1768512 (0x1AFC40)
    image bytes                  1877400 (0x1CA598)   (unchanged: `.text` growth stays inside the same 0x4000 bucket)
    .data end / .sysctl_set      0x801CA3B0 (0x158)
    .init_array                  0x801CA508 (0x90)
    .bss                         0x801CA5C0 .. 0x802035D8
    .bss printed size            0x39018
    realstubs.o .bss             0x80201000 (0x25C4), fill 0x24 -> 0x1C
    args / topOfKernelData       0x80205000 / 0x80400000
    headroom                     2083368

Three of those are rules rather than numbers, and all three held: the **image** and `.bss` start do
not move (`.text` grows by 0xD60, and 0x801AFC2C rounds up to 0x801B0000 either way); the **fill**
before `realstubs.o`'s `.bss` moves by exactly the 0x8 inserted (`new_pad = (old_pad - inserted) mod
64`); and **headroom grows by 0x40** while the printed `.bss` size falls by 0x40, because `__bss_end`
+ 0x10 + `align64` still lands on the same page for `args`.

The run's own markers agree to the byte: `xnu_entry_bss_bytes=0x00039018`,
`xnu_entry_image_bytes=0x001ca598`, `xnu_entry_args_pa=0x80205000`.

**The one miss is a 0x4 residue inside the mergeable `.rodata` run**, and the two-map diff says what
it is not: between 380's map and this one, **only the three new rows were added** and **only two
pre-existing rows changed size** — `realstubs.o`'s `.text` 0x3CA8 → 0x3C60 and its `.rodata.str1.4`
0x366E → 0x3622, both exactly as predicted — and **no `.rodata.str1.1` row changed size anywhere**.
So it is not a merge. Fifteen comparable rows measured 0x4 low from `bsd_kern_kern_memorystatus.o`'s
`.rodata` through `__TEXT,__const`; the pad before `__TEXT, initcode` then measured **0x8 where 0x0
was predicted**, pushing the three rows after it 0x4 high; and the closing `ALIGN(32)` brought the
section end back to 0x801AFC40 exactly. That is the known `SHF_MERGE` reading band — the map prints
each input's upper-bound size, so a cursor over those rows over-advances by a few bytes — and the pad
is the one term no sum of contents can represent, which is why it absorbs the difference.

## The run: silent

The payload's whole ladder completes. The last two lines it writes are

    stage90 xnu_entry: entering at 0x80000074
    stage90 xnu_entry: jumping to XNU's _start

and nothing comes back. **No `stub_hit=`, no `exception: <vector>`, no epilogue report.** The
checksum `0x904065c5` (380's was `0x90406605`), `xnu_entry_checks=0x00000005`,
`xnu_entry_failures=0x00000000`, `abort_entries=0`, no `panic` line.

Silence has exactly two readings — the walk **hung** before reaching the next stub, or it **reached a
stub whose reporting path failed** — and they are indistinguishable from the log. That is the fourth
ending this walk has, and it is the one the checkpoint instrument exists for (experiment 280).

**The step's prediction is therefore not confirmed.** Neither `task_affinity_deallocate` at key
`0x800c0fb8` nor `vm_purgeable_disown` at key `0x800c0fc0` was reached, and neither was any of the
six keys inside `ipc_port_destroy`'s frame that the falsifier named. What the run does say is that
making `io_free` real was **not enough to produce the next stop** — the same shape 380 was the
counter-example to (there, a retired stub did produce the next stop one frame out).

## Why the next stop was predicted two frames out

`io_free`'s body reaches no stub at all: its only calls are `ipc_port_finalize` (0x800d9668, real and
itself stub-free), `lck_spin_destroy` and `zfree`. `ipc_port_destroy`'s six remaining stub calls are
all guarded, and all six were skipped on the path 379 and 380 measured (`ipc_notify_port_destroyed`
key 0x800d8138, `ipc_pset_remove_from_all` 0x800d81AC, `ipc_kmsg_free` 0x800d828C,
`ipc_notify_send_once` 0x800d82C0, `ipc_kmsg_reap_delayed` 0x800d82CC, `ipc_notify_dead_name`
0x800d8330). So the frame returns, and the question is which caller it returns into. The run's own
state narrows it:

  * the port had `ip_receiver == IP_NULL` (the arm at `ipc_port_destroy + 0x80` that skips
    `ipc_notify_port_destroyed` entirely) — it is a **kernel** port;
  * `ipc_port_release_receive` had **no caller at all** in the image the run ran (its only caller is
    `ipc_object_destroy`, which is this step's own object), so that call site is impossible here;
  * `ipc_task_reset` is unreachable — its only caller is `task_mark_corpse`, which has no in-image
    caller;
  * every caller of `semaphore_dereference` is a syscall, and there is no user space in this boot.

What is left is `ipc_task_terminate` — reached from `task_deallocate`, whose callers include
`thread_deallocate + 0xF4`, the thread-teardown chain 378 already named — where a dying task's two
kernel ports are deallocated by `ipc_port_dealloc_special`, which tail-calls `ipc_port_destroy`. The
continuation is fixed and contains two stubs within four instructions of the return:

    task_deallocate + 0x118  bl ipc_task_terminate
    task_deallocate + 0x120  bl iokit_task_terminate     (real; no stub in its body)
    task_deallocate + 0x13C  bl vm_purgeable_disown      <== STUB, key 0x800c0fc0

with `task_affinity_deallocate` (stub, key 0x800c0fb8) on the guarded arm just before it. The hazard
named in advance: `entry_stub_hit` is terminal, but the code immediately *after* the
`vm_purgeable_disown` call is `ldrd r2, r3, [r4, #920]` / `orrs` / `bne panic`, so a later step that
makes it real has to face the panic at 0x800c0fdc — a diagnosis, not a brick, since both recovery
nets fire.

## The next run is a measurement, not a prediction

The instrument is `STAGE90_ENTRY_CHECKPOINT=<symbol>`: the build adds `--wrap=<symbol>`, and the
wrapper in `entry_checkpoint.c` reports `entry_stub_hit(name, lr)` at the **call site** before the
real body runs (with `_SKIP=<n>` to let the first n calls through and `_AFTER=1` to report the return
value instead). Its rule, stated in its own header, is what makes it a bisection: *if the checkpoint
reports, the image's reporting path works in this build and the boot reached that point — so the
silence of the plain run is about code after it. If the checkpoint is also silent, the reporting path
is what failed, and the frontier is not where the problem is at all.*

The first checkpoint is **`ipc_port_dealloc_special`**: it is the call the frame above
`ipc_port_destroy` is entered through, so a report both **proves the reporting path** in this build
and **names the caller** — by measurement rather than by elimination — which is exactly what the log
above could not do.

## The next object

Nothing is decided until the checkpoint reports. The wider goal is unchanged and still far off: no
basic driver is running and there is no user space; `throttle_init` (index 477) and `bsd_init`
remain unvisited.

## Safety

A non-persistent `fastboot boot` of `out/stage90/stage90-qcdt.img` (4896768 bytes, sha256
`1a5cfc43f44be275893552d35d3bad21f3e58eaef4062130adbf92f35a297afb`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading
of either. `disarm_hw_watchdog_en=0x00000001`, `abort_first_pc=0`, no `panic` and no `exception:`
line. Log 294553 bytes, 3929 lines, last line `No errors detected`.

The device came back to Android **on its own**, and by the net that needs neither the payload nor the
kernel to still be running: the payload writes its report only if the entry returns to it, and its
own software dead-man never dumped (the log carries its arming lines and no dump after them). So the
hardware watchdog is what returned the device — the third ending counted, and the one that makes a
silent boot safe to have.
