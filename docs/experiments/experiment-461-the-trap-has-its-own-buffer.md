# Experiment 461 — the trap gets a buffer of its own, and the plane says the property is there

**Status: on the device, one run, 465077 bytes of log, exit 0, device back on Android by itself. Two
results and one new question. The trap record is in the log for the first time since the walk reached
`IOFindBSDRoot`, and it names the panic by the literal in `r9` — `IOFindBSDRoot: specified root memory
device, %s, has not been configured\n` with `%s` = `md0` — which turns 459's inference into a
measurement. And the root-device chain is **correct on the device all the way to the registry**: the
plane exists, `fromPath("/chosen/memory-map", gIODTPlane)` *by the instrument* returns an entry, and
`getProperty("RAMDisk")` on that entry answers with the payload's own two words. What fails is the
OS's own `fromPath` — and it fails at the **first component**: `/chosen` is not reachable in the plane
at the moment `IOFindBSDRoot` asks, while it was reachable a few thousand records earlier.**

## Why, and the two things 459 could not read

459 left two readings behind it, and they are different in kind. The first is a *fixed* instrument:
`fleh_undef`'s whole report went through `entry_kv` into the same 8 KB buffer the `t268_*` kalloc
tracer fills, and 459's own report says how full it was - `xnu_entry_kv_written = xnu_entry_kv_in_dram
= 0x1fea` (8170 of 8192) with `xnu_entry_kv_dropped = 0x66bb` (26299 refusals) - so all 47 of the
handler's keys were written and *dropped in silence*, including the two that name the panic
(`xnu_entry_panic_str`, `xnu_entry_trap_r9_fmt`). That is this project's ledger entry 170 and 167's
rule: a refusal has to be visible, and a report has to have a place of its own.

The second is the gap 460's own replay narrowed but could not close. 460 compared the blob's tree
against a replay of `IODeviceTreeAlloc`'s stack loop and found them the same shape - but both of those
are *models of the same blob*, and neither is a reading of the object the OS asks. The plane,
`gIODTPlane`, is that object, and no host tool can see it.

## The change: one writer, two buffers

**`entry_stubs.c`**

- `entry_kv_into(buf, len, dropped, max, key, value)` is `entry_kv`'s body with the globals it used to
  close over replaced by its arguments, and `entry_kv`/`entry_panic_kv` are the two one-line callers.
  One definition of "how a record is spelled" - the alternative, a second copy for the trap, is this
  project's most expensive defect class. The linked image says the split kept the body: the writer is
  `entry_kv_into.constprop.0` with the bound constprop'd to 8192 (both buffers are 8192, so one clone
  serves both), and `entry_kv`/`entry_panic_kv` are nine-instruction forwarders that load their buffer,
  their length, their drop counter and their bound and call it.
- `g_panic_buf[ENTRY_PANIC_BUF=8192]` with `g_panic_len`, `g_panic_dropped` and `g_panic_entered`. 8192
  because the record has to fit *empty*: 47 call sites, four of them loops (the eight frame words, the
  `replay.node_n` ring times three keys, the chunk hashes), so a bad tree walks it past 5 KB. The run
  below measured 5045 bytes with nothing refused.
- `fleh_undef`'s 47 keys, and the four the epilogue appends **after the MMU is off**
  (`xnu_entry_stub_caller_e`, the three IRQ words), now go through `entry_panic_kv` - the second group
  for the same reason as the first, and 459's log has neither of them either, because the buffer they
  appended to was at 8170 bytes too. The trap handler increments `g_panic_entered` before anything
  else, so "the handler ran and wrote nothing" and "the handler never ran" are different logs.
- The epilogue cleans `g_panic_buf` **by address and separately** from `g_kv_buf`'s range (widening the
  range would clean whatever the linker put between the two buffers, and the neighbour is the payload's
  256 KB RAM disk), and prints it under its own heading: `MI4IOS6_STAGE90_XNU trap record:`. A report
  with no such line and a non-zero `xnu_entry_panic_entered` is a trap whose buffer did not reach DRAM.
- `entry_write_461_kv` publishes the new group: three panic-buffer keys, four plane keys, and the
  chain's counts. It is a function of its own for 455's reason (the epilogue's constant pool).

**`entry_trace.c`** - four readings, one of them a call rather than a wrapper:

| reading | taken | what it answers |
| --- | --- | --- |
| `xnu_entry_dtplane*` | the `IODeviceTreeAlloc` wrapper, after the real call returns | the plane exists, the tree's root nub, and `fromPath("/chosen/memory-map", gIODTPlane)` **called by the instrument** |
| `xnu_entry_dtpath*` / `xnu_live_path_*` | `--wrap` on `IORegistryEntry::fromPath` | every call the image makes: caller, the path's first twelve bytes as three words, the plane, the return |
| `xnu_entry_dtprop*` | inside the above, on what `fromPath` returned | XNU's `getProperty("RAMDisk")` on that entry and, when it answers, `OSData::getBytesNoCopy` and the two words |
| `xnu_entry_mdevadd*` / `mdevlookup*` | `--wrap` on both | the two calls the property is supposed to lead to, with their arguments |

`getProperty` is **virtual**, so it cannot be wrapped: `--wrap` renames an *undefined* reference, and a
vtable entry is a defined symbol in the object that also defines the function - 455's negative result,
which cost it eleven silent wrappers. It is therefore *called*, by mangled name, with `this` in r0
exactly as the vtable slot would pass it: `_ZNK15IORegistryEntry11getPropertyEPKc` is
`IORegistryEntry`'s own definition (`IORegistryEntry.cpp:631`), no subclass overrides it, and the body
is the code the OS's own call reaches. The same for `_ZNK6OSData14getBytesNoCopyEv`. The plane
reading's `fromPath` is called through `__real_fromPath` deliberately, so the `xnu_live_path_*` records
are the *OS's* calls and not the instrument's.

## The build, and the two checks it added

Both stages rebuilt in the canonical order (entry, then the payload that embeds it). `xnu_arm_entry.bin`
5291016 -> 5307400 bytes (`/tmp/entry-459.log`'s image bytes, the last build before this one - 460
changed tools only, so that is the image its run booted - to `/tmp/build461_entry.log`'s), `.text`
5097184 (was 5092896, +4288), image 5307400, `.bss` 614200 bytes to 0x805a5f38 (was 606008 - the 8 KB
the trap's buffer asks for, and `__bss_start` moves 0x8050c000 -> 0x80510000, which is where the
16 KB of image growth comes from), 1417416 bytes of headroom below `topOfKernelData` (was 1441992) -
and all six of 459's own checks still pass, including the RAM disk's bounds, which is what a `.bss`
member of 8 KB has to be checked against. The payload's `stage90-qcdt.img` is 8327168 bytes (was
8310784), md5 `e50df27f9a8dc52d287e264e21f1a19d` at the run, built from the payload sources unchanged
by this step (only the entry blob it embeds differs).

Three new `--wrap=` flags bring the list to 39, and the reachability check reads the linked image:
**35 reached by a branch, 2 same-object-only, 1 never called, 1 by address - and none dead**, which is
what says the three new wrappers can run at all. The third new check is the one 455's typo defect
argues for and this step needed: **the three mangled names the instrument calls must not be in the
pass-1 undefined set.** A name that nothing in the image defines is a name the stub generator invents a
stand-in for, so a misspelling links, and the instrument then calls the stand-in - a stop that looks
like a finding about the device. The check has no spelling rule to get wrong: pass 1 is the list of
names *no object defines*, so membership is the defect.

## The run

One `fastboot boot` through the two gates, log 465077 bytes, exit 0, device back on its own. The run's
course is **unchanged from 459** - `xnu_entry_ostext_chars = 0x399` (921) and `_lines = 0x13` (19),
byte for byte the same block, and `xnu_entry_block_count = 0x22`, `_returned = 0x0c` - so this step
measured the run it was built for rather than perturbing it.

### The trap record, and the panic it names

```
MI4IOS6_STAGE90_XNU trap record: xnu_entry_undef_lr=0x80030f20
 xnu_entry_undef_pc=0x80030f1c        <- the udf #0xfdee in DebuggerTrapWithState (debug.c:371)
 xnu_entry_undef_spsr=0x60000093
 xnu_entry_panic_str=0x00000000       <- the panic string is NULL *at the trap*
 xnu_entry_trap_r9_fmt=0x8048bb98     <- "IOFindBSDRoot: specified root memory device, %s, has not been configured\n"
 xnu_entry_panic_arg0=0xc061c700      <- whose first words are 'm','d','0',0
 xnu_entry_panic_arg1=0xde500000
 xnu_entry_zone_map_min=0xc05c2000
 xnu_entry_zone_map_max=0xc0800000
```

`xnu_entry_panic_len = 0x13b5` (5045) with `xnu_entry_panic_dropped = 0`, so the record is complete and
the 170 defect is closed. `r9` is the panic's format - the same road 362 used - and it resolves in this
image to the literal at `0x8048bb98`, which is `IOKitBSDInit.cpp:490`'s message; `arg0`'s contents are
`md0`, the `%s`; and `mdevlookup(0)` returned -1 immediately before it. That is 459's frontier, now
named by the instruction rather than inferred from a window. **`xnu_entry_panic_str` being zero is
itself a reading**: the global is clear by the time the handler reads it, so the *format in a register*
is the road that works, not the message string.

`xnu_entry_zone_map_min`/`_max` are **non-zero for the first time**. The comment above them was written
for 362 and predicted they would stay zero "for one more run"; that run is over. `zalloc.o` and
`kext_alloc.o` are both in the pool now, `zone_init`, `kmem_init`, `kmem_suballoc` and
`kext_alloc_init` are all real functions in this image (0x80072a38, 0x80083b00, 0x80083944, 0x80089ce8,
none of them a generated stub), so `zone_init` ran. A prediction that a reading will stay zero is a
prediction about the pool, and the pool has grown twice since it was written.

### The chain, measured end to end - and then the OS's own call

```
xnu_entry_dtplane=0xc05d6848          <- gIODTPlane, non-NULL, and the *same* pointer path records carry
xnu_entry_dtplane_root=0xc060d130     <- IODeviceTreeAlloc's return, the tree's root nub
xnu_entry_dtplane_map=0xc060d2e8      <- fromPath("/chosen/memory-map", gIODTPlane), by the instrument
xnu_entry_dtprop_calls=1  _hits=1
xnu_entry_dtprop_entry=0xc060d2e8     <- the same entry
xnu_entry_dtprop_key0=0x444d4152  _key1=0x006b7369     <- "RAMD", "isk\0"
xnu_entry_dtprop_obj=0xc0626580       <- getProperty("RAMDisk") answered
xnu_entry_dtprop_bytes=0x806e6998
xnu_entry_dtprop_w0=0x80512000        <- g_stage90_ramdisk
xnu_entry_dtprop_w1=0x00040000        <- 256 KB
```

So, at the moment `IODeviceTreeAlloc` returned: the plane exists, the plane holds `/chosen/memory-map`,
the node holds `RAMDisk`, and the property's bytes are exactly the two words the payload put in the
tree. The blob-to-plane link 459 could not see **is not the failure**.

Then the OS's own two calls, and they are the *only* two `fromPath` calls in the whole boot:

```
xnu_live_path_caller=0x802067f4  w0/w1 = "/cho"/"sen\0"          plane=0xc05d6848  ret=0
xnu_live_path_caller=0x8020696c  w0/w1/w2 = "/cho"/"sen/"/"memo" plane=0xc05d6848  ret=0
xnu_entry_mdevadd_calls=0x00000000
xnu_live_mdevlookup_caller=0x80206bc0  devid=0  ret=0xffffffff
```

`0x802067f4` and `0x8020696c` are the instructions after the two `bl`s in `IOFindBSDRoot`
(`IOKitBSDInit.cpp:404` for `/chosen`, `:440` for `/chosen/memory-map`), and `0x80206bc0` is the one
after `mdevlookup` at `:469`. Both paths are the right paths, both are asked on the **same plane
pointer** the instrument used, and **both return zero - and both fail at the first component**,
`chosen`, since even `/chosen` alone does not resolve. The registry answered for `/chosen/memory-map`
earlier in the same boot and does not answer for `/chosen` now.

## What this rules out, and the one link that is left

Ruled out, each by a reading rather than by an argument: the blob (XNU's own reader over it, host-side),
`IODeviceTreeAlloc` and the tree's physical address (`_dtalloc_arg = 0x806e0000`, `_ret = 0xc060d130`),
the plane's existence (`0xc05d6848`), the plane's shape and names (a `fromPath` *through the plane*
resolved), the property (`obj` non-zero, its two words the payload's), and the instrument's own
perturbation of the run (921 characters of OS console text, 34 blocks, 12 returns - all identical to
459, which had no such probe).

What is left is the **attachment between the meta root and the tree** at the moment
`IOFindBSDRoot` asks. `fromPath` starts at `gRegistryRoot->getChildEntry(plane)`, which is
`copyChildEntry`'s first element of the registry root's child set in that plane
(`IORegistryEntry.cpp:1536`), and the meta root's IODT child set has exactly one member for the life of
the boot - the tree's root, attached once at `IODeviceTreeSupport.cpp:204`. A zero return for `/chosen`
with a non-NULL plane means that lookup no longer names the tree, which is one of two things: the tree
root was detached from (or freed out from under) the meta root, or the root is still there and its
children no longer match `chosen` by name. Nothing else in `fromPath` can fail before the first
component is looked at.

## Next: 462, three readings at the failing moment

The instrument goes in the `fromPath` wrapper, *after* the real call, and only when the OS's own call
returned zero - the frontier itself:

1. `IORegistryEntry::getRegistryRoot()` (`_ZN15IORegistryEntry15getRegistryRootEv`) - the same object
   the probe's call walked from, or a different one.
2. `gRegistryRoot->getChildEntry(plane)` (`_ZNK15IORegistryEntry13getChildEntryEPK15IORegistryPlane`)
   - `0xc060d130` (the tree, still attached), `0` (detached or freed), or something else.
3. The instrument's own `fromPath("/chosen", plane, 0, 0, 0)` at that same moment - the control that
   separates "the registry changed" from "these two calls are different in some other way".

Whichever it is, the next step names it: if the root is attached, the frontier moves to the tree root's
child set and its names; if it is not, the question becomes which *release* took the last reference, and
the instrument for that is a count, not a name.

Two smaller things 462 should carry, both already argued for:

- `entry_write_461_kv`'s group is measured, so it stays; but **448's `_bad` slots are inverted for three
  of their four functions.** `IORecursiveLockAlloc` 0xc060b798, `IOSimpleLockAlloc` 0xc05caae0 and
  `IOCommandGate::commandGate` 0xc05d6708 are *pointers* - non-zero is success - while
  `kernel_thread_start`'s is a `kern_return_t` where zero is. A key named `_bad` holding a successful
  allocation is the ledger's "a measurement can be the thing that is wrong" waiting to happen; the fix
  is a pair of keys that says which sense each one is in, in the report rather than in a comment.
- The `--wrap` reachability check's failure mode is that a wrapper which links but is never reached
  looks exactly like a run where the measured thing did not happen. Three of 461's four readings are
  wrappers, so the check's `say` line is now load-bearing for the step's meaning, not just for the
  build.
