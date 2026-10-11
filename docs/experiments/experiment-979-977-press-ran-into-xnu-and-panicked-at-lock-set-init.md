# Experiment 979 — 977's press DID return a log: XNU ran into kernel C init and panicked at `lock_set_init`

**Status:** ✅ **RECOVERED AND LOCALIZED (2026-10-11).** The press of `armed-d13-d693864f`
(2026-10-11, §5 of experiment-977) did **not** leave a dark phone with a lost log. Its previous-boot
RAM console was recovered by `scripts/recover_last_kmsg.sh` and **it is a real XNU boot**: the kernel
passed `arm_vm_init`, `pmap_bootstrap`, the VM/zone/scheduler init, printed `Darwin Kernel Version`,
and then took a **kernel prefetch abort panic** at the **first instruction of `lock_set_init`**, called
from `ipc_bootstrap`. This **falsifies** 977's own theory (§2: "the SoC watchdog reset a pet-less
resident boot before the USB ladder ran"). XNU got far past the ladder site; nothing about the pet was
the cause. The frontier is a **kernel-text fault inside `ipc_bootstrap`**, not the watchdog.

Follows [[mi4-977-resident-d13-arms-the-watchdog]] §5, [[mi4-975-window-on-the-working-entry]] §6.

---

## 1. The recovery (NOT a press)

The operator rebooted to fastboot; `preflight` gate not involved. `scripts/recover_last_kmsg.sh` sent
a **plain `fastboot reboot`** into the installed ROM (no `fastboot boot`, nothing flashed) and read
`/proc/last_kmsg` = the **previous** boot's console. Capture (428065 B, 4097 lines):
`out/stage90/captures/recovered-armed-d13-d693864f-20261011-042933-last_kmsg.txt`.

The log self-identifies as ours (`MI4IOS6_STAGE90`), handoff mode `0x2`, entry ladder level `0x4`,
and carries the full D13 entry record (`xnu_entry_status=0x90000001`, `xnu_entry_failures=0`) — i.e.
the entry line that BOOTS, the one 975 was built on. **`xnu_entry_args_memSize=0x1e400000`** — 975's
484 MiB window rung **ran** (this was the open question 975 §6 named).

## 2. What XNU actually did (read off the log, by value)

The payload announced the jump, then the kernel's own console took over (`[os-console-459]`):

```
arm_vm_init: L2 address for identity mappings...
arm_vm_init: switching translation-tables now...
arm_vm_init: setting up segment information...
pmap_static_init: Bootstrapping pmap
cache: initializing i+dcache ... done
unknown CPU (ID = 0x512f06f1)
Cache level 2: 2048KB/128B ...
Darwin Kernel Version ###not-built-by-apple###
pmap_steal_memory: C0001000 - C0650810; size=00576810
vm_page_bootstrap: 119339 free pages and 667093 wired pages
PMAP: enabled ASID support
kext submap [0x80000000 - 0xfffeffff], kernel text [0x80000000 - 0x805699a0]
Scheduler: Default of traditional
standard timeslicing quantum is 10000 us
...
```

So the kernel reached **kernel-C initialisation**: `pmap_bootstrap`, cache init, the bootstrap
allocator (`vm_page_bootstrap`), ASID setup, and the scheduler parameters. The `kext submap` /
`kernel text` line is the kernel stating its own **text extent: `[0x80000000 - 0x805699a0]`**.

## 3. The panic — pc/lr resolved by value

```
panic(cpu 0 caller 0x846bb4b0): Kernel prefetch abort. (faulting address: 0x80569938)
   r0: 0xc0b32ce0  r1: 0x00000506  r2: 0x00000001  r3: 0x0000fbff
   r4: 0x8063a850  r5: 0x806401ec  r6: 0x00000001  r7: 0x00000000
   r8: 0x9e30040e  r9: 0x0f221a98 r10: 0x80000000 r11: 0x80000000
  r12: 0x8061bba8 sp: 0x8056bfb0  lr: 0x80565834  pc: 0x80569938
 cpsr: 0x80000193 fsr: 0x0000000f far: 0x80569938
Debugger called: <panic>
panic: We are hanging here...
```

`arm-none-eabi-nm -n out/stage90/xnu_arm_entry.elf` resolves both:

| value | symbol |
|-------|--------|
| `pc 0x80569938` | **`lock_set_init` +0x0** (the function's first instruction) |
| `lr 0x80565834` | **`ipc_bootstrap` +0x194** |
| `far 0x80569938` | = pc (the faulting address is the instruction address) |
| `fsr 0x0000000f` | permission fault, prefetch (instruction fetch) |

The disassembly closes it — `ipc_bootstrap+0x190` **is** the call, and its return address **is** `lr`:

```
80565830:  bl   80569938 <lock_set_init>     <- lr = 0x80565834
80565834:  bl   80565d00 <mk_timer_init>
```

The kernel's live-slot instrument agrees independently: `xnu_live_slot_ab_m4=0x80565830` (=the `bl`),
`xnu_live_slot_ab_m8=0x80569938` (=pc). And `xnu_live_slot_rtcab_*` shows the abort handler fired
(`rtcab_calls=0x1`, `rtcab_rin=0x2`).

**Reading:** XNU faulted **fetching the first instruction of `lock_set_init`**, entered from
`ipc_bootstrap`. The abort handler ran; there was no way to recover; the kernel printed
`We are hanging here...` (`panic: We are hanging here...`). This is a **kernel text / mapping fault**,
not a watchdog reset.

## 4. Where that address sits (a fact, ownership of the cause open)

`lock_set_init` is the **last function in `.text`**:

```
80569854 T udp_init
805698f4 T journal_init
80569938 T lock_set_init     (size 0x3c)
80569978 t __hw_lock_init_from_arm
80569980 t __OSAddAtomicLong_from_arm
805699a0 T __entry_text_end  = __entry_data_start
```

`.text` = `[0x80000000, 0x805699a0)`, size `0x5699a0`; `.data` starts the **next section at
0x8056a000** (sections are 4 KB-aligned). So `lock_set_init` lives in the **final 4 KB text page
`[0x80569000, 0x8056a000)`**, and that page's end (`0x805699a0`) is **not page-aligned** — the page is
only ~0x9a0 bytes of code and then padding to the section boundary. The kernel's own text map is
declared as `[0x80000000 - 0x805699a0]` (the `kext submap` line).

**Hypotheses for the fault (NOT yet separated — each is a different next experiment):**

1. **Text-map extent.** `arm_vm_init`'s `etext = sectTEXTB + sectSizeTEXT` (tree
   `arm_vm_init.c:477`) is taken from the **Mach-O `__TEXT` segment size**; the identity map is laid
   by `l2_map_linear_range(…, gPhysBase - sectionOffset, gPhysBase + gMemSize)` (`:398`). If the text
   map (or its page-table walk) ends at a boundary that excludes the final page's PTEs, the last text
   page is unmapped for fetch while the page **before** it (`ipc_bootstrap`, page `0x80565`) executed
   fine. Candidate: a page-vs-section or round-to-page mismatch at the `.text`/`.data` seam.
2. **Permission at the seam.** The final page may be mapped with fetch permission cleared (XN/W^X) if
   the map reused the following page's attributes (`.data`, no-exec). `fsr=0x0f` is a *permission*
   fault specifically — consistent with either an absent PTE (translation) surfaced as section
   permission on this core, or a genuine NX attribute.
3. **Segment-size misstatement.** `aria` — the Mach-O must describe the segments the boot code does
   arithmetic with ([[mi4-macho-must-describe-the-segments-boot-code-does-arithmetic-with]]); if
   `__TEXT`'s stated size rounds the wrong way, `etext`/`vm_kernel_etext` is short by a page.

**What is NOT the cause (falsified):** the pet. The run **never reached `machine_idle`**
(`xnu_live_door_seq` is absent — the summary's own "UNREAD" branch says the boot did not reach idle at
all), so `entry_wdt_pet` had no call site to fire from. `xnu_live_wdt_pets` being absent is a
**behavioural** absence (the boot died earlier), exactly as the summary's key-kind table predicts —
**not** the "no pet linked" defect 977 §2 proposed. 977's fix is neither necessary nor sufficient
here; the run's blocker is the kernel text fault.

## 5. What this changes

- **975's window question is answered:** `memSize=0x1e400000` ran. The 484 MiB window is not the
  blocker.
- **977's theory is falsified** and its memory entry is corrected. The USB ladder / pet are downstream
  of `ipc_bootstrap`; they were never reached because the boot panics in `ipc_bootstrap`.
- **The frontier moved into the kernel text map**, at the `.text` → `.data` seam, on the last text
  page. The next rung is to separate hypotheses 1–3 by value (read `l2_map_linear_range` and the
  generated Mach-O `__TEXT` size against `__entry_text_end`), then fix the one that holds. Each is a
  host-side, build-checkable fact — no new press required to *identify* it.

## 6. The owed reading — done; the next action

The recovery is **done** (this capture). The device came back (the plain ROM reboot reached Android,
exposing the log). **No press is currently pending.** The next step is host-side: resolve hypotheses
1–3 from `src/entry/build_entry.sh`'s section sizing, `entry.ld`, the generated Mach-O, and the tree's
`arm_vm_init.c` `l2_map_linear_range`. **PRESS IS THE OPERATOR'S** when the fix is built and parked.

*Provenance: recovery capture
`out/stage90/captures/recovered-armed-d13-d693864f-20261011-042933-last_kmsg.txt` (the runner's own
`--summarise` output contradicts itself under tool-render corruption and is NOT relied on here; every
value above was read by direct grep/awk/python over the capture). Symbols and disassembly from
`out/stage90/xnu_arm_entry.elf` via `arm-none-eabi-nm -n` / `objdump -d`. Tree source
`external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c:398,450-486`. Follows
[[mi4-977-resident-d13-arms-the-watchdog]], [[mi4-975-window-on-the-working-entry]],
[[mi4-970-the-window-is-ram]], [[mi4-silence-is-a-reading-only-if-success-is-silent]].*