# Experiment 452 — the live console works, and the boot publishes `IOBSD`

**Status: built, gated, run on hardware. The channel works on its first attempt, and the boot behind
it is the largest step this project has measured: 146 live records, 33 `thread_block`s (8 of which
returned), 16 kernel threads created, and `bsd_autoconf` reaching its last statement — the
`publishResource("IOBSD")` that says the BSD subsystem is up.** The instrument can now see a boot
that never reaches its epilogue, which is what 450 needed and 451 failed to give it. `.text` 5009408
(this step *removes* 451's 4096-entry scan), entry bin 5208596, window and layout unchanged, `.bss`
`0x804f7a40 .. 0x80548b98`, payload `a50b2e22...` 8228864 bytes.

## The correction: no anchor, no inherited bits

451 took its descriptor's protection bits from a neighbour section - XNU's own BLOCK for
`RAM_CONSOLE_BASE - 1 MB` - and that section does not exist, because the boot args declare
`memSize = 0x01000000` and the live table therefore holds descriptors only for indices `0x800..0x80f`
and `0xfff`. 452 removes the anchor rather than moving it. Everything the descriptor needs is either
XNU's own convention or measured at the moment of the write:

| bit(s) | source | why not inherited |
| --- | --- | --- |
| type `0b10` (section) | `ARM_TTE_TYPE_BLOCK` | the console's slot must be a 1 MB section; nothing else can map an address XNU never mapped |
| `AP[0]` (`0x400`) | `ARM_TTE_BLOCK_AF` | it is how `start.s` spells `AP_RWNA` - privileged read-write, user no access - on every kernel section |
| `S` (`0x10000`) | `ARM_TTE_BLOCK_SH` | `start.s` sets it on the same sections |
| domain `0` | `ARM_DAC_SETUP = 0x1` | checked against DACR before the write (refusal 3), instead of trusting a neighbour's domain field |
| `TEX/B/C` | **measured**: `SCTLR.TRE` decides whether TEX is remapped, and PRRR says which encodings are Strongly-ordered (refusal 5 if none is) | this is the one field that must *not* be inherited: `CACHE_ATTRINDX_DEFAULT` is write-back, and a console whose writes sit in a cache is a console a hang erases |
| PA | `RAM_CONSOLE_BASE` | it is the console |

and the table base comes from `TTBCR.N` rather than from assuming TTBR1: `N == 0` means every address
goes through TTBR0, `N >= 1` sends the upper window to TTBR1, and the console is above `0x80000000`,
so those are the only two cases. A base outside the kernel's window (`< 0x80000000`, or not 16 KB
aligned) is not a refusal but a *retry*: the first hooked call can fire before XNU has switched
tables at all, so `g_live_state` stays 0 and the next record tries again, up to eight attempts
(`g_live_attempts` is in the report so a retried success and a first-try one can be told apart).
Every other refusal is final, and `g_live_refuse` says which check it was.

Three descriptors are written, not one: the console's own MB, the MB after it (the buffer is 2 MB),
and one VA `0x01000000` past the console as a second, independent path to the same first MB. Each
slot is required to be invalid first (refusal 2 if not - an occupied slot is *skipped*, never
clobbered), and the acceptance test is the console's own signature read back through the new
descriptor (refusal 4). With the second VA taking as well, the same word is read through it too, so
a run can say the mapping was confirmed twice rather than once.

## The measurement

    wrote 299468 bytes to /tmp/cancro-452-last_kmsg.txt
    146 x xnu_live_* records, of which 16 are the header
    25 x persistent_write_attempted=0x00000000
    87 x failure_mask=0x00000000
    no stub_hit=, no exception:, no panic:
    device returned on its own

The header, in full - every value here was read out of the machine at the moment of the install:

    xnu_live_console=0x00000001        xnu_live_attempts=0x00000001
    xnu_live_ttbr0=0x8070404a          xnu_live_ttbr1=0x8070404a
    xnu_live_ttbcr=0x00000001          xnu_live_dacr=0x00000001
    xnu_live_l1=0x80704000             xnu_live_installed=0x00000007
    xnu_live_slot_before=0x00000000    xnu_live_desc=0xde51040e
    xnu_live_desc2=0xde51040e          xnu_live_alias_read=0x43474244
    xnu_live_sctlr=0x30c5787d          xnu_live_prrr=0x1f08022a
    xnu_live_attr=0x0000000c

Every row is worth its own sentence, because between them they settle four things this project had
only been able to argue about:

  * **`ttbcr = 1`** - `TTBCR.N = 1`, so TTBR1 answers for the console's address, and the register
    choice in the code is the one the hardware uses. (`TTBR0` and `TTBR1` are the same value here
    because `set_mmu_ttb` and `set_mmu_ttb_alternate` write the same base.)
  * **`l1 = 0x80704000`** - literally `topOfKernelData + 4 pages`, which is `arm_vm_init`'s
    `cpu_ttep = boot_ttep + ARM_PGBYTES * 4` (`arm_vm_init.c:373`). The table the instrument writes
    into is the one the kernel walks, and its address was computable all along.
  * **`slot_before = 0`** - the console's index was **fault** before the write. That is the identity
    case doing exactly what the source says: with `physBase == virtBase` `arm_vm_init`'s
    "clear V==P mappings" loop degenerates to `tte == tte_limit` and does *nothing*, and `start.s` is
    what fills the table - `invalidate_tte` over 10240 entries (all 4096 of the boot table and more),
    then `mapveqp` over `memSize` / 1 MB = 16 sections. Nothing but XNU's own window is mapped.
  * **`desc = 0xde51040e`** - the value derived by hand in the design
    (`0xde500000 | type 2 | AP[0] | SH | 0xc`), and `attr = 0xc` is PRRR index 3: `PRRR = 0x1f08022a`
    has its field for `i = 3` equal to 0, so the section is `TEX=000 B=1 C=1` and Strongly-ordered
    under `SCTLR.TRE` (`sctlr = 0x30c5787d`, TRE set - now measured live rather than recalled from
    236). `alias_read = 0x43474244` is `'DBGC'` through the second VA, which is the mapping confirmed
    twice through two different descriptors.
  * **`attempts = 1`** - the first hooked call of the boot is already deep enough that XNU's tables
    are live. That call is `OSUnserialize` from `IOCatalogue::initialize` (449's site), so
    *everything from the catalogue's parse onward is in the log*.

### The trace

146 records in order; the 33 `thread_block`s and the words around them, with the recorded caller
resolved against this step's image (`caller - 4` is the `bl`):

| record | what | resolved |
| --- | --- | --- |
| 15 | `unser_caller` | `IOCatalogue::initialize+0x18` |
| 17 | `dtalloc_arg`/`_ret` | the tree at `0x806e0000`, an allocator handle back |
| 19, 22 | `kthread_cont` | `IOWorkLoop::threadMain`, `_IOConfigThread::main` |
| 25 | `block_enter` #1 | `ml_get_max_cpus+0x38` |
| 27, 29 | `block_enter` | `zone_replenish_thread+0x6c` |
| 31 | `block_enter` | `mapping_replenish+0x28c` |
| 33 | `block_enter` | `Call_continuation+0x18` |
| 44 | `pub2_key` | `0x8046e7e8` = **`"IORTC"`** - `MSM8974PlatformExpert::start`'s second statement |
| 45, 46 | `initmax_cpus_caller`/`_arg` | `MSM8974PlatformExpert::start+0x28`, `arg = 1` |
| 47, 48 | `block_return` #1,`_returns=1` | `ml_get_max_cpus+0x38` - the 449-era stop, returned |
| 51-90 | blocks | `lck_mtx_sleep_deadline`, `lck_mtx_lock_contended` (5 returns), `lck_mtx_sleep`, `thread_terminate_self`, `_sleep` |
| 91-113 | `kthread_cont` (8 more) | `async_work_continue`, `mbuf_worker_thread_init`, `aio_work_thread` x2, `nwk_wq_thread_func`, `pf_purge_thread_fn`, `flowadv_thread_func`, `dlil_main_input_thread_func`, `ifnet_detacher_thread_func` |
| 96, 97, 114 | `maxcpus_caller` | `mcache_init+0x1c`, `mbinit+0x680`, `sysctl_mib_init+0xbc` |
| 115 | `pub2_key` | `0x8047110b` = **`"IOBSD"`** - `bsd_autoconf`'s last statement |
| 116-144 | blocks, none returning | `lck_mtx_sleep_deadline`, `memorystatus_thread`, `async_work_continue`, `thread_terminate_self`, `mbuf_worker_thread`, `aio_work_thread`, `_sleep` x4, `lck_mtx_sleep`, `Call_continuation` x2 |

The order is the boot's own order and it matches the sources exactly: `mcache_init` (`bsd_init.c:745`),
`mbinit` (`749`), `aio_init` (`772`), `nwk_wq_init` (`815`), `dlil_init` (`817`, which is where
`flowadv_init` and `dlil_create_input_thread(NULL, dlil_main_input_thread)` run), `sysctl_mib_init`
(`858`), `bsd_autoconf` (`861`) - and the `kthread_cont` records for `mbuf_worker_thread_init`,
`aio_work_thread`, `nwk_wq_thread_func`, `flowadv_thread_func`, `dlil_main_input_thread_func`,
`ifnet_detacher_thread_func` and `pf_purge_thread_fn` are the threads those functions create.

## What the run says about 450's prediction

Every row of 450's table held, and it held on *this* image: 33 blocks against a predicted ">= 2",
8 returns against ">= 1", `initmax_cpus` called once with `arg = 1` against the prediction of exactly
that (447 had measured the count as 0 - the writer had never run, because the run could not get
there), `pub2` naming `"IORTC"` as predicted, 2 `allocClassWithName` instantiations against ">= 1",
and the first block at `ml_get_max_cpus+0x3c` - the same offset 446/449 measured, with only the
address moved because the image did. 450's own run could say none of this, because the report it
would have arrived in did not exist yet.

Two readings the trace adds that the prediction did not contain:

  * **`block_enter` from `Call_continuation+0x18`** (records 33, 53, 79, 81, 140, 144) is not a block
    *inside* `Call_continuation` - that function is nine instructions ending in `blx r6` and contains
    no call to `thread_block` at all. The recorded caller is the return address of the `blx`, which
    means the thread's continuation reached `thread_block` through a **tail branch**: 449's rule
    ("a caller key on a tail-branched call names the branch's caller") applied to thread startup. So
    a new thread starting and blocking immediately is legible as such, but the thread's own entry
    point is *not* in the key - which is precisely the cheap thing a next step should add.
  * **`thread_block` from `thread_terminate_self+0x2f8`** is a dying thread parking
    (`thread_mark_wait_locked` then `lck_spin_unlock` then the block), i.e. normal, and not a
    frontier. Nothing in the trace is a stop: no stub was hit, no fault was taken, and every one of
    the 16 created threads is a worker the sources say should exist by this point.

## The frontier, as far as this measurement can name it

`bsd_autoconf` reached `IOKitBSDInit()` and published `"IOBSD"` - its last statement - so `bsd_init`'s
own body continued into `os_reason_init`, `dtrace_postinit`, `loopattach`, `gif_init`, `pfloginit`,
`ether_family_init`, `net_init_run`, `vnode_pager_bootstrap`, `inittodr(0)` and the rest. After the
publish there is exactly **one** record before the workers park: `lck_mtx_sleep_deadline+0x88`
(116), which never returns. In this tree `lck_mtx_sleep_deadline` has two callers - `msleep` with a
timeout (`bsd/kern/kern_synch.c:197`) and `vm_pageout.c:3491` - so the boot is *waiting*, not
stopping, and the two candidate sites are one of those.

That is as far as the record goes, and the limit is structural, not accidental: `entry_note_block`
records the address the wrapper was called from, which names the *primitive* that slept, never its
caller, and nothing in the log says *which thread* a record belongs to. The next step is therefore
the smallest one that answers both: one more frame of return address at the block, and the current
thread pointer, so a trace can be read per thread and a wait can be attributed to the function that
asked for it. Everything else - the timer (`ml_init_timebase` over the MSM8974 GPT), the remaining
`IOCPUInterruptController` work, the pthread table's other slots, `thread_bootstrap_return` - is
still owed and still out of reach of a trace that cannot say who is waiting.

Safety, unchanged and re-measured: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed; the device returned
to Android on its own; 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:` line. The one new exposure this step takes is writing a
descriptor into XNU's live page table, and the measurement bounds it: the slot was fault before the
write, the bits are XNU's own section convention with the memory type read from PRRR, the domain is
checked against DACR first, and the write is followed by a clean, a TLB invalidate and a read-back of
the console's signature - which returned `'DBGC'` through both descriptors, and the boot then ran
hundreds of millions of instructions past the change without a fault.
