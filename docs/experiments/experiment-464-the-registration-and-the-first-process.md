# Experiment 464 — the registration, and the boot bootstraps its first process

**Status: on the device, one run, 474170 bytes of log, exit 0, device back on Android by itself (Android
10, `up 1 min`). One statement added and the frontier moved from a 30 s wait to the last statement of
`bsd_init`. `MSM8974PlatformExpert::start` now ends with `registerService()`, and 463's three predicted
readings all came out: the walk over the `IOPlatformExpert` metaclass finds **one** instance
(`wcls_seen = 1`, `winst_inst = 0xc06088c0`, `winst_state = 0x1e`), the second wait's own first call
returns it (`wsvc_p4 = wsvc_p0 = 0xc06088c0`), and the wait **returns**
(`wmatch_seq = 4`, `ent = 1`, `ret = 0xc06088c0`) — so `IOSecureBSDRoot` runs
`pe->callPlatformFunction("SecureRootName", …)`, gets `kIOReturnUnsupported`, and `bsd_init` continues to
its last statement. The new frontier is the *first pthread-table slot the boot reaches*, reached from
`forkproc`'s own `pth_proc_hashinit(child_proc)`: the boot is bootstrapping process 1.** The instrument's
own 8 KB buffer was full when that record was written, so the log's name for it is the truncated
`stage90_pth`; the identity below comes from the record's *caller* and the disassembly, not from the name.

## The change, and why it is the faithful one rather than a shortcut

`IOSecureBSDRoot` (`iokit/bsddev/IOKitBSDInit.cpp:664-700`) opens with a class match and a 30 s wait; 463
measured that the wait could not be satisfied because the metaclass's `instances` set was empty, and that
the only writer of that set is `OSMetaClass::addInstance`, whose only caller is `IOService::doServiceMatch`
(`IOService.cpp:3694`). `doServiceMatch` is reached from `registerService` -> `startMatching`. The
`registerService` callers this tree has are `IOStartIOKit.cpp:167`'s root nub (an `IOPlatformExpertDevice`,
i.e. a *sibling* of this class, `IOPlatformExpert.h:290`), the platform nubs
(`IOPlatformExpert.cpp:205`, `:1306`), the NVRAM controller (`:1340`) and `gIOResources`
(`IOService.cpp:3543`) — none of them an `IOPlatformExpert`. So the class this project owns is the one that
has to publish itself, and publishing at the end of `start` is the documented idiom
(`IOService.h:510-515`: "The object should be completely setup and ready to field requests from clients
before `registerService` is called") and what Apple's own closed-source platform experts do. One statement:

```c
    g_pexpert_registers++;
    entry_live_write( "xnu_live_pexpert_reg_seq", g_pexpert_registers );
    this->registerService();
    entry_live_write( "xnu_live_pexpert_state_reg", (uint32_t) this->getState() );
```

## The mechanism the instrument had to be arranged for: the registration is a job, not a call

The prediction written before the build said `_state_reg = 0`. It is 0, and the reason is a reading, not
an inference: `registerService` -> `startMatching` computes
`sync = (options & kIOServiceSynchronous) || (provider && (provider->__state[1] &
kIOServiceSynchronousState))`; this object's provider is the root nub, whose own `registerService()` was
called with no options (`IOStartIOKit.cpp:167`) so its synchronous bit is clear — `sync` is false, and the
`else if( !sync || ...)` branch enqueues the match as a job
(`_IOServiceJob::startJob`, `IOService.cpp:857`). The job is picked up by a config thread. The log says so
between the two records this change writes, with nothing else in between:

```
 xnu_live_pexpert_reg_seq=0x00000001
 xnu_live_kthread_cont=0x801355f4          <- _IOConfigThread::main+0x0
 xnu_live_kthread_ret=0x00000000           <- KERN_SUCCESS
 xnu_live_pexpert_state_reg=0x00000000
```

The `kthread` pair is the `--wrap=kernel_thread_start` record of `_IOConfigThread::configThread()`
(`IOService.cpp:4194-4207`, reached from `pingConfig`), i.e. **inside `registerService()` the config
thread that will do the registration was created**. No driver is instantiated by that registration: the
catalogue files personalities by `IOProviderClass` (`arrayForPersonality`, `IOCatalogue.cpp:124-132`),
this image's three are filed under `IOPlatformExpertDevice` and `IOResources`, and neither name is on this
object's class chain — so `findDrivers` returns an empty set and `probeCandidates` is skipped (`:3720`),
which is also what keeps 362's `IOPanicPlatform` panic out of reach.

`findDrivers`' wrapper records only the call whose service is `getResourceService()` (455's filter), so the
platform expert's own `findDrivers` is **deliberately not in the log**; the state bits are what stand in
for it, and they are the two the wait tests.

## The four waits, and the two that matter

| \# | caller | dictionary | `ent` | `ret` |
| --- | --- | --- | --- | --- |
| 1 | `IOFindBSDRoot+0x6c` (`0x80206e0c`) | `0xc0634780` | 0 | 0 |
| 2 | `IOFindBSDRoot+0x6c` | `0xc0634780` | 1 | `0xc06062e8` = `getResourceService()` |
| 3 | `IOSecureBSDRoot+0x44` (`0x80207a64`) | `0xc06346e0` | 0 | 0 |
| 4 | `IOSecureBSDRoot+0x44` | `0xc06346e0` | 1 | **`0xc06088c0` = the platform expert** |

`to = 0x00000006fc23ac00` = 30 000 000 000 ns for all four. Wait 2 is 463's reading unchanged (the
resource root). Wait 4 is this step: the wait 463 measured as unable to return, returning. Its own probe,
taken before the real call:

```
 xnu_live_wsvc_seq=2  _dict=0xc06346e0  _sym=0xc06176f0  _p4=0xc06088c0  _p0=0xc06088c0
 xnu_live_wcls_seq=2  _sym=0xc06176f0  _meta=0x8058b4f0  _rsvc=0xc06062e8
 xnu_live_wcls_name0=0x6c504f49  _name1=0x6f667461                     -> "IOPl" / "atfo"
 xnu_live_winst_inst=0xc06088c0  _state=0x0000001e
 xnu_live_wcls_seen=0x00000001  _shown=0x00000001
```

`wsvc_p4` is `copyExistingServices(dict, kIOServiceMatchedState, kIONotifyOnce)` — the very call the wait
makes first — and it now returns the instance, which is why the sleep does not happen at all. `_state =
0x1e` is bits 1, 2, 3 and 4: `kIOServiceRegisteredState` (`:3702`), `kIOServiceMatchedState` (set by
`copyNotifiers(gIOMatchedNotification, kIOServiceMatchedState, 0xffffffff)` at `:3753`, and the bit
`instanceMatch` tests), `kIOServiceFirstPublishState` (`:3695`) and `kIOServiceFirstMatchState` (`:3756`) —
the same value the resource root carries, and the full expectation for a first, successful
`doServiceMatch` rather than just `0x06`. The two mechanisms 462 named are now *satisfied* rather than
proved irrelevant: the state bit is tested and holds, and the matched notification is what 463 measured
delivering.

## The sleeps: two, not three

Two `iolock` records, both at `waitForMatchingService+0xe0` (`0x801377c8`), both on `gNotificationLock`
(`0xc0604750`), each with `dl − now = 575999988` = 30.000 s − 12 ticks at 19.2 MHz (the GPT's
19.2 MHz, which is what fixes the ticks-to-seconds scale). 463 had three: the IORTC wait, `IOFindBSDRoot`'s
and `IOSecureBSDRoot`'s. The third is gone because wait 4's first call answers, and the remaining two are
246651 ticks (12.85 ms) apart — so `IOFindBSDRoot`'s sleep is entered ~13 ms after `IOKitInitializeTime`'s
and returns as soon as the resource root's own registration completes, exactly as 419, 457 and 463
measured it. **The two runs' absolute tick counts are not comparable and this step does not compare them**:
the counters are the free-running GPT and both the payload's own boot time and the point at which it jumps
into XNU vary between runs, so every number above is a within-run delta.

Two refinements of 463's account of wait 2, both from this run's records:

- `reg_seq=2` (taken at wait 2's sleep) is `ptr=0xc06062e8 state0=0x1e state1=0x84000002
  rm=0xc0634a20 count=2 idx=0xffffffff` — the resource root registered, and `IOResourceMatched` holding
  **two** entries with `IOBSD` still absent (`idx = 0xffffffff`), which is why the wait's own predicate
  cannot answer and the sleep is needed.
- `publishResource("IOBSD")` (`IOKitBSDInit.cpp:95`) sets a property *named* `IOBSD` on `gIOResources`
  (`IOService.cpp:3534-3536`), not an `IOResourceMatched` entry; the array is built later, from
  `copyPropertyKeys()`, by the resource root's own `doServiceMatch` tail (`:3751`). And because that
  registration is enqueued the same way this step's is, the array at wait 2's sleep is still the old one —
  the publish happened *before* the wait and the registration it triggers completes *during* the sleep.
  Both are the async-job mechanism of this experiment, one object over, and they are the reason wait 2's
  answer arrives through the sleep rather than through its first call.

## Where the boot went: `bsd_init`'s last statement, and the first process

Nothing live is recorded after wait 4's return — the records that follow are the tracer's, from its own
buffer — and what the stop names is not a wait and not a stub of this project's own making: the last thing
the boot did was

```
 xnu_entry_stub_caller_v=0x800d9bb8                    forkproc+0x5a8
   caller-4 = 0x800d9bb4  forkproc+0x5a4               the bl
   800d9bb4:	eb045ba8 	bl	801f0a5c <pth_proc_hashinit>
```

and the target is the pthread subsystem, one indirection deep:

```
 bsd/kern/bsd_init.c:1033     bsd_init_kprintf("calling bsd_utaskbootstrap\n");
 bsd/kern/bsd_init.c:1033     bsd_utaskbootstrap();
 bsd/kern/bsd_init.c:1144         thread = cloneproc(TASK_NULL, COALITION_NULL, kernproc, FALSE, TRUE);
 bsd/kern/kern_fork.c:966             if ((child_proc = forkproc(parent_proc)) == NULL)
 bsd/kern/kern_fork.c:1393        pth_proc_hashinit(child_proc);          #if PSYNCH
 bsd/kern/pthread_shims.c:360         pthread_functions->pth_proc_hashinit(p);
                                      -> stage90_pthread_slot_pth_proc_hashinit
```

Every link but the last is Apple's own code and is in the image: `pth_proc_hashinit` at `0x801f0a5c` is
twenty bytes — `movw`/`movt` of `pthread_functions`, `ldr [r1]`, `ldr [r1, #0x20]`, `bx r1` — so the slot
it reaches is **word 8** of the table (offset `0x20`, which is `pth_proc_hashinit`'s own offset in
`struct pthread_functions_s`), and `nm` puts this project's stand-in for exactly that slot at
`0x80269f4c` (`stage90_pthread_slot_pth_proc_hashinit`, 16 bytes). *Corrected by 465: this line read
`ldr [r1, #0x18]`, "word 6", which is `workqueue_mark_exiting`; the offset is read off the disassembly and
the identification rests on `nm` naming `0x801f0a5c` `pth_proc_hashinit` and on the caller being
`forkproc`'s `#if PSYNCH` line. Defect 185.* `bsd_init`'s last statement is
`bsd_utaskbootstrap` and `cloneproc`'s only caller in a boot with no userland is that statement
(`kern_fork.c:578`'s other call site is inside `fork1`'s vfork path, which needs a syscall), so this is the
bootstrapping of process 1: **the whole of `bsd_init` ran** — the mount, `IOSecureBSDRoot`, `VFS_ROOT`,
`microtime_with_abstime`, `devfs_kernel_mount`, `siginit`, `pal_kernel_announce` — and its last statement
is where the boot now stands. The three `ifdef`s that had to be on for this to be reachable are all on:
`CONFIG_EMBEDDED` (the `IOSecureBSDRoot` body), `CONFIG_IMAGEBOOT` (skipped: `imageboot_needed()` is false),
`PSYNCH` (the `pth_proc_hashinit` call).

The count of `thread_block` calls is the same 60 in this run as in 463, and the last of them is 11865 ticks
(0.62 ms) after the `IOFindBSDRoot` sleep was entered — so there is no block anywhere in the post-wait
path, and the whole of `IOSecureBSDRoot`, the mount's aftermath and `bsd_init`'s tail is under a
millisecond of real work. The threads the run did create past that point are in the `kthread_cont`
records (`IOWorkLoop::threadMain`, `memorystatus_thread`, `zone_replenish_thread`, `async_work_continue`,
and `_IOConfigThread::main` a dozen times), and the only `msleep`-family sleep the report kept is
`ifnet_detacher_thread_func+0x48` on channel `0x80593d40`.

## The frontier's name is not in the log, and that is the second 461

`stub_hit=` records go through `entry_kv` into the same 8 KB buffer the `t268_*` kalloc tracer fills
(`entry_stub_hit`, `entry_stubs.c:3719`), and this run filled it: `kv_written = kv_in_dram = 0x2000`
(8192 of 8192) with `kv_dropped = 0x8080` (32896 refusals). The last record is therefore cut mid-name —

```
 stub_hit=stage90_pth
```

— which names a symbol that does not exist. 461 gave the *trap* a buffer of its own for exactly this
reason; the *stub* path still shares the tracer's. The identity was recovered from the two things that did
survive: the caller (`xnu_entry_stub_caller_v`, a `.bss` slot written before the record) and the
`bl` it points at. Recorded as defect 181 in the ledger, with the consequence stated rather than implied:
every reading in this document about the frontier rests on the call site and the disassembly, and the name
in the log is a truncation that happens to look like one.

## The build, and the run

The platform objects are compiled by `tools/build_xnu_arm_kernel.sh` and not by `build_entry.sh` (463's
defect 180), so the platform expert was rebuilt first with **both** environment variables —
`XNU_KERNEL_CONFIG=STAGE90_XNU XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local ./tools/build_xnu_arm_kernel.sh --platform-only` —
and `build_entry.sh`'s `platform_obj_fresh()` check passed. The entry image's `.text` grew 5102144 ->
5102304 bytes (+160) and **nothing else moved**: image bytes 5307400 (unchanged from 462 and 463), `.bss`
0x80510000..0x805a5f38 (614200 bytes) and `headroom` 1417416 bytes both byte-identical, the metaclass
`IOPlatformExpert`'s own address `0x8058b4f0` identical to 463's — the extra text is the registration, the
count and the two records. 40 wraps, unchanged: **37 reached by a branch, 1 same-object-only, 1 never
called here (`sleep`), 1 by address only (`vcputc`), none dead**; pass 1 = 27 undefined; 463's `getState`
uniqueness and the twelve mangled-name checks all pass. The payload is 8327168 bytes, sha256
`84acb13641eeb3fe5ea63c8cbea7f3ba3b86383e36da6399de851128718979df` (*corrected by 465: this line said
"md5", and the value is the build's `sha256sum` output — 64 hex digits — so anyone who checked it with
`md5sum` would have seen a mismatch and concluded the artifact had changed. Defect 186.*; the entry blob
it embeds changed, its own sources did not). The entry blob rebuilt after the run is **byte-identical**
(`cmp` clean, md5 `4806a0a6f0aa186824e78202a56ce1cb`), so the artifact that booted is the tree that is
being committed.

One `fastboot boot` through the two gates. XNU's own console block is **byte-identical to 463's** (1051
bytes, 22 lines, ending at `BSD root: md0, major 1, minor 0`): the boot ran four statements and a process
bootstrap further and printed nothing new, because the next print on this path is the init program's. The capture
counters read `xnu_live_ostext_chars = _total = 0x3ee` (1006), `_heals = 1`, `_tank = 0x262` (610). No
`exception:`, `abort_entries = 0`, one `stub_hit`, `No errors detected`, and the device is on Android 10
afterwards on its own.

## What is not measured

1. **The thread that hit the stub.** `entry_stub_hit` records a name and a caller and no thread, and the
   report's eight block slots hold the *first* eight blocks, so the run does not say which thread
   bootstrapped process 1. The source argument that it is `bsd_utaskbootstrap` is an argument from
   reachability (no userland exists to make a `fork`/`vfork` syscall), not a reading.
2. **`callPlatformFunction("SecureRootName")`'s answer.** Nothing records it. What *is* recorded is the
   consequence: the RAM disk root survives (`mdevremoveall` did not run) and `bsd_init` continued, both of
   which the log shows.
3. **Anything about `forkproc`'s other call sites**, and nothing about `forkproc` itself before `:1393` —
   the statements above it set the new proc's other pthread-visible fields (`p_dispatchqueue_offset`,
   `p_dispatchqueue_serialno_offset`, `p_return_to_kernel_offset`, `p_mach_thread_self_offset`,
   `p_pth_tsd_offset`) without a record, because only the *table* is instrumented.
4. **The registered object's lifetime.** The wait's `copyExistingServices` returns a retained array and
   `OSSafeReleaseNULL(pe)` drops it; the registration itself holds no reference. Nothing in this run
   measures the platform expert's reference count.
5. **The kalloc stream's contents.** The 8 KB buffer is full and 32896 writes were refused, so the
   `t268_*` records in the log are the *first* 8192 bytes of a much longer stream; the tail of the boot's
   allocation history is not in this log at all.

## Next: 465, the first pthread slot — and a record that cannot be cut

1. **The object of the step is `pth_proc_hashinit`, not another reading.** `PSYNCH` is on, and the boot's
   first pthread-subsystem call is `forkproc`'s per-proc hash initialiser. The contract is in
   `bsd/sys/pthread_shims.h:91` (`void (*pth_proc_hashinit)(proc_t p)`) and the consumer is `PSYNCH`'s own
   hash table; a body that is honest about what this image has (`pth_proc_hashdelete` is the next call, in
   `forkproc_free`) is what the slot needs, and the prediction to write before the build is the *next*
   `stub_hit`, not this one.
2. **The terminal record must not go through a full buffer.** `entry_stub_hit` writes `stub_hit=<name>`
   with `entry_kv` into the buffer the tracer fills, so the one record that names a stop is the one record
   most likely to be cut. The cheapest fix that survives a full buffer is the 463 pattern applied to this
   path: write the name and the caller on the **live** channel first (they are terminal records — nothing
   later can be lost by writing them early), and keep the buffer copy for the dump.
3. Still owed from 461/463: 448's `_bad` slots as a pair of keys naming each one's sense; the timer
   (`ml_init_timebase` + an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, 19.2 MHz, IRQ 19);
   `IOCPUInterruptController`; the pthread table's other 38 slots (now with a first consumer measured);
   `osfmk/kperf/kperfbsd.c`; `thread_bootstrap_return`; making the epilogue's key list a data table.
