# Experiment 498 — the frame is the device, and its line is a cell

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured once per run
Artifacts: `stages/stage90/xnu_platform/MSM8974Timer.cpp`, `stages/stage90/stage90_main.c`,
`tools/check_timer_line.py`, `tools/check_irq_routing.py`, `tools/check_driver_catalogue.py`

**Result: the timer's line is SPI 8 = intid 40, the frame is the block at `0xf9021000` rather than the
one at `0xf9020000` that 494 read a zero from, and the machine's own device-tree code now files that
specifier with the right controller — after a first run that aborted on the *spelling* of one
property, which is this project's oldest defect class and which the step's own check had missed.**

Two runs. The first booted an image whose tree said `phandle` where XNU's `AddPHandle` registers
`AAPL,phandle`; `FindPHandle` then answered 0 for the value `/timer` names, `IODTGetICellCounts` was
called with a `parent` of 0, and the kernel took a data abort at `IODTGetICellCounts+8` —
`panic(cpu 0 caller 0x80450e88): kernel abort type 4: fault_type=0x1, fault_addr=0x0`, `pc 0x80179428`,
`lr 0x80177a24` — inside XNU's own device-tree code, before the driver that is this step's subject
ever reached the frame. The second booted the same entry
image (sha256 `7c3dbe7c215b6f1b7a310667238fb8a5f985cd7436bfc4232df0cf3a05cb56d1`, **byte-identical**)
with a payload whose tree spells the key Apple's way, and every number this step is about is in the
log. So the fix is one property *name* in the device tree, and it is the first thing this project has
changed that the entry image does not move for at all.

## The frame is the device, and 494's stored zero has a name

494 mapped `/timer`'s `reg[0]` — `0xf9020000` — and read one word: `_rd0 = 0`. The property that says
why is in the device's own tree, and it is not a number:

    arch/arm/boot/dts/msm8974.dtsi:153-167
      timer@f9020000 {                      <- the node the driver matched; reg = <0xf9020000 0x1000>
          compatible = "arm,armv7-timer-mem";
          clock-frequency = <19200000>;
          frame@f9021000 {
              frame-number = <0>;
              interrupts = <0 8 0x4>, <0 7 0x4>;
              reg = <0xf9021000 0x1000>, <0xf9022000 0x1000>;
          };
      };

and the device's own kernel reads a frame's registers and its line out of the **frame**:
`timer_base = of_iomap(frame, 0)` (`arch/arm/kernel/arch_timer.c:623`) and `arch_timer_spi =
irq_of_parse_and_map(frame, 0)` (`:629`). `0xf9020000` is a container with no registers of its own,
which is exactly what a stored zero looks like when it is read; the device is one page up, at
`0xf9021000`, where the tree puts the `interrupts`. **494's owed second definition is therefore
structural rather than numerical**: the property that says this block is a device is on the frame,
one `reg` entry away from the block that answered zero. The driver takes it from the OS's own
resolution of `reg` rather than by address — `devmem->getObject( MSM8974_TIMER_FRAME_ENTRY )`, and no
file in the image contains the string `0xf9021000`.

## The line, and the rule that turns the tree's cells into it

The frame's `interrupts` is `<0 8 0x4>, <0 7 0x4>`, and the binding the same kernel ships says of a
frame: *"interrupts : Interrupt list for physical and virtual timers in that order"*
(`Documentation/devicetree/bindings/arm/arch_timer.txt`). The device's kernel takes index 0, so the
physical timer's line is the first specifier. The three cells are `<type number trigger>` because
`/interrupt-controller` declares `#interrupt-cells = <3>`; type `0` is a shared peripheral interrupt,
whose GIC id is `32 + number`, and type `1` is a private one, whose id *is* the number. So the line is
`32 + 8` = **intid 40**, level-high (`0x4`). The rule is published beside the number it produced
(`_line_rule`), because `8` and `40` are both defensible-looking readings of that property and only
one of them is this machine's. The run answers both: `_line_num = 0x08`, `_line_rule = 1`,
`_line_intid = 0x28`.

## The OS's own reading, and the run that stopped it

The driver reads the property itself and then asks `IODTMapInterrupts`
(`IODeviceTreeSupport.cpp:790`) to read it, which is a second reading of the same fact through a
different road: it resolves `interrupts` through `IODTFindInterruptParent` (`:467`), files the
specifier under `IOInterruptSpecifiers`, and files the *controller's name* under
`IOInterruptControllers`, where the name is `IOInterruptController%08X` built from the controller
node's phandle (`IODTInterruptControllerName`, `:489-501`).

**This is the road that 498's first run drove for the first time, and it aborted twenty lines in.**
Two things made it reachable at once: `/timer` now carries an `interrupts` property (so XNU's own
publication walk, `:221-225`, maps it — that walk runs over *every* node in the plane), and it also
carries an `interrupt-parent` (so `IODTFindInterruptParent` takes its first branch instead of falling
back to the parent node). The first branch is `FindPHandle(phandle)`, and `FindPHandle` searches
`gIODTPHandles` — the table `AddPHandle` fills. `AddPHandle` tests `gIODTInterruptCellKey` and
registers the node's **`gIODTPHandleKey`** property, and

    gIODTPHandleKey = OSSymbol::withCStringNoCopy("AAPL,phandle");      // :137-138

— not `phandle`. The tree was written with Linux's spelling, and that is the spelling the device's own
`msm8974.dtsi` uses. So `AddPHandle` registered nothing, `gIODTPHandles` stayed empty, `FindPHandle(1)`
returned 0, and the very next line (`:532-533`) called

    IODTGetICellCounts( parent /* = 0 */, &icells, &acells );

which loads a vtable pointer from address 0 at its third instruction. The log says so in three
independent ways: the panic's register dump has `r0: 0x00000000`, the fault is `fault_type=0x1,
fault_addr=0x0, fsr=0x5` (a translation fault on a read of address 0), and `pc 0x80179428` /
`lr 0x80177a24` resolve in the image to `IODTGetICellCounts+8` and `IODTMapInterruptsSharing`. The
kernel then took its own `MACH Reboot` path, which is why the phone came back on its own with
`No errors detected` and no persistent write.

**The step's own check had passed this tree**, and the reason is the shape of the claim it made. Claim
3 compared the controller's phandle with `/timer`'s `interrupt-parent` — two *values*, both 1 — and
never asked what the property was *called*. The comment beside the property asserted the right pair
("`#interrupt-cells` and `phandle`") using the wrong name. So 498's first run is the measured instance
of the project's oldest defect in a new place: **one decision, two spellings, and the check comparing
the half that was right.** The repair is not a better comment: `tools/check_timer_line.py` now reads
the three key names out of `IODeviceTreeSupport.cpp` (each `OSSymbol::withCStringNoCopy("…")`
declaration) and refuses a payload that spells any of them otherwise, with the two mutations
`the_controller_has_no_phandle` and `the_controller_spells_the_phandle_the_linux_way` covering the two
ways that can go wrong.

## What the driver reads, and what it does not write

The frame is mapped the same way 494's `reg[0]` was — the OS's own resolution, then
`map( kIOMapAnywhere )` — and three things are read out of it: its frequency register, its 64-bit
count twice with `IODelay( 1000 )` between, and its control word. **Nothing is written.** The line is
not enabled at the distributor and the timer is not armed, so this step cannot raise an interrupt the
machine has to survive; arming it is the next step's business and it will need the distributor's
enable, its target and its priority.

The frequency is a *third* definition of 19.2 MHz and it is read as a register: the tree says
19200000, the CPU says it in `CNTFRQ`, and the frame says it in its own `FREQ` register. 492 compared
the first two and recorded that the third did not exist. It exists, and the run reads it:
`_frame_freq = 0x0124f800` = 19 200 000 with `_frame_freq_agree = 1`.

The rate is *measured* rather than declared, and the tolerance is derived rather than chosen: the
frame's count delta across a one-millisecond busy wait is held against `mach_absolute_time()`'s delta
over the same window, and the allowance is `_frame_slack` = four frame reads, the number of frame
accesses inside the window. A correct rate gives `0 <= mac_d - cnt_d <= slack`; the run measured
`cnt_d = 0x00037f8a` and `mac_d = 0x00037fa7`, a difference of **29** against a slack of **112**, so
`_frame_rate_ok = 1`. A 32 kHz frame in the same window would answer about 32 against 19 200.

The handler is registered with `entry_irq_register_client` (496's registry) and not with
`IOService::registerInterrupt`, and the reason is a hang rather than a preference:
`lookUpInterruptController` (`IOPlatformExpert.cpp:379-397`) is an unbounded `IOLockSleep` whose only
waker is `registerInterruptController`, whose only caller in the whole tree is `IOCPU.cpp:783` inside
the CPU interrupt controller's initialiser this image never reaches. Claim 6 refuses a build in which
*any* source file in the image calls it. The registry took the fourth client: `_line_cli_rc = 1`,
`_cli_capacity = 4`, `_cli_intid = 0x28`.

The handler masks the frame before it publishes anything and before it returns — `CTRL |= IT_MASK` —
because a level-sensitive line that the device does not drop is re-entered forever. The run shows the
line never fired (`_isr_calls = 0`), which is what an un-armed and un-enabled line should do.

## The two runs, and what each one is evidence for

**Run 1 — the abort.** Artifact `stage90-qcdt.img` sha256 `3d78265e1a7b42224fbf6a8bd26cf027ea1947569ba5a86fd3a4ceb9575be014`
(payload `kernel_size = 5982648`), 460 844 bytes captured. It reached XNU's entry, printed the OS
console block, and aborted 20 lines later inside `IODTGetICellCounts`. It is the run that *names the
defect*: no test in this repository could have said the key was wrong, because our tree and our check
agreed with each other and both disagreed with Apple's code. The evidence is in the panic text and in
the address resolution, not in a counter.

**Run 2 — the readings.** Artifact sha256 `39e0d1c64bc1bae7323b90792fa560c78a5a1e40fa90e060834d26d43859709b`
(payload `kernel_size = 5982656`, eight bytes more than run 1's — the five characters `AAPL,` and
their padding), entry image unchanged, 557 216 bytes captured, exit 0, `No errors detected`, device
back on Android on its own. Nothing else moved:

    xnu_live_timerdrv_frame_phys       = 0xf9021000     the OS resolved the frame, not the container
    xnu_live_timerdrv_frame_len        = 0x00001000
    xnu_live_timerdrv_frame_objkind    = 0x00000002     an IOMemoryDescriptor
    xnu_live_timerdrv_frame_va         = 0xc202d000     the mapping the driver reads through
    xnu_live_timerdrv_frame_freq       = 0x0124f800     19200000, the frame's own register
    xnu_live_timerdrv_frame_freq_agree = 0x00000001     tree, CPU and frame agree
    xnu_live_timerdrv_frame_cnt_d      = 0x00037f8a     229258 frame counts in the window
    xnu_live_timerdrv_frame_mac_d      = 0x00037fa7     229287 kernel-counter ticks, same window
    xnu_live_timerdrv_frame_slack      = 0x00000070     112, four frame reads
    xnu_live_timerdrv_frame_rate_ok    = 0x00000001     29 <= 112
    xnu_live_timerdrv_frame_ctl        = 0x00000002     ENABLE 0, IT_MASK 1, IT_STAT 0
    xnu_live_timerdrv_line_cells       = 0x00000003
    xnu_live_timerdrv_line_type        = 0x00000000     SPI
    xnu_live_timerdrv_line_num         = 0x00000008
    xnu_live_timerdrv_line_trig        = 0x00000004     level-high
    xnu_live_timerdrv_line_rule        = 0x00000001     32 + num
    xnu_live_timerdrv_line_intid       = 0x00000028     40
    xnu_live_timerdrv_line_parent      = 0x00000001     the phandle interrupt-parent names
    xnu_live_timerdrv_osmap_ok         = 0x00000001     the OS's own mapping ran
    xnu_live_timerdrv_osmap_n          = 0x00000001     one specifier
    xnu_live_timerdrv_osmap_same       = 0x00000001     and it is the tree's cells
    xnu_live_timerdrv_osmap_ph         = 0x00000001     the phandle it read out of the controller name
    xnu_live_timerdrv_osmap_ph_agree   = 0x00000001     and it is the one interrupt-parent names
    xnu_live_timerdrv_line_cli_rc      = 0x00000001     the payload's registry took the handler
    xnu_live_timerdrv_isr_calls        = 0x00000000     the line has not fired: nothing enabled it

`_osmap_*` is the group that run 1 could not produce at all, and it is the group that says the
`AAPL,phandle` repair reached the right place: the OS's own reader of the same property now resolves
the controller, files one 3-cell specifier, builds the controller's name from the phandle, and the
eight trailing hex digits of that name parse back to the same value `/timer`'s `interrupt-parent`
names. Every other part of the boot is 497's: `sleh_seen = 0x20`, `sleh_armed = 0x1c`,
`sleh_redirected = 0x1a`, `sleh_storm = 9`, `irq_late_count = 0` with `irq_timer_count` to `0x800`,
and the OS console block is **byte-identical** to 497's (1288 characters, sha256
`a0593af02867eac3ddf107a6523292ea9e5016861188faa0a2c796d3ce54cc8d`).

## The claim: eight claims and sixteen mutations

`tools/check_timer_line.py` is new in this step: **8 claims, 16 mutations**, all refused, run in
`build_entry.sh` in both modes beside `check_irq_routing.py`.

  * **1. the frame's eight register offsets and its three control bits are the device's own kernel's,
    name by name** — `MSM8974_FRAME_*` against `arch_timer.c`'s `QTIMER_*` and `ARCH_TIMER_CTRL_*`,
    and the two *index* decisions (`of_iomap(frame, 0)`, `irq_of_parse_and_map(frame, 0)`) below
    them, since this node carries the frame's two regions flattened after the parent's. The clause
    that checks the *citations* was added because both of this step's were wrong for the whole time
    the code and the check agreed with each other (they said 631 and 637; the calls are at 623 and
    629), and nothing in the repository could have said so — what caught them was re-reading the
    kernel while writing this report. The line number is now derived from the kernel's own text, from
    both directions: `the_citation_points_at_another_line` moves the prose and
    `the_device_kernel_moves_and_the_citation_does_not` moves the kernel under it.
  * **2. the line our tree declares for `/timer` is the line the device's own tree declares for its
    frame** — two trees, one device, and no authority inside our tree for either half.
  * **3. the controller's phandle and `/timer`'s `interrupt-parent` are one value, under Apple's two
    keys** — the clause run 1 added, described above.
  * **4. the interrupt id is `dt_num + MSM8974_GIC_SPI_BASE`**, with no `#define`d intid and no literal
    in the registration. `the_line_is_read_without_the_rule` drops the addend; `the_intid_is_a_constant`
    replaces the rule with `40u`. The first is what makes "8" and "40" distinguishable, and it is the
    one that matters, because a tree with `interrupts = <0 8 4>` and a driver registering for 8 is a
    driver nothing will ever call.
  * **5. `msm8974_timer_isr` masks the frame before it publishes and before it returns** — the store
    to `CTRL` precedes the first `entry_live_write`, uses `IT_MASK`, and reads `g_timer_frame_va`.
  * **6. no source in this image calls `registerInterrupt`** — a scan of `xnu_platform/` and
    `xnu_arm_boot/`, not a claim about a run, because a run that made that call is a run that has to
    be recovered by the watchdog. **The scan reads the files through `Facts.source_of` rather than off
    the disk**, because the first version reopened the path while the mutation edited the in-memory
    copy, and the check then reported `ok` about a file the mutation had changed — the
    `--wrap`-that-cannot-see-the-call and `pgrep`-matching-itself family (455, 459) in a third shape,
    a report true of the wrong object.
  * **7. the frame is the OS's array indexed the way the tree's ordering gives it**, every register is
    `frame_va + offset`, and no literal `0xf9020000`/`0xf9021000`/`0xf9022000` is in the driver.
  * **8. every number the run needs in order to be read as a record is published** — 35 key suffixes,
    from the three ways the frame can answer zero to the four the handler can reach.

`tools/check_irq_routing.py` grew its static-storage claim to three files and now runs **9 claims / 57
mutations**; `tools/check_driver_catalogue.py`'s claim 9 needed a repair of its own, and the repair is
this project's comment/needle family in a fourth shape: its needle for the entry's object-kind record
was `r'xnu_live_\w+_objkind'`, and 498's new `xnu_live_timerdrv_frame_objkind` satisfied it, so
deleting the *entry's* kind stopped refusing the mutation while the check printed `ok`. The needle is
now a suffix test against the keys actually published under `xnu_live_timerdrv_`, and all 81
mutations are refused again.

## A build defect found on the way, and not by the device

The ARM layer's object directory is **not keyed by configuration**. `tools/assemble_arm_layer.sh`
reads `CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}` and writes every object to `out/xnu_asm_obj`, so running
it without the variable rebuilds the layer from RELEASE's defines *over* the STAGE90_XNU objects.
That happened in this session's first rebuild attempt: the image was then assembled with
`machine_load_context` reading `TH_CTH_SELF` at `[r0, #1480]` where this configuration's `assym.s`
says `#1496` — a 16-byte shift, i.e. a load of a different field of `thread_t` — and the only thing
between that and a boot was `check_asm_config.py`, which `build_entry.sh` runs before linking. The
check is the working part here; the defect is that the configuration is a property of the *pool* (which
records it in `out/config.stamp`) and of the caller's shell, and the layer's directory records
neither. It is the same family as the entry-image rows of 288/291: one value, two definitions, and the
half that moves is the one nothing stamps.

## What is owed

  * **Arming the line.** The distributor's enable, target and priority for intid 40, and the frame's
    `CTRL` with `ENABLE` set — three registers whose addresses are all derived from the intid. Until
    then `_isr_calls` is 0 by construction and the handler has never run on this machine.
  * **The handler's return path.** The ISR publishes and masks; it does not return into
    `rtclock_intr`'s world, and the `AckC`/EOI question for a level line delivered through
    `entry_irq_register_client` is unasked.
  * **The `AckC` question** and the second client the registry refuses when `_cli_capacity` is reached
    — today's cap turns a full registry into a named stop, which is the right shape, but the stop has
    never been taken.
  * Unchanged from 497: the conditional clause of its claim 8 has no mutation until two images exist;
    the unregister guard's asymmetry; a timeout that outlives its asker; `/timer`'s second definition
    (partly delivered here); the other device nodes; the two services the catalogue answers with
    nothing; `MSM8974RootResource`'s `state0 = 0`; a name/class reader wider than eight characters;
    the release as a reading; `vm_fault`; 488's flag-list; 490's frames band; `xnu_live_dec_same`.

## Safety

Both runs went through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only, never a flash. Neither
wrote anything persistent. Run 1 ended in a kernel panic inside XNU's own device-tree code and the
kernel's `MACH Reboot` path, which the hardware watchdog would have covered had `MACH Reboot` not
come first; the phone returned to Android on its own and `No errors detected` is in the log. Run 2
ended the way 497's did. The hardware watchdog was armed and running in both (`_enabled = 1`,
`_timeout_s = 0x19`, `_bite_after = 0x000dffac`, `_readback_ok = 1`, `_counter_running = 1`,
`_countdown_plausible = 1`, `_bite_truncated = 0`), and the software dead-man ran to its samples
(`deadman_samples = 0x258`) without firing in either. **This step never enables an
interrupt line**, so the margin between it and the machine is the same as 497's: the driver reads
three registers of a device that is not asserting, and the only code this step added that can stop the
boot is a `panic` inside Apple's own mapping — which is what run 1 measured, and which recovered by
itself.
