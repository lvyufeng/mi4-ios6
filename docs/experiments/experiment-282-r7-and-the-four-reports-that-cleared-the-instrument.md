# Experiment 282 — `r7`, and the four reports that showed the silence was the instrument's

**Step:** not an object. The second half of the bisect 280's silence forced: one more checkpoint run,
three fixes to the pad, and three runs that cleared the instrument — after which the *boot's* frontier
appeared for the first time since 280.
**Prediction:** for the last run of the experiment, a report naming a symbol — the walk's normal mode,
unavailable since 280.
**Result:** `stub_hit=kernel_set_special_port`, `xnu_entry_stub_caller=0x800b7648` — the symbol the
280-era ledger predicted from reading `ipc_host.c`, four experiments late, and the frontier that
experiment 283 then links. **Every silence between 280 and this experiment was the instrument's.**

## The run that pinned the call number, and the code that pinned it back

`STAGE90_ENTRY_CHECKPOINT=bcopy STAGE90_ENTRY_CHECKPOINT_SKIP=5 STAGE90_ENTRY_CHECKPOINT_AFTER=1` was
built and run: **294042 bytes, silent** — the size of every silent log this walk has taken.

Before reading anything into that, the call numbering was re-derived from the *linked image* rather
than from the census that produced the prediction, because the prediction had been wrong once already.
`cpu_machine_idle_init` is at `0x80004130`, and its `bl`s in address order are `PE_parse_boot_argn` x2,
`ml_static_vtop` x2, `bcopy_phys` (the `wfi == 0` branch, not taken), `ml_vtophys`, `ml_io_map`,
**`bcopy` at 0x8000425c**, **`bcopy` at 0x80004270** (`cpu.c:566-567`, the exception-vector copies),
`ml_static_vtop` x3, **`bcopy_phys` at 0x800042dc**, `ml_static_vtop` x2, **`bcopy_phys` at 0x80004318**,
`CleanPoC_DcacheRegion` at 0x8000432c, `ml_static_vtop`, **`bcopy` at 0x80004364** (`cpu.c:589`, the
running-signature copy), `clean_dcache`. Inside `bcopy_phys` there are exactly two wrapped `bcopy`
references: the fast path's `b __wrap_bcopy` and the slow path's `bl __wrap_bcopy`.

With call 3 measured as `arm_vm_init`'s, the dynamic order is therefore: 1 and 2 (the two
`PE_init_platform` sites), 3 `arm_vm_init`, 4 and 5 the vector copies, **6 the fast path of the first
`bcopy_phys`**, 7 the second's, 8 the running-signature copy. Call 6 is what `SKIP=5` reports on, so
the numbering was right and the silence was not. And because 282g's report came from the call site at
`0x8000423c`, which is *after* the vector copies, calls 4 and 5 had already returned: the silence is not
the vector copies, and the interval 282m's silence bounded does not exist.

## Three versions of the pad, and the one that works

**The first version was no pad at all**, and it lost the two instructions the sweep needs — the
mechanism experiment 281 describes.

**The second version was a pad of NOPs**, on the claim that "a NOP overwritten by an `ands` is still a
NOP". That is false in exactly the way that mattered: a NOP is only still a NOP if nothing *executes*
it after the write, which is true of the boot path — the pad is inside the epilogue, and the boot never
runs the epilogue — and false of the report path, which always runs through the pad with XNU's data in
it.

**The third version repaired the NOPs at run time** — store `nop` back over both addresses before the
sweep reads them, `dsb`/`isb` after. It was built, verified in the linked image (the two stores at
`0x800023e4`/`0x800023e8` precede the pad; both addresses read back as `nop`), and run with
`STAGE90_ENTRY_CHECKPOINT=machine_startup`: **still silent.** Self-modifying code is why, and this
project cannot measure the I-side of it from here: the store puts the new bytes in the D-cache and
`dsb sy` publishes them at the point of coherency, but whether the *fetch* of the pad's line sees them
depends on whether that line was prefetched beforehand, and no log line can say. An instrument whose
correctness rests on an unmeasurable cache property is not an instrument.

## `r7`, measured in the linked image

The measurement that made the mechanism concrete is in `entry_epilogue`'s own disassembly, not in a log:

```
80002358: ldr r7, [r4, #4]        ; prologue, r4 = entry_vectors_stack
800023f4: 32 nops                 ; the pad, at 0x80002404/0x80002408 - the two words XNU writes
80002574: mov r1, r7              ; first use of r7, 0x180 bytes past the pad
800025b8: ldr r7, [r4, #32]       ; and it is not reloaded before then
```

`r7` is a callee-saved register holding a value the epilogue loaded before the pad and does not touch
again until well after it, where it becomes a kv value, half of a pointer (`add r0, r7, #4`) and the
base of a byte-table read. `0x80147000` decodes as `andshi r7, r4, r0` — register-form `ANDS` with
`cond=HI`, so it writes `r7` exactly when C is set and Z is clear, and changes the flags either way.
Whether it fires depends on the flags at that instruction, which depend on the code before the pad,
which is why this defect appeared and disappeared between builds whose code was instruction-identical:
the corrupted *values* are addresses, and the addresses are the layout.

## The pad, finally — and the three runs that cleared everything

The pad now carries no content that matters, because it is **never executed**: it opens with `b 1f`
over 31 NOPs. XNU's two writes still land inside it, on two of the NOPs the branch skips, and a word
that is never executed cannot break anything whatever it decodes to — regardless of the D-cache, the
I-cache, the prefetcher and the flags. The branch sits at the pad's first word, twenty bytes below
`0x80002404`, so it is not one of the corrupted words either.

`build_entry.sh` now checks the *structure* rather than the content, against two linked labels
(`entry_skip_pad`, `entry_skip_pad_end`): the first word is a branch, it targets the second label, the
pad is at least 128 bytes, the branch is at least four bytes below `0x80002404`, and both of XNU's
addresses are strictly inside the range it skips. Every build says so:

```
entry_skip_pad at 0x800023d4 branches over 128 bytes to 0x80002454, and XNU's two writes
(0x80002404, 0x80002408) land inside what it skips
```

Three runs followed, and each one reported:

```
CleanPoC_DcacheRegion   reports  caller 0x80004310, cp_arg0 0x80000000, cp_arg1 0x00001000
machine_startup         reports  caller 0x8000334c, cp_arg0 0x80147000
plain (no checkpoint)   reports  stub_hit=kernel_set_special_port, caller 0x800b7648
```

- The first is three instructions past the second `bcopy_phys`, so **both `bcopy_phys` calls returned**,
  and so did the two wrapped `bcopy`s inside them — the fast path's and the slow path's. Its two
  arguments are exactly the predicted ones (`cpu.c:583`'s `phystokv(gPhysBase)` and `PAGE_SIZE`), and
  its caller reads `0x80004310` rather than the predicted `0x80004330` by exactly the twenty bytes the
  new pad shape removed from the function.
- The second proves the whole of `arm_init`'s ladder ran, which is the interval 280's silence made
  unreachable and which four experiments were spent inside.
- The third is the walk's normal mode, restored: the first symbol the image needs and does not have.

**The conclusion, stated plainly because it cost four experiments:** the frontier was never inside
`bcopy_phys`. 282m's silence — the one the ledger read as "the stop is inside `bcopy_phys`'s first
call" — was a *report* the pad had broken, at a call site the boot reaches. The walk bisected a
four-byte `memmove` and a `pmap_cache_attributes` call that both return, and it did so with an
instrument that was the thing that was wrong: this project's oldest defect, for the eighth time, and
this time the measurement was the reporting path itself.

**Safety on every run:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, no abort other than the `high_va_data_abort_handler` contract keys, and
the device returned to Android on its own after every one.

**Next:** experiment 283 — `osfmk_kern_host.o`, the object `kernel_set_special_port` is defined in.
