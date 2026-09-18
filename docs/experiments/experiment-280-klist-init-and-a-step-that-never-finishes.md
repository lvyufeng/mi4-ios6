# Experiment 280 — `klist_init`, and a step that is built, predicted and never finishes

**Step:** link the object that defines `klist_init` — `bsd/kern/kern_event.c` (`manifest:34`),
`bsd_kern_kern_event.o`, the name 279's run stopped at. The largest step this walk has taken by a
wide margin: **43084 bytes of text, 336 of data, 136 of bss, 1349 of strings, 448 of rodata, 229
definitions and 165 references.**
**Prediction:** `stub_hit=kernel_set_special_port`, with the caller at **`ipc_host_init+0x78`** — the
return address of `bl kernel_set_special_port`, reached after two more functions run for the first
time.
**Result:** **neither half. The run stopped nowhere at all.** The payload's whole ladder completes
and the jump is taken; the entry image then writes not one byte — `stub_hit` count 0, no
`exception:` line, none of the epilogue's own records. Three runs, three silences. **The 279
configuration, rebuilt from the same ledger in the same session and run on the same device through
the same capture, reached its frontier exactly** — so the image is the only variable, and this step
does not advance the walk.

## The step, and the object

`bsd_kern_kern_event.o` is the BSD kqueue/kevent implementation, and `klist_init` is the smallest
thing in it:

```
4bc8: mov r1, #0
sbcc: str r1, [r0]
4bd0: bx lr
```

— `SLIST_INIT(list)`, three instructions, no call, no `lr`. So it completes and returns, and the
step is not about this function at all; it is about the 43 KB that arrive with it.

**10 resolved, 69 added.** The ten retired are `klist_init` and nine of the ten `knote_*` / `kev_*` /
`waitq_set__CALLING_PREPOST_HOOK__` names 279's own run obliged:

```
kdp_workloop_sync_wait_find_owner   kev_post_msg      klist_init
knote                               knote_adjust_sync_qos   knote_attach
knote_detach                        knote_init        knote_vanish
waitq_set__CALLING_PREPOST_HOOK__
```

The 69 added are 68 names the object needs plus one hand-written definition: the generator turns 48
of the 68 into function stubs and 20 into storage stand-ins (which is why the undefined count rises
by 58, not 68 — ten names went the other way). The 69th is the one case this project's storage
generator was built to fail on rather than paper over.

## The one size decision the step needed

`bpfread_filtops` has **no definition anywhere in the object pool**. `bsd/net/bpf.c` is
`optional bpfilter` in `bsd/conf/files:192`, a flag the device table this project builds from does
not select, so the file is not in the manifest for this *configuration* and not one of the pool's
~700 objects defines the name. That is also the first concrete instance of a deferred question this
walk owes an answer to: **~126 `optional` sources are in the manifest's file list and not in the
build**, and each of them is a name like this one waiting to appear.

The definition is in `entry_stubs.c`, with the size argued two ways: fifteen sibling filter tables in
the pool all measure **0x28**, and `struct filterops` (`bsd/sys/event.h:939-951`) is two `bool`s and
nine pointers = **40** on armv7. Nothing on this path dereferences it — it is one element of a table
only `kern_event_init` indexes, and this walk stops far short of that.

## The prediction, walked by hand before the build

```
ipc_host_init+0x2c   bl ipc_port_alloc_special      ; entered in 278, stopped in this one
ipc_port_alloc_special+0x90  bl ipc_mqueue_init     ; entered in 279
ipc_mqueue_init+0x58 b  klist_init                  ; a tail call - the step's own name
klist_init           mov/str/bx lr -> returns to `ipc_port_alloc_special+0x94`
ipc_port_alloc_special+0x94  mov r0, r4 / pop {r4, r5, fp, pc}  -> ipc_host_init+0x30
ipc_host_init+0x30   mov r4, r0 / cmp r0, #0 / bne +0x48        ; r0 is the port, non-null
ipc_host_init+0x5c   bl ipc_kobject_set             ; REAL - ipc_kobject.o, linked in 266
ipc_host_init+0x64   bl ipc_port_make_send          ; REAL - retired by 278, never yet run
ipc_host_init+0x74   bl kernel_set_special_port     ; A STUB  <- the stop
ipc_host_init+0x78   mov r2, r0                     ; the return address
```

Every link in that chain is verifiable in the linked image and every one of them checks out:

```
800b7550 <ipc_host_init>            0x230 bytes
800b75c4: bl 800dd29c <kernel_set_special_port>     ; +0x74, return address +0x78
800dd29c  kernel_set_special_port   0x18 bytes      ; a healthy generated stub
800c0788  klist_init                0x0c bytes      ; real: mov/str/bx lr
800ba680  ipc_mqueue_init           0x5c bytes      ; 0x58 is its last instruction
```

`ipc_kobject_set`'s body calls only `lck_spin_lock` and `lck_spin_unlock` (its four `b` targets —
`mk_timer_port_destroy`, `mach_destroy_memory_entry`, `host_notify_port_destroy` — are a *destroy*
switch this call does not take), and `ipc_port_make_send`'s calls only `lck_spin_lock`,
`OSCompareAndSwap` and `lck_spin_unlock`. All five are real, so the first stub the run can reach is
`kernel_set_special_port`. The caller key is `ipc_host_init+0x78` — an address **inside** the function
the `bl` is in, the same shape 276/277/278 had, and the third different value this walk has reported
from `ipc_host_init`: 277 measured `+0x30`, and this is `+0x78`, because two whole functions ran in
between.

## The build

| | 279 | 280 |
| --- | --- | --- |
| undefined | 863 | **921** |
| function stubs | 789 | **827** |
| storage stand-ins | 74 | **94** |
| `.text` | 968100 | **1013620** (+46528 = 0xB5C0) |
| image bytes | 1082488 | **1115688** (one 16 KB alignment step) |
| `.bss` | `0x80107a00` .. `0x8013d548` | `0x8010fb50` .. `0x80145c88` |
| headroom | 1845944 | **1811320** |
| `args` | `0x8013f000` | **`0x80147000`** |

`topOfKernelData` is unmoved at `0x80300000` and the image is still nowhere near it.

## What the run did

**Nothing.** The log's last line is `stage90 xnu_entry: jumping to XNU's _start`, and after it there
is no further output of any kind.

The payload half of the log is **structurally identical to the 279 control's**, line for line, with
hexadecimal addresses masked — 3918 lines with zero differences after normalisation, and the only
raw differences are the ones the payload's own 0x8000 of growth produces. So the payload's layout
arithmetic, the device tree, the `boot_args`, the BSS zeroing, the cache clean, the I-cache
invalidate and the jump all happened, and `xnu_entry_args_pa=0x80147000` sits one page above
`bss_end=0x80145c88` exactly as the invariant requires.

The entry image's half is empty. In 279's run the same section of the log carries the epilogue's own
accounts — `xnu_entry_csselr_before=0x00000002`, `xnu_entry_ccsidr_before=0xf0ffe03b`,
`xnu_entry_ccsidr_l1=0xa007e01a`, a 661-word `entry_kv` dump, and then `stub_hit=klist_init`. In 280's
there is none of it: `stub_hit` count **0**, no `exception: <vector>` line (236-242 established that a
fault reports its vector, its DFAR/DFSR and the faulting instruction), and zero abort entries.

**Three runs, three silences.** 280 plain, 280 repeated, and 280 with the checkpoint instrument
below.

## The positive control that makes that a measurement

The 279 configuration was rebuilt from this same ledger in this session (with `BSD_KERN_KERN_EVENT_OBJ`
stripped from the link list and nothing else changed) and run on the same device through the same
`run_and_capture.sh`. It reached

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=klist_init
 xnu_entry_stub_caller_v=0x800ba300
 xnu_entry_kv_written=0x5b      (91 = 21 + 34 + 36)
 xnu_entry_kv_in_dram=0x7f      (127 = 91 + 36)
 xnu_entry_kv_dropped=0x0
 xnu_entry_abort_entries=0x0
```

— an exact match to 279's recorded measurement, in a build made minutes earlier. The device, the
payload, the boot flow, the capture harness and the entry image's report path are therefore all
healthy at the moment 280 fails. **One object's worth of difference is the whole of it.**

## The instrument: a terminal checkpoint at one symbol

`entry_checkpoint.c` is new, plus `STAGE90_ENTRY_CHECKPOINT=<symbol>` in `build_entry.sh`. The link
adds `--wrap=<symbol>` and compiles a 24-byte wrapper

```
__wrap_klist_init:  movw r0, #<name>   push {lr}   mov r1, lr
                    movt r0, #0x800f   pop {lr}    b entry_stub_hit
```

so that a *named* function becomes a terminal stop of exactly the shape a missing symbol produces:
the run reports at that symbol and stops there, without needing the symbol to be missing.
`--wrap` redirects *references* and leaves the definition alone, so the image still contains the
object's real code and nothing about the step changes except which function reports it. Unset — the
default, and every stage image — the translation unit is not compiled and the link is untouched.

**Its result was negative.** `STAGE90_ENTRY_CHECKPOINT=klist_init` on the 280 configuration:

- the build reported image bytes 1115688 and `.bss` `0x8010fb50` .. `0x80145c88` — the same image,
  with `__wrap_klist_init` at `0x800e06d4` and `klist_init` still real at `0x800c0788`;
- the host side verified the wrap took effect: `ipc_mqueue_init+0x58` re-pointed at
  `__wrap_klist_init`;
- the run was **silent**, in exactly the way the plain run was.

A checkpoint at `klist_init` requires no new code to run: it stops at the point 279's run *did*
report from. So in this image either the run does not reach that point, or the reporting path that
reported it in 279's image does not report in this one — and the instrument was built to say which,
so its reading is the second one: **the frontier is not where the problem is.**

## Host-side rule-outs

Completed before and after that run, and none of them found anything:

- **The early code is the 279 image's.** Disassembly of `[0x80000000, 0x800bbbc0)` — `_start`,
  `L_arm_init`, `arm_init`, `arm_vm_init` and the entire IPC allocation path — differs in 1761 raw
  halfwords, every one of them an address that legitimately moved once `movw`/`movt` pairs are
  resolved as single addresses or a literal pool is recognised as shifted. **No instruction
  differs.**
- **Every callee is real.** `lck_spin_lock` is `b hw_lock_lock`, whose spin is the same
  `ldrex`/`strex` shape as `lck_mtx_lock_spin_always` — which `zalloc` executed successfully in 278,
  and which proves the `mrc 15,0,rX,cr13,cr0,{4}` per-CPU read and the `[TPIDRPRW+0x5c0]`
  preemption-counter increment both work. `OSCompareAndSwap` is a bounded `ldrex`/`strex` with no
  unbounded loop.
- **All 94 storage stand-ins match a pool definition's size exactly**, `bpfread_filtops` is the only
  hand-sized one, and nothing on this path dereferences it.
- **The report path has no size-dependent gate.** `entry_probe_dump_kv_words` is not on the
  `entry_stub_hit` path; `g_hex`, the table `entry_kv` uses for caller digits, is outside `.bss`;
  and `entry_epilogue`'s address ranges are all `&g_*`-derived.

## The layout, measured rather than argued

A per-symbol address delta between the two images (`nm` on both, 5685 names in common) accounts for
the whole difference and leaves nothing unexplained:

| 279 address range | delta | what it is |
| --- | --- | --- |
| `0x80000000` .. `0x800bafff` | **0** | 2726 symbols at the same address — `_start`, `arm_init`, `arm_vm_init`, the whole IPC path |
| `0x800bb000` .. `0x800e?fff` | `+0xabf8` / `+0xabe0` | the regenerated stub object; 68 names inserted, 10 removed |
| `.data` `0x800f0000` → `0x800f8000` | `+0x8000` | the 16 KB snap; the 0x3A54 bytes of padding below it in 279 are 0x484 in 280 |
| `.data + 0x8000` | `intstack` / `fiqstack`, 16 KB each | `intstack_top` is at `.data + 0x8000` in **both** images, so the boot stack is the same size at the same relative address |
| `__DATA,__const`, `__DATA,__data` | `+0x8150` | the 336 bytes of `kern_event.o` `.data` inserted after `intstack_top` |
| `.bss` | `+0x81c0` | the 20 new stand-ins |

`klist_init`'s *stub* at `0x800d298c` is simply gone, superseded by the real definition at
`0x800c0788` — which is why the stub region shifts by less than `.text` grows.

The only negative deltas in the whole table are `__entry_data_size` and `__entry_data_filesize`:
they are **absolute linker symbols** — values, not addresses — and are not addresses at all.

## A correction owed to 279

279's committed layout paragraph says the 279-vs-280 diff "shows only 101 symbols moved".
Re-measured, **2959** of the 5685 common symbols moved; 101 is the count of some subset, not of the
moved set. The *code*-identity conclusion 279 draws from it survives, because that conclusion rests
on the disassembly comparison and not on the count — but the sentence as written is wrong, and
279's `Reproduce` note is not a sound argument for it.

## Safety

Three runs, `fastboot boot` only, **nothing flashed**. `persistent_write_attempted=0x00000000` in all
25 places and `failure_mask=0x00000000` in all 87 contracts on the plain run whose log was read, and
zero aborts. The device returned to Android on its own: the recovered log ends with the
`No errors detected` line that the *running* Android kernel appends after it has read the previous
boot's log, and the 279 control run that followed ran on the live device minutes later.
`sys.boot_completed=1` appears 0 times in the 280 log, so the capture window closed before Android
finished booting; the trailing line is what says it came up. One honesty gap: the checkpoint run's
own log was overwritten by the 279 control that followed it, so its safety counters were not read
separately.

## What is next

**Not another object.** The walk stands one object short of the frontier it reached in 279, and
adding the next one would produce the same silence. Two candidate causes remain, and neither is
settled:

1. **The run never reaches `klist_init`**, in which case the failure is in code that is
   instruction-identical to 279's and the difference has to be dynamic — and the deepest entry
   window's own record is the one place this project has already measured a *dynamic* defect in
   exactly this code: 269-271's `g_kv_buf` digits came out corrupted **while the I-cache was on and
   XNU's page tables were live** and came out right from the epilogue with both off, and the ledger's
   own note on that is that instruction fetches from the entry window are unpredictable because this
   configuration maps it `SO_ONLY`. A *hang* is the same defect with a different symptom, and it is
   not claimed here.
2. **The reporting path failed in this image**, which is what the checkpoint instrument's own design
   says its silence means.

The next move is to separate those two before anything else is linked: a checkpoint at a symbol
**`_start` calls within its first dozen instructions** — `cpu_data_init` (`0x80003cf8`, called from
`arm_init` at `0x80002fc4`) — in **both** configurations. If it reports in 280 as well as in 279, the
report path is alive in the 280 image and the run is simply not reaching `klist_init`, and the
interval between `arm_init` and `ipc_mqueue_init`'s tail can be bisected the same way, one symbol at
a time. If it is silent in both, the probe is at fault rather than the image. If it reports in 279
and is silent in 280, the report path itself is broken by the layout, and the next question is why.

## Reproduce

```bash
# entry image: the comment block above BSD_KERN_KERN_EVENT_OBJ records the step, its prediction,
# and what the run measured
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)      # log -> /tmp/cancro-last_kmsg.txt

# the checkpoint instrument, at one named symbol
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_CHECKPOINT=klist_init ./build_entry.sh)

# the 279 configuration, as the positive control: a copy of this script with the
# `BSD_KERN_KERN_EVENT_OBJ` entry removed from LINK_OBJS and from the `require` list,
# and nothing else changed
```

The line that carries a *result* is ` stub_hit=<name>`; a run whose log ends at
`stage90 xnu_entry: jumping to XNU's _start` measured nothing, and that is what 280's three runs are.
