# Stage50 cancro public-XNU target identity scaffold
#
# This is deliberately declarative. Stage50 validates public source references
# and records the cancro/MSM8974 ARMv7 target boundary; it does not mutate the
# ignored external/ checkouts and it does not try to build a full mach_kernel.

STAGE50_TARGET := cancro
STAGE50_ARCH := armv7
STAGE50_CPU := cortex-a15
STAGE50_MACHINE := msm8974
STAGE50_PLATFORM := xiaomi-mi4-cancro

STAGE50_XNU_BASELINE := xnu-2050.22.13
STAGE50_XNU_BASELINE_COMMIT := cc8a9b0ce917bb7115f5c97a78b38db871557db0
STAGE50_XNU_MASTER_VERSION := 12.3.0
STAGE50_ARM_REFERENCE := xnu-4570.1.46

STAGE50_PUBLIC_ONLY := 1
STAGE50_NO_FULL_MACH_KERNEL := 1
STAGE50_NO_XNU_EXECUTION := 1
STAGE50_NO_PUBLIC_XNU_EXECUTION := 1
STAGE50_NO_EXTERNAL_MUTATION := 1
STAGE50_STAGE51_OBJECT_PLAN := targets/cancro.stage51.objects
