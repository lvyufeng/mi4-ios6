# Experiment 286 — `bsd_kern_kern_ntptime.o`, and a prediction that is a chain rather than a name

**Step:** link the object that defines `ntp_init` — `osfmk/kern/ntptime.c`,
`bsd_kern_kern_ntptime.o` — the name the 285 run stopped at.
**Prediction:** that the frontier moves **four functions** past the stop, to `coalitions_init` —
which required predicting that `ntp_init`, `clock_config`, `machine_init`, `clock_oldinit` and
`ledger_init` all complete. Predicted caller `0x8000e228`.
**Result:** exactly that. **`stub_hit=coalitions_init` at `xnu_entry_stub_caller=0x8000e228`**, with
all three build counts unchanged exactly as predicted — the first step in a long while whose stub
counts do not move at all.

## Why the prediction is a chain

The 285 step's own shape — a step that overshoots — is the normal case now, and this one overshoots
by five calls. The whole argument, written into the ledger before the build, with each disposition
measured in the *linked image* rather than read off the source:

```
ntp_init        lck_grp_attr_alloc_init, lck_grp_alloc_init, lck_attr_alloc_init,
                lck_spin_alloc_init, nanoseconds_to_absolutetime, timer_call_setup
                                                    - all six real -> completes
clock_config    after ntp_init, one tail call: nanoseconds_to_absolutetime, real -> completes
machine_init    `is_clock_configured = TRUE; if (debug_enabled) pmap_map_globals();`
                                                    -> returns, because debug_enabled is 0
clock_init      `b clock_oldinit` (real since 285); clock_oldinit references only clock_count
                and clock_list and makes no call at all -> completes
ledger_init     5 instructions ending in `b lck_grp_init`; lck_grp_init calls __bzero,
                strlcpy and lck_mtx_lock, all real -> completes
coalitions_init ***STUB***  <- the stop
```

## `debug_enabled` is the hinge, so it was measured

The prediction turns on `machine_init` taking its `if (debug_enabled)` branch as false — if it were
true, the run would tail-call `pmap_map_globals` and stop somewhere else entirely. So the variable
was read three ways rather than assumed:

- **Its declaration:** `SECURITY_READ_ONLY_SPECIAL_SECTION(volatile uint32_t, "__TEXT,__const")
  debug_enabled = FALSE;` (`pexpert/arm/pe_init.c:39`).
- **Its only writer in the tree:** `pe_init.c:350`, which `bcopy`s it out of the device tree property
  `/chosen/debug-enabled`. The payload's synthetic `/chosen` carries name, boot-args, stdout-path,
  ram-console-reg, random-seed and (with the consistent-debug switch) consistent-debug-root — and no
  `debug-enabled`. `PE_init_platform` is real in this image, so the lookup does run and finds nothing.
  The boot-args line does contain `debug=0x144`, and that is worth stating explicitly: it sets
  `debug_boot_arg`, which `PE_i_can_has_debugger` *reports*, and it does **not** set `debug_enabled`.
- **Its value in the image:** at `0x800fa640`, the first four bytes of `__TEXT,__const`, the .bin
  contains `00000000`.

The falsifier was named in advance and is cheap: `pmap_map_globals` is real and has no `bl` in its
first 0x60 bytes, so a report naming it would mean `debug_enabled` was set somewhere this reading did
not find — a measurement about the device tree rather than about this step. It did not happen.

## The object, measured

3068 bytes of text, 12 of data, 172 of bss, 35 of `rodata.str1.1`, 8 definitions, 24 references.
Resolves **2** (`ntp_init`, `ntp_update_second`), adds **2** (`mac_system_check_settime` from
`security_mac_system.o`, 0xf0, and `nanotime` from `bsd_kern_kern_time.o`, 0x2c) — all four are
functions, so **all three stub counts are unchanged by this step**, which is the first time that has
happened in a long while. The other six definitions (`adjtime`, `ntp_adjtime`, `ntp_get_freq`,
`ntp_gettime`, `time_esterror`, `time_status`) are names nothing references yet; the last two are the
object's own `.data`, 4 bytes each, and they are the reason the image grows by 12 rather than by 0.

## The build and the run

896 undefined, 800 function stubs, 96 storage, `__bss_start` 0x80113b58 → 0x80113b60, bss end
0x80149f48 → 0x8014a008, headroom 1794232 → 1794040 — all three counts and both invariants exactly
as predicted. Text 1027984 → **1031088** and image 1132128 → **1132136**; the arithmetic predicted
1031052 and 1132140, so text came out 36 bytes larger than the object's sections suggest and the image
4 bytes smaller. Then:

```
stub_hit=coalitions_init        xnu_entry_stub_caller=0x8000e228
```

`0x8000e228` is the return address of `bl coalitions_init` at `0x8000e224` in `kernel_bootstrap` — the
fourth of the eight `bl`s in that run of the bootstrap, between `ledger_init` and `task_init`.

So one run measured seven functions: `ntp_init` completes with all six of its lock and timer calls
real; `clock_config` completes; `machine_init` takes its `debug_enabled` branch as the linked word
said; `clock_init`'s tail call into `clock_oldinit` returns; and `ledger_init`'s tail call into
`lck_grp_init` returns. Four of those were named in advance as things that would *not* stop the run.

Preflight clean: `loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`, log
301116 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own.

**Next:** experiment 287 — `osfmk/kern/coalitions.c` (`osfmk_kern_coalitions.o`), the object
`coalitions_init` is defined in. Worth reading the body before the build for the reason 285 and 286
both were: `coalitions_init` may complete as well, in which case the frontier moves again without
this object's name appearing anywhere in the report.
