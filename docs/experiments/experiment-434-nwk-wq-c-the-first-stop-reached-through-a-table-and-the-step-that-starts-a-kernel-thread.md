# Experiment 434 — `bsd/net/nwk_wq.c`: the object that retires `nwk_wq_init`, the first stop the walk reaches *through* a table the image supplies, and the step whose own object starts a kernel thread

**Step:** one object appended to `LINK_OBJS`, between `IOKIT_BSDDEV_IOKITBSDINIT_OBJ` and
`STAGE90_PTHREAD_FUNCTIONS_OBJ` — `bsd_net_nwk_wq.o` (manifest: `bsd/net/nwk_wq.c`), the pool's only
definer of 433's stop.

**Effect:** **1 resolved (1 function, 0 storage) / 0 added** — `nwk_wq_init` out, nothing in — for
**768 → 767** undefined, **616 → 615** function, **152 → 152** storage, exactly as
`tools/entry_object_effect.py` predicted:

    nwk_wq_init            object T, stand-in was func T

**All 13 of the object's references are already satisfied, and all 13 are *real*.** That second half
is not what the tool answers — a generated stub satisfies a reference too — so each name was checked
against the linked image:

    80011584 lck_grp_attr_alloc_init   80013d44 lck_mtx_init     801afc50 msleep0
    80011608 lck_grp_alloc_init        80013f34 lck_mtx_lock     801aff08 wakeup
    800119b4 lck_attr_alloc_init       80014430 lck_mtx_unlock   801d0b90 assfail
    8000b384 kernel_thread_start       8002deb4 panic            8017f9f0 _FREE
    800097b4 thread_deallocate

Thirteen `T`s in the image's real, low region; not one in the stub run at `0x8020xxxx`. **This is the
first step in the walk where the frontier is reached *through* a table the image itself supplies** —
433's `pthread_init` body returned, `bsd_init` walked on, and the stop is once again an ordinary
missing symbol, which is what kind 19's write-up predicts a kind-19 stop hands back to.

**The object's other 18 definitions change no count, and that is a reading rather than an omission.**
Six `.bss` variables (`nwk_wq_head` 8, `nwk_wq_lock` 8, `nwk_wq_lock_group` 4,
`nwk_wq_lock_grp_attributes` 4, `nwk_wq_lock_attributes` 4, `nwk_wq_waitch` 4 = 32 bytes), nine
`.rodata.str1.1` strings, and three more functions (`nwk_wq_thread_func` 0x70, `nwk_wq_enqueue` 0x54,
`nwk_wq_thread_cont` 0x150). Nothing in the image referenced any of them, so they had never become
stand-ins, and the effect tool counts *references* — "0 added" says nothing about the 18 names an
object brings with it.

## The body, and why the walk may predict through it

`nwk_wq_init` initialises a doubly-linked list head (`str r1, [r0]; str r0, [r0, #4]` on the object's
own `nwk_wq_head`), allocates a lock group, a lock attribute and a mutex out of the four real `lck_*`
calls, and then:

    bl <kernel_thread_start>       r0 = nwk_wq_thread_func, r1 = 0, r2 = &thread
    cmp r0, #0 ; beq  a0
    bl <panic>                     .L.str.1 with .L__func__.nwk_wq_init
  a0: bl <thread_deallocate>
    pop {r4, pc}                   <- returns

**It starts a kernel thread.** The thread it starts runs `nwk_wq_thread_func`, which takes
`nwk_wq_lock` and then calls `msleep0(nwk_wq_waitch, nwk_wq_lock, 21, "nwk_wq", 0, nwk_wq_thread_cont)`
— **a zero timeout, i.e. no deadline**. It sleeps until a `wakeup(nwk_wq_waitch)` that only
`nwk_wq_enqueue` performs, and nothing in this boot calls `nwk_wq_enqueue`. So the thread blocks and
stays blocked, which is why this step does **not** need the timer: `ml_init_timebase` is still owed,
and a `msleep0` with no deadline takes no deadline. `bsd_init`'s thread continues.

`kernel_thread_start` is the only name on this path the walk has never been through
(`osfmk_kern_thread.o`, real at `0x8000b384`), and
`tools/xnu_entry_callwalk.py --root kernel_thread_start` answers "no stub on the straight-line path".
Its guarded sites — `thread_create_internal+0x38 -> zalloc`, `+0x84 -> uthread_alloc` — are branches,
and `thread_create_internal` is the machinery step 300 already went through when the walk first left
the return-to-caller idiom at `machine_load_context`.

## The prediction, and the measurement

**Prediction, written before the build: `stub_hit=dlil_init`, caller key `0x8003B200`** — the return
address of the `bl <dlil_init>` at `bsd_init + 0x80C`, the next call in the statement list after
`bl <nwk_wq_init>` at `+0x808`. `dlil_init` is defined by `bsd/net/dlil.c` (`bsd_net_dlil.o`, `T dlil_init`
at `+0x1BEC` in the object), **which is not linked**, so the name is still a stand-in.

**Measured on hardware: exactly that, to the byte.**

    line 3912:  xnu_entry_image_bytes=0x002542b0
    line 3914:  xnu_entry_bss_end=0x80296218
    line 3933:  xnu_entry_kv_written=0x0000008e
    line 3935:  xnu_entry_kv_dropped=0x00000000
    line 3942:  xnu_entry_abort_entries=0x00000000
    line 3973:  xnu_entry_stage90_pthread_functions_ptr=0x802315cc
    line 3974:  stub_hit=dlil_init
    line 3975:  xnu_entry_stub_caller=0x8003b200
    line 3979:  No errors detected

`0x802315CC` is the table's *new* linked address, so 433's body ran, compared and recorded — no
`not_registered`, no `panic`, no `exception:`. `_w0`/`_w1` spell the key back as ASCII (`"8003"`,
`"b200"`), the third path to the same number.

**No stop occurred inside `nwk_wq_init` or inside `kernel_thread_start`**, which is the measurement
that makes the thirteen-name check worth having done: the object's whole body ran, the thread was
created, `thread_deallocate` returned, and `nwk_wq_init` returned to `bsd_init`.

## The reading: one row of the layout prediction is wrong, and the miss is the interesting part

**`.text` grew by 0x3A0** — 0x236000 → **0x2363A0** — against a predicted range of 0x2A0..0x396. The
prediction's *top* was the sum of the named terms (+0x2C4 object `.text`, +0xF6 object strings,
−0x18 the retired stub body, −0xC its name slot = 0x396), and the measured value is that plus a
**0xA fill term**. The range was written rather than a number precisely because the fill term is the
one quantity this walk has never derived in advance; it has now run 0x12, 0x55, 0x11, 7, 2 and 0xA
across six steps. `.text` ends at 0x802363A0, **0x1C60** below the `.data` boundary at `0x80238000`,
so `.data`, `.sysctl_set`, `.init_array` and the image size all stayed put — the third step running.

**And `.bss` did not stay unmoved, which the prediction said it would.** Measured 0x41F18 → **0x41F58**
(+0x40), with `__bss_start` unmoved at 0x802542C0 and `__bss_end` 0x802961D8 → **0x80296218**. The
object's own 32 bytes landed at **0x80292C60** — exactly where `stage90_pthread_callbacks` had been,
which moved to 0x80292C80 — so the *placed* growth is 0x20 and the section's aligned end moved a
further 0x20.

**This is the first direct measurement of "an object's `.bss` costs `align64`" for a real object.** The
ledger already had the rule from 331 and 354 for *stand-ins* ("a storage stand-in costs
`align64(the symbol's own size)`"), and the prediction treated `.bss` as "nothing here allocates
storage" — true of 432 and 433, both of which brought no writable data, and false here because
`nwk_wq.c` brings six variables of its own. The tell is worth carrying: **a run of steps in which a
row does not move is not evidence that the row cannot move, and the row's silence has to be
re-derived from the object each time rather than carried forward.**

**The table moved +0x3A4** (0x80231228 → **0x802315CC**) against a `.text` growth of +0x3A0. The extra
four bytes are the alignment of the orphan `.rodata` region the table sits in — the two numbers are two
different rows, not a disagreement.

**`xnu_entry_kv_written` fell, 0x90 → 0x8E, and that is not a lost record.** It is `g_kv_len`, the
number of *bytes* in the kv buffer rather than a record count (`entry_stubs.c:749`), and the report's
record set is identical between the two runs — 3794 `key=0x…` occurrences, the same keys with the same
counts. The two bytes are the stub name `entry_stub_hit` writes into the buffer: `nwk_wq_init` is 11
characters, `dlil_init` is 9, and **11 − 9 = 2 = 0x90 − 0x8E**. `xnu_entry_checksum` moved the other
way, `0x9040E1ED` → **`0x9040E22D`**, because `bss_end` is one of the words it XORs and `.bss` is the
one marker that changed — which makes this pair a clean illustration of both readings: a checksum that
covers only unchanged quantities stays put (433), and one that covers a quantity which moved, moves.

## Layout

    entry text   0x2363A0 (2319264)     <- was 0x236000: +0x3A0 (0x396 of terms + 0xA fill)
    entry image  0x2542B0 (2441904)     <- unmoved, third step running
    .data        0x80238000 (0x1C018)   <- unmoved; 0x1C60 of slack below it
    .sysctl_set  0x80254018 (0x204)     <- unmoved
    .init_array  0x8025421C (0x94)      <- unmoved
    bss          0x802542C0 .. 0x80296218 (270168)   <- start unmoved, end +0x40
    layout       args 0x80298000, topOfKernelData 0x80400000, tree 0x80600000, window 8388608
    headroom     1482216 bytes below topOfKernelData  (was 1482280: -0x40, the .bss move)
    payload      out/stage90/stage90-qcdt.img, sha256
                 b0c6e842a593ec6b3cdaf3786c3423e66da902c612d4e6e71951c9dcd71806d3 (5462016 bytes)
    entry bin    2441904 bytes; sha256
                 5de342c75232cff11ecad285e22299e9e269541f08646da9f73fd3f158776596

The object's `.text` landed on **0x80203D48** — where 432's/433's object had been, and where
`IOKitBSDInit.o` ends: the eighth consecutive landing on the contiguous `.text` chain of
`entry.ld:45`, and the first time the object placed there is Apple's again. `nwk_wq_init` is at
`0x80203D48`, `nwk_wq_enqueue` at `0x80203E68`, `nwk_wq_thread_func` at `0x80203DF8`,
`nwk_wq_thread_cont` at `0x80203EBC`.

Both stages rebuild byte-identically from the committed sources, after the prediction block was
finished and amended.

## Safety

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); nothing flashed, nothing written to storage.
25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, `abort_entries=0`,
`checks=5` / `failures=0`, no `exception:` and no `panic:` line, `kv_dropped=0`. The only net armed
across the jump was the hardware watchdog (`hw_watchdog_counter_running=0x00000001`,
`hw_watchdog_bite_truncated=0x00000000`), with the dead-man PPI disarmed before it
(`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). The two `data abort` lines are the payload's own
probes, at lines 201 and 3443, byte for byte the same as 433's. Device returned to Android on its own
and was confirmed there (`ro.product.device` cancro, `ro.build.version.release` 10). 301858 bytes /
3979 lines, ending `No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run433_kmsg.txt`, `run434_kmsg.txt`.

## Where the frontier is now, and what 435 is

**`dlil_init`** — `bsd/net/dlil.c` (`bsd_net_dlil.o`, `dlil_init` at `+0x1BEC`, `bsd_init + 0x80C`,
key `0x8003B200`), the stub the run just stopped at, and the start of the network layer's
initialisation: `dlil_init` is where `dlil`'s thread and the protocol-input thread are started, so 435
is shaped like 434 in the way that matters — the object it links may itself start threads and its
references have to be checked name by name for the same reason.

Behind it in `bsd_init`'s statement list are `proto_kpi_init` (`+0x810`, key `0x8003B204`),
`socketinit` (`+0x814`, `0x8003B208`), `domaininit` (`+0x818`, `0x8003B20C`) and `iptap_init` (`+0x81C`,
`0x8003B210`), all still stand-ins; the mount machinery (`IOFindBSDRoot`, `vfs_mountroot`,
`IOSecureBSDRoot`) is further along still.

**Still owed and unchanged: the timer.** `ml_init_timebase` with an MSM8974 `tbd_ops_t` over the GPT at
`0xf9020000` (19.2 MHz, IRQ 19), plus 405's `IOCPUInterruptController`. This step did not move it —
the thread `nwk_wq_init` starts sleeps with no deadline — but the objects that follow are where a
deadline first appears.
