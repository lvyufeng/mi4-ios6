# 607: the ceiling is non-use, not unreachability — and a pet is one store

606 drew a consequence from a number the gate has printed since 584 and printed it in the gate's
`--allow-xnu-entry` narration: *the payload arms the net before the jump, nothing in the image that runs
afterwards carries its page, and so the SoC resets the machine on its own clock* — a run of this arm is
capped at 28 s.

**The cap is right. One of its legs was stated the wrong way round.** The gate's scan measures that no
instruction in the entry image materialises an address in `[0xf9010000, 0xf901ffff]`. That is true, and
it is not the same claim as "the registers are unreachable" — 584's own paragraph, immediately above,
says so in as many words. The reachability question has a **measured** answer and it is the opposite one:
**this image maps the watchdog's registers, on every boot.**

That changes the cost of the only alternative 606 offered, so it is corrected here before anything is
built on it.

Host-side only: one narration block corrected in `preflight_boot_check.sh`, reads of two archived
captures and of the payload's and entry image's own sources. No build, no device, no `fastboot`, no
`adb`, nothing written to storage, no boot. **TWRP stays withheld.**

## 1. The registers are mapped, and the measurement is in two captures

`entry_gic_probe`'s first act is `entry_mmio_section(STAGE90_GIC_DIST_BASE, STAGE90_GIC_DIST_BASE, …)`
(`entry_gic.c:377`) — and the file says what that is: *"one 1 MB section descriptor into XNU's own L1"*
(`entry_gic.c:109`). `STAGE90_GIC_DIST_BASE` is `0xf9000000`. **The watchdog is at `0xf9017000`, inside
that megabyte.**

Both archived captures carry the descriptor the install produced:

| key | value | what it decodes as |
| --- | --- | --- |
| `xnu_live_gic_map` | `0x00000001` | the mapping succeeded (a 0 would mean the slot was occupied and the probe returned without reading anything) |
| `xnu_live_gic_desc` | `0xf901040e` | bits[1:0] = `0b10` → **section**; bits[31:20] = `0xf90` → base **`0xf9000000`**, covering `0xf9000000–0xf90fffff` |
| `xnu_live_attr` | `0x0000000c` | `ARM_TTE_BLOCK_ATTRINDX(3)`: the PRRR field that is 0, which `SCTLR.TRE` remaps to **Strongly-ordered** |

So the mapping is not incidental to the watchdog — it is the *right kind* for it, and a store through it
would land uncached. The `hw_watchdog.c` header says the same thing from the payload's side, and said it
years of steps earlier: *"0xf9017000 is inside the 1 MB section 0xf9000000-0xf90fffff, which both the
identity table and the candidate L1 already map as MMIO, so this needs no new mapping."*

## 2. What the scan actually establishes

A store to `0xf9017004` requires the address to be materialised — a literal-pool word, a
`movw`/`movt` pair, or a `mov`/`mvn` immediate. The gate scans for all three in
`[0xf9010000, 0xf901ffff]` and finds **zero**, with the payload as a positive control (it arms the net,
so it must carry the page, and it does: 10 `movt`).

So the scan **does** establish the conclusion — *nothing in this image stores to the watchdog's
registers* — as an indirect consequence. What it does not establish, and what the narration must not
say, is *cannot*. The honest sentence is **reachable and unused**, and the ceiling rests on the second.

This is not pedantry, and the reason is the next section.

## 3. The corrected cost of the alternative

606 §4 said the choice was *either not arm the net, or pet it from XNU — "a new driver in the entry
image, which is a new build — and a new build replaces the parked arm."*

The last clause is true and the first is wrong twice over:

* **no new mapping.** The 1 MB section is installed by the GIC probe on every boot of this image
  (§1), so the registers are already addressable from privileged code by the time XNU runs.
* **no new driver.** A pet is **one store to `0xf9017004`** (`WDT0_RST` = 1, the same register the
  payload's own `hw_watchdog.c` pets and the vendor driver's pet path writes). The entry image already
  has wrappers on paths XNU runs repeatedly — `__wrap_thread_quantum_expire`, `__wrap_machine_idle`,
  `__wrap_thread_block` — so the placement is a choice among existing call sites, not new machinery.

What remains true is the cost that actually matters: **any new build of the entry image replaces the
parked arm** (`60063c47…`), so the press that is owed now would be spent on a different question. That
is a decision about *when*, not about *feasibility*, and it is the user's.

## 4. And the pet is self-limiting in exactly the way a real kernel watchdog is

Worth stating because it is what makes the option safe rather than a hole in the net: a pet driven by
the **tick** stops when the tick stops. A hang that kills interrupts — the `pop {fp, pc}` state, an abort
storm, a spin with interrupts masked — also kills the pet, and the net still fires and still brings the
phone back. That is how Android's own `msm_watchdog_v2` works: the watchdog is armed and the kernel pets
it; the pet is *evidence of liveness*, not a way to silence the net.

The design question is therefore **placement, not mechanism**. A pet placed on a path that runs while
the machine is wedged — a spin loop, or anything reachable with interrupts off — would defeat the net and
turn a recoverable hang into a power press that eats its own log. A pet on the tick cannot. That is the
one thing a step building this would have to get right, and it is a property checkable at build time
(the same way the gate already measures reachability): the pet's call sites must be a subset of the
interrupt-driven paths.

## 5. What this does not do

* **It does not boot anything, and it changes no arm, payload, prediction or gate verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not build the pet**, and it does not change the ceiling's arithmetic: 28 s stands, and a run
  of this arm still ends on the net.
* **It does not make the pet obviously right.** Not arming the net is the other option and it trades the
  safety property the user has been explicit about — the recovery reference's own record is that a
  net-less hang costs a power press *and* the log. Both options are the user's call.
* **It does not change 594's witness or falsifier.** `xnu_live_poll_seq=3` with
  `poll_timeout_ms >= 1000` is still the prediction, and `poll_seq` still 2 is still the falsifier.
* **It does not sweep the `FAIL` branches** (603 §7, 604 §5) — still owed.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
host-side file edited (`stages/stage90/preflight_boot_check.sh`, not in `xnu_arm_entry-sources.txt`), in
its `--allow-xnu-entry` narration only — no new `exit`, `fail` or `die`. Gate re-run on this tree:
**EXIT=0 / 537 stdout lines / 0 stderr** (531 before; +6, all `echo`), exit census unchanged. Reads of
533's and 513's archived captures and of `entry_gic.c`, `entry_stubs.c`, `hw_watchdog.c`. The payload,
the parked frozen pair at `/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified.
`fastboot boot` only — never `flash` — so no outcome of any of this can write to storage.
