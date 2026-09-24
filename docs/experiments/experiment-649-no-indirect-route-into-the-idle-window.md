# 649: no indirect route into the idle window — the address that matters is materialised nowhere in the image

599 established that on the sleeper arm the idle window is unreachable by construction, and did it with
a tool that counts **`bl` sites**: `tools/check_idle_window_unreachable.py` asserts that each of the
three window wrappers has exactly one `bl` site in the whole image, all inside `cpu_idle` past the
gate, and censuses branches into the window's region. That is a strong argument — and its own coverage
boundary is a **branch** census. A route that is not a branch is invisible to it, and there is exactly
one realistic class of those: a materialised address (a function-pointer table, a literal-pool word, a
`movw`/`movt` pair) reached by an `ldr`/`blx` the checker never sees as a `bl`.

This step measures that direction, on the armed entry ELF, before the press. **The result is clean, and
it is the first independent corroboration of 599 from outside its own method.** No device action, no
build, no arm change: `out/stage90/xnu_arm_entry.elf` is `7f80c2cb…` and `out/stage90/stage90-qcdt.img`
is still `60063c47…`, still **UNRUN**.

## 1. The two calls, counted from the linked image

`arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf` gives the players, and the addresses decide the
reading — `platform_cache_idle_exit` is the routine the fatal `pop` lives in, and the `__wrap_` symbol
is what `--wrap` redirected every reference to:

```
8000d934 T cpu_idle
8000da50 T cpu_idle_exit
800462d4 T platform_cache_idle_exit
8047c81c T __wrap_SetIdlePop
8047c944 T __wrap_platform_cache_idle_exit
```

A full disassembly of the image (1,253,975 lines) carries **exactly one** `bl` to each:

```
8000da3c:  bl 8047c944 <__wrap_platform_cache_idle_exit>   <- inside cpu_idle, 208 bytes past the gate
8047c96c:  bl 800462d4 <platform_cache_idle_exit>          <- inside __wrap_platform_cache_idle_exit
```

So the chain is complete and has no second entry: `cpu_idle`'s gate → the wrapper → the real routine.
The gate itself, read out of the same disassembly, is the shape 599 described:

```
8000d95c:  beq 8000d978            <- !idle_enable        -> the door
8000d960:  ldr r0, [r5, #40]       <- cpu_data->cpu_signal
8000d964:  cmn r0, #1              <- == cpu_signal & 0x80000000
8000d968:  ble 8000d978            <- SIGPdisabled set    -> the door
8000d96c:  bl 8047c81c <__wrap_SetIdlePop>      <- THE GATE
8000d970:  cmp r0, #0
8000d974:  bne 8000d980            <- SetIdlePop() != 0   -> the window
8000d978:  mov lr, pc
8000d97c:  b 8047c7f8 <__wrap_Idle_load_context>   <- the door
8000d980:  ...                     <- the window, and cpu_idle ends at 0x8000da50
```

**And there is a second `bl __wrap_SetIdlePop` that a reader will find and should not worry about** —
`0x8000da20`, inside the window region, which the tool identifies as
`if (cpu_data->rtcPop != lastPop) SetIdlePop();` (`osfmk/arm/cpu.c:150`). It is *downstream of the
window's entry*, so it cannot admit anything: reaching it already means the gate was passed. The tool
knows this and uses the earliest site as the gate; a reader counting sites without the ordering would
see two gates where there is one. It is recorded here because it is the first thing a site count turns
up.

## 2. The address is materialised nowhere

A byte scan of the whole 6.7 MB image for both addresses, in both spellings, with each hit mapped to
its section:

| searched value | occurrences | where |
| --- | --- | --- |
| `0x800462d4` (real exit) | **1** | file offset `0x5b7468` = `.symtab+0x61b44` — the symbol's own table entry |
| `0x800462d5` (same, Thumb bit set) | **0** | — |
| `0x8047c944` (wrapper) | **1** | file offset `0x5a7438` = `.symtab+0x51b14` — likewise its own entry |
| `0x8047c945` (Thumb bit set) | **0** | — |

`.symtab` is not loaded at run time, so those two are the symbols being *defined*, not any code
referring to them. **Nothing else in the image contains either address**, which rules out a
function-pointer table, an `.init_array` entry, and a literal-pool word — the three forms an indirect
call needs.

The disassembly agrees, and it is the form that would catch an address the compiler *assembled* rather
than stored:

| searched form | hits | coverage / liveness |
| --- | --- | --- |
| `.word 0x800462d4` / `.word 0x8047c944` | **0** | against **22,139** `.word` literal-pool lines in the image |
| `movw rX, #25300` (`0x62d4`, the exit's low half) | **0** | `movt rX, #32772` (`0x8004`) appears **78** times, so a `movt`-based pair is a live pattern here |
| `movw rX, #51524` (`0xc944`, the wrapper's low half) | **0** | same |
| `#287444` (`0x462d4`, the exit's offset within `.text`) | **0** | offset-form materialisation from the image base |
| `#4704580` (`0x47c944`, the wrapper's offset) | **0** | same |

**Each zero is paired with a number that shows the search could have seen the pattern** — 22,139 for
the `.word` form, 78 for `movt`, and, as a last liveness check, the symbol name itself appears 6 times
in the same disassembly. That is the discipline `mi4-measurement-defects` states as *establish that the
extractor could have seen it*; a grep that returns zero four times is worthless without it.

## 3. What this does and does not rule out

**It rules out** every route into the window that is not a branch: no data word, no literal pool, no
assembly-time construction of either address. Combined with 599's branch census — every branch
targeting the window's region starts inside it except the gate's own — the window has one way in, and
it is behind a test that cannot be passed on this arm.

**It does not rule out** an address computed at run time from registers (an `add rX, rBase, rY` whose
operands were loaded from elsewhere). It cannot, and no scan of an image can: the claim this step makes
is about *this image's contents*, not about every computation a running kernel could perform. What it
does say is that the inputs to such a computation are not in the image as constants, so a route of that
kind would have to be constructed from values that are — and none of them is either address.

**It does not re-derive 599's conclusion and does not need to.** 599's verdict rests on the
`SIGPdisabled` fixed point; this step takes that as given and asks only whether the checker's blind
side hides a door.

## 4. What it does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The OS is still not observed
  booting; the frontier is unchanged (the idle exit's `pop {fp, pc}` inside the third `poll`, with
  `poll_seq` stopping at 2). What it does is make the *owed* press's pre-registered outcome — the
  witness, `poll_seq=3` with `poll_tmo_max_ms >= 1000` — rest on a property checked from two
  independent directions instead of one.
* **It changes no artifact.** No build, no `out/` write, no arm, no park, no gate, no runner. The two
  staged repair sets (637/640 in the gate, 646 in the runner) are untouched and still deferred for the
  owed log.
* **It is not a new tool.** The scan is ten lines of Python over the ELF and a disassembly grep; it is
  written down here rather than added to `tools/` because its subject is one image and its result is
  yes/no. If a future arm is built, this is a check worth re-running on it by hand — and worth not
  trusting me about.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 5. Safety

Host-side and read-only. Three reads: `arm-none-eabi-nm`, `arm-none-eabi-objdump -d` and a Python byte
scan, all against `out/stage90/xnu_arm_entry.elf`; no `fastboot`, no `adb`, no `sudo` against a device,
nothing written to storage, so no outcome here can write to storage and a brick is impossible by
construction. `fastboot boot` only, never `flash`. Both catchers were alive throughout (relay pid
4067419; catch 2 of 16, pid 1680547, process start 2026-09-24 01:55:07, its own bound 07:58:49).
`git status --porcelain` empty before the commit that adds this file.
