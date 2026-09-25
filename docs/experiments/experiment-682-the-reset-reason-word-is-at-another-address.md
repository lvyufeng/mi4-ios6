# 682: the reset-reason word is at a different address than the tree says — `0x0fa00000` is the shared RAM, not the IMEM

681 corrected a comment on the strength of a claim, and the claim was the wrong part. This step went to the
SoC's own bring-up code and to the phone's own device tree, and found that **both** of the addresses the
previous step argued about are wrong: `0x0fa00000` is not the IMEM, and `RESTART_REASON` is not Android's
restart-reason word. The real word is `0xFE80565C`, and nothing in this project has ever written it.

The reset itself is untouched — every returning run's reset comes from `PS_HOLD ← 0`, which is measured on
every run that has ever come back. What is wrong is the *breadcrumb* written beside it, the name of the
constant it is written through, and the claim in `entry_reset.h` that Android reads it back. **No device was
booted, nothing was flashed, nothing was written to storage, and no press is armed.**

## 1. The write site, read from the device's own kernel

```
arch/arm/mach-msm/restart.c:48     #define RESTART_REASON_ADDR 0x65C
                            :371   restart_reason = MSM_IMEM_BASE + RESTART_REASON_ADDR;
                            :284   __raw_writel(0x77665500, restart_reason);   /* cmd "bootloader" */
                            :286   __raw_writel(0x77665502, restart_reason);   /* cmd "recovery"   */
                            :288   __raw_writel(0x77665503, restart_reason);   /* cmd "rtc"        */
                            :292   __raw_writel(0x6f656d00 | code, restart_reason); /* "oem-<hex>" */
                            :299   __raw_writel(0x77665501, restart_reason);   /* the default      */
```

So the **offset** this project uses is right: the restart reason is IMEM `+ 0x65C`, and it is the channel Android's
own `reboot bootloader` / `reboot recovery` / `reboot oem-*` go out on. That much `stage90.h:18` has right,
and 681 was right to trust it. What has never been checked is the **base**.

## 2. `MSM_IMEM_BASE` is a virtual address, and the IMEM's physical page is not `0x0fa00000`

```
msm_iomap.h:72    #define MSM_IMEM_BASE      IOMEM(0xFA00A000)     /* 4K */
msm_iomap.h:92    #define MSM_SHARED_RAM_BASE IOMEM(0xFA400000)    /* 2M */
msm_iomap.h:122   #define MSM_SHARED_RAM_SIZE SZ_2M
board-dt.c:82     int __init msm_scan_dt_map_imem(...)
                    compat = "qcom,msm-imem";
                    map.virtual = (unsigned long)MSM_IMEM_BASE;
                    map.pfn     = __phys_to_pfn(be32_to_cpu(imem_prop[0]));
                    map.length  = be32_to_cpu(imem_prop[1]);
```

The `IOMEM()` values in this header are **virtual** addresses and the physical page comes from the device tree
— the same header's other pair proves the convention (`MSM_DEBUG_UART_BASE IOMEM(0xFA71E000)` beside
`MSM_DEBUG_UART_PHYS 0xF991E000`). So `restart_reason` is a *virtual* address in the fixed iomap, and the word
it writes is at whatever physical page the DT's `qcom,msm-imem` node names.

**Measured on the phone itself** (2026-09-25 09:03 UTC, read-only over `adb`, `od` of the DT properties):

| node | `reg` | meaning |
| --- | --- | --- |
| `soc/qcom,msm-imem@fe805000` | `0xFE805000`, `0x1000` | **the IMEM page** — so the word is **`0xFE80565C`** |
| `soc/qcom,smem@fa00000` | `0x0FA00000`, `0x200000` (+ `0xF9011000`/`0x1000`, `0xFC428000`/`0x4000`) | the SMEM region |
| `soc/qcom,ipc-spinlock@fa00000` | `0x0FA00000`, `0x200000` | SMEM's first client, at the SMEM base |
| `soc/qcom,pm-8x60@fe805664` | `0xFE805664`, `0x40` | a client **inside the IMEM page**, 8 bytes past the word |
| `soc/qcom,wdt@f9017000` | `0xF9017000`, `0x1000` | the watchdog, whose bite this project uses |
| `chosen/pureason` | `0x00000191` | the DT property `bootinfo.c` prints as the powerup reason |

The in-tree dtsi agrees and names the SoC variant: `msm8974pro.dtsi:32` and `msm8974-v2.dtsi:46` are
`qcom,msm-imem@fe805000`, while `msm8974-v1.dtsi:46` is `0xfc42b000`. cancro is 8974Pro-AC and its own DT says
`fe805000`, so the v1 alternative (`0xFC42B65C`) is excluded on the device — and **either way it is not
`0x0fa0065c`.**

## 3. And `0x0fa00000` is the shared RAM, named as such by the same header

```
msm_iomap-8974.h:26   #define MSM8974_MSM_SHARED_RAM_PHYS 0x0FA00000
io.c:317              msm_shared_ram_phys = MSM8974_MSM_SHARED_RAM_PHYS;
```

The same value is the shared-RAM base for APQ8084, MSM8226, MPQ8092 and samarium, while the IMEM (DT-provided)
differs per SoC — so it is the shared RAM, not the IMEM. The phone's DT calls the region `qcom,smem@fa00000`
with a 2 MB size, and it carries a second range at `0xF9011000` whose purpose this step did not establish. So:

**`RESTART_REASON = MSM_IMEM_BASE_PHYS + 0x65c = 0x0FA0065C` is SMEM + `0x65C`** — a word written into the
shared-memory region, 2 MB away from, and in a different region than, the IMEM page `RESTART_REASON` claims to
be. The `_PHYS` suffix on the constant is the part that makes it look authoritative, and the digits are the
SoC's shared-RAM base, not its IMEM base.

## 4. `entry_reset.h`'s claim is false on two independent grounds, and one of them is measured

The header says the word is *"the IMEM word Android's own `bootinfo.c` reads back as `powerup_reason_details`
(0x0fa0065c)"*. Both halves fail:

1. **It is not the IMEM word** (§2, §3).
2. **`bootinfo.c` never reads any IMEM word.** `powerup_reason_details` is `get_powerup_reason()`
   (`bootinfo.c:126`), set once at boot from the DT property `pureason`
   (`drivers/of/fdt.c:587` → `setup.c:686` → `set_powerup_reason`). It is the **bootloader's** value, put into
   the DTB the bootloader hands over. Nothing in the kernel reads a restart reason out of a device register at
   all.

**And the measurement settles it without our marker being involved.** At 08:05:11 UTC on 2026-09-25 the phone's
own Android took `adb reboot bootloader` — which writes **`0x77665500`** to its real IMEM word, §1. The next
Android boot (kernel start 08:05:41, measured twice: 677 §3 and again here, uptime `3433.09` at host 09:02:54)
reports `0x191`, i.e. `pu_reason >> 16 == 0` (`unknown reboot` is `find_first_bit(0,32) == 32 ≥ RS_REASON_MAX`,
`bootinfo.c:83-117`; the low half decodes as WARMRST | KPD | USB_CHG | HWRST over `bootinfo.h:21-43`). **A word
we know was written, by Android's own kernel, at the address the mechanism names, was not in the next boot's
report.** So the mechanism is not "the IMEM word becomes `pureason`" — the bootloader consumes or clears it,
and what it reports is its own composition. That refutation does not depend on any byte this project writes.

**A second, independent absurdity argument.** `0x7766550*`'s high half is `0x7766`, and `0x7866` for ours; both
have bit 1 set, and `find_first_bit` would return 1, printing `reset_reasons[1]` = **`kpanic`**, with
`is_abnormal_powerup()` true (`0x20000 & ...`, `bootinfo.c:68`). Every bootloader reboot would report a panic.
It does not (measured above), so the IMEM word demonstrably does not reach that field.

## 5. What this changes, and what it deliberately does not

* **The reset is unaffected, and that is measured rather than argued.** 678's ending and `entry_epilogue` write
  `RESTART_REASON` then `PS_HOLD ← 0`; *every run that has ever returned a log* has done both, and the reset is
  `PS_HOLD`'s (`MSM8974_MPM2_PSHOLD_PHYS 0xFC4AB000`, `msm_iomap-8974.h:37` — the project's constant, confirmed
  against the SoC's own header for the first time here). The breadcrumb is decorative in both writers.
* **So 674's claim survives intact** — step 4 needs no new mapping, because step 4's two registers are the GIC
  megabyte and `PS_HOLD`, both already mapped. `RESTART_REASON` was never one of them; 681's "the line the
  reset path depends on" was wrong about that too.
* **The breadcrumb's *purpose* is now nameable, and it is not the one `entry_reset.h` states.** The IMEM is the
  one region on this SoC that **persists across a reset by design** — that is why the reboot reason lives there
  — and this project is not writing to it. A breadcrumb aimed at `0xFE80565C` would survive a hang and be
  readable by the **next** run's payload, which is exactly the gap 663 named (a hang destroys the reading,
  because the log lives in DRAM and needs a return). **That is the design consequence of this step**, and it is
  named here as a candidate for the next build, not taken: a payload change is a build, and the arm in `out/`
  is owed a press (660 §5).
* **The decision about the current write is owed, and it is one of two:** aim it at `0xFE80565C` (a new section
  for the `0xFE800000` megabyte, and it buys the cross-hang channel above), or drop it and keep `PS_HOLD` alone.
  Both are payload builds and belong with the items 676 §6 and 677 §6 already queued for one sitting.
* **`entry_reset.h`'s false claim is owed, not edited here**: that file is under `stages/stage90/xnu_arm_boot/**`,
  which the gate hashes against the manifest, so an unbuilt edit makes the gate refuse the armed press — 673 §3's
  order, unchanged.
* **The naming defect is `stage90.h:17`'s and it is left in place.** `MSM_IMEM_BASE_PHYS` names the shared RAM.
  Renaming it is free; changing its *value* is not (it moves the payload's bytes), so the comment records the
  measurement and the value stays until the build that decides the write's future.

## 6. Safety, and why this write has never been seen to matter

The word this project writes lands in the **SMEM region, at offset `0x65C`**. SMEM is live: the RPM, the modem
and Android's `qcom,smem`/`qcom,ipc-spinlock` clients all read it through the DT nodes §2 measured. So this is a
stray 32-bit write into a shared region — and it is worth saying plainly that **the SMEM header and heap layout
are the bootloader's and are not in this tree**, so whether `0x65C` is header or heap is **not established
here**. It is named, not guessed.

What *is* established: it has never been observed to matter. Every run that has returned has written it and
Android has come up every time — this reading's own run included (kernel start 08:05:41, one hour of stable
uptime at the time of measurement) — and the plausible reason is that the payload runs after a warm reset from
`fastboot boot`, when the modem has not been brought up and nobody is reading SMEM concurrently, and that the
bootloader re-establishes SMEM before Android reads it. **The constraint holds by measurement, not by design,
and that gap is the reason the decision in §5 is worth taking rather than inheriting.**

## 7. What was changed in the tree

Two comment-only edits, both verified **byte-inert** by rebuilding with the arm's own flags
(`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`) and comparing all six artifacts against the park: **all six
SAME**, and `resolve_arm_set.sh out/stage90` still answers `armed-seam-endrun-88972ba9 / 94c95342... / 8540160`.

* `xnu_arm_vm_init_full_pmap.c` — 681's Phase 5 comment replaced (it asserted the IMEM), and its Phase 4 comment
  trimmed to the part that survives (neither address is the IMEM; this VA has no reader in either image).
* `stage90.h` — a comment above the two constants recording the measurement and why the value is *not* changed.

Read-only otherwise: `grep`, `sed` and `od` over the tree, and `adb` reads of five DT `reg` properties, three
`sysfs` values and two `/proc/uptime` samples. **No build output was installed anywhere but `out/stage90/`
itself, and it is byte-identical to the park** — no `fastboot`, no boot, no gate, no runner, nothing written to
storage, the neighbour `33e80afe` off the bus and untouched.

**This does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot, no new device reading of the
frontier: it makes a false comment true, a wrong constant's name recordable, and a future instrument nameable.
The frontier is where 652 left it — XNU reaches pid 1, runs the userland phase, dies at the idle exit's
`pop {fp, pc}` — and **TWRP-to-storage stays withheld**.
