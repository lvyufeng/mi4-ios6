# Experiment 284 — `osfmk_kern_clock.o`, and the first frontier that is *inside* the object it links

**Step:** link the object that defines `clock_config` — `osfmk/kern/clock.c`, `osfmk_kern_clock.o` —
the name the 283 run stopped at, called by `machine_init` at `+0xc`.
**Prediction:** a report, and this time the *name* as well as the shape: `clock_config` is not a
wrapper, five of its eight calls are already real in this image, and the sixth is `clock_oldconfig` —
a name nothing references today, so it arrives as a fresh stub. Predicted caller `clock_config+0x68`.
**Result:** exactly that. **`stub_hit=clock_oldconfig` at `xnu_entry_stub_caller=0x800b97e4`**, with
all three build deltas (900 undefined / 806 function stubs / 94 storage) to the unit, and **one
prediction in the ledger that the build itself corrected** — the function offsets, off by 0xc.

## What makes this step different from every step before it

Twenty-eight steps of this walk linked an object for a symbol the run had stopped at, and in every
one of them the symbol was a *leaf of the missing set*: the object carried the definition, the boot
called it, and the frontier moved past. This is the first step where the symbol the run stopped at
is defined in an object whose **own body still needs something**:

```
clock_config, function-relative call order, each target checked against this image
  +0x10  arm_usimple_lock_init         real (osfmk_arm_locks_arm.o, linked long ago)
  +0x14  lck_grp_attr_alloc_init       real (osfmk_kern_locks.o)
  +0x30  lck_grp_alloc_init            real
  +0x40  lck_attr_alloc_init           real
  +0x60  lck_mtx_init                  real
  +0x64  clock_oldconfig              *NEW STUB*  <- the stop, return address +0x68
  +0x68  ntp_init                     *NEW STUB*
  +0x84  nanoseconds_to_absolutetime   real (tail call, osfmk_arm_rtclock.o)
```

So the frontier *resumes* inside the function the step links rather than leaving it. That is what
made this step predictable by name — the body could be read before the build — and it is also the
first sign that the walk is entering the region where each step buys less: from here on, a linked
object can be worth a single symbol.

## The object, measured

`osfmk_kern_clock.o` is **6316 bytes of text, 4 of data, 176 of bss, 12 of rodata, 48 definitions and
41 references**. Against this image it resolves **17** — fifteen function stubs and two storage
stand-ins, whose sizes agree with the object byte for byte (`hz_tick_interval` is `D` size 4 in the
object and `0x4` in the generated stand-in; `mach_absolutetime_asleep` is `B` size 8 and `0x8`):

```
absolutetime_to_continuoustime      clock_absolutetime_interval_to_deadline
clock_config                        clock_continuoustime_interval_to_deadline
clock_deadline_for_periodic_event   clock_get_calendar_microtime
clock_get_calendar_nanotime         clock_get_uptime
clock_init                          clock_interval_to_deadline
clock_timebase_init                 continuoustime_to_absolutetime
delay                               mach_continuous_approximate_time
mach_continuous_time                hz_tick_interval (D, 4)
mach_absolutetime_asleep (B, 8)
```

and adds **8** new obligations, all functions this project has already compiled:

```
clock_oldconfig   clock_oldinit                          (osfmk_kern_clock_oldops.o)
ntp_init          ntp_update_second                      (bsd_kern_kern_ntptime.o)
commpage_update_boottime        commpage_update_mach_continuous_time
                                   (osfmk_arm_commpage_commpage.o)
PEGetUTCTimeOfDay               PESetUTCTimeOfDay        (iokit_Kernel_IOPlatformExpert.o)
```

The other 24 of the 48 definitions are names nothing in this image has referenced yet —
`clock_gettimeofday`, `delay_for_interval`, `clock_update_calendar`, `clock_lock`, … — and none of
them collides: each is defined exactly once in the pool, by this object, and the pass-1 image does
not define it. Linking the object adds no duplicate definition; it makes 24 more names available.

## Two readings that were wrong, and the corrections

Both are the project's oldest defect class — a number that is an artifact of how it was taken — and
both were caught by the build rather than by the run, which is where they should be caught.

- **Object-section offsets are not function offsets.** The first version of the call table above was
  written with the raw `.text` offsets from the object's disassembly: `+0x1c`, `+0x20`, …, `+0x70`.
  The object's `.text` begins with a different function, `kdp_clock_is_locked`, which is 0xc bytes,
  so every one of those numbers was 0xc too large. The linked image corrected it: `clock_config` is
  at `0x800b977c` and the `bl clock_oldconfig` at `0x800b97e0`, which is `+0x64`, not `+0x70`. The
  error was 12 bytes on a prediction of a return address, and the run reported `0x800b97e4` — the
  corrected number, written into the ledger from the *build* before the device was touched.
- **The image does not grow by the object's text.** `osfmk_kern_clock.o` carries 6316 bytes of text
  and the run's image grew by **8**. The entry image's `.bin` ends at the end of `__DATA,__data`,
  and `.data` is placed 16 KB-aligned after the read-only region — `0x800fc000` both before and
  after this step. So within the slack (currently 0x1be4 bytes, the read-only region ending at
  `0x800fa41c`) a step's text growth is invisible in the image size, and what shows is only what
  `.data` itself grew: 4 bytes for `hz_tick_interval`, plus 4 of alignment. `text size` and
  `__bss_start` are the numbers that move monotonically, and they are the ones to predict with.

The 283 step's image jump of +16440 is the same effect seen from the other side — that step's text
growth crossed the boundary and moved `.data` a whole 16 KB.

## The run

**301116 bytes**, the preflight clean (`loader_xnu_entry_stub_status=0x90000001`,
`high_va_data_verified=0x00000001`), and:

```
stub_hit=clock_oldconfig      xnu_entry_stub_caller=0x800b97e4
```

`0x800b97e4` is `clock_config+0x68`, the return address of the `bl clock_oldconfig` at
`0x800b97e0` — the sixth of the eight calls in address order, and the first one this image cannot
run. So the run measured that `machine_init` reaches `clock_config`, that `clock_config` executes
`arm_usimple_lock_init` and the four lock allocators and `lck_mtx_init` (all real, all previously
unexercised), and that the boundary of what this kernel can do has moved from the name
`machine_init` calls first to a name *it* calls.

Build: 909 → **900** undefined, 813 → **806** function stubs, 96 → **94** storage — all three
predicted exactly — text 1019088 → 1025040, `__bss_start` 0x80113b50 → 0x80113b58, bss end
0x80149dc8 → 0x80149e08, image 1132120 → 1132128, headroom 1794616 → 1794552 bytes.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, no `exception:` line, and the device returned to Android on its own.

**Next:** experiment 285 — `osfmk_kern_clock_oldops.o`, the object `clock_oldconfig` is defined in.
Its body is worth reading before the build: `clock_oldconfig` calls `arm_usimple_lock_init`,
`thread_call_setup` and `timer_call_setup`, and both `*_call_setup` are **already real** in this
image, so it may run further than the stub's name suggests, and the stop after it may again be
neither the first call nor the last.
