# 1006 press-read — adversarial verification

Verdict: **CLAIM CONFIRMED (with one overstated link).** The record's operational
conclusion — *the ~24.4 s freeze does not overlap the idle path* — is supported. One
sentence ("its total absence is not the record cap … never entered") slightly overstates
the inference: the baseline keys are gated by `entry_live_ready()`, not written
unconditionally, so the absence strictly means "never entered **with the channel up**."

## By value

### 1. `entry_freeze_sample` is the first statement of `__wrap_machine_idle`?
`src/entry/entry_trace.c:2285` `__wrap_machine_idle`; the call is at `:2308`, guarded by
`#if defined(STAGE90_XNU_FREEZE_WITNESS) && STAGE90_XNU_FREEZE_WITNESS` — a build switch
that is ON for this elf (the elf carries `xnu_live_freeze_*` literals). It precedes
`current_proc()`/`entry_note_idle`. No early return before it.

Disassembly (`out/stage90/xnu_arm_entry.elf`):
```
8049dba8 <__wrap_machine_idle>:
8049dbc4: bl 800068f4 <entry_freeze_sample>   ; first call
8049dc04: bl 80006da8 <entry_note_idle>
```
`build_1006.sh:854` asserts the call is image-wide once and inside the wrapper. Sole
caller of the wrapper is `processor_idle+0x1a4` (`8007bc08 bl 8049dba8`); wrapper is `T`.
CONFIRMED.

### 2. Direct uncapped channel?
`entry_freeze_sample` (`entry_stubs.c:5356`) writes `xnu_live_frzbase_n/_d` and
`xnu_live_freeze_*` via `entry_write_kv`, **not** `entry_live_write`. `entry_write_kv`
(`:2627`) appends to the RAM console at `RAM_CONSOLE_BASE`, bound `0x200000-12`; it has
NO `ENTRY_LIVE_CAP` test. CONFIRMED: the sampler's own writes are uncapped.

BUT — **this does not make the keys unconditional.** `entry_freeze_sample` returns early
at `:5364` `if (entry_live_ready() == 0u) return;` *before* the `c <= 4` baseline block.
So the frzbase keys are emitted only when the channel is installed. See §3.

### 3. `entry_live_ready()` gate?
```
static/inline uint32_t entry_live_ready(void){ return (g_live_state == 1u) ? 1u : 0u; }
```
`g_live_state` is set to 1 only in `entry_live_init` (`:2453`), reached from
`entry_live_write`'s first call (`:2522-2523`). So `entry_live_ready()==0` is possible
before the channel's first (usually early) write. **This alone could explain the
absence without "never entered machine_idle."** It is not the *whole* story here (§6
shows the channel was up before the exec tail), but the claim's phrase "its total absence
is not the record cap … the wrapper was never entered" skips this third option:
*entered before the channel was up*. Flagged as a gap in the reasoning, not fatal to the
conclusion.

### 4. strings / capture counts
| artifact | `xnu_live_idle_seq` |
|---|---|
| `out/stage90/xnu_arm_entry.elf` (1006) | 1 |
| `out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf` (1005) | 1 |
| `...20261014-040919` (1006 press) | 0 (`xnu_live_idle_` total 0) |
| `...20261014-020404` (1005 press) | 0 |
| `...20261013-160126` | 0 |
| `last_kmsg-983-20261011-235804` | 1 (`xnu_live_idle_` total 7) |

1006 elf `xnu_live_freeze` literals = 6; frozen 1005 elf = 0 (witness is 1006-only).
All match the claim exactly. CONFIRMED.

### 5. Reachability / "different loss mechanism"
- wrapper reachable: sole caller `processor_idle`; `nm` `T __wrap_machine_idle`,
  `T machine_idle` both present. The `--wrap` link rule is not spelled in the checked-in
  scripts (grep found no `-Wl,--wrap=machine_idle`), but the `bl __wrap_machine_idle`
  from `processor_idle` and the exit `b machine_idle` prove the redirection is in the pool
  link. CONFIRMED reachable.
- Loss by cap/overwrite: **capture carries `xnu_live_capped=0x00006000`** = 24576 =
  `ENTRY_LIVE_CAP`, and `entry_live_write` printed it at `g_live_records == CAP`. The
  counted channel IS at cap. That does not touch the sampler (uncapped), but it proves
  the run is a full-length one, which strengthens §6.
- Console not full at the tail: the LAST records in the capture are `xnu_live_stall_*`
  (direct, uncapped) climbing `n=0x0d..0x0f` into the live register frame
  (`sp=0xcfdbbf00 lr=0x8046b95c`), then `No errors detected`. Direct records plainly
  survive to the terminal stall frame — so "console full / overwritten" is refuted.

### 6. Bounding the claim (depth vs 1005)
1006 capture: `xnu_live_exec_done_probe_seq=1`, `_sig=0x43474244`, `_records=0x444d`,
`_state=1`, and `launchd` + `cs_invalid_page` reach the same depth as 1005. The run
reached the same launchd/exec/walk tail as 1005 — not an earlier death. And because
`_state=1` at exec-done, the channel was already installed by the exec tail, so any idle
pass in that tail would have published frzbase. The absence is therefore *not* explained
by "entered before the channel was up": it is explained by "never entered." §3's ceiling
is thus weak for the tail, and the record's operational conclusion holds.

## Most important caveat
The inference "frzbase absent ⇒ never entered `machine_idle`" is stated as if the baseline
keys were unconditional; they sit behind `entry_live_ready()`. The chain only closes
because the channel is independently shown to be up before the exec tail
(`exec_done_probe_state=1`), which is not stated in the record. State the gate when
citing this rung, or the next reader inherits a claim stronger than the code supports.