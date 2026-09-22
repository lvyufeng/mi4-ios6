# 557: the cache window is entered exactly once, and that first entry is the death

A host-side reading of 520's own log and of the frozen tree's `cpu_idle`, no device and no build. It
produces three facts the phase did not have:

1. **`cpu_idle` has two ways out to `Idle_load_context`, and only one of them is inside the cache
   window.** The other one skips the window *and* the `WFI` entirely, and it is the one a pass takes
   while the idle door is shut.
2. **520's boot took that bypass ≥32768 times and the window exactly once**, which four independent
   per-pass publishers in the log each attest, and the exit of that single pass is where it died.
3. **The ram console holds two writers in two blocks, and the file's line order is chronological only
   *inside* one of them** - so the panic text's position relative to the key records means nothing.
   This is a trap this session's own first reading fell into.

So "survive the first idle pass" - the phase's phrase for its frontier - is now a *measured* statement
rather than a slogan: the first pass through the cache window is the only pass through it, and it fails.

## 1. The two exits, from the source and from the bytes

The source (`external/xnu-4570.1.46/osfmk/arm/cpu.c:117-124`) opens the loop with two ways to leave it:

```c
void __attribute__((noreturn))
cpu_idle(void)
{
	cpu_data_t     *cpu_data_ptr = getCpuDatap();
	uint64_t	new_idle_timeout_ticks = 0x0ULL, lastPop;

	if ((!idle_enable) || (cpu_data_ptr->cpu_signal & SIGPdisabled))
		Idle_load_context();
	if (!SetIdlePop())
		Idle_load_context();
	lastPop = cpu_data_ptr->rtcPop;

	pmap_switch_user_ttb(kernel_pmap);
	...
```

and this image (`out/stage90/xnu_arm_entry.elf`, `cpu_idle` at `0x8000d934`) compiles exactly that:

| address | instruction | which line of the source |
| --- | --- | --- |
| `0x8000d938`-`:94c` | `movw/movt` 0x80551910, `ldr` | `idle_enable` - the map says so: `0x80551910 idle_enable`, `cpu_common.c:67` |
| `0x8000d954`-`:95c` | `cmp r0,#0` / `beq 0x8000d978` | `!idle_enable` |
| `0x8000d960`-`:968` | `ldr r0,[r5,#40]` / `cmn` / `ble 0x8000d978` | `cpu_signal & SIGPdisabled` |
| `0x8000d96c`-`:974` | `bl __wrap_SetIdlePop` / `cmp r0,#0` / `bne 0x8000d980` | `if (!SetIdlePop())` - inverts, so non-zero continues |
| **`0x8000d978`-`:97c`** | `mov lr, pc` / `b __wrap_Idle_load_context` | **the bypass**: `Idle_load_context();` - *no* `WFI`, *no* cache window |
| `0x8000d980` … `0x8000da24` | `pmap_switch_user_ttb`, the notify blocks, `kpc_idle` | the rest of the loop |
| `0x8000da28` | `bl __wrap_platform_cache_idle_enter` | 546's window opens |
| `0x8000da38` | `bl __wrap_cpu_idle_wfi` | the sleep |
| `0x8000da3c` | `bl __wrap_platform_cache_idle_exit` | **where this boot dies** |
| `0x8000da40`-`:44` | `mov r0,#1` / `bl ClearIdlePop` | |
| `0x8000da48`-`:4c` | `mov lr, pc` / `b cpu_idle_exit` | the tail, which then tail-branches to `__wrap_Idle_load_context` at `0x8000dad0` |

Both the bypass (`0x8000d978`) and the full path (`0x8000dad4`) call the **same** wrapper
`__wrap_Idle_load_context`, so the door record counts both and cannot by itself say which one a pass
took. (`Idle_load_context` has no other callers in this image - two `mov lr, pc; b` sites in
`cpu_idle`/`cpu_idle_exit` and the wrapper's own `bl`.)

**The bypass is what a shut door buys.** `SIGPdisabled` is the bit 513 named the door, and its only
clearer is the platform's IPI delivery - a uniprocessor port's absent event - so while it is set *every*
pass leaves at `0x8000d978` and `SetIdlePop` is never called at all. `idle_enable` is `TRUE` here
(`PE_parse_boot_argn("jtag")` absent, `cpu.c:547-551`, and the log's own `door_en=1`), so the first
operand never fires; the door is the only reason the bypass is ever taken on this hardware.

## 2. What the log says: four per-pass publishers, three of them with one record

`docs`' rule holds that a count published on a schedule is not a total - all of these publish on
`n <= 4 || (n & (n-1)) == 0` (`entry_stubs.c`), so the *number of records is bounded* and what it
proves is the inverse: **a site that publishes once was called once**, because a second call would have
published `n = 2`.

| key | records in `/tmp/cancro-last_kmsg.txt` | the site | what its count means |
| --- | --- | --- | --- |
| `xnu_live_door_seq` | **16** at `:7895`…`:8115`, last `0x8000` | `__wrap_Idle_load_context` (both paths) | ≥32768 completions of the loop's tail |
| `xnu_live_sip_seq` | **1** at `:8295`, `sip_ret=1`, `sip_site=0x8000d810` | `SetIdlePop` | called **once** - so exactly one pass got past the door test, and it returned *true* |
| `xnu_live_pce_seq` / `_after_seq` | **1** each at `:8300` / `:8312` | the enter wrapper's note | the cache window was entered **once** |
| `xnu_live_slot_pre_calls` / `rtcpre_calls` | **1** each at `:8329` / `:8337` | the exit wrapper's pre bracket | the window's exit was entered **once** |
| `xnu_live_slot_ab_calls` / `rtcab_calls` | 5 each (`1,2,3,4,8`) | the abort-path notes | **not** per-pass - they track the nine abort episodes |
| `xnu_live_sleh_seq` | 8 at `:7643`…`:8228` | the abort handler | 8 aborts published, the 9th is the fatal one |

Four independent per-pass sites agree: the window was entered once. And `sip_ret=1` is the reason it
was entered at all - `if (!SetIdlePop())` bypasses, so only a *true* return reaches
`platform_cache_idle_enter`.

## 3. The chronology, which the log's own writer makes readable

`entry_live_write` appends at the buffer's size field, which is its cursor (`entry_stubs.c`, the comment
above `ENTRY_OS_BLOCK`: *"Records append at the buffer's size field, which **is** their cursor"*), so
**line order within the records is chronological**. Read that way, the tail of 520's boot is:

```
:7643-:7795   abort episodes 1-4 (with the two abort-path slot notes)
:7842, :7857  abort episodes 5, 6 - the user-mode ones, pc 0x1118 and 0x1124
:7895-:8115   the door, 16 records, ending at 32768  <- >=32768 bypass passes, no window, no WFI
:8188, :8228  abort episodes 7 (pc 0x11a4) and 8 (pc 0x800176e0)
:8248, :8256  the abort-path slot notes reach 8
:8295         SetIdlePop, called for the first time, returning 1   <- the door has opened
:8300, :8312  the enter wrapper's note, first call
:8329, :8337  the exit wrapper's pre bracket, first call
:8341-:8350   THE FATAL EPISODE: storm=9, pc=0x04b79074, lr=0x800462dc
```

**And the console text is not in that order at all.** The panic's human-readable dump sits at `:4002`
and `MACH Reboot` at `:4009` - *above* the records - because the capture holds two blocks: the records,
and `ENTRY_OS_BLOCK` = 128 KB reserved once and space-filled ("*a second cursor cannot be added to that
field*", same comment). Reading `:4002` as "before" `:7895` is a reading of the file's layout, not of
time - and it is the mistake this session made first: the panic at `:4002` and the keys at `:8341` are
the *same* death, carried twice, once as text and once as fields (547 §1 read the first and this step
reads the second).

## 4. What this settles, and what it does not

**Settles.** The frontier's shape: the boot reaches user mode (aborts 5-7 are user `pc`s), runs the idle
loop ≥32768 times **entirely on the bypass** - no cache window, no `WFI` - and then, on the first pass
that has both a cleared door and a non-zero `SetIdlePop`, enters `platform_cache_idle_enter`, reaches
the exit, and dies at the exit's `pop {fp, pc}`. 547's mechanism is therefore exercised on **one** pass
per boot, which is why the enable-off arm produces a panic and a reboot rather than a storm, and why
521/522/526 - which change what happens *inside* that window - take the log with them instead.

**Does not settle.** Whether the mechanism is the mechanism (547 §3's stale-L2 reading is still a
hypothesis, and this step does not test it), and it says nothing about the 32768 bypass passes except
that they never entered the window. The counter `door_now` spans `0x04a00a8a` → `0x04a91f52`
(595,144 counts over the 16 records) but **the counter's rate is not established here**, so no per-pass
cost is derived from it.

**One consequence for the phase.** 533's instrument predicts `pre_calls` and `rtcab_calls` published
with `post_calls` absent, and this reading says why that is the whole of what a run of it can say: the
pass that publishes them is the only pass there will be. It also means the run is *cheap in passes and
expensive in boots* - one cache-window entry is the entire experiment, so a non-return costs a power
press and nothing else can be recovered from it.

## 5. Safety

No device action. `objdump -t`/`-d` over `out/stage90/xnu_arm_entry.elf`, `grep`/`sed` over the already
captured `/tmp/cancro-last_kmsg.txt`, and `grep` over `external/xnu-4570.1.46/osfmk/arm/cpu.c` and
`entry_stubs.c`. Nothing written outside `docs/` and the memory files; no build run; no file under
`out/` touched; `flash` not used, nothing written to storage. Frozen pair untouched
(`1daaf44e624563694e…` / `f202f2465886aba6…`), and the device is off the bus awaiting a power press.
