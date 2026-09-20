# Experiment 485 — the boot thread finishes, and the tree's nodes are registered

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/entry_trace.c`, `entry_stubs.c`, `build_entry.sh`,
`tools/check_boot_completion.py` (new)

**Result: the kernel's own boot thread runs to the end of its tail.** All five of
`kernel_bootstrap_thread`'s closing calls run, in Apple's order, and the fifth is `vm_pageout` — whose
wrapper records its position *before* the call, at `xnu_live_tail_site = 0x80071ef4` =
`vm_pageout+0x0`. `vm_pageout` never returns, so it cannot be entered unless `bsd_init` returned: **the
kernel's boot completed, in both runs of this step**, and the kernel then goes on running threads —
`xnu_live_block_seq` reaches `0x48`, `xnu_live_kthread_cont` names 30 different continuations, and
`xnu_live_sleep_ent` shows threads sleeping with timeouts. This is the first time this project has read
the boot thread's own position rather than inferring it from what the console printed.

**And the driver layer is a table now, not an inference.** The census walks the device tree from the
root the OS's own walk starts from and finds **21 children, every one named and every one at
`__state[0] = 0x1e`** — `Registered | Matched | FirstPublish | FirstMatch` (`IOService.h:72-76`), with
`__state[1] = 0`: `device-tree`, `iokit-catalog-property-dryrun`, five `stage90-*`, six `msm8974-*`,
`chosen`, `defaults`, `memory`, `cpus`, `arm-io`, `interrupt-controller`, `timer`. The table is
cross-checked from outside itself: the census's `chosen` node is the same pointer
`IORegistryEntry::fromPath("/chosen", gIODTPlane)` answers with, and that call — one of the two the OS
makes in `IOFindBSDRoot`, which 461 measured **returning 0** — now returns a node in both runs.

**The first run read the wrong root, and the instrument could not say so.** 461 recorded the object
`IODeviceTreeAlloc` returned and called it the tree's root; the census walked *that* pointer and read
`xnu_live_dtk_count = 0`, while the same log's 462 walk, a few records later, had the *registry* root's
own IODT child at 21 children. Run B walks the registry's child, publishes both pointers, and reports
`xnu_live_dtk_same = 0x0`: **they are different objects, in both runs, at every moment after
`bsd_init`'s first `fromPath`.** Which one the OS reads is now a measurement (`root`, `set`, `kids`,
`count`) rather than a comment.

The boot is unperturbed and the device came back on its own, so the never-brick half holds. **The minimum
bar is half met, and the half that is met is measured.** "Enter the OS" is now a reading: `bsd_init`
returned, the pageout thread runs, the timer interrupt is serviced (`xnu_live_irq_timer_count = 0x800`),
threads block and wake. "The basic drivers run" is a table of the generic IOKit machinery —
`IOService` nodes registered, published and matched — and **not** of a device driver: the boot's own
last work in this log is `load_init_program`'s string copies (`exec_copyout_strings`, `exec_save_path`,
`load_init_program_at_path` → `mmu_kvtop_wpreflight`), and this image still contains no Apple
`IOPlatformExpert` subclass (461's reconnaissance).

## What 484 left, and why the boot thread's own tail is the question

484 ended with a sentence that has two halves: *"the boot is not stopped by a missing timer, and it is
not stopped by a driver: it is stopped by the program in the RAM disk calling a syscall that returns
immediately."* The second half of that sentence is a claim about the **kernel**, and nothing in this
project had ever read it: every run so far has been read through what the boot printed (458), what
process 1 did (475/480), or what the clock was asked (483/484). `kernel_bootstrap_thread` is the
kernel's own main thread, and its last seven calls are Apple's (`startup.c:628-646`):

    bsd_init();  OSKextRemoveKextBootstrap();  kdebug_free_early_buf();  serial_keyboard_init();
    vm_page_init_local_q();  thread_bind(PROCESSOR_NULL);  vm_pageout();   /*NOTREACHED*/

Five of those are wrapped here. `bsd_init` is not: its completion is audible in the console text the
run already reads. `thread_bind` is not either, and Apple's own body is the argument — it calls
`thread_bind` **twice**, the earlier time to `processor` (`startup.c:452`), so a record of the name would
fire away from the tail and name no position. The remaining five are called from exactly one place in
the whole kernel, which is what makes a record of one of them a *position*: `tail_seen[4] != 0` is
`vm_pageout` entered, and `tail_seen[0..3]` beside it is the control — a run where the wrappers were
never reached looks the same as a run that stopped earlier if only the last counter is published.

## The instrument

**Five wrappers, each writing its position before the call it wraps.** `entry_note_boot_tail(index,
site)` publishes `xnu_live_tail_seq` (how many of the five have run), `xnu_live_tail_idx` (which one,
from Apple's source order read out of `startup.c` by the check), and `xnu_live_tail_site` (the *real*
function's address, so the log names the function rather than a number). The record precedes the call
because the fifth call never returns: a wrapper that recorded on return would record nothing for the one
call this step exists to prove.

**The census runs inside that fifth wrapper**, at the moment downstream of every match attempt and
upstream of the loop that never ends — the settled state of the driver layer rather than a snapshot of
it in flight. It reads the root the way the OS reads it (`getRegistryRoot()`, then that entry's child in
`gIODTPlane` via `getChildEntry`), the children through `getChildSetReference` and `getName(plane)`,
each child's `__state[0]` through `IOService::getState` (the tree's one implementation, which
`build_entry.sh` pins by disassembly and by counting definitions) and `__state[1]` by offset — that
offset being 455's layout reading of `getState`'s own `ldr r0, [r0, #36]` plus the one word
`IOService.h:328` declares. Names are copied out as bytes (`entry_str8`), for 484's reason: a pointer
names nothing in a log read after the tree has moved on.

**Two names for the root, published together** (`recorded` = 461's `IODeviceTreeAlloc` return, `root` =
the registry's child, `same` = whether they agree), **two child counts** (`getChildCount` on the entry
and `OSArray::getCount` on the set that accessor is written in terms of), and a third record for *no
reading at all* (`xnu_live_dtk_none`: 0 = no IODT plane, 1 = the registry root has no child in it).
Every one of those is a live-channel write as well as a report key.

`tools/check_boot_completion.py` (new, 52 mutations refused) makes each of those structural: the
indices are compared against Apple's `startup.c` and the order of the transfers in the linked
`kernel_bootstrap_thread` is compared against the same source; the exclusivity of the five call sites is
read out of the object pool (one defining object, one referencing object, the same object for all five,
and it is the one that defines the thread); `thread_bind` must still be referenced by more than one
object, because that is the argument for leaving it alone; the tail count is one `#define` read by both
the counter array and its guard; the state bits are `IOService.h`'s enum members; and every key this
step publishes has to be written by a writer the report path calls. The last of the image claims counts
a **tail branch** as well as a `bl`: `vm_pageout` is `noreturn`, so `-O2` emits `b __wrap_vm_pageout`
after popping `{r4, lr}` — a check that looked only for `bl` would report that the fifth wrapper is
never reached, which is the one reading this step exists to take.

## The defect the first run found, and the fix

Run A answered the first question and falsified the second instrument's premise. `xnu_live_tail_seq`
reached 5 with `tail_site = 0x80071ef4` in Apple's order — and `xnu_live_dtk_calls = 0x1` with
`xnu_live_dtk_count = 0`. No children. But the same log, from 462's walk, held
`xnu_live_walk_count = 0x1` with `xnu_live_walk_kids = 0x15` (21) — the registry root's IODT child, read
nine records earlier, with 21 children. Two records in one log answering "how many children does the
tree have" differently, and **no way to tell from the log which object each was about**: the census's
root, plane and set pointers were report-only keys, and the report path never ran.

That last part is a reading of its own and the reason the fix is where it is: `xnu_entry_dtk_*` count
**0** in both runs' logs. The payload's report is written from its own program after the kernel returns
control, and in this step the kernel never does — it reaches `vm_pageout` and keeps the CPU. **The run
that answers this step's question is exactly the run in which the report is not written**, and 459's
defect (a report buffer that filled up while the run went on) was the same lesson one step earlier.
The fix is therefore in the live channel: the census publishes its root, plane, set, both counts and its
per-child records there, and the check fails the build if any of those records stops being written.

Run B, rebuilt and re-run on the device, is the measurement below. `xnu_live_dtk_same = 0x0` and 21
children, and 461's recorded root is a different object from the registry's child *in both runs*.

## The measurement

The boot thread's tail, run B (run A is identical in every field):

| `tail_seq` | `tail_idx` | `tail_site` | symbol |
| --- | --- | --- | --- |
| 1 | 0 | `0x8011b9c0` | `OSKextRemoveKextBootstrap+0x0` |
| 2 | 1 | `0x80048c50` | `kdebug_free_early_buf+0x0` |
| 3 | 2 | `0x80041588` | `serial_keyboard_init+0x0` |
| 4 | 3 | `0x80024310` | `vm_page_init_local_q+0x0` |
| 5 | 4 | `0x80071ef4` | **`vm_pageout+0x0`** |

The census, run B — one call, `plane = 0xc05e6690`, `recorded = 0xc061df78`, `root = 0xc05e8030`,
`same = 0`, `set = 0xc0625a80`, `kids = 0x15`, `count = 0x15`, then 21 records (name words decoded,
`__state[0]` in bits):

| seq | name (first eight bytes, decoded) | `__state[0]` |
| --- | --- | --- |
| 0 | `device-tree` | `0x1e` R M P F |
| 1 | `iokit-pl`… = `iokit-platform-scaffold` | `0x1e` R M P F |
| 2 | `msm8974-`… = `msm8974-platform-driver` | `0x1e` R M P F |
| 3 | `msm8974-`… = `msm8974-interrupt-service` | `0x1e` R M P F |
| 4 | `msm8974-`… = `msm8974-timer-service` | `0x1e` R M P F |
| 5 | `msm8974-`… = `msm8974-cpu-service` | `0x1e` R M P F |
| 6 | `msm8974-`… = `msm8974-rejected-driver` | `0x1e` R M P F |
| 7 | `iokit-ca`… = `iokit-catalog-property-dryrun` | `0x1e` R M P F |
| 8–12 | `stage90-`… = `stage90-{platform,interrupt,timer,cpu,rejected}-personality` | `0x1e` R M P F |
| 13 | `chosen` | `0x1e` R M P F |
| 14 | `defaults` | `0x1e` R M P F |
| 15 | `memory` | `0x1e` R M P F |
| 16 | `cpus` | `0x1e` R M P F |
| 17 | `arm-io` | `0x1e` R M P F |
| 18 | `msm8974-`… = `msm8974-io` | `0x1e` R M P F |
| 19 | `interrup`… = `interrupt-controller` | `0x1e` R M P F |
| 20 | `timer` | `0x1e` R M P F |

The eight-byte forms are what the census records, and the names they expand to are the fixture's own
nodes (`stage90_main.c`), in the fixture's own creation order — 21 of them, one for one, which is a
fourth cross-check: the tree the OS was handed is the payload's, complete, and nothing in it is missing
at the end of the boot.

Every `__state[1]` is `0`. Three readings keep this table from being self-confirming:

  - **the two child counts agree** — `getChildCount(plane) = 0x15` and `OSArray::getCount` on the set it
    returns `= 0x15`: 21 children, counted twice by two different accessors;
  - **the table agrees with the OS's own lookup** — census child 13 is `0xc061d8f0`, and
    `fromPath("/chosen", gIODTPlane)` returns `0xc061d8f0`; `fromPath("/chosen/memory-map")` returns
    `0xc061ddc0`, the same pointer 461's own call from inside the `IODeviceTreeAlloc` wrapper got;
  - **the registry's own child moved** — 462's walk inside the `IODeviceTreeAlloc` wrapper (seq 1) has
    `walk_root = 0xc061c858`, `walk_count = 1`, `walk_first = 0xc061df78` (= `dtalloc_ret` = the census's
    `recorded`), and seq 2 through 7 — every `fromPath` call after it — have the same root and count with
    `walk_first = 0xc05e8030` (= the census's `root`). The registry has one IODT child both times and it
    is a **different object** in 485 B than in 461's run.

The OS's own console text is in this run's log and says where the boot is: `Added memory device
md0/rmd0 (02000000/0D000000) at 0000000080505000 for 0000000000002000` — the root device 461's frontier
died on is now *added* — then `load_init_program: attempting to load /usr/local/sbin/launchd.development`
→ `failed loading ...: errno 2` → `attempting to load /sbin/launchd`. The run's abort population is the
same 32 records with a different centre of gravity: the kernel-mode sites are `Lcopyout_wordwise_loop+0x4`,
`Lcopyin_wordwise_loop+0x0`, `L64loop+0x8`, `L_64loop+0x0`, `mmu_kvtop_wpreflight+0xc` (lr
`load_init_program_at_path+0x30`), `exec_copyout_strings+0x128`, `exec_save_path+0x38`, and `copypv+0x84`
— **the copies that build process 1's arguments**, each handled and retried (`sleh_back = 1`,
`at_back = 1`). The two user-mode aborts are the fixture's own thread state (`0x00001118`, `0x00001124`).

## What moved, and what did not

| | 483 C | 484 B | 485 A | 485 B |
| --- | --- | --- | --- | --- |
| `xnu_live_tail_seq` / `tail_idx` | — | — | 5 / 4 | **5 / 4** |
| `xnu_live_tail_site` (5th) | — | — | `0x80071ef4` | **`0x80071ef4`** |
| `xnu_live_dtk_calls` / `count` | — | — | 1 / **0** | 1 / **21** |
| `xnu_live_dtk_same` | — | — | — | **0** |
| `xnu_live_walk_kids` | `0x15` | `0x15` | `0x15` | `0x15` |
| `fromPath("/chosen")` | 0 | 0 | `0xc061d8f0` | **`0xc061d8f0`** |
| `xnu_live_getpid_count` | `0x800000` | `0x800000` | `0x800000` | **`0x800000`** |
| `xnu_live_sleh_seen` | `0x20` | `0x20` | `0x20` | `0x20` |
| `xnu_live_irq_timer_count` | past 2048 | past 2048 | `0x800` | `0x800` |
| OS console text | `0x3ee` | identical | identical + the two `load_init_program` lines | same |

The three rows that did **not** move are the ones that say what this step is not: the fixture's program
still runs its 8 388 608 `getpid` calls, the abort population is still 32, and the console text is still
the same. What moved is entirely the *kernel's* side of the boot: the tail is finished, the tree's nodes
are registered and matched, and the boot's own last work is the string copies for process 1's exec.
Whether the two user-mode aborts at `0x00001118`/`0x00001124` are the same two of 484's (whose kernel
counterparts moved by another `+0x620`, the instrument's own growth) is not settled here.

## Defects this step's own session found

Three, in `mi4-measurement-defects.md` (237–239):

  - **237 — a root named in a comment, read from a pointer, and never compared with the root the OS
    uses.** The census walked `g_dtplane_root` — 461's reading of `IODeviceTreeAlloc`'s return, called
    "the tree's root nub" in the same comment — and reported 0 children of it, while the log's own 462
    walk had the registry root's IODT child at 21. The instrument could not say which object the number
    was about: no set pointer, no root pointer, and the report group that held them was never written.
    **The tell: a measurement whose subject is asserted by prose rather than published as a value.**
    The fix is both halves — read the root the OS reads, and publish both candidates with `same`.
  - **238 — a claim whose precondition was never satisfied, so it never ran.** The check's index claim
    built its expected numbering only if *every* name in `apple["hits"]` was called exactly once, and
    `thread_bind` is called twice, so `expected` was always empty and a wrapper with a swapped index was
    **accepted** by `--selftest`. The mutation is what said so. The guard is now over `WRAPPED` alone.
  - **239 — a mutation that mutated nothing.** `wrapper_index_swapped` replaced `entry_note_boot_tail(0u,`
    and then `entry_note_boot_tail(1u,`, and the second replacement undid the first (the first edit had
    already put a `(1u,` earlier in the file), so the mutation was a no-op and the check was right to
    accept it. Both replacements now carry the call they wrap in the pattern. **The tell: a selftest's
    mutation is a claim about the *effect* of an edit, and `_bump`-style helpers check the edit's
    presence, not its consequence.**

## Build, layout, and artifacts

| | 484 B | 485 A | 485 B |
| --- | --- | --- | --- |
| `.text` | 5240896 | 5246400 | **5246784** |
| image bytes | 5454452 | 5470836 | **5470836** |
| `.bss` | `..0x8058b6c8` (359496) | `..0x8058f888` (359944) | **`..0x8058f8c8`** (360008) |
| headroom | 1526072 | 1509240 | 1509176 |
| undefined / wraps | 26 / 54 | 26 / 59 | **26 / 59** |

The 59 wraps are the 54 of 484 plus this step's five, and the layout adds no undefined name: the check
that reads the object pool and the two it reads in the image are the whole claim. Entry image
(`xnu_arm_entry.bin`) sha256, run B
`392abd16eb1fa15e5deba902b0097be8c9471d9f8a17229536f045d9ad9aacf9` (`.elf`
`cb11c4b4c340ad81a9296457ab204cd02fee6da3d60e7691b0dad6d12405aa44`) — run A's entry image is not
recorded because run B's build wrote over it, which is the one number in this table that is missing
rather than measured. Payload `stage90-qcdt.img` 8 491 008 bytes, run A
`c6e40fb8996b2f564aa40aa4f9d16448b254040ae25837c1a2343f635e20e0c5`, **run B
`ae41f6090a2fa9646c567be9b9612807e500bc50d11b7a23d627e7221ed5deef`**, `kernel_size = 5964890`,
`dt_size = 2521088`. Build lines, in order, all green: `check_undef_handler.py --split` (61 mutations
refused), `check_timebase_registration.py` (12), `check_gic_routing.py` (43), `check_irq_routing.py`
(36), `check_timer_sources.py` (42), `check_boot_completion.py` (**52**). Both runs went through
`preflight_boot_check.sh --allow-xnu-entry` and `run_and_capture.sh --allow-xnu-entry`, exit 0, device
back on Android on its own both times; payload logs 499 322 (A) and 504 130 (B) bytes.

## What 486 has to do

Two things this step opened and did not close.

**One registry slot, two root objects.** `xnu_live_dtk_same = 0` in both runs, and the sequence is
precise: immediately after `IODeviceTreeAlloc` returns, the registry root's IODT child *is* that
return value (`walk_seq = 1`: `walk_first = walk_first` = `dtalloc_ret`) with 21 children; by the first
`fromPath` of `bsd_init` it is a different object (`walk_seq ≥ 2`) with the same 21 children and **the
same child-set pointer** (`0xc0625a80`). 462's own title — "the delete list was every entry" — is the
lead: the tree the OS built first is gone from the slot, and the entries of the tree the OS reads answer.
486's instrument is cheap and specific: publish `getChildEntry`'s result *and* the registry root's
child-set pointer at both moments, the class name and retain count of each candidate root
(`OSMetaClass::getClassName`), and wrap the detach path (`IORegistryEntry::detachAbove` /
`IOPlatformExpert`'s re-init) to see which call removes the first root and which builds the second. Until
that is answered, "the tree the OS is handed" is a phrase with two referents, and every earlier step that
used 461's `g_dtplane_root` as *the* tree root inherited the wrong one.

**The copies that build process 1's arguments fault and retry.** `exec_save_path+0x38`,
`exec_copyout_strings+0x128`, `load_init_program_at_path+0x30` → `mmu_kvtop_wpreflight+0xc`, and the
`copyout/copyin` word loops — `far = 0x1000`, `fsr = 0x805`. Whether each of those is a demand fault the
kernel's own handler services (which would make this image's **first serviced demand fault** part of the
reading, which 484's hand-off asked for) or a retry of the same translation fault is not decidable from
the counts: 486 wraps `vm_fault`/`arm_fast_fault` (the pair still owed since 480) and publishes the VA's
translation before and after the handler returns.

Still owed, and unchanged: which of `arm_fast_fault`/`vm_fault` serviced 480's write fault; why the
`VM_FLAGS_FIXED` stack allocation at `0x26E00000` is refused; 474's `thread->map = 0` moment; 448's
`_bad` slots as a named pair; the pthread table's other ~34 slots; `osfmk/kperf/kperfbsd.c`; the
untraced build's `entry_stubs.c` compile errors; `thread_bootstrap_return`; and the fixture's timer node
(`interrupts = <1 2 0 1 3 0>` → INTID 18/19), which 482 falsified for `CNTV` and which now matters to
the quantum timer's own chapter.
