# 958 — recognising the device's 3 GB: the total rides the report, not the map (2026-10-09)

957 measured the device's own `/memory/reg` at **`0xC0000000` = 3.000 GiB** (two banks around the
`0x80000000` boundary) and designed the port. 956 proved the 3 GB **cannot** ride the linear map:
D13's managed map is `gMemSize` long from the fixed `MANAGED_BASE`, and 1 GiB fills its 16 KB L1
exactly. This rung implements the port those two pinned: **`hw.memsize` reports 3 GB; the map stays
the boot bank.**

## 1. The two numbers, and why they are different

The payload has exactly one RAM constant today — `memory_reg[] = { RAM_PHYS_BASE,
RAM_CONSOLE_BASE - RAM_PHYS_BASE }` = `{0x80000000, 0x5e500000}` — and it drives the boot args, the
`/memory` node, and `/defaults hw.memsize` alike. The device has **3 GiB**; the payload can only
make **`0x5e500000`** (the high bank up to its own ram_console at `0xde500000`) linearly usable. The
port names them separately (`src/stage90.h`):

| name | value | what it feeds |
|---|---|---|
| `RAM_BOOT_BANK_SIZE` | `0x5e500000` | `args->memSize`, `/memory/reg`, every payload assert, the pmap |
| `RAM_DEVICE_TOTAL` | `0xC0000000` (3.000 GiB) | `/defaults hw.memsize` — the total XNU recognises |

`RAM_BOOT_BANK_SIZE` is *defined as* `(RAM_CONSOLE_BASE - RAM_PHYS_BASE)` (the same expression, one
definition), so nothing that reads it moves. `RAM_DEVICE_TOTAL` is the number 957 measured.

## 2. The channel, and why the reg was NOT rewritten

957 §2's first draft proposed emitting a two-bank `/memory/reg`. That is the wrong blast radius: the
payload **re-reads** `reg` in three places and asserts it against the boot bank in five more —
`pe_state.c:38-39` (`PE_state.memorySize`, asserted in `mmu.c:2335`/`xnu_early_pmap_platform_init.c:287`/
`xnu_pe_init_platform_false.c:351`/`xnu_pexpert_hook_readiness_contract.c:283`/`pe_state_validate`),
`pexpert.c:65-70`, and `mmu.c:2381-2408` (`root_dt_memory_*`). A two-bank reg makes word1 = bank 0's
size and breaks every one. So `reg` stays the **boot bank** — which is also *correct*: it is the
region the linear map actually covers, not the device's total.

The total rides `/defaults hw.memsize` instead — the property XNU **already** reads for exactly
"physical ram size". `PE_get_default("hw.memsize", …)` (`pexpert/gen/bootargs.c:318`) looks up
`/defaults` then the named property; the payload has emitted that node since experiment 193
(`stage90_main.c:867`), so the channel exists and the port only changes its **value**, not the tree
shape (no new node, no property count to bump).

## 3. The D13 edit: `max_mem` alone

`arm_vm_init.c:327` sets `max_mem = mem_size = sane_size = gMemSize`. Those four globals are **not**
one thing:

- `mem_size` sizes the pmap's page tables (`pmap.c:3492-3526`), `sane_size` sizes kalloc and the zones
  (`kalloc.c:350`, `vm_init.c:154`) — the *machine*;
- **`max_mem` alone is the reported total**: `bsd/kern/kern_mib.c:365` serves `hw.memsize` from
  `&max_mem`, and `startup.c:198` copies it into `machine_info.max_mem`.

So the port raises **`max_mem`** to the DT total and leaves the other three at the boot bank.
`tools/patch_d13_memory_total.py` turns the assignment into:

```c
    max_mem = mem_size = sane_size = gMemSize;
#ifdef STAGE90_XNU_MEM_TOTAL
    { uint32_t _hw_memsize = 0u;
      if (PE_get_default("hw.memsize", &_hw_memsize, sizeof(_hw_memsize)) != 0 && _hw_memsize != 0u)
          max_mem = (uint64_t)_hw_memsize; }
#endif
```

Guarded, so an undefined `STAGE90_XNU_MEM_TOTAL` leaves Apple's line alone and the shipped arms do
not move. **Measured byte-neutral**: the switch-off object is `sha256 a40bcc59…`, identical with and
without the patch (`arm_vm_init.o`). Getting that property required one correction — the include and
the file-scope arm marker go **after** the function's last `panic` (line 290), not at the top of the
file: this file's `panic("…")` calls expand `__FILE__ ":" __LINE__` into `.rodata` literals, so an
insert above them shifts every one and the object differs in `.rodata.str1.1` alone (measured: the
first placement moved the strings `:189→:196` etc.). The code was identical; only the line numbers
moved. Placing the insert after the last `__LINE__` and before `arm_vm_init` fixes it.

**Measured on**: the switch-on object compiles (134/134 osfmk), carries `entry_xnu_mem_total_arm_on`,
and its `arm_vm_init` disassembly has `bl PE_get_default` → test → store to the `max_mem` global.

## 4. Why 4570 is not touched, and cannot be

4570 has no `MANAGED_BASE` (956) — its linear map is derived from `MEM_SIZE_MAX`, so a total far above
the window has no fixed base to overflow. It also **already** reads the same property: 4570's
`arm_init:282` does `PE_get_default("hw.memsize", &memsize, …)` into `xmaxmem`, clamped by
`MEM_SIZE_MAX`. Raising the total there is inert (the window `memSize` is far below it, so
`arm_vm_init`'s `if (mem_size > memory_size) mem_size = memory_size;` never fires). So the switch is
**D13-only**: `build_xnu_arm_kernel.sh` defines `STAGE90_XNU_MEM_TOTAL` only when the tree is D13
(`osfmk/sys/types.h` exists) and **refuses it on 4570**, the mirror of `build_entry.sh`'s 4570 refusal
of the D13-rejected `MEM_SIZE_MAX` switch.

## 5. The checks

`tools/check_d13_memory_total.py` re-derives every premise (the port is correct only if all hold),
with 4570 as the control:

1. **`max_mem` is the reported total** — `kern_mib.c`'s `SYSCTL_QUAD(..., HW_MEMSIZE, ..., &max_mem, …)`.
2. **the D13 edit is present and byte-neutral off** — the `#else` arm is Apple's line exactly, the
   `#include` is inside the guard, the marker exists.
3. **the map is untouched** — `gMemSize = args->memSize`, the managed `l2_cache_to_range(…, gMemSize,
   TRUE)`, and `MANAGED_BASE 0xC0000000` (956's premise).
4. **the payload publishes the two DIFFERENT quantities** — `RAM_BOOT_BANK_SIZE` = `0x5e500000`,
   `RAM_DEVICE_TOTAL` = `0xC0000000` (above the bank), `/memory/reg` from `memory_reg[]`,
   `/defaults hw.memsize` from `RAM_DEVICE_TOTAL`.

`--selftest` feeds thirteen measured mutations (a moved total, a total not above the bank, a reg that
is no longer the boot bank, a marker that vanished, `hw.memsize` that stopped reading `max_mem`, a map
that stopped using `gMemSize`, …). Registered in `make check` (both forms) and in `build_entry.sh`
beside the two checks that guard the same tree. The switch `STAGE90_XNU_MEM_TOTAL` is in
`ENTRY_ARM_KEYS`, so a build that changed it without asking is refused, and the record names it.

## 6. What this rung does NOT do

- **No low-bank pages.** The goal's verb is *recognise*; the kernel now reports 3 GB. Making the
  3 GB *allocatable* is 915's option (A), a bank iterator — not this rung.
- **No window change.** 956 caps the window at 1 GiB; the total does not ride it.

**Next concrete step:** a D13 press whose log shows `hw.memsize` = `0xC0000000` with
`xnu_entry_args_memSize` still the boot bank and the pmap vstart unchanged. **PRESS IS THE OPERATOR'S.**

*Provenance: `external/xnu-hd2-darwin13/xnu` (`arm_vm_init.c`, `kern_mib.c`, `kalloc.c`, `vm_init.c`,
`pmap.c`, `startup.c`, `bootargs.c`) verified this session; the payload compiled (`scripts/build.sh`
rc=0); the kernel object compiled switch-on (134/134) and is byte-neutral switch-off. Follows
[[mi4-957-3gb-measurement-and-region-reader]], [[mi4-956-d13-entry-window-ceiling]],
[[mi4-915-multibank-region-list-design]]. **Device unmodified.***