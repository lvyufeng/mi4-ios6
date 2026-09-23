# 569: the exit-side flush fires one call before the seam, and the gate now says so

`stages/stage90/preflight_boot_check.sh` narrated `STAGE90_XNU_EXIT_POC_FLUSH=1` as *"the exit-side
FlushPoC_Dcache IS in this image - the one operation this project has now twice seen a device not come
back from"* and stopped there. The sentence was true and it was not enough: it names the operation and not
its **site**, and the site is the whole question, because 547 section 5 names a seam for the next arm that
this flag's call does not reach. A run with this flag on would have printed a paragraph that reads like
535's arm and is not it.

**This is a narration change only.** No image, no `out/`, no device, no arm: the 568 pair stays spent and
parked, and nothing here can brick anything. It is filed as an experiment because it changes what the next
run's gate *says* about the next run, and because the file it changes is the one a device action is gated
on.

## 1. The site, read off the wrapper

`stages/stage90/xnu_arm_boot/entry_trace.c:1935` opens `__wrap_platform_cache_idle_exit`, and its first
statement is the flag:

```c
void __real_platform_cache_idle_exit(void);
void __wrap_platform_cache_idle_exit(void)
{
    uint32_t sp;

#if STAGE90_XNU_EXIT_POC_FLUSH
    FlushPoC_Dcache();          /* <- first statement of the wrapper */
#endif

    __asm__ volatile ("mov %0, sp" : "=r"(sp));
```

So the flush runs **before** the wrapper's own `mov %0, sp`, before `__real_platform_cache_idle_exit()`,
and therefore **before the real exit's `push {fp, lr}`**. The same file already records whose flag this is
(`entry_trace.c:1891`): *"It is off unless `STAGE90_XNU_EXIT_POC_FLUSH=1` names it, and that is 517's own
first run talking."* This is 517's switch, carried unrun in the frozen image.

## 2. What 565 section 2 measured

565's title is "535 is designed, and the rebuild cannot wait", and its section 2 is the finding this
change exists to publish: flipping `STAGE90_XNU_EXIT_POC_FLUSH` is **not** 535. 535's seam, as 547
section 5 wrote it, is the `bl FlushPoU_Dcache` *inside* the real `platform_cache_idle_exit` - the call
this image's own ELF returns from at `0x800462dc` - which is **one call later** than the wrapper-top flush.
The two are a different hypothesis wearing the same name.

That is not a subtlety. A flush placed before the push cannot cover a line the push has not written yet,
and the death 568 measured is a *contents* death on the pop (the push and the pop are 8/8 with no `sp`
change between them). So a run of this flag is evidence about 517's call site and says nothing about 535's,
and reading it as 535's run is the project's most-repeated defect class: one value, two definitions.

## 3. The change

Both branches of the `V_EXIT_POC_FLUSH` block now carry the site. `=1` names it:

```
  window's far end (EXIT_POC_FLUSH=1): the exit-side FlushPoC_Dcache IS in this image - the one
      operation this project has now twice seen a device not come back from. **Read where its site
      is before reading this arm as the one under test: it is the FIRST statement of entry_trace.c's
      __wrap_platform_cache_idle_exit, so it runs before that wrapper's own `mov %0, sp` and before
      __real_platform_cache_idle_exit() - i.e. BEFORE the real exit's `push {fp, lr}`.** 565 section 2
      measured what that costs: the seam 547 section 5 names for the next arm is the `bl
      FlushPoU_Dcache` *inside* the real exit (the call this image's own ELF returns from at
      0x800462dc), which is one call LATER than this flag's. A flush before the push cannot cover a
      line the push has not written yet, so a run with this flag on does not test that arm, and
      calling it that arm's run is one value with two definitions.
```

and `=0` stops being readable as "there is no inner seam":

```
  window's far end (EXIT_POC_FLUSH=0): no exit-side flush of this image's own. That is an absence of
      THIS flag and not evidence that the inner seam is absent: an arm that flushes inside the real
      exit would arrive as its own switch, and the converse clause above refuses a record carrying a
      switch this gate does not print - so such an arm cannot be booted with nobody having read it.
```

An absent flag is not an absent seam, and the gate's own voice is the place to say so: this block is what
a person reads immediately before they touch the device.

## 4. Proof

In order, on the frozen tree:

```
bash -n stages/stage90/preflight_boot_check.sh                                     clean
git diff --numstat -- stages/stage90/preflight_boot_check.sh                       13  2
./preflight_boot_check.sh --allow-xnu-entry                                        GATE EXIT=0
```

The `2` deletions are the two `echo` lines the block replaced - a clean replacement with no suffix loss,
which on a file whose blocks are read by line number is the thing to check rather than assume. The full
gate run is the load-bearing one: the changed block is `echo`-only, so the gate's exit is unchanged
**and the changed text was printed by the same run that exited 0** - not a rehearsal standing in for it.

Both branches were then rehearsed **independently**, from the script's own bytes rather than from a copy
of what I meant to write: lines 467-483 were extracted to a temp file and sourced with `V_EXIT_POC_FLUSH`
set to `1` and to `0`. The `=1` branch printed the "BEFORE the real exit's `push {fp, lr}`" warning; the
`=0` branch printed the converse-clause backstop. Neither was observed only through the branch the live
gate happens to take.

## 5. The backstop, and what would falsify this

The `=0` text asserts a property the gate already enforces elsewhere, and it is worth being exact about
which line does the enforcing. The converse clause at `:377-380` reads every `STAGE90_*` key out of the
entry record and `fail`s on any key the gate does not print:

> `$ENTRY_CFG carries key(s) this gate does not print: … - a switch recorded on the build side and not
> shown here is a switch the next run would go out with unread; add it to ENTRY_CFG_KEYS above`

So if 535 lands as a **new** switch - which is what 565 section 2 says it must, since a call inside the
real exit is a different source edit and a different `__wrap_` - the gate fails on the next run **in the
false direction**, before the device is touched, with a message that names the key. That is the intended
signal, and it is why the `=0` branch can safely say an unread arm cannot be booted.

The falsifier for *this* change is the one thing it cannot check for itself: if 535 ever ships by flipping
**this** flag rather than adding one, then section 3's `=1` paragraph is what says the run is not 535's -
and the paragraph would be the thing that is wrong. Nothing in the tree can decide that today; the flag's
own site in `entry_trace.c` is the evidence, and 565 section 2 read it.

## 6. State

- **533's pair (`1daaf44e…` / `f202f246…`) is spent**, parked under `out/stage90/captures/533-*`. 568's
  run is 547 section 4 row (i): dies at the idle exit's `pop {fp, pc}`, log present, device returns - so
  535 is to be built as designed.
- **The frontier is 535**, in the peer session's lane: a per-level CSSELR PoC invalidate wrapped on
  `lr == 0x800462dc`. It costs one non-persistent `fastboot boot`.
- **TWRP-to-storage stays withheld.** The goal clause `如果os已经能进去了的话` is the same unmet clause as
  `起码要能进入操作系统`: the boot reaches pid 1 and does not survive the idle pass.
