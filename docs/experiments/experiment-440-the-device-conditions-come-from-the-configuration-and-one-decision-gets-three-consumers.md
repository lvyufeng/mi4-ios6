# Experiment 440 — the device conditions come from the configuration, and one decision gets three consumers that are compared

**Status: prediction written; build and hardware run below.**

## Why: 439's stop named a symbol this image both executed and refused to build

Experiment 439 ended on `stub_hit=bpf_init` at `xnu_entry_stub_caller=0x8003B260`, and it was the
cleanest stop this project has had: a stub returned nothing, so `pty_init(16)`, `ptmx_init(1)` and
`mdevinit(1)` had *run to completion on the device* before it. 439's own reading of where the
frontier was is the sentence this step is the answer to:

> `bpf_init` (`bsd/net/bpf.c`, `optional bpfilter`) has its cause in a measured defect — the
> configuration declares eight devices (`bpfilter ether fsevents loop mdevdevice ptmx pty random`)
> and `out/device_table.txt` holds four, so `bpf.c`, `ether_if_module.c` and `if_loop.c` never enter
> the manifest.

The defect is worse than an omission, and that is why 440 is a *derivation* and a *check* rather than
three more lines in a table. **One device decision was answered in three places, by three different
mechanisms, with three different answers:**

```
out/device_table.txt                      bpfilter -> absent   <- hand-written, from the wrong premise
out/xnu_device/RELEASE/bpfilter.h         #define NBPFILTER 0 <- hand-written, four headers, chosen values
out/xnu_pseudo_inits/RELEASE/*.c          {4, bpf_init},     <- derived from config/MASTER (439)
```

The manifest left `bsd/net/bpf.c` out because the table said the device was absent. The header said
`NBPFILTER 0` because someone had chosen that value to agree with the table. And the `pseudo_inits[]`
array — generated in 439 from the configuration, correctly — carried `4`, the number
`config/MASTER` gives `pseudo-device bpfilter 4`, and handed it to `bpf_init`. **A count written into
a table this image executes is a declaration that the device exists,** and nothing compared it with
the manifest.

The premise of the old table was one grep, and the grep was right:

```
grep -c '^pseudo-device' */conf/files        # 0, in every component
```

4570's `*/conf/files` publish no device declarations, so the hand-written table concluded that the
configuration published none either and recorded four choices. But `*/conf/files` is where an
`optional <cond>` condition is **tested**; `config/MASTER` is where the device set that answers it is
**declared**, and `config/MASTER` publishes **24** `pseudo-device` lines (167–459). `expand.sh
RELEASE` filters them to eight, and `expand.sh STAGE90_BOOT` to three. This is
[[mi4-not-absent-its-build-output]] for the twelfth time: not absent — in the other file. A search in
the directory the *user* of a value lives in, for the value's *declaration*.

## The fix: one parse, three consumers, and each consumer applies its own rule

`tools/xnu_config/devices.py` is new and is the only place the declarations are read. It runs
`expand.sh <CONFIG>` — the same pipeline the manifest and the array already read — and returns
`[(name, number_or_None, init_or_None, kind)]` in configuration order, matching the four
`pseudo-device` alternatives `parser.y:207-229` accepts (`Dev`, `Dev NUMBER`, `Dev INIT ID`,
`Dev NUMBER INIT ID`) and keeping `kind` so a `device` line cannot be silently dropped by a
`pseudo-device`-only pattern.

**`number is None` is not the same as a written `0`**, and that distinction is the reason the parse
returns the raw declaration instead of a computed count. Apple has two rules on `d_slave` and they
disagree about `UNKNOWN` on purpose:

```c
mkheaders.c:85-101    count = dp->d_slave != UNKNOWN ? dp->d_slave : 1;   /* the header */
mkioconf.c:79-100     count = d_slave; if (count <= 0) count = 1;         /* pseudo_inits[] */
```

A bare `pseudo-device loop` has `d_slave == UNKNOWN` and becomes `NLOOP 1` and `{1, loopattach}` —
the same 1 by two routes. An explicit `pseudo-device bpfilter 4` keeps its 4 in both. So
`devices.py` exports `header_count()` and `array_count()` as two named functions with their
citations, and no consumer is allowed to have its own opinion:

| consumer | before 440 | after 440 |
| --- | --- | --- |
| `tools/xnu_config/device_table.py` | four hand-written rows, `bpfilter ether loop` absent | every device the configuration declares, at 1, plus two `OVERRIDES` rows with reasons |
| `tools/gen_device_headers.py` | `gen_device_headers.sh`, five headers, values chosen to match the table | generated; the emitted set is the **intersection** of the configuration's devices and the conditions some `conf/files` line tests; the value is `header_count()` |
| `tools/gen_pseudo_inits.py` | derived from the configuration in 439 (correct) | refactored onto `devices.py`, unchanged in behaviour |
| `tools/gen_option_headers.py` | wrote `ether.h`/`bpfilter.h` as `#define ETHER 0` | skips device-named rows; the device half has one generator |

Two consequences of the emitted set being an intersection, and both were hand-written omissions
before: `mdevdevice`, `fsevents` and `random` are devices of RELEASE that **no** `conf/files` line
tests (`randomdev.c`, `memdev.c`, `vfs_fsevents.c` are `standard`), so they get no header at all —
and the old shell generator agreed by omitting them, by hand, for a reason it could not state.

**The other half of the fix, and it was a defect inside the fix.** The first 440 full build reported
`615 tried / 614 compile / 1 fail` — a clean success — and had built nothing the step was about: the
device table was written, but `out/xnu_arm_manifest.txt` was a *hand-run prerequisite* of the kernel
build, so the build compiled the **stale** manifest. `tools/build_xnu_arm_kernel.sh` now runs
`list_sources.py --write "$MANIFEST"` itself, and the second build reported `621 tried / 620 compile
/ 1 fail` and 703 objects. **A generator that has to be run by hand to have an effect will be
replaced by one whose effect is the build.** The device headers and
`tools/check_device_conditions.py` are wired in the same place, for the same reason.

## The guard: five comparisons, because a restatement is not a check

`tools/check_device_conditions.py` is new and every one of its checks is a **cross-file comparison**
rather than a restatement of the generator's own rule:

1. the configuration vs the table — every declared device is on, unless `OVERRIDES` says otherwise
   *with a reason* (`monotonic` and `xpr_debug` are in that table and are not devices at all);
2. the table vs the headers — on and tested implies a header, and the value is `header_count()`;
3. the headers vs the sources — every device macro the tree **reads** (`NETHER` 4 sites, `NBPFILTER`
   18, `NPTY` 9, `NLOOP` 8) must be defined, because an undefined identifier in `#if` is `0` and that
   is a silent "off", not an error;
4. the headers vs the array — two rules on one `d_slave`, and where they differ it is reported
   rather than assumed away;
5. the two generators must be **disjoint**, and every generated device header must be force-included
   by a `meta_features.h` (`do_header` appends `#include <hname.h>` there), because a header nothing
   includes is a macro no source can see.

Both directions in all five. Failure mode (5) is not hypothetical and it is the one that was live:
before this step both generators wrote `ether.h` — `out/xnu_options/` said `#define ETHER 0` and
`out/xnu_device/` says `#define NETHER 1` — and `-I$OPTION_HEADERS` comes first, so the device header
was **shadowed** and `#if NETHER > 0` in `bsd/kern/bsd_init.c:890` stayed a silent zero while the
manifest built `bsd/net/ether_if_module.c`. Both failure modes were proven by making them happen: a
deleted `ether.h` gives

> `NETHER is read by the tree (4 site(s), first …/bsd/kern/bsd_init.c:890) and no generated header defines it`

and a colliding `out/xnu_options/RELEASE/ether.h` gives

> `ether.h exists in both … — two headers, one name, one include path, and `-I` order decides which a source gets`.

**Two more latent object-kind stand-ins retired as a side effect**, and they were found by
`tools/check_stub_kinds.py`'s both-direction table rather than by looking for them: the six objects
this step adds make `etherbroadcastaddr` and `lo_ifp` real definitions, and the check **refused the
entry build** because `KNOWN_KINDS` listed two exceptions that no longer existed. Both entries were
deleted. An exception that outlives its cause is worse than no exception, and this is the first time
that table has been pruned by a measurement rather than by hand.

## The prediction, written before the build and before the run

The six sources the configuration implies are, measured by diffing `list_sources.py` against the old
table's answer:

```
bsd/net/bpf.c                     bsd/net/ether_if_module.c      bsd/net/ether_inet6_pr_module.c
bsd/net/bpf_filter.c              bsd/net/ether_inet_pr_module.c bsd/net/if_loop.c
```

**1. The count, not the composition.** The stub list goes 42 → 27 undefined, and the 15 retired
names are the ones these six objects define. A count is not a composition and 439 already published
that lesson; the A/B below names them.

**2. The walk completes.** 439's array is
`{16,pty_init} {1,ptmx_init} {1,mdevinit} {4,bpf_init} {1,fsevents_init} {1,random_init} {0,0}` and
the stop was entry **4**. `fsevents_init` and `random_init` are real in the pool, so the walk should
now run to the `{0,0}` terminator for the first time.

**3. `bsd_init`'s remaining straight line runs.** After the inlined `bsd_autoconf` body
(`bsd_init+0x850`): `IOKitBSDInit`, `os_reason_init`, `loopattach`, `ether_family_init`,
`net_init_run`, `cfil_init`, `necp_init`, `netagent_init`, `utun_register_control`,
`ipsec_register_control`, `netsrc_init`, `nstat_init`, `tcp_cc_init`, `mptcp_control_register`,
`vnode_pager_bootstrap`, `inittodr`, `siginit`, the inlined `bsd_utaskbootstrap` at `+0xaa0`, and
`pal_kernel_announce`. Then `bsd_init` returns to `kernel_bootstrap_thread+0x1f8` and that function's
tail runs — `OSKextRemoveKextBootstrap`, `kdebug_free_early_buf`, `serial_keyboard_init`,
`vm_page_init_local_q`, `thread_bind(0)` — and tail-calls **`vm_pageout`**, which does not return.

`bsd_init` has exactly **two** indirect calls and 439 found the first. The second is at `+0xad4`:

```
8003b4b0:	movw	r0, #0xfeb4
8003b4b4:	movt	r0, #0x804f        -> 0x804ffeb4 = mountroot_post_hook (B, .bss)
8003b4b8:	ldr	r0, [r0]
8003b4bc:	cmp	r0, #0
8003b4c0:	beq	8003b4c8           <- taken: the pointer is NULL
8003b4c4:	blx	r0                 <- not executed
```

`mountroot_post_hook` is `B`, in `.bss`, and nothing sets it, so `bsd_init.c:1041`'s
`if (mountroot_post_hook != NULL)` is false. **So after the walk terminates, `bsd_init` has no
reachable indirect call left** — the whole remainder is direct calls, every one of which is defined
by the pool.

**4. Therefore the stop is not a stub inside `bsd_init`, and the prediction is one of two things, in
this order:**

**(4a) `stub_hit=thread_bootstrap_return`, reached through a context switch.** The one place on this
path where a stub is reachable and *no static walk can say so* is the init thread's first run.
`bsd_utaskbootstrap` (inlined into `bsd_init` at `+0xaa0`) calls `cloneproc`, and `cloneproc` reaches
`thread_create_internal2(task, new_thread, …, (thread_continue_t)thread_bootstrap_return)`
(`osfmk/kern/thread.c:1426/1434/1511`). `thread_bootstrap_return` is defined in
**`osfmk/arm/locore.s:1902`** — a file this image replaces wholesale — so it is one of the 27 stubs.
The thread does not run during `bsd_init`; it runs when the scheduler first switches to it, and by
then the caller is `Call_continuation` and not a `bl` in any walk. `tools/xnu_entry_callwalk.py
--root kernel_bootstrap_thread` reports **no stub on the straight-line path** and lists
`thread_create_internal+0x358: blx r1` among the indirect calls it cannot follow — that is this call,
and the report is correct about the code and silent about the switch.

**(4b) No stop at all: `vm_pageout` reaches its idle loop** and the run ends with no `stub_hit=`, no
`exception:` and no `panic:`, with the hardware watchdog and not a stub being what ends it. This is
the branch if the pageout thread blocks before the init thread is scheduled, or if `vm_pageout`
never blocks.

**5. Layout.** Stub count 42 → 27 (−15). The six objects' `.text` sums to **27420** (`bpf` 15172,
`bpf_filter` 3224, `ether_if_module` 2608, `ether_inet_pr_module` 2420, `ether_inet6_pr_module` 840,
`if_loop` 3156); the 15 retired stubs are 15 × 0x18 = 360 bytes of `.text` and their name strings
leave `.rodata.str1.4`; `check_stub_kinds.py` should report `0 storage` stand-ins of the wrong kind
and two fewer `KNOWN_KINDS` rows than 439. Predicted image: text **above** 4934624 by the six
objects, `.bss` by the six objects' `.bss` (84) plus two fewer 64-byte stand-in slots.

**Falsifiers, named in advance.** A `stub_hit=` inside the walk again (the array or one of entries 5
and 6 not real); a `stub_hit=` at one of `IOKitBSDInit`'s C++ indirect calls
(`IORegistryEntry::fromPath`, `OSMetaClassBase::safeMetaCast` — the walk lists 676 of them); an
`exception:` with `abort_entries>0`, which would mean a fault rather than a missing symbol and would
most likely be in the `ml_*`/`pmap`/`zalloc` path, since **the timer is still owed**
(`ml_init_timebase` + an MSM8974 `tbd_ops_t`) and `vm_pageout`'s first five instructions already call
`ml_set_interrupts_enabled`; or the run reaching `vm_pageout` and the log simply stopping.

## The measurement: the manifest, the counts, and the A/B that names the retired symbols

**The device set, read from the configuration, is eight for RELEASE and three for STAGE90_BOOT:**

```
./tools/xnu_config/devices.py RELEASE
  ether      pseudo-device  d_slave=UNKNOWN  header NETHER=1    init=-
  loop       pseudo-device  d_slave=UNKNOWN  header NLOOP=1     init=-
  pty        pseudo-device  d_slave=16       header NPTY=16     init=pty_init
  ptmx       pseudo-device  d_slave=1        header NPTMX=1     init=ptmx_init
  mdevdevice pseudo-device  d_slave=1        header NMDEVDEVICE=1 init=mdevinit
  bpfilter   pseudo-device  d_slave=4        header NBPFILTER=4 init=bpf_init
  fsevents   pseudo-device  d_slave=1        header NFEVENTS=1   init=fsevents_init
  random     pseudo-device  d_slave=1        header NRANDOM=1    init=random_init
```

`ether` and `loop` have no number, which is `d_slave == UNKNOWN`, and both counts are 1 — by the two
different rules, which is the only reason the module returns the raw value.

**The manifest delta is exactly six sources, and it is a diff of two runs of the same tool rather
than a claim about the code.** `list_sources.py RELEASE --device-table <the 439 table>` selects
**722** sources, 680 present on disk; with the derived table it selects **728**, 686 present:

```
bsd/net/bpf.c                     bsd/net/ether_if_module.c      bsd/net/ether_inet6_pr_module.c
bsd/net/bpf_filter.c              bsd/net/ether_inet_pr_module.c bsd/net/if_loop.c
```

**The counts.** The kernel build reports `621 tried / 620 compile / 1 fail` (`osfmk/kperf/kperfbsd.c`,
not on the boot path) and `83/83` C++ — 703 objects in `out/xnu_kernel_obj`. The entry link reports
**431 objects added, 286 already named above, refused by name: `iokit_KernelConfigTables.o locore.o
start.o`** (439: 424 and the same three refusals). And the stub set goes **42 → 27** — each figure
the build's own report, not a count of anything else:

| | 439 | 440 |
| --- | --- | --- |
| undefined | 42 | **27** |
| stubs | 42 function, 0 storage | **27 function, 0 storage** |
| objects added to the entry link | 424 | **431** |
| `.text` | 4934624 | **4995104** |
| image bytes | 5141640 | **5192212** |
| `.bss` | — | 0x804f3a40 .. 0x80544a18 (331736) |
| headroom | 1865416 | **1816040** |

**Which 15 names were retired: five of them by measurement, the rest bounded.** The 15 is arithmetic
(42 − 27). Five are *named* by comparing the two builds' own reports — `build_entry.sh` runs
`check_stub_kinds.py`, which prints its object-declared list and its unclassified list in full:

```
in 439's unclassified list, gone in 440:   bpf_init, bpfkqfilter, sysctl__net_link_ether_children
in 439's KNOWN_KINDS, deleted in 440:      etherbroadcastaddr, lo_ifp
```

and the other ten are among the 14 names 439's check counted as *declared in a header as functions*
plus the 3 the pool defines — which that check prints as counts and never as names.

**The A/B, and it is 1.57 seconds because nothing recompiles.** Move the six objects out of
`out/xnu_kernel_obj` and rebuild the entry:

```
  46 symbol(s) undefined - see …/xnu_arm_entry_undef.txt
stub kinds are not safe for 2 name(s):
  'etherbroadcastaddr' is declared as an OBJECT (bsd/net/ethernet.h:131) …
  'lo_ifp' is declared as an OBJECT (bsd/net/if_var.h:1402) …
```

Both halves of that are results. The undefined set is **46**, and its difference from 27 is the
**19 names the six objects supply** (measured, exact):

```
bpf_attach bpfattach bpfdetach bpf_init bpfkqfilter bpfread_filtops bpf_tap_in bpf_tap_out
ether_add_proto etherbroadcastaddr ether_check_multi ether_del_proto ether_demux
ether_family_init ether_frameout_extended ether_ioctl lo_ifp loopattach
sysctl__net_link_ether_children
```

and the build **refused to link** rather than emit a function stand-in for two objects — which is
`check_stub_kinds.py`'s both-direction table firing a *second* time, on demand, on the two rows 440
had just deleted. (The A/B is deliberately not a 439 rebuild: the device headers changed too, so
`bpfread_filtops` — the hand-written stand-in 440 retired, so not a stub in either build's list —
and `loopattach`/`ether_family_init` (behind `NLOOP 0`/`NETHER 0` in 439) are in the A/B's 46 and
could not have been in 439's 42. That is the four-name gap between 46 − 27 = 19 and 42 − 27 = 15,
and one of the four is not identified; the honest bound is that 15 of the 19 are the retired set and
5 of those 15 are named above.)

**And the one prediction that missed is the one worth recording.** The prediction said text would
rise *by the six objects*. Measured: **+60480**, of which the six objects' own `.text` is **27420**
(`bpf` 15172 + `bpf_filter` 3224 + `ether_if_module` 2608 + `ether_inet_pr_module` 2420 +
`ether_inet6_pr_module` 840 + `if_loop` 3156) and the 15 retired stubs give back 360 (15 × 0x18).
**+33420 is not the six objects**, and the cause is measurable even though the A/B cannot separate
it: the device headers changed four values (`NBPFILTER 0 → 4`, `NETHER 0 → 1`, `NLOOP 0 → 1`,
`NPTY 1 → 16`), and objects that are *not* among the six compile `#if NBPFILTER > 0` bodies they
previously dropped — 18 `NBPFILTER` sites, 8 `NLOOP`, 4 `NETHER`. Those objects are already compiled
by the time the entry link runs, so the A/B is blind to them by construction. The decomposition stops
at +27420 − 360 and the remainder is recorded as open rather than attributed.

## The measurement: on hardware, no stub at all — and XNU panics in its own device-tree walk

The payload is `out/stage90/stage90-qcdt.img`, **8212480 bytes**, sha256
`f80dc972926ef143a15e8aaf4828960c50136282e0fee7d5d5368a3754097c37`, booted non-persistently through
both gates (`fastboot boot`, no flash). The log is 302922 bytes / 4008 lines, ending
`No errors detected`; 25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`,
and the device returned to Android on its own.

**There is no `stub_hit=` line in the log, and that is the step's central result.**
`xnu_entry_stub_caller_v=0`, `_digits=0`, `_e=0` — the fields exist and are zero, so the walk did not
stop on a stand-in. The prediction's premise (2) and (3) hold: the array ran to its `{0,0}`
terminator and `bsd_init`'s remaining statement list executed.

**What stopped it is an exception, and the instruction is XNU's own deliberate panic trap:**

```
xnu_entry_undef_lr=0x8002dd8c   xnu_entry_undef_pc=0x8002dd88   xnu_entry_undef_spsr=0x60000093
xnu_entry_why=0x80454270  -> "exception: undefined instruction"
xnu_entry_abort_entries=0x00000000   (every xnu_entry_abort_first_* is zero: no abort)
```

`nm` puts 0x8002dd88 inside **`DebuggerTrapWithState`**, and the disassembly is unambiguous — the
trap is the instruction the function deliberately executes after `DebuggerSaveState` returns:

```
8002dd60 <DebuggerTrapWithState>:
8002dd84:	bl	8002ddc8 <DebuggerSaveState>
8002dd88:	e7ffdefe 	udf	#65006	; 0xfdee      <- the trapped instruction
```

`udf #0xfdee` is `osfmk/kern/debug.c:371`, reached from `panic()` at `:645` and `Debugger()` at
`:458`. **The kernel panicked.**

**The message is read out of `r9`, the instrument's established route.** `xnu_entry_trap_r9_fmt =
0x8045bf97`, and that address is the first byte of the literal

```
8045bf97  "Device tree property overflow: prop %p, length 0x%x\n"
```

which is `pexpert/gen/device_tree.c:56`, inside `next_prop()`:

```c
static inline DeviceTreeNodeProperty* next_prop(DeviceTreeNodeProperty* prop)
{
	uintptr_t next_addr;
	if (os_add3_overflow((uintptr_t)prop, prop->length, sizeof(DeviceTreeNodeProperty) + 3, &next_addr))
		panic("Device tree property overflow: prop %p, length 0x%x\n", prop, prop->length);
	next_addr &= ~(3ULL);
	return (DeviceTreeNodeProperty*)next_addr;
}
```

The trap's other registers agree with the instrument's own table, so the block is not shifted:
`r8 = 0xc2113d80` is `panic_args`, `frame_r8` equals it, `frame_r9 = 0x8002dd8c` equals `undef_lr`,
`frame_r4 = 0` is `db_panic_options`'s low half, `frame_r5 = 1` is `db_proceed_on_sync_failure`, and
`trap_sl_options_hi = 0`. `xnu_entry_panic_str/_caller/_message` and
`panic_ap/_element/_zonename` are all zero, which is the known behaviour: the debugger's cached panic
state is cleared before the trap (`debug.c:905` then `:947`), and the `va_list` read is refused by
the image guard. **The caller of `panic` is not recoverable from this log** — the instrument's own
comment says so, and `panic_trap_to_debugger` overwrites the register that used to carry it.

**The call site is recovered from the image instead, and the chain has one candidate at the end of
it.** `next_prop` is `static inline`, so its panic is inlined into each of its four callers, and each
one materializes the same literal — which is how they can be found: search the disassembly for the
only four `movw …, #49047` / `movt …, #32837` pairs in the image.

```
0x80005598  DTLookupEntry+0x50
0x800058d0  DTIterateEntries+0x6c
0x80005a8c  DTIterateProperties+0x60      <- the only caller is 0x80160d3c
0x80005ae8  skipTree+0x20
```

Three of the four are on paths earlier runs already walked to completion — `pe_identify_machine`
calls `DTLookupEntry` and `DTIterateEntries`, `ml_parse_cpu_topology` calls `DTIterateEntries`,
`PE_get_default` and `PE_get_random_seed` call `DTLookupEntry`. **`DTIterateProperties` has exactly
one caller in the whole image**:

```
80160d3c: bl 80005a2c <DTIterateProperties>      ->  _ZL18MakeReferenceTableP13OpaqueDTEntryb+0x98
```

`MakeReferenceTable(OpaqueDTEntry, bool)` is `iokit/Kernel/IODeviceTreeSupport.cpp:344`, and it is
called only from `IODeviceTreeAlloc` (`:96`), which is called only from
`IOPlatformExpertDevice::initWithArgs+0x14` (`IOPlatformExpert.cpp:1565`, `dtTop &&
(dt = IODeviceTreeAlloc(dtTop))`), which IOKit reaches from `IOKitBSDInit` — the call `bsd_init`
makes at **`+0x880`, the first statement after the `pseudo_inits` walk returns**:

```
8003b25c: e12fff31  blx	r1                 <- the walk, 439's stop
8003b270: eb071de5  bl	80202a0c <IOKitBSDInit>
```

So the causal chain this run finally executed is

```
bsd_init+0x86c  pseudo_inits walk  (completes: 6 entries, {0,0} terminator)
bsd_init+0x880  IOKitBSDInit
                IOPlatformExpertDevice::initWithArgs+0x14
                IODeviceTreeAlloc
                MakeReferenceTable+0x98
                DTIterateProperties  ->  next_prop  ->  panic
```

and **what it found is a property whose `length` field cannot be a length.** The walk is *exact*
about this: `next = (prop + length + 11) & ~3`, which equals `prop + 8 + round_up(length, 4)` for
every `length` — so the panic is not an alignment convention this project got wrong, it is a word
that is not a length at all. `DTIterateProperties` advances at most `entry->nProperties` times, so
the drift is inside one node's own property list, and the DT is one this project builds itself
(`stages/stage90/apple_dt.c`, handed over at `deviceTreeP=0x006083a4`, `deviceTreeLength=0x00007358`,
4 root properties and 0x15 root children).

**A second first, in the same run and easy to miss:** `xnu_entry_zone_map_min=0xc056f000` and
`xnu_entry_zone_map_max=0xc07ad000` are **non-zero**. The instrument's own comment says both read
zero because `zone_init` has never run in this image. The zone allocator is up, and the two values
are in the 0xC0000000 region the base change of experiment 241 predicted.

**What the prediction got right and wrong, stated plainly.** Predicted: no stub inside `bsd_init`
(4) ✓; no reachable indirect call left after the walk, because `mountroot_post_hook` is `NULL` ✓;
the walk completing ✓; either a stub through a context switch (4a) or no stop at all (4b). Measured:
**neither** — an exception, which the falsifier list did allow but with the wrong cause guessed
(`ml_*`/`pmap`/`zalloc`, the owed timer). `stub_hit=thread_bootstrap_return` (4a) did not happen: the
panic arrives *before* any context switch, inside `bsd_init`'s own line, at the first call after the
walk. The frontier did not move by one object; it moved from *a missing symbol* to **a malformed
value in data this project hands the kernel** — which is the one of
[[mi4-stub-walk-frontier-kinds]]'s three kinds that no name in the undefined list can express.

## Readings

| | |
| --- | --- |
| configuration-declared devices | RELEASE 8, STAGE90_BOOT 3 |
| device table rows | 4 → 10 (8 devices + 2 `OVERRIDES` non-devices) |
| device headers | 5, values now `NBPFILTER=4 NETHER=1 NLOOP=1 NPTMX=1 NPTY=16` (was `NBPFILTER 0`, `NETHER` undefined, `NLOOP 0`, `NPTY 1`) |
| option headers | 89 flat, 31 on; the device half removed from the option generator |
| manifest | 722 → **728** selected, 680 → **686** present on disk (+6, named above) |
| kernel compile | 621 tried / 620 compile / 1 fail (`osfmk/kperf/kperfbsd.c`), 83/83 C++, 703 objects |
| undefined symbols | 42 → **27** (0 storage by `nm`, 3 defined by the pool, 24 with no pool definition) |
| kind check | 2 object-declared, both in `KNOWN_KINDS` (was 4); 18 unclassified (was 21) |
| entry objects added | 424 → **431**; refusals unchanged (`iokit_KernelConfigTables.o locore.o start.o`) |
| the A/B (six objects removed) | **46** undefined, and the build refused by `check_stub_kinds.py` on `etherbroadcastaddr` and `lo_ifp`; 19 names supplied by the six objects |
| `check_device_conditions.py` | 8 declared, 8 on, 5 headers, 4 macros read by the tree all defined, 6 array entries agree; emitted but read by nothing: `NPTMX` |
| `Device tree` properties | root 4 props / 0x15 children; length 0x7358; handed over at PA 0x006083a4 |
| stop | `udf #0xfdee` at `DebuggerTrapWithState+0x28`, message `next_prop`'s overflow panic |
| stub hits | **0** — `xnu_entry_stub_caller_v = _digits = _e = 0` |
| aborts | `abort_entries=0`, every `abort_first_*` zero |
| zone allocator | `zone_map_min=0xc056f000`, `zone_map_max=0xc07ad000` — **non-zero for the first time** |
| safety | `fastboot boot` only; 25 × `persistent_write_attempted=0`; 87 × `failure_mask=0`; `checks=5` / `failures=0`; `checksum=0x90702a09`; no `panic:` line; log ends `No errors detected`; device back on Android on its own |

## Where the frontier is now

The stop is no longer a symbol. It is `pexpert/gen/device_tree.c:56`'s overflow check, fired from
`DTIterateProperties` inside IOKit's `MakeReferenceTable`, on a tree `stages/stage90/apple_dt.c`
builds — and that makes the next step a **host-side** one, which is the kind this project prefers:

1. build `apple_dt.c` for the host (it is plain C, and it is already compiled to
   `out/stage90/apple_dt.o`), dump the blob, and replay XNU's own walk over it —
   `next = (prop + length + 11) & ~3`, for `nProperties` steps per node — and report the first node
   where the pointer leaves the node. That turns "a word is not a length" into "this property of
   this node, at this offset".
2. the strongest prior, and it is findable in the source: `MakeReferenceTable` iterates properties of
   the root and of **every** entry, so this is the first exhaustive property walk the tree has had,
   while `pe_identify_machine`'s walks are targeted. A property whose value length is not what the
   payload wrote — a node whose `nProperties` is one too many, a name pointer that is not a pointer,
   or a terminator that is not there — is invisible to a targeted lookup and fatal to this one.

Still owed, unchanged and now behind this: the timer (`ml_init_timebase` + an MSM8974 `tbd_ops_t`
over the GPT at 0xf9020000, 19.2 MHz, IRQ 19) and 405's `IOCPUInterruptController`; the pthread
table's other 38 slots; and `osfmk/kperf/kperfbsd.c`, the one file that does not compile and is not
on the boot path.

