# Experiment 460 — the blob the walker reads, and what the plane replay says about 459's missing property

**Status: host-side only, no device touched. Two results, one of them a defect in this project's own
tooling that 459's edit exposed and that would have answered 460's first question wrongly.**

459 left the boot at `IOFindBSDRoot`'s `rd=md0` branch, panicking because `mdevlookup(0)` returned -1:
`mdevadd` was never called, and `mdevadd`'s call is inside `if (data)` for
`fromPath("/chosen/memory-map", gIODTPlane)`'s `RAMDisk` property. The host check has walked that node
with XNU's own reader since the step was built and passes on the two words, the length and the
boot-arg, so the frontier is either **the plane's shape** (the blob's tree is not the tree
`IODeviceTreeAlloc` attaches) or **the plane's existence** (`gIODTPlane` NULL, which no host tool can
see). The plan's step 0 was to settle the first host-side by replaying `IODeviceTreeAlloc`'s stack
loop. That replay is below, and it says the shape is not the problem.

## The defect: the walker was reading a tree from before the edit

`tools/xnu_dt_walk.py` reads `out/apple_dt_host/apple_dt.bin`, which `tools/apple_dt_host_dump.sh`
writes by compiling the payload's real `build_stage90_apple_dt`. Two tools needed that builder and
they found it **two different ways**: `tools/host_dt_check.sh` by matching the function's braces (so it
followed 459's edit), and `tools/apple_dt_host_dump.sh` by **hard-coded line numbers** — 4, 31, 34,
41, 43, 831, each end asserted to be the brace or declaration it was supposed to be. 459's edit moved
those lines, so:

- the dump script's assertions fired and it exited 1 — correct behaviour, and it printed the line it
  found instead;
- **but `apple_dt.bin` was left on disk from the previous run**, and it is 29528 bytes: the pre-459
  tree;
- and the walk of that file is internally consistent, prints 26 nodes, agrees with the payload's own
  `skip_node`, and reports `ok`. Read far enough, it says `/chosen` has **five properties and zero
  children**: no `memory-map`, which is exactly the missing-property shape 459 was hunting.

So the tool that was to answer "is the plane the blob's tree?" was answering about a tree the device
did not build. Nothing in it was wrong except its input, and no check in the project compared the two
files' ages. This is 440's *the generated file is one build behind*, in the tool whose whole purpose is
to be the independent reading of a generated file.

The tree the device actually built is **29632 bytes (0x73c0)**, which is what 459's own log reports for
it — `xnu_entry_device_tree_len=0x000073c0` — so the fresh blob and the device's tree are the same
length, and the stale one was 104 bytes short: `memory-map`'s node header, its two property headers and
their two values.

## What changed

| change | file | why |
| --- | --- | --- |
| one extractor | `tools/apple_dt_extract.py` (new) | the builder's extent, found by declaration and closed by braces: `g_apple_dt` with its bound and alignment, `STAGE90_CHOSEN_RANDOM_SEED_BYTES`, the rule string, `build_chosen_random_seed`, `build_stage90_apple_dt`. `static` comes off the buffer and both functions (the harness is a different unit), and each piece that cannot be found aborts loudly. Both consumers now call it, so the extent has one definition |
| the buffer's size, once | `tools/host_dt_harness.c`, `tools/host_dt_selftest_probe.c`, `tools/host_dt_check.sh` | the bound 32768 stood in **three** files — `stage90_main.c`'s declaration, the harness's own `uint8_t g_apple_dt[32768]`, and the shim's `extern uint8_t g_apple_dt[32768]` — with nothing comparing them. The storage is now the payload's own declaration, de-`static`ed by the extractor, and both other copies take the bound from the extractor's `--facts` reading |
| the entry header | `tools/apple_dt_host_dump.sh` | the builder now uses `STAGE90_XNU_RAMDISK_VA`/`_SIZE`, which live in the generated `out/stage90/xnu_arm_entry.h`, so the dump compiles against that same header (as the payload does) instead of failing on undeclared identifiers. A missing header is a loud stop naming `build_entry.sh` |
| the staleness guard | `tools/xnu_dt_walk.py` | the walk refuses a blob older than `stage90_main.c`, `apple_dt.c` or `xnu_arm_entry.h`, and says which file is newer. Tested in both directions: touching `stage90_main.c` makes it exit 1 with that message, and re-running the dump makes it exit 0 with the blob byte-identical |
| the plane replay | `tools/xnu_dt_walk.py --plane` | models `IODeviceTreeAlloc`'s stack loop, then resolves the paths the boot resolves |

## The replay, and what it says

    $ tools/xnu_dt_walk.py --plane
    blob out/apple_dt_host/apple_dt.bin  29632 bytes (0x73c0)
    XNU's walk ends at 0x73c0 of 0x73c0; the payload's skip_node ends at 0x73c0; agree
    nodes walked: 27

    the IODT plane IODeviceTreeAlloc builds, node by node:
      nodes 27, attachToParent calls 26 (every node but the root), DTEnterEntry pushes 26
      ok   every node is attached to the parent the blob's own walk gives it
      ok   every node carries a `name`, so every node is reachable by path

    the paths the boot resolves in that plane:
      ok   /chosen                  plane 0x00672c, DTLookupEntry 0x00672c
      ok   /chosen/memory-map       plane 0x00693c, DTLookupEntry 0x00693c
      ok   /cpus                    plane 0x006ab4, DTLookupEntry 0x006ab4
      ok   /arm-io                  plane 0x006ff8, DTLookupEntry 0x006ff8
      ok   /device-tree             plane 0x0000dc, DTLookupEntry 0x0000dc
      ok   /memory                  plane 0x006a00, DTLookupEntry 0x006a00
      absent*  /options, /efi/platform - XNU asks, our tree has none, by design

The model is the loop literally, and two of its details are the whole reason a replay is worth
writing: `DTEnterEntry` (`pexpert/gen/device_tree.c:283-302`) returns `kSuccess` for **any non-NULL
child**, so every child is entered and the push happens once per node, leaves included — a leaf is
pushed and popped with nothing attached to it; and `DTExitEntry` (`:305-322`) restores the scope *and
the index that scope had reached*, which is what makes the outer loop's `while (stack->getCount() &&
kSuccess == DTExitEntry(...))` a depth-first walk rather than a re-walk. The stack holds exactly the
ancestors of the iterator's current scope, and `parent_of[child] = parent` above the push is the only
line that assigns a parent.

**So the plane's shape is the blob's shape and every node is named: 459's NULL lookup is not a shape
difference.** `fromPath` walks the same structure `DTLookupEntry` walks, matches by the same `name`
property, and finds `/chosen/memory-map` on the same node — 0x693c in both. What remains is the one
thing a host tool cannot see: **whether the plane exists at all**. `gIODTPlane` is created as
`IODeviceTreeAlloc`'s first statement (`iokit/Kernel/IODeviceTreeSupport.cpp:112`), and `fromPath`
returns 0 immediately when its `plane` argument is NULL (`IORegistryEntry.cpp:1257-1258`) — so if
`IOPlatformExpertDevice::initWithArgs` was called with `dtTop == 0`, the plane is never built,
`PE_init_iokit`'s `StartIOKit(PE_state.deviceTreeHead, ...)` (`pexpert/arm/pe_init.c:279`) passed a zero,
and every `fromPath` in the boot returns NULL while the DT blob itself is perfect. That hypothesis
predicts 459's log exactly, and it is the first thing 461 measures.

## What 461 (the step that answers it) has to be

Three reads and two wrappers, all of them in the image already:

1. **`gIODTPlane`** — a plain data symbol with external linkage in `IODeviceTreeSupport.cpp`, so a
   data read and not a wrapper. Non-NULL means the plane was built.
2. **`PE_state`** — `deviceTreeHead` (the pointer XNU was handed), `bootArgs` and `initialized`
   (`pexpert/arm/pe_platform.c`'s struct; the payload's own `pe_state` values are already in the log
   under `stage90_xnu_entry_stub_*`, but those are the *payload's* copy of the struct, not the one
   XNU's `PE_init_platform` filled in).
3. **`--wrap=IODeviceTreeAlloc`** — cross-object from `IOPlatformExpert.cpp:1565`, so `--wrap` reaches
   it (455's rule): the `dtTop` argument and the returned pointer answer "was it called, with what,
   and did it return a tree".
4. **`--wrap=` `IORegistryEntry::fromPath`** — the OS's own answer for the one path that matters,
   with the plane pointer it was given and the node it returned; and `--wrap=mdevadd`/`mdevlookup`,
   which are plain C in `bsd/dev/memdev.c`, so both are ordinary undefined references.

And 459's own instrument defect is a prerequisite, not a nicety: `fleh_undef`'s six keys must move off
the 8 KB buffer the kalloc tracer fills before they can report a silent panic, because the whole reason
the trap could not be named is that its PC was written into a full buffer.

## Safety

No device was touched in this step: every command in it is a host build or a host walk. The standing
rule is unchanged for the run that follows - `fastboot boot` only, every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh
--allow-xnu-entry`, the hardware watchdog armed by the payload before the jump and XNU never touching
it.
