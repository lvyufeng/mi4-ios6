# Experiment 285 — `osfmk_kern_clock_oldops.o`, and a step that overshoots the symbol it links

**Step:** link the object that defines `clock_oldconfig` — `osfmk/kern/clock_oldops.c`,
`osfmk_kern_clock_oldops.o` — the name the 284 run stopped at.
**Prediction:** a report, and *not* on anything in this object. `clock_oldconfig`'s three direct
callees are all real and its one indirect call is guarded by a storage stand-in that is four zero
bytes, so it runs to completion and the frontier moves past it — to `ntp_init`, the call after it in
`clock_config`. Predicted caller `clock_config+0x6c`.
**Result:** exactly that. **`stub_hit=ntp_init` at `xnu_entry_stub_caller=0x800b97e8`**, all three
build counts exact.

## Why this object should *not* move the frontier

This is the first step where the prediction is that the step overshoots. Twenty-nine steps have
linked an object for the symbol the run stopped at and found the frontier either moved past it (most
of them) or resumed inside it (284). This one was predicted from the *body* of the function it links,
before the build:

```
clock_oldconfig, 0xf4..0x1a8 (0xb4 bytes), every call in it
  +0x14  arm_usimple_lock_init    real
  +0x2c  thread_call_setup        real (osfmk_kern_thread_call.o) - its own body is a `bl __bzero`
                                  and a tail call, measured in the linked image, no stub in it
  +0x44  timer_call_setup         real (osfmk_kern_timer_call.o) - its own body calls
                                  arm_usimple_lock_init, lck_spin_lock and lck_mtx_lock_spin_always,
                                  all real
  +0x94  blx r0                   indirect, in a loop over clock_list - and the loop is entered zero
                                  times, because it is guarded by clock_count, and clock_count
                                  arrives from this step as a *storage stand-in*: four zero bytes
```

`ldr r0, [r5]; cmp r0, #1; blt +0xa4` — with `clock_count == 0` the whole list walk is skipped, so
the one call in `clock_oldconfig` that could have gone somewhere unknown is never made. The function
returns, and `clock_config`'s very next instruction after `bl clock_oldconfig` is `bl ntp_init`.

The two names that decide the shape of the step are therefore the two **storage** ones: the run does
not stop because `clock_list` is empty, and `clock_list` is empty because nothing has ever written it.

## The object, measured

3128 bytes of text, 52 of rodata, 29 of `rodata.str1.1`, 172 of bss, 32 definitions, 25 references.
Resolves **10**, every one a function — no storage at all among them, so no size the generator could
get wrong:

```
clock_oldconfig   clock_oldinit        clock_alarm          clock_get_attributes
clock_get_time    clock_service_create clock_set_attributes clock_set_time
host_get_clock_control                 host_get_clock_service
```

and adds **6**: four functions and two storage (all six measured against the object that defines
them, not guessed):

```
clock_alarm_reply   mach_clock_reply_user.o        T 0x6c
ipc_clock_enable    osfmk_kern_ipc_clock.o         T 0x2c
ipc_clock_init      osfmk_kern_ipc_clock.o         T 0x64
port_name_to_clock  osfmk_kern_ipc_clock.o         T 0x6c
clock_count         osfmk_arm_conf.o               D 4      <- the four zero bytes the step turns on
clock_list          osfmk_arm_conf.o               D 0x18   <- struct clock_list_entry[CLOCK_COUNT],
                                                              CLOCK_COUNT 2, two 12-byte entries
```

## The build, and the run

`896` undefined, `800` function stubs, `96` storage — all three predicted to the unit. `__bss_start`
unchanged at `0x80113b58`, bss end `0x80149e08 → 0x80149f48`, headroom `1794552 → 1794232`, and the
image **unchanged at 1132128 bytes**, which is the 284 lesson applied: the read-only region ends at
`0x800fa41c` with 0x1be4 bytes of slack, this object's 3209 bytes of read-only content fit inside it,
so `.data` — and with it `.bin` — does not move. Text 1025040 → **1027984**; the arithmetic predicted
1028168, and the 184-byte difference is the object's read-only content landing partly in sections
that were already aligned.

`clock_oldconfig` links at `0x800bb110`, `ntp_init`'s stub at `0x800e16a4`, and the run reported:

```
stub_hit=ntp_init        xnu_entry_stub_caller=0x800b97e8
```

`0x800b97e8` is `clock_config+0x6c` — the return address of the `bl ntp_init` at `0x800b97e0`, which
is the instruction immediately after the `bl clock_oldconfig` the *previous* step stopped at. So one
run measured that `clock_oldconfig` completes end to end: `arm_usimple_lock_init`, two thread/timer
call setups and everything inside them, and the `clock_list` walk taken zero times.

Preflight clean: `loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`, log
301109 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own.

**Next:** experiment 286 — `osfmk/kern/ntptime.c` (`bsd_kern_kern_ntptime.o`), the object `ntp_init`
is defined in, and the first step of this walk into the BSD side of the clock. The body is worth
reading before the build for the same reason 285's was: `ntp_init` may have real callees and a
storage variable of its own.
