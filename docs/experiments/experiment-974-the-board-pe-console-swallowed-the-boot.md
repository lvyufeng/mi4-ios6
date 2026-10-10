# Experiment 974 — the board PE's console swallowed the boot

**Status:** ✅ **BUILT, PARKED (`armed-d13-73475747`).** After 973 linked the board platform expert,
the boot's console text was routed into a **private 1 KB ring with no reader** — so a boot that may
be *resident* looks like a hang. 974 points the board PE's `uart_putc` at the **entry image's own
captured RAM console**, the same sink `__wrap_vcputc` uses. Entry image `73475747` (6331476 B),
payload `stage90-qcdt.img c70b3c75…` (9351168 B), payload switch record `6c2b6038` **unchanged**.
`make check` 0. **THE PRESS IS THE OPERATOR'S.**

Continues `experiment-973-the-board-platform-expert-was-compiled-but-never-linked.md` (whose press
this explains and repairs the observability of).

---

## 0. The arm

`armed-d13-73475747` is `armed-d13-12427611` (973's board-PE link) **plus one source edit** in
`src/platform/darwin13/pe_msm8974.c`. No `STAGE90_XNU_*` switch moves, so the payload record
`6c2b6038` is byte-identical to 968/970h/971/972/973. Medium unchanged; `HDD_WRITE=0`, fully
reversible.

## 1. What 973's non-return was, and the sink it exposed

973's press did not return; 972 (one rung below) returned clean in ~17 s. The board PE the press now
reaches fills `gPESocDispatch`. `pexpert/arm/common/pe_init.c`:

```
146:        PE_init_SocSupport();
...
151:        PE_kputc = gPESocDispatch.uart_putc;
```

**Before 973 the boot STOPPED at `:146`** — `PE_init_SocSupport` reached the generated stub
(`stub_hit=PE_init_SocSupport_stub`) and never returned — so `:151` never ran, and XNU's console went
through whatever sink the image had put in `gPESocDispatch.uart_putc`. **After 973 the board PE
RETURNS**, `:151` runs, and `PE_kputc` — plus `PE_early_puts` (`pe_serial.c:44` reads the same
pointer) — became our **private 1 KB ring** (`msm8974_console[]`) with **no reader**. Every XNU
message after `PE_init_platform` was discarded in silence.

So the 973 non-return has two readings the host cannot distinguish, and 974 removes the ambiguity:
**residency** (the boot entered the idle path and stays in XNU — `wdt_pets > 0`,
[[mi4-911-resident-nonreturn-is-not-a-wedge]]) vs a **hang/fault**. Before 974 the console was dark
either way.

## 2. The edit

`src/platform/darwin13/pe_msm8974.c` only — no tree edit, no arm switch:

```c
extern void entry_os_console_char(int ch, uint32_t which);   /* the entry image's own sink */

void
msm8974_putc(char c)
{
    entry_os_console_char((int)(unsigned char)c, 2u);        /* which = the serial/uart route */
}
```

`entry_os_console_char` (`T` @0x800043a0, `src/entry/entry_stubs.c:2696`) is the **same** sink
`__wrap_vcputc` uses in `entry_trace.c`: it appends to the RAM console block the runner reads, and
is safe *before* the live tables are installed (it holds text in `.bss`). `which = 2u` is the
source's own tag for the serial/uart route. The private ring (`MSM8974_CONSOLE_BYTES`,
`msm8974_console[]`, `msm8974_console_count`) is deleted.

This is the 458 route, not a second definition of the console.

## 3. By-value verification

In `out/stage90/xnu_arm_entry.elf`:

```
801ae290 <msm8974_putc>:
  801ae290:  mov  r1, #2
  801ae294:  b    800043a0 <entry_os_console_char>
```

`out/xnu_platform_obj_d13/pe_msm8974.o` carries `U entry_os_console_char`, satisfied by the entry
image's `T`. Two instructions — no ring, no gate.

## 4. What the press must show

- **The boot's console text appears** — `xnu_live_ostext_*` in the log (the capture counters) grows
  with `PE_early_puts` text, and the runner's captured `last_kmsg` carries it.
- **Which reading it is.** `wdt_pets > 0` ⇒ **resident** (the goal's "保持在 xnu 里", not a hang).
  A fault repeats its `far`/`fsr`; a hang shows the last key before silence.
- **Falsification:** if the log is still empty after 974, the board PE's `uart_putc` is not the only
  sink on the reached path — record and re-derive.

## 5. Books

Entry image `73475747` (6331476 B); payload `c70b3c75…` (9351168 B); record `6c2b6038` unchanged.
`make check` 0. The `xnu_entry_973` clause still passes — the board PE is the real body at
`0x801ae3c0` (it moved with the shorter `msm8974_putc`).

## 6. Scope / non-goals

- **The 3 GB** is untouched (958; 915-B's low-bank pmap port is separate).
- **The USB ladder** (959–962) is untouched.
- 974 moves **no** arm switch: it is a console-link, and the arm is named by the entry bin hash.