# Experiment 487 — two planes, one state

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/entry_trace.c`, `entry_stubs.c`, `build_entry.sh`,
`tools/check_driver_plane_census.py` (new), `tools/check_boot_completion.py` (two mutations)

**Result: the drivers ran, and the step's prediction was falsified by its own run.** The tree's 21
top-level nodes are no longer the objects `IODeviceTreeAlloc` made. Every one of them is an
**`IOPlatformDevice`** — the nub `IODTPlatformExpert::createNub` makes
(`IOPlatformExpert.cpp:1279-1290`: `new IOPlatformDevice` at `:1283`, then `nub->init( from, gIODTPlane )` at `:1285`, which
is the *adoption* 486 found one class up). The second plane is the same object as the first census's
root, and it holds exactly two children.

| reading | object | class | how the class is named |
| --- | --- | --- | --- |
| the ODT census's 21 children | 21 distinct addresses | **`IOPlatformDevice`** | vptr `0x804a0268` = `_ZTV16IOPlatformDevice` (`0x804a0260`) + 8, in **21 of 21** records |
| the ODT census's root | `0xc05590a0` | **`IOPlatformExpertDevice`** | vptr `0x8049fed8` = `_ZTV22IOPlatformExpertDevice` (`0x8049fed0`) + 8 |
| 485's recorded tree root | `0xc058ef78` | **`IORootParent`** | vptr `0x8049e088` = `_ZTV12IORootParent` (`0x8049e080`) + 8 |
| service-plane child 0 | `0xc0591740` | **`MSM8974PlatformExpert`** | vptr `0x804bb96c` = `_ZTV21MSM8974PlatformExpert` + 8, metaclass `0x8057e80c` = `MSM8974PlatformExpert::gMetaClass` |
| service-plane child 1 | `0xc058ef20` | **`IOResources`** | vptr `0x8049a544` = `_ZTV11IOResources` + 8, metaclass `0x805749d8` = `IOResources::gMetaClass` |

The 8-byte name truncation the instrument publishes (`dtcc_c0/c1 = 0x6c504f49/0x6f667461` = `IOPlatfo`)
cannot separate `IOPlatformDevice` from `IOPlatformExpertDevice` — both begin `IOPlatfo`. The vptr can,
and does: the 21 children's vptr is the vtable `nm` resolves to `IOPlatformDevice`, the root's is the
one it resolves to `IOPlatformExpertDevice`, and they are different addresses.

**And the step said the opposite in advance.** Its design note predicted `IOService` for all 21 children
— "the fixture's nodes name `IOClass` values that do not exist in this kernel, so no concrete
`IODTPlatformExpert` subclass exists for `configure` to run in" — and an empty service plane. The
premise was wrong: this kernel *has* a concrete `IODTPlatformExpert` subclass,
`stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp`, and the fixture's own personality
(`/msm8974-platform-driver`, `IOClass = MSM8974PlatformExpert`, `IOProbeScore = 0x650`) makes it the
root nub's driver. `configure` therefore runs, and the nub pass with it.

## What 485 and 486 left, and why the *child's* class is the question

485 asked what the 21 children of the tree's root are, and answered it three ways that its own log
could not separate: the objects it read were named by pointer only. 486 named the *roots* — the
registry's IODT child is an `IOPlatformExpertDevice`, the entry 461 recorded out of
`IODeviceTreeAlloc` is by then an `IORootParent` — and left the children unmeasured. 487's question is
the one the goal actually cares about: **the children are what the driver layer replaces**. Apple's
device-tree platform expert makes one nub per top-level entry:

    IODTPlatformExpert::configure     -> processTopLevel( provider )      IOPlatformExpert.cpp:1269, :1274
    IODTPlatformExpert::processTopLevel-> createNubs( this, ids )         IOPlatformExpert.cpp:1314
    IODTPlatformExpert::createNubs    -> nub->attach( parent );           IOPlatformExpert.cpp:1293, :1305
                                         nub->registerService();
    IODTPlatformExpert::createNub     -> new IOPlatformDevice;            IOPlatformExpert.cpp:1279, :1283
                                         nub->init( from, gIODTPlane );

so a child named `IOService` in the ODT plane is a node the driver layer has not touched, and a child
named `IOPlatformDevice` is a nub that exists *because* `configure` ran. The class name is the whole
answer, which is why 486's instrument — the vtable slot, the preamble guard, the metaclass name — is
reused here unchanged rather than reinvented.

**What was not known, and is what makes the reading worth taking:** 485's census and 486's both walked
the *same* child set (`0xc05d13a0` in 486's build) and found it holding the 21 names. A census of names
cannot see a nub pass: the nub adopts the node's position and its name in the plane it is initialized
in (`nub->init( from, gIODTPlane )` is `IORegistryEntry::init( old, plane )`,
`IORegistryEntry.cpp:333-385`, 486's mechanism), so the children's *names* are identical before and
after. Only the class moves.

## The instrument

`entry_probe_service_plane` is the same census as `entry_probe_dt_children` with a different plane
global — `gIOServicePlane` instead of `gIODTPlane`, two globals that the image keeps at
`0x80574a08` and `0x80575144` and that the run reads as the plane objects `0xc0557758` and
`0xc05577f8` — the same four accessors (`getChildCount`, `getChildSetReference`, the child set's
`getCount`/`getObject`, `getName(plane)`), the same `STAGE90_DTK_STATE0_OFF + 4u` state word, and the
same 24-child cap. `tools/check_driver_plane_census.py` is what holds the two to one shape: it reads
both bodies out of `entry_trace.c` and fails the build unless the six accessor calls, the state-word
expression, the two counts and the caps agree, and unless neither census re-spells the accessors
directly. A second census written "the same way" is a claim about a file until something compares the
two.

Three things the second census adds beyond a second plane:

- **The root is handed over, not re-derived.** `entry_probe_dt_children` now *returns* the root it
  walked, and the service census takes it as an argument and publishes `same = (root == iokit_root)`.
  Apple's own construction says the two must be one object: `IOService::attach( 0 )`
  (`IOService.cpp:663-666`) sets `gIOServiceRoot = this` for the object `IOStartIOKit.cpp:157` calls it
  on, and that object is the `IOPlatformExpertDevice` the same function made at `:153`. Two censuses of
  two planes would otherwise be two readings that agree by luck.
- **The class goes through the object's own vtable** — `entry_class_words( STAGE90_CLS_CHILD, … )` and
  `entry_class_words( STAGE90_CLS_SVCCHILD, … )`, the 486 road, with the preamble guard 486's run B and
  run C paid for. The sites are `#define`s with a distinct number each (1 recorded walk, 2 root, 3
  recorded root, 4 ODT child, 5 service child) and the check fails the build if two of them are equal or
  if one appears in the other's loop: the site is the only thing that says which reading a record is.
- **The child's class is a record of its own.** `entry_note_dtchildcls` and `entry_note_svcchild` are
  new writers rather than two more arguments on 485's `entry_note_dtchild`, because that record's shape
  is 485's claim and a step does not re-open a previous step's claim to carry a new number.

## The run

One run, through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`, exit 0, device back on Android on its own, 534 988 bytes and
7 082 lines in `/tmp/stage90-487-run1.log`. The console text is byte-identical to 486's run D at the
same lines (`Added memory device md0/rmd0 …`, `BSD root: md0`, `load_init_program: attempting to load
/sbin/launchd` at lines 3988-3993 of both), the run ends with `No errors detected`,
`persistent_write_attempted = 0x00000000` 25 times, `failure_mask = 0x00000000` 87 times,
`entry_checks = 0x00000000`, `entry_failures = 0x00000000`, `entry_checksum = 0x9071ec79`,
`xnu_entry_panic_entered = 0x00000000`, and four data aborts all handled (`xnu_entry_sleh_seq = 4`,
each with `sleh_frame_ok = 0x00000001` and `sleh_back`/`sleh_at_back` reaching its own sequence,
`sleh_storm = 0x00000000`).

**One number that does not reconcile, recorded because it was this run's own check that noticed.** 486's
doc and its memory both write "entry checks 5 / failures 0" for its run D. 486's log carries
`entry_checks = 0x00000000`, `entry_failures = 0x00000000` and `entry_checksum = 0x9071e9b9`, and no key
whose name contains `checks` with the value 5 (`high_kernel_callout_status_checksum = 0x00000005` is a
checksum, not a count). Where that 5 came from is not visible in the log this step re-read; 487's own
numbers are the keys above and nothing here is carried between runs by prose.

The two censuses, as the log carries them:

| | ODT plane | service plane |
| --- | --- | --- |
| plane object | `0xc05577f8` (`gIODTPlane`) | `0xc0557758` (`gIOServicePlane`) |
| root | `0xc05590a0` | `0xc05590a0` — **`svc_same = 1`** |
| child set | `0xc0596520` | `0xc05ac600` |
| `getChildCount` / set's `getCount` | 21 / 21 | 2 / 2 |
| shown / named | 21 / 21 | 2 / 2 |
| matched / registered | 21 / 21 (485's fields) | 2 / 2 |
| classes | 21 × `IOPlatformDevice` | `MSM8974PlatformExpert`, `IOResources` |
| `__state[0]` of every child | `0x1e` | `0x1e` |

485's `recorded` pointer is carried beside it: `dtk_recorded = 0xc058ef78`, class **`IORootParent`**,
0 children, no child set — 486's reading, re-measured in a new build.

**The class instrument's arithmetic closes.** The live channel holds 32 `cls_*` records
(`xnu_entry_cls_calls = 0x20`), and the sites account for exactly 32 reads: 7 walk records + 2 roots +
21 children + 2 service children. `xnu_entry_live_records = 0x90e` (2318) against the channel's 4096
with `xnu_entry_live_refusals = 0x00000000`, so no record in this run was dropped for want of room —
the reading is complete rather than sampled.

## What the run says, and the one place it is an argument

**The nub pass ran, over all 21.** Not "some": every one of the 21 child records carries the same vptr,
`0x804a0268`, and 21 distinct objects sharing one vptr is 21 instances of one class. The class's name is
`IOPlatformDevice` (`nm`: `_ZTV16IOPlatformDevice` at `0x804a0260`), the class Apple's `createNub` news,
and its metaclass is the one whose `getMetaClass` the image keeps at `0x8027d770`/`0x8027d938` for the
`MSM8974*` classes and at `0x80175480` for `IOPlatformExpertDevice` — the reads went through the
object's own vtable slot, which is why a class that overrides `getMetaClass` is still named correctly.

**The two service children are the two objects Apple's own code puts there.**

- `0xc0591740`, class `MSM8974PlatformExpert`, is *this kernel's* platform expert instance — the one
  whose `start` ran `configure`. Its provider is the root nub, so the match path attached it under the
  root in the service plane (`IOService.cpp:3334`, `inst->attach( this )`). An independent instrument
  agrees it exists: the earlier walk's `applyToInstancesOfClassName( "IOPlatformExpert" )` record
  (`xnu_live_wcls_seq = 2`) yields **one** instance at `0xc0591740` with `__state[0] = 0x1e`, taken
  during matching, long before this census.
- `0xc058ef20`, class `IOResources`, is `gIOResources` — and that is not an inference either: the same
  earlier instrument publishes `wcls_rsvc = 0xc058ef20`, and `IOService::getResourceService()`
  (`IOService.cpp:1181`) returns `gIOResources`. Apple's `IOService::setPlatform` attaches it exactly
  here: `gIOResources->attachToParent( gIOServiceRoot, gIOServicePlane )` (`IOService.cpp:1187`).

**Why the children's names and classes read identically is Apple's fallback, not a defect in the
instrument.** `entry_xnu_entry_name` for both service children returned the same eight bytes as their
class — the check's own selftest is what would have caught a duplicated accessor, and did not — because
`IORegistryEntry::getName( plane )` ends with `return( (getMetaClass())->getClassName() )`
(`IORegistryEntry.cpp:749-764`) when the plane's name key is absent, which it is for an object that was
adopted into the *ODT* plane and never named in the service plane. The reading is therefore "these two
objects have no service-plane name", which is a fact about the adoption, and two reads agreeing because
one of them *is* the other's fallback is the kind of agreement that has to be explained rather than
counted.

**What is argument rather than reading:** that the 21 nubs were created by *this* `createNubs` pass
rather than by some other road to `new IOPlatformDevice`. Nothing here watches the allocation — the
instrument reads the object's vtable, and the vtable says which class it is, not who called `new`. What
supports the mechanism is the shape of the answers around it: the platform expert instance exists and is
attached where the match path puts a client, `gIOResources` is attached where `setPlatform` puts it, the
tree root's block is already `IORootParent`, and `createNub` is the only `new IOPlatformDevice` in the
image's IOKit sources (`IOPlatformExpert.cpp:216` is `IOPlatformExpert::createNub` for a dictionary, not
for a device-tree entry). 488's way to turn it into a record is named below.

**And a boundary with 462, which this sharpens rather than answers.** `StartIOKit` is called from
`PE_init_platform` (`pexpert/arm/pe_init.c:279`), which `arm_init.c:159` calls very early — long before
`IOFindBSDRoot` (`bsd_init.c:1099`, reached from `IOKitBSDInit`). So both the adoption and the nub pass
are *already done* at the moment 461 measured `fromPath( "/chosen" )` returning 0 at the first
component. "The children had not been made yet" is therefore not the reason, and the two moments that
461 separated are both after the tree reached the shape this census reads. 462's instrument — the
control `fromPath` on the same plane at the same moment, plus `getRegistryRoot()` and
`getChildEntry(plane)` — is what is left.

## Defects this step's own session found

Four, in `mi4-measurement-defects.md` (247–250). None of them is in the device run; three are in the
checks that were supposed to be gating it, which is the pattern this repository keeps re-finding: **a
check is a measurement too, and its own selftest is the only thing that measures it.**

- **247 — a mutation that names its neighbour instead of the line it is about.** 485's selftest mutated
  `"    entry_probe_dt_children();\n    __real_vm_pageout();"` — two adjacent lines — and 487 put the
  second census between them, so the needle matched nothing and `_bump`'s assertion aborted the build on
  an image that was right. The claim itself was fine (it still reads "the census is between the record
  and the call", and still holds). Fixed by making the two mutations move and drop the *line* holding
  `entry_probe_dt_children();`, which is the thing the claim is about.
- **248 — two mutations that stopped mutating, silently.** `the_second_state_word_moves_a_word_back`
  replaced the **first** `STAGE90_DTK_STATE0_OFF + 4u` while the claim is an existence test over the
  whole file, and 487's second census added a second spelling — so the mutation edited the census and
  left the claim standing. `the_site_word_is_not_the_function` patched the literal `movw r1, #7924`,
  and 487's code in `__wrap_vm_pageout` moved that constant, so the patch replaced nothing. Both were
  **accepted** — the selftest reported them as `ACCEPTED:` and the only reason they were caught is that
  it reports acceptances at all. Fixed by replacing every spelling and by finding the `movw` by the
  value it materialises (`symbols["vm_pageout"] & 0xFFFF`) rather than by its line. **A mutation that
  matches nothing is a mutation that proves nothing, and it looks exactly like a passing check.**
- **249 — the recorded ritual and the build's own cross-check disagreed.** The four-step build as this
  project records it runs step 2 as `./tools/gen_assym.sh` and `./tools/assemble_arm_layer.sh`; both
  default `XNU_KERNEL_CONFIG` to RELEASE, and a run without the variable in the environment therefore
  assembled the ARM layer against **RELEASE's** `assym.s`. Step 3 refused the build and said why
  (`TH_CTH_SELF 1496` in the configuration's `assym.s` against `#1480` in `cswitch.o`) — the check
  worked, and what was wrong was the instruction. Every step below is run with the variable set.
- **250 — the `assym.s` generator's flag list is a copy that has drifted, and what it now produces is
  RELEASE's offsets.** `tools/gen_assym.sh` says of its own flags: "Kept in step by intent rather than
  by construction, which is the one place in this pipeline where a divergence would be silent". It is
  not silent any more, and the divergence is not the one that comment foresaw:
  - `XNU_KERNEL_CONFIG=STAGE90_XNU ./tools/gen_assym.sh` **exits 2** — `genassym.c did not compile:
    btlog.h:88: error: unknown type name 'leak_site_proc'`. Its flat force-include list defines
    `DEVELOPMENT 1` (`out/xnu_options/STAGE90_XNU/development.h`, reached from the *flat*
    `meta_features.h`), but four of the headers it force-includes *before* that — `kern/queue.h`,
    `kern/ast.h`, `mach/task_policy.h`, `arm/simple_lock.h` — reach `kern/debug.h`, whose include guard
    then latches with `DEVELOPMENT` undefined, so `btlog.h`'s guarded declaration is kept while
    `debug.h`'s guarded typedef is not. Measured by bisecting the `-include` list (each of the four
    reproduces it) and by moving `-include meta_features.h` to the front, which compiles.
  - **What it would produce if it compiled is the wrong triplet.** With that one edit the generator's
    `TH_KSTACKPTR / TH_CTH_SELF / TH_CTH_DATA` come out `1464 / 1480 / 1488` — which is exactly what
    RELEASE's `assym.s` says, and exactly the offsets that experiment 468's device run died on
    (`machine_load_context` reading `[r0,#1464]` for `TH_CTH_SELF`, five `sleh_abort`s with
    `pc = machine_load_context+0x30`). The configuration's `assym.s` on disk says `1480 / 1496 / 1504`,
    and so does the kernel's own compiled object: `out/xnu_kernel_obj/osfmk_arm_machdep_call.o`'s
    `thread_get_cthread_self` is `ldr r0, [r0, #1496]`, i.e. **the kernel's C objects and the ARM layer
    agree, and the generator no longer reproduces the file they agree on.** The stale artifact is
    correct and the tool that wrote it is not, so `out/xnu_assym/STAGE90_XNU/assym.s` (11:45) is what
    this build depends on — and nothing would notice if a regeneration replaced it with RELEASE's
    numbers, because `check_assym_cswitch.py` compares the ARM layer against that same file. 488's
    first job is named below.

## Build, layout, and artifacts

| | 486 D | 487 |
| --- | --- | --- |
| `.text` | 5248352 | **5254568** |
| image bytes (`xnu_arm_entry.bin`) | 5470836 | **5470836** (unchanged) |
| `.bss` | 360072 | **361032** |
| headroom | 1509112 | 1508152 |
| undefined / wraps | 26 / 59 | 26 / 59 |

`.text` grew by 1440 bytes — the second census, the per-child class records and the site `#define`s —
and the image did not move, the `.data` 16 KB alignment absorbing it (284's lesson, unchanged). Entry
image `xnu_arm_entry.bin` sha256
`43524b4f130ee4e8e90437f820dd9bfb92e9c4148e24452c951ccc2f687dc81a` (md5 `079dddc52dd68f3f82bcec8452765759`;
`.elf` 6638204 bytes sha256 `4e2efd849c17eba594a088aca239dbeb1add50c34524e50d367879d5ef408130`, text
5254568, data 206796, bss 361032). Payload `stage90-qcdt.img` 8491008 bytes sha256
`897b9dbfce7355e620597d213e340f229af3ea7a70e049a65214b3784971977d` (`stage90.bin` 5966100 sha256
`56b3d7c087085c81de3a875d63f38ea1e7017211d347db1e829c93b4e0cc73f9`), `kernel_size = 5966100`,
`dt_size = 2521088`. Build lines, in order, all green: `check_undef_handler.py --split` (61 mutations
refused), `check_timebase_registration.py` (12), `check_gic_routing.py` (43), `check_irq_routing.py`
(36), `check_timer_sources.py` (42), `check_boot_completion.py` (**52**, two of them rewritten by 247
and 248), `check_registry_adoption.py` (66), `check_driver_plane_census.py` (**46**, new).

One run, through the gate, exit 0, device back on Android on its own: 534 988 bytes, 7 082 lines.

## What 488 has to do

**The generator's flags, which is now a correctness item and not tidiness.** `tools/gen_assym.sh`'s
force-include set, option-header paths and per-component slice are a hand-kept copy of
`build_xnu_arm_kernel.sh`'s and have diverged (250). The fix is to derive one from the other rather than
keep two lists, and to stop the divergence being invisible: **one `TH_*` offset, pinned against a
*kernel object*'s disassembly** — `thread_get_cthread_self`'s `ldr r0, [r0, #1496]` is the pattern, and
it is the same shape as `check_saved_state_offsets.py`'s "four sources at once". `assym.s` is currently
checked against the assembler that reads it and against nothing that compiles C.

**The nub pass as a record rather than a shape.** The platform expert instance `0xc0591740` is a child
of the service root, and `createNubs` attaches its 21 nubs *to the instance* (`nub->attach( parent )`
with `parent = this`), so a third census — the instance's service-plane children — should find 21
`IOPlatformDevice`s under it. Reading that is the difference between "21 nubs exist" and "these 21 nubs
were made by this pass", and it is one more census of the same shape, taken from a child the run already
found rather than from a global.

**And, unchanged: the release as a reading.** 486's mechanism still has one inferential step — that the
tree root's block was freed and handed to `new IORootParent` — and the two instruments named for it are
the class of `g_dtplane_root` at every walk and a wrapper on the release/allocation path under it.
462's "the delete list was every entry" is the other half of the same question and is still open.

**And, unchanged, 485's second half: the copies that build process 1's arguments.** `exec_save_path+0x38`,
`exec_copyout_strings+0x128`, `load_init_program_at_path+0x30` -> `mmu_kvtop_wpreflight+0xc`, the
`copyin`/`copyout` word loops and `copypv+0x84` are still unattributed to `arm_fast_fault` or `vm_fault`
— and `arm_fast_fault` handles only the pmap reference/modify faults, so `vm_fault`
(`osfmk/arm/trap.c:443` user, `:563` kernel) is the decisive wrapper, not `arm_fast_fault`. Also owed:
why the `VM_FLAGS_FIXED` stack allocation at `0x26E00000` is refused; 474's `thread->map = 0` moment;
448's `_bad` slots as a named pair; the pthread table's other ~34 slots; `osfmk/kperf/kperfbsd.c`; the
untraced build's `entry_stubs.c` compile errors; `thread_bootstrap_return`; and the fixture's timer node
(`interrupts = <1 2 0 1 3 0>` -> INTID 18/19), falsified for `CNTV` by 482.
