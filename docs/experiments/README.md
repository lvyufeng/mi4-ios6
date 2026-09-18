# Experiment logs

One log per bring-up experiment. The logs are ordered chronologically, and the
numbering is offset from the stage numbering: **experiment NN corresponds to stage
NN-3**. Experiment 01 and 02 predate the `stageN/` directory convention, so they
have no stage.

Path commands inside these logs are a record of what was actually run at the time.
For the six retained snapshots the paths have been updated to the current layout
(`stages/stage86/...`); the logs belonging to the archived stages (00-84) keep
their original commands, since those stages are no longer in the working tree -
see `../../stages/README.md` for the retention policy and how to restore an
archived stage with `tools/stage-archive.sh`.

| Stage | Experiment | Title |
| --- | --- | --- |
| — | [experiment-01](experiment-01-cmdline.md) | Custom Kernel Cmdline via Non-Persistent Boot |
| — | [experiment-02](experiment-02-usb-log-loop.md) | USB-Only Persistent Kernel Log Loop |
| stage0 | [experiment-03](experiment-03-stage0-bare-metal.md) | Stage0 Bare-Metal ARMv7 Payload |
| stage1 | [experiment-04](experiment-04-stage1-boot-wrapper.md) | Stage1 Boot Wrapper / XNU Boot Args Stub |
| stage2 | [experiment-05](experiment-05-stage2-c-runtime.md) | Stage2 C Runtime + Fuller Apple Device Tree |
| stage3 | [experiment-06](experiment-06-stage3-hardware-probes.md) | Stage3 Read-Only Hardware Probes |
| stage4 | [experiment-07](experiment-07-stage4-timebase-vectors.md) | Stage4 Timebase + Exception Vectors |
| stage5 | [experiment-08](experiment-08-stage5-xnu-skeleton.md) | Stage5 XNU-Adjacent Kernel Skeleton |
| stage6 | [experiment-09](experiment-09-stage6-pe-state-abort.md) | Stage6 PE_state Skeleton + Data-Abort Recovery |
| stage7 | [experiment-10](experiment-10-stage7-gic-skeleton.md) | Stage7 GIC Driver Skeleton (Read-Only) |
| stage8 | [experiment-11](experiment-11-stage8-sgi-irq.md) | Stage8 Controlled SGI/IRQ Delivery |
| stage9 | [experiment-12](experiment-12-stage9-timer-irq.md) | Stage9 ARM Generic Timer IRQ |
| stage10 | [experiment-13](experiment-13-stage10-mmu-identity.md) | Stage10 ARMv7 MMU Identity Mapping |
| stage11 | [experiment-14](experiment-14-stage11-high-alias.md) | Stage11 High Virtual Alias Mapping |
| stage12 | [experiment-15](experiment-15-stage12-high-call.md) | Stage12 High-Virtual Function Call |
| stage13 | [experiment-16](experiment-16-stage13-high-bootstrap.md) | Stage13 High-Virtual Kernel Bootstrap Handoff |
| stage14 | [experiment-17](experiment-17-stage14-high-pe-state.md) | Stage14 High-Virtual PE/XNU State Bootstrap |
| stage15 | [experiment-18](experiment-18-stage15-high-validation.md) | Stage15 High-Virtual Bootstrap Validation Status |
| stage16 | [experiment-19](experiment-19-stage16-high-init-sequence.md) | Stage16 High-Virtual Init Sequence |
| stage17 | [experiment-20](experiment-20-stage17-high-root.md) | Stage17 High-Virtual Root Kernel Path |
| stage18 | [experiment-21](experiment-21-stage18-high-dt-summary.md) | Stage18 High-Virtual Boot Args and Device-Tree Summary |
| stage19 | [experiment-22](experiment-22-stage19-platform-result.md) | Stage19 Versioned High-Root Platform Result |
| stage20 | [experiment-23](experiment-23-stage20-phase-table.md) | Stage20 High-Root Phase Table |
| stage21 | [experiment-24](experiment-24-stage21-service-table.md) | Stage21 High-Root Service Table |
| stage22 | [experiment-25](experiment-25-stage22-phase-service-deps.md) | Stage22 Phase-Service Dependencies |
| stage23 | [experiment-26](experiment-26-stage23-phase-dispatcher.md) | Stage23 Descriptor-Driven Phase Dispatcher |
| stage24 | [experiment-27](experiment-27-stage24-service-dispatcher.md) | Stage24 Descriptor-Driven Service Dispatcher |
| stage25 | [experiment-28](experiment-28-stage25-bootstrap-registry.md) | Stage25 High-Root Bootstrap Registry |
| stage26 | [experiment-29](experiment-29-stage26-boot-policy.md) | Stage26 Registry-Gated Boot Policy |
| stage27 | [experiment-30](experiment-30-stage27-bootstrap-manifest.md) | Stage27 Descriptor-Driven Bootstrap Manifest |
| stage28 | [experiment-31](experiment-31-stage28-launch-contract.md) | Stage28 Manifest-Driven Launch Contract |
| stage29 | [experiment-32](experiment-32-stage29-startup-boundary.md) | Stage29 Contract-Consuming Startup Boundary |
| stage30 | [experiment-33](experiment-33-stage30-startup-routine.md) | Stage30 Startup Routine Handoff |
| stage31 | [experiment-34](experiment-34-stage31-startup-entry.md) | Stage31 High-Virtual Startup Entry |
| stage32 | [experiment-35](experiment-35-stage32-kernel-callouts.md) | Stage32 Startup-Entry Kernel Callouts |
| stage33 | [experiment-36](experiment-36-stage33-kernel-context.md) | Stage33 Kernel-Start Context |
| stage34 | [experiment-37](experiment-37-stage34-vm-plan.md) | Stage34 VM Bootstrap Plan |
| stage35 | [experiment-38](experiment-38-stage35-vm-state.md) | Stage35 VM Bootstrap State |
| stage36 | [experiment-39](experiment-39-stage36-bootstrap-allocator.md) | Stage36 Bootstrap Allocator Descriptor |
| stage37 | [experiment-40](experiment-40-stage37-pmap-workspace.md) | Stage37 Pmap Bootstrap Workspace |
| stage38 | [experiment-41](experiment-41-stage38-kernel-object-table.md) | Stage38 Kernel Object Table |
| stage39 | [experiment-42](experiment-42-stage39-kernel-collection-handoff.md) | Stage39 Kernel Collection Handoff |
| stage40 | [experiment-43](experiment-43-stage40-kernel-collection-entry-table.md) | Stage40 Kernel Collection Entry Table |
| stage41 | [experiment-44](experiment-44-stage41-kernel-collection-object-graph.md) | Stage41 Kernel Collection Object Graph |
| stage42 | [experiment-45](experiment-45-stage42-kernel-collection-dependency-resolution.md) | Stage42 Kernel Collection Dependency Resolution |
| stage43 | [experiment-46](experiment-46-stage43-mach-o-xnu-loader-probe.md) | Stage43 Mach-O/XNU Loader Probe |
| stage44 | [experiment-47](experiment-47-stage44-mach-o-load-plan.md) | Stage44 Mach-O Fixture Load Plan |
| stage45 | [experiment-48](experiment-48-stage45-macho-materialization.md) | Stage45 Local Mach-O Materialization |
| stage46 | [experiment-49](experiment-49-stage46-xnu-tte-dryrun.md) | Stage46 XNU TTE Workspace Dry-Run |
| stage47 | [experiment-50](experiment-50-stage47-tte-verify-vtop-dryrun.md) | Stage47 TTE Descriptor Verification and VTOP Dry-Run |
| stage48 | [experiment-51](experiment-51-stage48-high-va-safe-table-materialization.md) | Stage48 High-VA Safe Table Materialization |
| stage49 | [experiment-52](experiment-52-stage49-controlled-ttbr-roundtrip.md) | Stage49 Controlled TTBR0 Round-Trip |
| stage50 | [experiment-53](experiment-53-stage50-public-xnu-workspace-cancro-scaffold.md) | Stage50 Public-XNU Workspace and Cancro Scaffold |
| stage51 | [experiment-54](experiment-54-stage51-public-xnu-object-subset-compile.md) | Stage51 Public-XNU Object Subset Compile |
| stage52 | [experiment-55](experiment-55-stage52-public-xnu-controlled-link-proof.md) | Stage52 Public-XNU Controlled Link Proof |
| stage53 | [experiment-56](experiment-56-stage53-xnu-compile-graph.md) | Stage53 Public-XNU Compile Graph Migration Proof |
| stage54 | [experiment-57](experiment-57-stage54-pexpert-platform-compile-graph.md) | Stage54 Public-XNU pexpert/platform Compile Graph Proof |
| stage55 | [experiment-58](experiment-58-stage55-pexpert-consistent-debug-compile-graph.md) | Stage55 Public-XNU pexpert consistent-debug Compile/Link Proof |
| stage56 | [experiment-59](experiment-59-stage56-xnu-bootstrap-mapping-contract.md) | Stage56 XNU bootstrap mapping contract |
| stage57 | [experiment-60](experiment-60-stage57-xnu-pmap-bootstrap-contract.md) | Stage57 XNU pmap/bootstrap allocation contract |
| stage58 | [experiment-61](experiment-61-stage58-xnu-pmap-table-dryrun-contract.md) | Stage58 XNU pmap table population dry-run contract |
| stage59 | [experiment-62](experiment-62-stage59-xnu-pmap-page-dryrun-contract.md) | Stage59 XNU pmap page-granular dry-run contract |
| stage60 | [experiment-63](experiment-63-stage60-xnu-pmap-attr-dryrun-contract.md) | Stage60 XNU pmap cache/MMU attribute dry-run contract |
| stage61 | [experiment-64](experiment-64-stage61-xnu-pmap-multiwindow-dryrun-contract.md) | Stage61 XNU pmap multi-window page-granular dry-run contract |
| stage62 | [experiment-65](experiment-65-stage62-xnu-pmap-transition-dryrun-contract.md) | Stage62 XNU pmap safe live-table transition prerequisite dry-run contract |
| stage63 | [experiment-66](experiment-66-stage63-xnu-pexpert-hook-readiness-contract.md) | Stage63 MSM8974 XNU pexpert interrupt/timer hook readiness contract |
| stage64 | [experiment-67](experiment-67-stage64-xnu-iokit-platform-scaffold.md) | Stage64 XNU IOKit/platform-driver scaffold readiness contract |
| stage65 | [experiment-68](experiment-68-stage65-xnu-iokit-match-dryrun-contract.md) | Stage65 XNU IOKit service/driver match dry-run contract |
| stage66 | [experiment-69](experiment-69-stage66-xnu-iokit-registry-service-dryrun-contract.md) | Stage66 XNU IOKit registry/service dry-run contract |
| stage67 | [experiment-70](experiment-70-stage67-xnu-iokit-provider-plane-dryrun-contract.md) | Stage67 XNU IOKit provider-plane dry-run contract |
| stage68 | [experiment-71](experiment-71-stage68-xnu-iokit-catalog-property-dryrun-contract.md) | Stage68 XNU IOKit catalog/property dry-run contract |
| stage69 | [experiment-72](experiment-72-stage69-xnu-iokit-property-inheritance-dryrun-contract.md) | Stage69 XNU IOKit property-inheritance / registry-entry dry-run contract |
| stage70 | [experiment-73](experiment-73-stage70-xnu-iokit-registry-topology-dryrun-contract.md) | Stage70 XNU IOKit registry-plane / IODeviceTree topology dry-run contract |
| stage71 | [experiment-74](experiment-74-stage71-xnu-iokit-attach-start-readiness-dryrun-contract.md) | Stage71 XNU IOKit attach/start readiness dry-run contract |
| stage72 | [experiment-75](experiment-75-stage72-xnu-iokit-lifecycle-register-service-readiness-dryrun-contract.md) | Stage72 XNU IOKit lifecycle/register-service readiness dry-run contract |
| stage73 | [experiment-76](experiment-76-stage73-xnu-iokit-provider-notification-delivery-readiness-dryrun-contract.md) | Stage73 XNU IOKit provider-notification / interest / delivery-readiness dry-run contract |
| stage74 | [experiment-77](experiment-77-stage74-xnu-iokit-provider-callback-client-notification-readiness-dryrun-contract.md) | Stage74 XNU IOKit provider-callback / client-notification readiness dry-run contract |
| stage75 | [experiment-78](experiment-78-stage75-xnu-iokit-client-open-provider-claim-close-readiness-dryrun-contract.md) | Stage75 XNU IOKit client-open / provider-claim / close-readiness dry-run contract |
| stage76 | [experiment-79](experiment-79-stage76-live-xnu-like-execution-probe.md) | Stage76 live XNU-like execution probe |
| stage77 | [experiment-80](experiment-80-stage77-xnu-entry-stub.md) | Stage77 XNU entry stub |
| stage78 | [experiment-81](experiment-81-stage78-early-pmap-platform-init.md) | Stage78 early pmap/platform-init micro-sequence |
| stage79 | [experiment-82](experiment-82-stage79-pe-init-platform-false.md) | Stage79 PE_init_platform(FALSE,args)-shaped pre-VM pexpert/platform micro-sequence |
| stage80 | [experiment-83](experiment-83-stage80-arm-init-post-pe-bootstrap.md) | Stage80 arm_init post-PE_FALSE bootstrap/timebase-registration boundary |
| stage81 | [experiment-84](experiment-84-stage81-arm-vm-init-live-pmap.md) | Stage81 arm_vm_init live-pmap installation window |
| stage82 | [experiment-85](experiment-85-stage82-renumbering-validation.md) | Stage82 Renumbering Validation Baseline |
| stage83 | [experiment-86](experiment-86-stage83-full-kernel-pmap.md) | Stage83 Full Kernel Virtual Address Space L1+L2 Pmap |
| stage84 | [experiment-87](experiment-87-stage84-sgi-irq-timer.md) | Stage84 GIC/IRQ Timer Boundary |
| stage85 | [experiment-88](experiment-88-stage85-high-va-code-exec.md) | High-Virtual Code Execution at 0x80000000 (L2 Pages) |
| stage86 | [experiment-89](experiment-89-stage86-high-va-irq-handler.md) | High-Virtual IRQ Handler Relocation at 0x80000000 (L2 Pages) |
| stage87 | [experiment-90](experiment-90-stage87-high-va-data-abort-handler.md) | High-VA Data Abort Handler |
| stage88 | [experiment-91](experiment-91-stage88-high-va-undef-handler.md) | High-VA Undefined Instruction Handler |
| stage89 | [experiment-92](experiment-92-stage89-macho-loader.md) | Mach-O Kernel Loader |
| stage90 | [experiment-93](experiment-93-stage90-phase0-preflight-watchdog.md) | Stage90 Phase 0: Preflight Watchdog Run |
| stage90 | [experiment-94](experiment-94-stage90-phase0-watchdog-and-baseline.md) | Stage90 Phase 0: Recovery Net Proved, and the Two Bugs the Baseline Run Found |
| stage90 | [experiment-95](experiment-95-stage90-phase1a-normal-nc.md) | Stage90 Phase 1a: Normal, Non-cacheable DRAM |
| stage90 | [experiment-96](experiment-96-stage90-phase1-exclusives-work.md) | Stage90 Phase 1: LDREX/STREX Work Here, and the Probe Said Otherwise for Four Runs |
| stage90 | [experiment-97](experiment-97-stage90-phase1-icache.md) | Stage90 Phase 1: The I-Cache On, and the Six Places That Asserted Caches Are Off |
| stage90 | [experiment-98](experiment-98-stage90-phase1-dcache.md) | Stage90 Phase 1: The D-Cache On, and Phase 1 Closed |
| stage90 | [experiment-99](experiment-99-stage90-phase2-boot-args-on-hardware.md) | Stage90 Phase 2: The Conforming boot_args on Hardware, Under the Caches |
| stage90 | [experiment-100](experiment-100-first-public-xnu-execution.md) | The First Public-XNU Code to Execute on the Device |
| stage90 | [experiment-101](experiment-101-phase3-shim-first-hardware-run.md) | Phase 3 Platform Shim, First Hardware Run: One Missing Assignment |
| stage90 | [experiment-102](experiment-102-xnu-consistent-debug-registry.md) | XNU's Crash-Log Registry Runs on MSM8974, and It Writes a Record |
| stage90 | [experiment-103](experiment-103-xnu-entry-point-assembles.md) | XNU's Entry Point Assembles, and the Missing Configuration Is Now a Symbol List |
| stage90 | [experiment-104](experiment-104-shim-drives-the-timer.md) | The Platform Shim Drives the Timer, Through XNU's Own Interface |
| stage90 | [experiment-105](experiment-105-xnu-console-and-boot-args.md) | XNU Writes Into the Crash Log, and Acts on Our Boot Arguments |
| stage90 | [experiment-106](experiment-106-xnu-start-executes.md) | XNU's `_start` Executes on MSM8974 |
| stage90 | [experiment-107](experiment-107-xnu-arm-entry-path-measured.md) | XNU's ARM Entry Path, Measured: Seven Missing Names, Three of Them MIG |
| stage90 | [experiment-108](experiment-108-mig-builds-and-generates-headers.md) | MIG Builds on This Host, and 23 Mach Interface Headers Are Generated |
| stage90 | [experiment-109](experiment-109-xnu-arm-entry-path-compiles.md) | XNU's ARM Entry Path Compiles: `arm_init.c` 35 Errors → 0 |
| stage90 | [experiment-110](experiment-110-xnu-arm-layer-compiles.md) | XNU's Entire ARM Layer Compiles: 32 of 32, 443 Symbols From Linking |
| stage90 | [experiment-111](experiment-111-osfmk-measured.md) | The Rest of osfmk Measured — and the Build Configuration Was Never Absent |
| stage90 | [experiment-112](experiment-112-apple-kernel-config-extracted.md) | Apple's Own Kernel Configuration, Extracted and Used |
| stage90 | [experiment-113](experiment-113-arm-kernel-build-manifest.md) | Apple's Own Build Manifest for an ARM Kernel, Resolved: 694 Files |
| stage90 | [experiment-114](experiment-114-kernel-manifest-compiled.md) | The Kernel's Own File List, Compiled: 172 of 569 |
| stage90 | [experiment-115](experiment-115-minimal-boot-configuration.md) | A Minimal-Boot Configuration, and One Define That Unlocked Twelve Files |
| stage90 | [experiment-116](experiment-116-define-generator-and-timeout.md) | A 45-Minute Hang, and Two Tooling Defects It Exposed |
| stage90 | [experiment-117](experiment-117-shadows-and-broad-paths.md) | Two Shims Were Shadowing Real Headers, and a Broad Include Path Cost Four Files |
| stage90 | [experiment-118](experiment-118-per-component-defines.md) | The Per-Component Defines, and the 127-File Defect They Were |
| stage90 | [experiment-119](experiment-119-build-generated-headers.md) | The Headers the Build Generates, and the Three Ways This Project Was Not Generating Them |
| stage90 | [experiment-120](experiment-120-options-generated-headers.md) | The Third Kind of Generated Header: `OPTIONS/`, and the Ten Shims It Replaced |
| stage90 | [experiment-121](experiment-121-apple-defines-and-two-pass-mig.md) | `__APPLE__`, and the MIG Run That Was Missing Half Its Arguments |
| stage90 | [experiment-122](experiment-122-makesyscalls-outputs.md) | `makesyscalls.sh` Has Six Output Kinds and This Project Asked for One |
| stage90 | [experiment-123](experiment-123-link-gap-and-force-includes.md) | The Link Gap, Measured: 1187 Symbols — and the Force-Include That Was Reaching Nine Files |
| stage90 | [experiment-124](experiment-124-mig-output-set.md) | Generate What the Makefiles Say: 40 MIG Outputs, Not Everything — and the Include Order Flips Back |
| stage90 | [experiment-125](experiment-125-first-link-and-option-scope.md) | The First Real Link, and the 47 Files That Were Compiled as the Wrong Configuration |
| stage90 | [experiment-126](experiment-126-clockt-and-libsa-types.md) | `CLOCK_T` Was a Workaround for the Old Defect, and `<types.h>` Is the Kernel's |
| stage90 | [experiment-127](experiment-127-device-headers-and-a-source-incompatibility.md) | The Loopback Header, and the First Failure That Is Not a Configuration Gap |
| stage90 | [experiment-128](experiment-128-manifest-missing-component.md) | The Manifest Was Missing a Whole Component, and It Cost 200 Symbols |
| stage90 | [experiment-129](experiment-129-caddr-t.md) | `caddr_t`, the Biggest Boot-Path File, and the Three Items Left Behind It |
| stage90 | [experiment-130](experiment-130-two-negative-results.md) | Two Boot-Path Files Investigated, Neither Fixed, and Why |
| stage90 | [experiment-131](experiment-131-device-table.md) | The Build Defined `MONOTONIC` and the Manifest Never Built the File |
| stage90 | [experiment-132](experiment-132-pty-device-and-a-named-unknown.md) | The pty Device, Resolved by Measurement — and One Value Left With No Evidence |
| stage90 | [experiment-133](experiment-133-device-baseline-revalidated.md) | The Device Line, Re-Validated After Fourteen Stages of Host-Side Work |
| stage90 | [experiment-134](experiment-134-pe-arm-init-interrupts-replacement.md) | The Replacement for `pe_arm_init_interrupts`, Run on the Device |
| stage90 | [experiment-135](experiment-135-first-xnu-image.md) | An XNU Image, Linked at XNU's Own Addresses, With a Real `_start` |
| stage90 | [experiment-136](experiment-136-assym-and-a-correction.md) | The Sixth Generator (`genassym.c`), and a Correction to the Fifth Experiment |
| stage90 | [experiment-137](experiment-137-work-order.md) | The Work Order, Computed From the Image: `arm_init` Reaches a Stub One Edge In |
| stage90 | [experiment-138](experiment-138-underscore-convention-and-work-list.md) | The Assembly Underscore Convention, and the Work List Completed |
| stage90 | [experiment-139](experiment-139-two-ast-headers.md) | Two Headers Named `kern/ast.h`, and Apple's Per-Component Include Order |
| stage90 | [experiment-140](experiment-140-three-missing-names.md) | Three Missing Names, Three Narrow Answers |
| stage90 | [experiment-141](experiment-141-vnode-trim-is-the-triple.md) | `vnode_trim` Is a Target-Triple Conflict, and No Macro Can Fix It |
| stage90 | [experiment-142](experiment-142-darwin-assembles.md) | The Darwin Target Assembles Every File the EABI Target Cannot |
| stage90 | [experiment-143](experiment-143-fiq-not-available.md) | MSM8974 Will Not Deliver an FIQ to Non-Secure PL1, Measured |
| stage90 | [experiment-144](experiment-144-sync-qos-and-the-export-lists.md) | Why `sync_qos_count_t` Is Not a Missing Typedef, and What the Export Lists Say |
| stage90 | [experiment-145](experiment-145-the-simport-answer.md) | The `simport` Answer, Found in Two Makefile Rules — 602 → 606 of 615 |
| stage90 | [experiment-146](experiment-146-arm-case-and-the-last-three.md) | `__ARM__` Versus `__arm__`, and the Three Single-File Failures Left |
| stage90 | [experiment-147](experiment-147-phase3-shim-design-closed.md) | The Phase 3 Shim's Design, Closed on Hardware |
| stage90 | [experiment-148](experiment-148-nbpfilter-and-the-last-category.md) | `NBPFILTER` Is a Count, and the Category of Files Only Valid With an Option Built |
| stage90 | [experiment-149](experiment-149-macho-path-measured.md) | The Mach-O Path, Built and Measured, So the Decision Has a Price Tag |
| stage90 | [experiment-150](experiment-150-lld-cannot-link-armv7-and-the-dialect-translation.md) | `ld64.lld` Cannot Link 32-bit ARM Mach-O; the Dialect Translation Closes All 17 |
| stage90 | [experiment-151](experiment-151-force-includes-and-stdbool.md) | The Force-Includes Were Breaking a File, and the Fix Is Per-File |
| stage90 | [experiment-152](experiment-152-the-cpp-block-and-one-dead-line.md) | The Whole C++ Block Is Behind One Dead Line, and No Flag Can Fix It *(retracted — see 154)* |
| stage90 | [experiment-153](experiment-153-shim-audit.md) | The Shims Audited: 16 Shadow Real Headers, and None of Them Matter |
| stage90 | [experiment-154](experiment-154-cpp-entered-the-build.md) | The C++ Block Entered the Build at 75 of 83 — and the Line Blocking It Was Mine |
| stage90 | [experiment-155](experiment-155-the-translator-wrote-into-the-tree.md) | The Assembler Translator Was Writing Into Apple's Tree, Through a Symlink |
| stage90 | [experiment-156](experiment-156-the-mig-run-was-missing-kernel.md) | The MIG Run Was Missing `-DKERNEL` — 13 Symbols, and Every Routine Name |
| stage90 | [experiment-157](experiment-157-the-force-include-was-rewriting-declarations.md) | `-include kern/queue.h` Was Rewriting Declarations Before the Compiler Saw Them |
| stage90 | [experiment-158](experiment-158-the-closure-is-the-whole-kernel.md) | `arm_init`'s Closure Is the Whole Kernel; the Manifest Omits 126 `optional` Sources |
| stage90 | [experiment-159](experiment-159-the-real-arm-init-ran.md) | The Real `arm_init` Ran on the Device, and Named `cpu_data_init` |
| stage90 | [experiment-160](experiment-160-vm-object-compiles.md) | `vm_object.c` Compiles at Last — 58 of the 189, and `-Dconst=` Is Per-File |
| stage90 | [experiment-161](experiment-161-the-target-abi.md) | The Target ABI: an ELF Triple with Darwin's Type Widths — 132 → 97 |
| stage90 | [experiment-162](experiment-162-the-firehose-seam-is-a-component.md) | The Firehose Seam Is a Component, Not a Value — `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` Is 16, 97 → 72 |
| stage90 | [experiment-163](experiment-163-the-nearest-blocker-was-not-xnu-s.md) | The Nearest Blocker Was Not XNU's: the EABI Runtime, 72 → 63, and a Figure of Mine That Was Wrong |
| stage90 | [experiment-164](experiment-164-an-off-option-has-two-spellings.md) | An Off Option Has Two Spellings, and the Tree Depends on Both — `task.c` Compiles, 220 → 114 |
| stage90 | [experiment-165](experiment-165-two-files-one-line-each.md) | Two Files, One Line Each: the C++ Block Is Complete, and No Boot-Path Stub Is Behind a Compile Failure (114 → 102) |
| stage90 | [experiment-166](experiment-166-the-generated-inputs-existed-for-one-configuration.md) | The Generated Inputs Existed for One Configuration Only — `pty.h`/`loop.h` Fell Through to the Host's (102 → 94) |
| stage90 | [experiment-167](experiment-167-the-string-h-shim-was-one-block-short.md) | The `<string.h>` Shim Was One Block Short, and the Measurement Was Reading an Older Directory (94 → 92) |
| stage90 | [experiment-168](experiment-168-cpu-data-init-ran-and-named-pe-init-platform.md) | The Second Real XNU Object Ran on the Device, and Named `PE_init_platform` |
| stage90 | [experiment-169](experiment-169-the-image-crossed-the-limit-and-stopped-at-strlcpy.md) | The Image Crossed Its Old Page-Table Limit, and `PE_init_platform` Stopped at `strlcpy` |
| stage90 | [experiment-170](experiment-170-xnu-own-pe-state-named-the-device-tree.md) | XNU's Own `PE_state` Named the Device Tree: `xnu_entry_dtinit_base=0x00280000` |
| stage90 | [experiment-171](experiment-171-the-reader-ran-and-stopped-at-strcmp.md) | XNU's Own Reader Ran Against This Project's Tree, and Stopped at `strcmp` |
| stage90 | [experiment-172](experiment-172-the-reader-reached-a-cpu-nodes-state.md) | The Reader Walked to a CPU Node's `state` Property — Six Claims from `stub_hit=strncmp` |
| stage90 | [experiment-173](experiment-173-pe-init-platform-reached-its-last-statement.md) | `PE_init_platform` Reached Its Last Statement, `pe_init_debug` |
| stage90 | [experiment-174](experiment-174-pe-init-debug-ran-and-stopped-at-pe-boot-args.md) | `pe_init_debug` Ran, and the Frontier Is `PE_boot_args` |
| stage90 | [experiment-175](experiment-175-the-entry-image-is-derived-not-written-down-twice.md) | The Entry Image Is Derived From the Link, Not Written Down Twice |
| stage90 | [experiment-176](experiment-176-pe-init-platform-returned-and-the-soc-base-came-from-the-tree.md) | `PE_init_platform` Returned, and the SoC Base Came Out of the Tree |
| stage90 | [experiment-177](experiment-177-xnu-counted-four-cpus-and-the-frontier-is-cpu-processor-alloc.md) | XNU Counted Four CPUs in This Project's Tree, and the Frontier Is `cpu_processor_alloc` |
| stage90 | [experiment-178](experiment-178-the-two-halves-of-cpudataentries-agree-and-the-frontier-is-thread-bootstrap.md) | The Two Halves of `CpuDataEntries` Agree, and the Frontier Is `thread_bootstrap` |
| stage90 | [experiment-179](experiment-179-thread-bootstrap-ran-and-the-frontier-is-timer-init.md) | `thread_bootstrap` Ran Its Assignments, and the Frontier Is `timer_init` |
| stage90 | [experiment-180](experiment-180-thread-bootstrap-ran-to-its-last-statement-and-the-frontier-is-machine-set-current-thread.md) | `thread_bootstrap` Ran to Its Last Statement, and the Frontier Is `machine_set_current_thread` |
| stage90 | [experiment-181](experiment-181-the-tpidrprw-round-trip-held-and-the-frontier-is-rtclock-early-init.md) | The TPIDRPRW Round Trip Held, and the Frontier Is `rtclock_early_init` |
| stage90 | [experiment-182](experiment-182-the-timebase-callback-ran-and-the-frontier-is-the-toolchains-aeabi-uldivmod.md) | The Timebase Callback Ran, and the Frontier Is the Toolchain's `__aeabi_uldivmod` |
| stage90 | [experiment-183](experiment-183-the-thread-pointer-round-tripped-and-the-frontier-is-kernel-early-bootstrap.md) | The Thread Pointer Round-Tripped Through TPIDRPRW, and the Frontier Is `kernel_early_bootstrap` |
| stage90 | [experiment-184](experiment-184-the-timebase-came-out-as-the-trees-19-2-mhz-and-a-wrong-prototype-wrote-to-address-zero.md) | The Timebase Came Out as the Tree's 19.2 MHz, and a Hand-Written Prototype Wrote to Address 0 |
| stage90 | [experiment-185](experiment-185-lck-mod-init-ran-for-real-and-the-frontier-is-strncpy.md) | `lck_mod_init` Ran for Real, and the Frontier Is `strncpy` |
| stage90 | [experiment-186](experiment-186-the-lock-group-read-back-as-its-own-bytes-and-the-frontier-is-lck-mtx-init-ext.md) | The Lock Group Read Back as Its Own Bytes, and the Frontier Is `lck_mtx_init_ext` |
| stage90 | [experiment-187](experiment-187-three-lock-groups-read-out-of-xnus-own-list-and-the-frontier-is-timer-call-get-priority-params.md) | Three Lock Groups Read Out of XNU's Own List, and the Frontier Is `timer_call_get_priority_params` |
| stage90 | [experiment-188](experiment-188-the-timer-table-converted-into-ticks-and-the-frontier-is-do-cpuid.md) | The Timer Table Converted into Ticks, and the Frontier Is `do_cpuid` |
| stage90 | [experiment-189](experiment-189-the-cpu-identified-itself-and-the-frontier-is-processor-bootstrap.md) | The CPU Identified Itself, and the Frontier Is `processor_bootstrap` |
| stage90 | [experiment-190](experiment-190-the-schedulers-own-processor-checked-pointer-by-pointer-and-the-frontier-is-processor-data-init.md) | The Scheduler's Own Processor, Checked Pointer by Pointer, and the Frontier Is `processor_data_init` |
| stage90 | [experiment-191](experiment-191-processor-data-init-ran-the-interrupt-state-and-the-frontier-is-ml-set-interrupts-enabled.md) | `processor_data_init` Ran, the Interrupt State, and the Frontier Is `ml_set_interrupts_enabled` |
| stage90 | [experiment-192](experiment-192-the-interrupt-state-ran-for-real-and-the-frontier-is-a-device-tree-node-not-an-object.md) | The Interrupt State Ran for Real, and the Frontier Is a Device-Tree Node, Not an Object |
| stage90 | [experiment-193](experiment-193-defaults-was-the-missing-node-and-the-f-bit-does-not-read-1.md) | `/defaults` Was the Missing Node, and the F Bit Does Not Read 1 on This Device |
| stage90 | [experiment-194](experiment-194-the-image-describes-itself-in-a-mach-o-header-and-the-frontier-is-vm-set-page-size.md) | The Image Describes Itself in a Mach-O Header, and the Frontier Is `vm_set_page_size` |
| stage90 | [experiment-195](experiment-195-the-vm-object-runs-and-the-flush-was-reading-the-wrong-cache.md) | The VM's Own Object Runs, `pmap_bootstrap` Is Reached, and the Flush That Carries the Evidence Was Reading the Wrong Cache |
| stage90 | [experiment-196](experiment-196-the-payloads-whole-cache-flush-was-reading-the-l2-and-selecting-no-way.md) | The Payload's Whole-Cache Flush Was Reading the L2's Geometry and Selecting No Way |
| stage90 | [experiment-197](experiment-197-the-pmap-runs-and-the-frontier-is-patch-low-glo-static-region.md) | The pmap Runs, `arm_vm_init` Reaches Its Last Instruction, and the Frontier Is `patch_low_glo_static_region` |
| stage90 | [experiment-198](experiment-198-patch-low-glo-runs-arm-vm-init-returns-and-the-frontier-is-printf-init.md) | `patch_low_glo` Runs, `arm_vm_init` Returns, and the Frontier Is `printf_init` |
| stage90 | [experiment-199](experiment-199-printf-init-runs-and-the-frontier-is-bsd-log-init.md) | `printf_init` Runs, the Four Bytes Are Now the Object, and the Frontier Is `bsd_log_init` |
| stage90 | [experiment-200](experiment-200-an-empty-function-is-a-whole-object-and-the-frontier-is-panic-init.md) | An Empty Function Is a Whole Object, `arm_init` Walks Back Into `osfmk/`, and the Frontier Is `panic_init` |
| stage90 | [experiment-201](experiment-201-xnu-reads-this-images-mach-o-header-and-the-frontier-is-pe-consistent-debug-inherit.md) | XNU Reads This Image's Mach-O Header, the Panic Stand-In Is Retired, and the Frontier Is `PE_consistent_debug_inherit` |
| stage90 | [experiment-202](experiment-202-the-smallest-object-so-far-and-the-frontier-is-pe-init-kprintf.md) | The Smallest Object So Far, a Prediction That Depended on a Payload Switch, and the Frontier Is `PE_init_kprintf` |
| stage90 | [experiment-203](experiment-203-pe-init-kprintf-runs-to-serial-init.md) | `PE_init_kprintf` Runs Its Whole Body Up to `serial_init`, and Two Storage Stubs Become Real Variables |
| stage90 | [experiment-204](experiment-204-serial-init-runs-and-a-prediction-that-was-wrong-because-there-are-two-command-lines.md) | `serial_init` Runs, a Step With No Cost, and a Prediction That Was Wrong Because There Are Two Command Lines |
| stage90 | [experiment-205](experiment-205-initialize-screen-runs-its-no-video-branch-and-the-frontier-is-switch-to-serial-console.md) | `initialize_screen` Runs Its No-Video Branch, XNU Decides There Is No Framebuffer, and the Frontier Is `switch_to_serial_console` |
| stage90 | [experiment-206](experiment-206-arm-inits-tail-runs-and-the-frontier-is-io-map.md) | `arm_init`'s Tail Runs, XNU's Own Platform Interrupt Mapping Is Reached, and the Frontier Is `io_map` |
| stage90 | [experiment-207](experiment-207-io-map-runs-and-the-frontier-is-bcopy-phys.md) | `io_map` Runs, XNU Creates a Kernel Mapping for Itself, and the Frontier Is `bcopy_phys` |
| stage90 | [experiment-208](experiment-208-bcopy-phys-runs-and-the-frontier-is-clean-dcache.md) | `bcopy_phys` Runs, the Secondary-CPU Handshake Is Written Into the Low-Vectors Page, and the Frontier Is `CleanPoC_DcacheRegion` |
| stage90 | [experiment-209](experiment-209-caches-asm-links-and-the-frontier-is-clean-dcache.md) | The Cache-Maintenance Surface Links, `cpu_machine_idle_init` Reaches Its Last Call, and the Frontier Is `clean_dcache` |
| stage90 | [experiment-210](experiment-210-xnu-enters-pe-arm-init-interrupts-itself-and-the-frontier-is-early-random.md) | XNU Enters `pe_arm_init_interrupts` Itself, `cpu_machine_idle_init` Returns, and the Frontier Is `early_random` |
| stage90 | [experiment-211](experiment-211-the-prng-links-the-handoff-tree-gets-boot-entropy-and-the-frontier-is-ccdrbg-factory-nisthmac.md) | The PRNG Links, the Handoff Tree Grows Boot Entropy, and the Frontier Is `ccdrbg_factory_nisthmac` |
| stage90 | [experiment-212](experiment-212-the-drbg-factory-runs-and-the-frontier-is-cchmac-init.md) | the DRBG Factory Runs, `early_random` Makes Its First Indirect Call, and the Frontier Is `cchmac_init` |
| stage90 | [experiment-213](experiment-213-the-prediction-was-wrong-and-the-frontier-is-a-null-compress-pointer.md) | the Prediction Was Wrong, the Run Took a Prefetch Abort at Address Zero, and the Method That Predicted It Was Reading Only Half the Calls |
| stage90 | [experiment-214](experiment-214-ccsha1-eay-di-gets-its-value-and-the-frontier-is-cchmac-update.md) | `ccsha1_eay_di` Gets Its Value, Real SHA-1 Compression Runs on the Device, and the Frontier Is `cchmac_update` |
| stage90 | [experiment-215](experiment-215-four-bytes-and-the-frontier-is-ccdigest-update.md) | the Smallest Object There Is, `cchmac_update` Resolves and Adds Nothing, and the Frontier Is `ccdigest_update` |
| stage90 | [experiment-216](experiment-216-ccdigest-update-runs-to-completion-and-the-frontier-is-cchmac-final.md) | `ccdigest_update` Runs to Completion, the First Generic Digest Helper on the Device, and the Frontier Is `cchmac_final` |
| stage90 | [experiment-217](experiment-217-cchmac-final-resolves-and-the-frontier-is-ccdigest-final-64be.md) | `cchmac_final` Resolves, Nothing New Executes, and the Frontier Is `ccdigest_final_64be` |
| stage90 | [experiment-218](experiment-218-the-whole-hmac-finalisation-runs-and-the-frontier-is-cchmac.md) | the Whole HMAC Finalisation Runs on the Device, the Text Arithmetic Gets Three Terms, and the Frontier Is `cchmac` |
| stage90 | [experiment-219](experiment-219-a-whole-one-shot-hmac-runs-and-the-frontier-is-cc-clear.md) | a Whole One-Shot HMAC Runs, `cchmac` Resolves, and the Frontier Is `cc_clear` |
| stage90 | [experiment-220](experiment-220-cc-clear-adds-memset-s-and-the-frontier-is-memset-s.md) | `cc_clear` Resolves and Adds `memset_s`, `.text` Does Not Move, and the Frontier Is `memset_s` |
| stage90 | [experiment-221](experiment-221-the-whole-drbg-generate-path-runs-and-the-frontier-is-cc-cmp-safe.md) | the Whole DRBG Generate Path Runs to Its FIPS Compare, and the Frontier Is `cc_cmp_safe` |
| stage90 | [experiment-222](experiment-222-the-prng-is-finished-and-the-frontier-is-kernel-bootstrap.md) | the PRNG Is Finished, `arm_init` Completes, and the Frontier Is `kernel_bootstrap`'s `bsd_scale_setup` |
| stage90 | [experiment-223](experiment-223-bsds-unix-startup-links-and-the-frontier-is-a-group.md) | BSD's `unix_startup` Links, the Frontier's First Group, and the Next Stop Is `kernel_debug_string_early` |
| stage90 | [experiment-224](experiment-224-bsd-init-links-a-hundred-stubs-and-the-frontier-is-kernel-debug-string-early.md) | `bsd_init` Links, a Hundred Stubs Arrive, and the Frontier Is `kernel_debug_string_early` |
| stage90 | [experiment-225](experiment-225-the-first-neon-runs-and-the-frontier-is-vm-mem-bootstrap.md) | the First NEON Executes, `kernel_debug_string_early` Runs, and the Frontier Is `vm_mem_bootstrap` |
| stage90 | [experiment-226](experiment-226-the-prediction-was-wrong-and-why.md) | the Prediction Was Wrong — `vm_compressor_init_locks` for `zone_bootstrap` — and Why |
| stage90 | [experiment-227](experiment-227-the-tools-first-prediction-and-vm-map-steal-memory.md) | the Tool's First Prediction, `vm_map_steal_memory`, and the NEON That Waited Two Experiments |
| stage90 | [experiment-228](experiment-228-the-prediction-was-wrong-and-the-guard-rule-could-not-tell-two-loops-apart.md) | the Prediction Was Wrong — `OSCompareAndSwap16` for `zone_bootstrap` — and the Guard Rule That Could Not Tell Two Loops Apart |
| stage90 | [experiment-229](experiment-229-ten-stubs-for-nothing-and-the-frontier-moved-two-levels-in.md) | Ten Stubs for Nothing, Zero Added, and the Tail Term Identified to the Byte |
| stage90 | [experiment-230](experiment-230-the-stop-was-inside-the-object-just-linked.md) | the Stop Was Inside the Object Just Linked — and `--root <frontier>` Names It |

Stage90's other notes live inside the snapshot directory:
`../../stages/stage90/README.md`, `IMPLEMENTATION.md`,
`IMPLEMENTATION_STATUS.md` and `QUICK_START.md`.
