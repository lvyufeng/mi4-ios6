# Stage52 cancro public-XNU target identity scaffold
#
# This is deliberately declarative. Stage52 validates public source references
# and records the cancro/MSM8974 ARMv7 target boundary; it does not mutate the
# ignored external/ checkouts and it does not try to build a full mach_kernel.

STAGE52_TARGET := cancro
STAGE52_ARCH := armv7
STAGE52_CPU := cortex-a15
STAGE52_MACHINE := msm8974
STAGE52_PLATFORM := xiaomi-mi4-cancro

STAGE52_XNU_BASELINE := xnu-2050.22.13
STAGE52_XNU_BASELINE_COMMIT := cc8a9b0ce917bb7115f5c97a78b38db871557db0
STAGE52_XNU_MASTER_VERSION := 12.3.0
STAGE52_ARM_REFERENCE := xnu-4570.1.46

STAGE52_PUBLIC_ONLY := 1
STAGE52_NO_FULL_MACH_KERNEL := 1
STAGE52_NO_XNU_EXECUTION := 1
STAGE52_NO_PUBLIC_XNU_EXECUTION := 1
STAGE52_NO_EXTERNAL_MUTATION := 1
STAGE52_STAGE52_OBJECT_PLAN := targets/cancro.stage52.objects
STAGE52_OBJECT_SUBSET_COMPILE := 1
STAGE52_OBJECT_SUBSET_OUTPUT_DIR := out/stage52/xnu-objects
STAGE52_CONTROLLED_PUBLIC_XNU_LINK_PROOF := 1
STAGE52_XNU_LINK_OUTPUT_DIR := out/stage52/xnu-link
STAGE52_NO_PUBLIC_XNU_EXECUTION := 1
STAGE52_NO_MACHO_EXECUTION := 1
