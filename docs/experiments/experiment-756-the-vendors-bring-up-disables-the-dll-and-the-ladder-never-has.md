# 756: the vendor's bring-up disables the DLL before its first command, and the ladder has never touched that register

**Read from the vendor kernel tree and the payload source, with the archive used only to refute.** No
device action of any kind — no `fastboot`, no `adb` to the device, no press, no gate, no runner, no build,
and `out/` untouched.

**The one-line finding.** Rungs 2, 6 and 7 transcribe three pieces of the vendor's bring-up — `sdhci_msm_init`'s
mode sequence, `sdhci_msm_set_clock`, and `mmc_power_up`'s PASS A — and **all three are faithful
transcriptions of functions that do not contain the write in question.** The write is in
`sdhci_msm_set_uhs_signaling` (`sdhci-msm.c:2537-2601`), which the MMC core reaches through
`sdhci_set_ios → sdhci_do_set_ios` **during power-up, before the first command**, and at clock 400 kHz it
does two things: it clears `SDHCI_HOST_CONTROL2 0x3E` bits[2:0], and it **disables the DLL** by setting
`CORE_DLL_RST` and `CORE_DLL_PDN` in `CORE_DLL_CONFIG 0x100`. The vendor's own comment says what the
second one is for — *"the feedback clock must be provided and DLL must not be used so that tuning can be
skipped"* — which is a statement about **how the controller samples** the incoming data. The ladder's
measured stall is confined to receiving: a command that asks for nothing back completes in 0.255–0.30 ms,
and every command that must receive a response is taken and then never completes, with no status bit at
all.

**`CORE_DLL_CONFIG 0x100` is the half that survives scrutiny, and there is not one cell about it in the
archive.** `HOST_CONTROL2 0x3E` is unread too, but for it there is a plausible reason to expect it is
already right (§4) — so both are named, and §6 proposes reading **both in the same arm**, because the arm
costs one press whatever it reads and the two questions are in the same window.

## 0. Why this was read rather than pressed

It is a **source** question — what does the vendor's bring-up do that this ladder does not — and the ladder
has transcribed that bring-up function by function, so the answer is a diff and not a measurement. The one
press the ladder is waiting on (rung 21, `armed-storage-46fe6737`) tests a *different* thing and nothing
here moves it. This is 748's method aimed at the sampling path instead of at three named registers.

## 1. The path, verified link by link

Nothing below is from memory. Each link is a line in the vendor tree, read this session.

**The write.** `drivers/mmc/host/sdhci-msm.c:2537`, `sdhci_msm_set_uhs_signaling` — the body quoted
verbatim, with the timing `else if` chain elided (it is a seven-arm chain assigning one value per
`MMC_TIMING_*`, `:2547-2560`):

    ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
    /* Select Bus Speed Mode for host */
    ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
    if (uhs == MMC_TIMING_MMC_HS400)
        ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
    else if (uhs == MMC_TIMING_MMC_HS200)
        ...                                        /* :2549-2560, seven arms */
    /*
     * When clock frquency is less than 100MHz, the feedback clock must be
     * provided and DLL must not be used so that tuning can be skipped. To
     * provide feedback clock, the mode selection can be any value less
     * than 3'b011 in bits [2:0] of HOST CONTROL2 register.
     */
    if (host->clock <= CORE_FREQ_100MHZ) {                    /* :2567 */
        if ((uhs == MMC_TIMING_MMC_HS400) ||
            (uhs == MMC_TIMING_MMC_HS200) ||
            (uhs == MMC_TIMING_UHS_SDR104))
            ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;

        /*
         * Make sure DLL is disabled when not required
         *
         * Write 1 to DLL_RST bit of DLL_CONFIG register
         */
        writel_relaxed((readl_relaxed(host->ioaddr + CORE_DLL_CONFIG)
                | CORE_DLL_RST),
                host->ioaddr + CORE_DLL_CONFIG);              /* :2578 */

        /* Write 1 to DLL_PDN bit of DLL_CONFIG register */
        writel_relaxed((readl_relaxed(host->ioaddr + CORE_DLL_CONFIG)
                | CORE_DLL_PDN),
                host->ioaddr + CORE_DLL_CONFIG);              /* :2583 */
        mb();

        /*
         * The DLL needs to be restored and CDCLP533 recalibrated
         * when the clock frequency is set back to 400MHz.
         */
        msm_host->calibration_done = false;
    }
    ...
    sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);          /* :2597, unconditional */

**Two details of that body matter and both are easy to get wrong.**

- **Bits[2:0] do end at 0 even for `MMC_TIMING_LEGACY`.** The top `ctrl_2 &= ~SDHCI_CTRL_UHS_MASK` clears
  them, and **no** arm of the `else if` chain matches `MMC_TIMING_LEGACY` (there is no `LEGACY` arm), so
  nothing restores them. The inner `&= ~UHS_MASK` at `:2568-2571` is redundant for the case a boot at
  400 kHz is in. So the net effect at the ladder's own clock is the field = 0 = `SDHCI_CTRL_UHS_SDR12`.
- **The DLL branch is taken at the ladder's clock.** `CORE_FREQ_100MHZ` is `(100 * 1000 * 1000)`
  (`:164`) and the ladder's `st_clock_set` runs at **400 kHz**. `400000 <= 100000000`, and it is also
  `<=` for the 0 the core passes on the first `set_ios`. Both the vendor's first command and the ladder's
  sit inside the branch.

**The caller.** `sdhci.c:1784-1785`, inside `sdhci_do_set_ios` (`:1616`):

    if (host->ops->set_uhs_signaling)
        host->ops->set_uhs_signaling(host, ios->timing);

and `sdhci-msm.c:2653`'s ops table supplies it (`:2659` in the same table is `.set_clock =
sdhci_msm_set_clock`, which the ladder *does* transcribe).

**Reachability, and this is the link that has to hold.** `sdhci_do_set_ios` has **exactly three** callers
in the vendor tree, and the grep above found all three: `sdhci_set_ios` (`:1857`, reached from
`sdhci_set_ios`'s role as `.set_ios` at `:2384`, i.e. driven by the **MMC core**), `sdhci_resume_host`
(`:3002`) and `sdhci_runtime_resume_host` (`:3097`). The MMC core calls `.set_ios` from `mmc_power_up`'s
`mmc_set_initial_state` and again on every clock change — **so the DLL disable happens before the vendor's
first command, and the ladder, which never calls `set_uhs_signaling` on any path, never makes it.**

**The one thing that could have made this a non-finding, and it does not.** If `sdhci_msm_set_clock`
contained the same DLL writes, the ladder would have them at rung 6 by transcription and the whole
observation would collapse. **It does not.** `sdhci_msm_set_clock` (`:2402-2535`, read in full) writes
`CORE_VENDOR_SPEC` and the standard clock registers and calls `clk_set_rate`; **`CORE_DLL_CONFIG` appears
nowhere in it.** The DLL is the one piece of the vendor's low-clock setup that lives on `set_uhs_signaling`
alone, which is why a faithful transcription of `set_clock` inherits the gap silently.

**And nothing else in the vendor reaches these two writes at low clock.** The only other DLL code is
`msm_init_cm_dll` (`:577`), which **enables** the DLL (`:605` sets `CORE_DLL_RST`, `:614` clears it; `:609`
sets `CORE_DLL_PDN`, `:618` clears it; `:622` sets `CORE_DLL_EN`; `:626` sets `CORE_CK_OUT_EN`) — the
opposite direction. Its two callers are `sdhci_msm_cdclp533_calibration` (`:661`, called at `:820`) and
`:846`, both inside `sdhci_msm_execute_tuning` (`:792`) — the `.execute_tuning` op, which the MMC core
invokes only for tuning, at SDR104/HS200. **So the vendor has exactly two statements about the DLL at
400 kHz — "disable it" on the `set_ios` path, and "enable it" on the tuning path — and this block is on
the first one.**

**The registers, and where they sit.**

| | offset | address | in the declared `reg` window | ladder ever reads | ladder ever writes |
| --- | --- | --- | --- | --- | --- |
| `SDHCI_HOST_CONTROL2` | **0x3E** | `0xf982493E` | yes | **no** | **no** |
| `CORE_DLL_CONFIG` | **0x100** | `0xf9824A00` | yes | **never, in any rung** | **never** |

`SDHCI_HOST_CONTROL2 0x3E` is `sdhci.h:162` with `SDHCI_CTRL_UHS_MASK 0x0007` and `SDR12 0x0000`,
`SDR25 0x0001`, `SDR50 0x0002`, `SDR104 0x0003`, `DDR50 0x0004` (`:163-168`). `CORE_DLL_CONFIG 0x100`
is `sdhci-msm.c:80`, with `CORE_DLL_EN (1 << 16)`, `CORE_CDR_EN (1 << 17)`, `CORE_CK_OUT_EN (1 << 18)`,
`CORE_DLL_PDN (1 << 29)` and `CORE_DLL_RST (1 << 30)` (`:82-87`), and a **separate** status register
`CORE_DLL_STATUS 0x108` carrying `CORE_DLL_LOCK (1 << 7)` (`:89-90`).

**The window, corrected.** The base `msm8974.dtsi:503` declares `reg = <0xf9824900 0x11c>`, and
`msm8974pro.dtsi:1765-1766` **overrides it for the Pro** — `&sdhc_1 { reg = <0xf9824900 0x1a0>; }` — and
cancro is MSM8974Pro-AC, so the applicable window is **`0x1a0`**. Both contain `0x100`; the Pro's is the
relevant one and it is the more generous of the two, which is worth stating because a reader checking the
first dtsi hit would find the smaller number.

## 2. The archive's census skips both, and the census is the instrument that would have caught them

Rung 3's census (`st_standard_census`, `entry_storage.c:612-674`) reads **ten** values and publishes each
immediately — eight from the register file and two from the MSM core block — in this order:

    software_reset 0x2F    power_control 0x29    host_control 0x28    clock_control 0x2C
    slot_int_status 0xFC   present_state 0x24    capabilities_1 0x44  max_current 0x48
    pwrctl_mask 0xE0 (core)                      pwrctl_ctl 0xE8 (core)

**`0x3E` is not there**, and neither is `0x100` — and the whole file never names either:

    $ grep -n 'HOST_CONTROL2\|0x3E\|0x3e\|host_control2\|DLL_CONFIG\|dll_config\|dll' src/entry/entry_storage.c
    (nothing — exit 1)

**The file is otherwise careful about exactly this.** `ST_SDHCI_HOST_CONTROL 0x28`'s own comment reads
*"a BYTE, and at an offset no 32-bit access reaches"* (`entry_storage.c:204`), and `ST_SDHCI_CAPABILITIES
0x40` and `ST_SDHCI_CAPABILITIES_1 0x44` are two separate constants (`:200-201`) of which the census reads
the second — so offsets and widths have been attended to one register at a time. `0x3E` is simply absent,
between `0x2C` and `0x44`.

**And the omission is not symmetric in its consequence.** `0x44` is read and `0x40` is not, which means the
census was already choosing which register of a family to read one press at a time. The two omits here are
of that same kind — and one of them, `0x100`, is not in the register *file* at all, so no census of the
standard offsets would ever have reached it.

## 3. Why this is a candidate for the stall the ladder is actually stuck on

The measured asymmetry, from the ladder's own five-command table (746) and 749:

| command | asks for | outcome |
| --- | --- | --- |
| CMD0 `0x0000` | nothing | **taken → completed**, 0.28–0.30 ms |
| CMD3 `0x0300` (rung 20) | nothing | **taken → completed**, 0.255 ms |
| CMD3 `0x031A` (rung 19) | a 48-bit response | **taken, `CMD_INHIBIT` held for the whole window, never finished** |
| CMD1 `0x0102` | R3 | taken (`_cmd1_word_read = 0x00000102`), `INT_STATUS` 0 over 5,088,256 polls |
| CMD2 `0x0209` | R2 | taken, the same |

**Every failure is on the receive side and none is on the transmit side.** The block takes the word —
`CMD_INHIBIT` rises on the read after the store — and then **nothing**: `_status_any = 0` across the whole
`INT_STATUS` register for 1.2 s, no `TIMEOUT`, no `ERR`, no completion. **A receive path that cannot
sample has exactly this signature, and a command that asks for nothing back cannot see it.**

**And the vendor's write is about which clock samples.** The comment does not say *"configure the bus
width"* or *"set the clock"*; it says the feedback clock must be provided **and the DLL must not be used**,
so that timing tuning can be skipped — and `msm_host->calibration_done = false` is the flag that makes a
later tuning pass redo the calibration. The ladder is at 400 kHz and has never made either half of that
statement to the block. **What the block's DLL state actually is, nobody knows: the archive has no cell.**

**What this does not prove, stated plainly:** that the DLL state *is* the cause. Nothing here measures the
block. What is measured is (a) the vendor makes the write on a path the ladder transcribes nowhere, (b)
`sdhci_msm_set_clock` — which the ladder *does* transcribe — is verified not to contain it, so the gap is
structural and not a transcription slip, (c) the vendor's own comment says the write decides which clock
samples the incoming data, and (d) the ladder's failure is confined to receiving. **(a)–(d) are four facts;
the causal link between them is a hypothesis, and §6 says what would settle it.**

## 4. The `HOST_CONTROL2` half, and why it is the weaker one — argued, not measured

**The argument that the ladder already has this one right, and its exact strength.** `sdhci_reset`
(`sdhci.c:229-283`, read in full) issues `SDHCI_RESET_ALL` when `sdhci_init(host, 0)` runs, and the ladder
does the same store at rung 4 (`SOFTWARE_RESET 0x2F <- 0x01`). The SDHCI specification defines *Software
Reset for All* as resetting the host controller, which would take `HOST_CONTROL2`'s UHS field to 0 —
**the same value the vendor's code writes for `MMC_TIMING_LEGACY`.** If the reset does that, the ladder
reaches the vendor's destination by a different route.

**The strength of that argument is exactly this and no more: it is the spec read against one store, with
no cell.** And there is real evidence against resting on it, in the vendor's own code: `sdhci_reset` writes
**only** `SDHCI_SOFTWARE_RESET 0x2F` — `HOST_CONTROL2` appears nowhere in the function — and the vendor
never relies on the reset for the UHS field; it writes the field explicitly on every `set_ios`, immediately
after a reset on the `sdhci_add_host` path. **A register a driver always writes explicitly is a register
that driver does not trust a reset to have set.** So the honest status of `0x3E` is **unknown**, and §6
reads it rather than arguing about it.

**Why the DLL half is different even so.** `CORE_DLL_CONFIG` is at **`0x100`** — far outside the register
file *Software Reset for All* is defined over. And the vendor treats DLL state as something a reset does
**not** leave sane: the same `set_uhs_signaling` sets `calibration_done = false` and its comment says the
DLL *"needs to be restored and CDCLP533 recalibrated when the clock frequency is set back to 400MHz"*.
**A reset that left the DLL in a usable state would make both the flag and the comment unnecessary.** That
is the same kind of argument §4's first paragraph makes — and it points the other way, which is why the
DLL is the half that carries the hypothesis.

## 5. The other three families of the vendor's write set, and why they are not candidates

**This is the negative half of the same survey, and it is worth as much as the positive one.** Every
register `sdhci-msm.c` writes (`grep` over `writeb|writew|writel` on `core_mem` or `host->ioaddr`) falls
into four families. Three are already implemented **and measured**:

| family | the vendor's lines | the ladder | the archive's answer |
| --- | --- | --- | --- |
| the mode / core-reset sequence | `:2844-2868` (`CORE_HC_MODE 0x78`, `CORE_POWER 0x0`, `HC_MODE_EN`, `FF_CLK_SW_RST_DIS`) | **rung 2**, four stores | `_mode_w0_read 0x0 → _mode_w1_read 0x1 → _mode_w2_read 0x2001`, **`_mode_bit_after = 1`** — the block IS in SDHC mode, on all **13** captures |
| the power-IRQ handshake | `:2006`, `:2069`, `:2079-2085` (`CORE_PWRCTL_CLEAR`/`CTL`, `CORE_VENDOR_SPEC` pad) | **rungs 8 and 9** | `_pwr_irq_status = _pwr_irq_status32 = 0x02`, `_pwr_irq_ctl_before 0x00 → _pwr_irq_ctl_after 0x01` (753) |
| the clock and its vendor register | `sdhci_msm_set_clock :2402-2535` — `CORE_VENDOR_SPEC 0x10C`'s MCLK-select and `HC_SELECT_IN` fields, then the standard clock registers | **rung 6**, and its `#error` text names exactly these: *"two CORE_VENDOR_SPEC 0x10C read-modify-writes (MCLK select <- DFLT, HC_SELECT_IN cleared)"* | `_clk_*` census, `_clk_set_cc_after 0xe045` |
| **the DLL / CDC / calibration** | `:351-630` and `:661-690` — `msm_dll_poll_ck_out_en`, `msm_config_cm_dll_phase`, `msm_find_most_appropriate_phase`, `msm_cm_dll_set_freq`, `msm_init_cm_dll`, `sdhci_msm_cdclp533_calibration` — plus `:2537-2597`'s DLL half and `:2211-2220` (`CORE_CDR_EN`); the registers are `CORE_DLL_CONFIG`, `CORE_DLL_STATUS`, `CORE_DDR_200_CFG`, `CORE_CSR_CDC_*` | **nothing** | **nothing — no cell of any kind** |

**And a fourth thing I checked and can now retire**, because it is the same shape as 748 §2 and deserves
to stop being proposed: **`SDHCI_TIMEOUT_CONTROL 0x2E`.** `grep` over `sdhci.c` finds it exactly twice —
read at `:115`, inside `sdhci_dumpregs` (the debug dump, exactly like `SLOT_INT_STATUS 0xFC`), and written
at `:828`:

    sdhci.c:828      sdhci_writeb(host, count, SDHCI_TIMEOUT_CONTROL);

which is inside `sdhci_prepare_data`, whose guard is the line above it:

    sdhci.c:826      if (data || (cmd->flags & MMC_RSP_BUSY)) {

and **not one of the ladder's six commands carries `MMC_RSP_BUSY`** (`1 << 3`, `include/linux/mmc/core.h:31`):
the words are `R2 0x07`, `R1 0x15`, `NONE 0x00`, `R1_NOIDX 0x05` and `PRESENT 0x01`, and
`ST_SDHCI_CMD_RESP_SHORT_BUSY 0x03` exists in the payload only as a macro and a `_Static_assert`, never in
a `st_send_command` call. **So on the command path this register is dead for the same reason
`SLOT_INT_STATUS` is** — and it becomes live the moment the driver has a **data** path, which is where the
storage driver the goal needs will first need it. That is a note for a future rung and not a proposal for
this one.

**So the survey's conclusion is a clean one: the vendor has no MSM-specific write on the
command-acceptance path that this ladder has not either implemented and measured, or now identified. The
identified one is the DLL — and the reason it survived this ladder's careful transcription is structural:
it is the only low-clock setup step that lives on `set_uhs_signaling` rather than on a function the ladder
ported.**

## 6. The discriminator: two reads, one arm, three outcomes each — and the read is safe by construction

**Read `hc_mem + 0x3E` and `hc_mem + 0x100` (`0xf982493E`, `0xf9824A00`).** `0x3E`'s bits[2:0] against 0;
`0x100`'s bit 30 (`CORE_DLL_RST`) and bit 29 (`CORE_DLL_PDN`). Each has three outcomes and each names the
next act:

| reading | what it means | the next act |
| --- | --- | --- |
| `0x100` bits 30 and 29 **both SET** | the DLL is already reset and powered down; the vendor's destination state is reached some other way (the bootloader, or the reset does reach `0x100` on this part) | **the hypothesis in §3 is dead**, and nothing further is owed here |
| `0x100` **either bit CLEAR** | the block is running with an undischarged DLL at 400 kHz, against the vendor's own comment | **doing what the vendor does** — set both bits — becomes the cheapest untried thing on this path |
| `0x100` **unreadable** (`fsr` on the read) | the register is not in the window this block actually decodes | a finding about the `0x1a0` region's tail in its own right |
| `0x3E` bits[2:0] **= 0** | §4's spec argument holds: rung 4's `RESET_ALL` did reach it | the `HOST_CONTROL2` half is closed in one cell |
| `0x3E` bits[2:0] **≠ 0** | the reset does **not** clear the field on this IP, and the ladder is in a UHS mode the vendor never asks for at 400 kHz | the second half of the vendor's write becomes live too |
| `0x3E` unreadable | as above, for the register file's tail | — |

**All six of those are answered by two reads in one arm**, and neither read changes a bit. That matters
because the ladder is mid-flight and `out/` is parked: **an arm that only publishes two registers cannot
change the block's behaviour**, so this proposal carries no mechanism risk at all.

**The reads are safe by the project's own interlock, and here it passes rather than needing to be
argued.** [[mi4-a-device-address-can-be-right-and-undereferenceable]] requires an `addr >> 20` check
before any arm dereferences a new device register — 692 pressed it and faulted on a *third* device's
megabyte. Here:

    hc_mem            0xf9824900  >> 20 = 0xf98
    hc_mem + 0x3E     0xf982493E  >> 20 = 0xf98
    hc_mem + 0x100    0xf9824A00  >> 20 = 0xf98     <- all three the same megabyte
    reg = <0xf9824900 0x1a0>                         Pro (cancro); 0x100 < 0x1a0

**Same megabyte as every register this image already reads, both offsets inside the declared window.** No
new mapping is implied and the interlock is satisfied before the arm is written, not after.

**And there is a cheaper half-step if the operator prefers it**: the same two reads with **no**
command in the arm at all — a census extension. It costs one press where the full arm also costs one
press, and it answers the DLL question without spending the command path on it. **Which of the two is
spent is the operator's decision and neither is armed by this document.**

## 7. What this document does not say

- **It does not say the DLL is the cause.** §3 lists four facts and names the causal link as a hypothesis;
  §6 says what would settle it. **No rung is designed, armed or altered by this document.**
- **It does not say `HOST_CONTROL2` is set wrong.** §4 gives both directions of the argument, states the
  strength of each exactly (a spec read against one store, against the vendor's own refusal to rely on
  it), and puts the question into §6's arm instead of settling it in prose.
- **It does not re-open 753's finding.** The power path is measured and settled; §5 lists it only because
  the survey found it already implemented and measured.
- **It does not move the arm, the payload, `out/`, or the entry image.** Nothing here is compiled. Rung 21
  remains **ARMED AND NOT PRESSED**, and it tests the `INDEX` bit and not the sampling clock — **so this
  finding does not change what rung 21 will read**, and it is not a reason to alter that arm.
- **It does not arm anything, and no press may be spent without the operator's authorization.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it does is name the one
  family of controller state that the vendor's own bring-up programs at this clock and this ladder has
  never read, with the reason it survived a careful function-by-function port (it lives on the one
  function that was not ported), a two-read discriminator, and a verified safe address — and that is the
  whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
