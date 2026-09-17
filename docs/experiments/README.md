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

Stage90's other notes live inside the snapshot directory:
`../../stages/stage90/README.md`, `IMPLEMENTATION.md`,
`IMPLEMENTATION_STATUS.md` and `QUICK_START.md`.
