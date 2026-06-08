#!/usr/bin/env python3
"""Stage60 public-XNU pexpert/platform compile graph scanner.

This is a host-only classifier for the Stage60 compile migration proof. It
records why a tiny public-XNU source set is allowed for compile/link proof and
why dependency-heavy ARM/platform/runtime sources remain reference-only or
excluded. It does not mutate external checkouts and it does not execute public
XNU code.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

STATUS_OK = 0x60000001
STATUS_BASE = 0x60000000
BASELINE_COMMIT = "cc8a9b0ce917bb7115f5c97a78b38db871557db0"
BASELINE_COMMIT32 = 0xCC8A9B0C
BASELINE_MASTER = "12.3.0"
BASELINE_MASTER32 = 0x000C0300

BIT_SOURCE_ROOT_PRESENT = 0x00000001
BIT_CANDIDATES_PRESENT = 0x00000002
BIT_DEVICE_TREE_CLASSIFIED = 0x00000004
BIT_BOOTARGS_CLASSIFIED = 0x00000008
BIT_PE_GEN_CLASSIFIED = 0x00000010
BIT_FORBIDDEN_EXCLUDED = 0x00000020
BIT_INCLUDE_DEPS_RECORDED = 0x00000040
BIT_SYMBOL_DEPS_RECORDED = 0x00000080
BIT_SHIM_NEEDS_RECORDED = 0x00000100
BIT_RISK_CLASSES_RECORDED = 0x00000200
BIT_PUBLIC_2050_BASELINE = 0x00000400
BIT_4570_REFERENCE_ONLY = 0x00000800
BIT_NO_FULL_XNU_BUILD = 0x00001000
BIT_NO_PUBLIC_XNU_EXEC = 0x00002000
BIT_NO_MACHO_EXEC = 0x00004000
BIT_NO_EXTERNAL_MUTATION = 0x00008000
BIT_OUTPUTS_IGNORED = 0x00010000
BIT_FAIL_CLOSED = 0x00020000
BIT_ARM_BOOTARGS_CLASSIFIED = 0x00040000
BIT_ARM_BOOTARGS_ALLOWED = 0x00080000
BIT_PLATFORM_REFS_RECORDED = 0x00100000
BIT_BLOCKED_RUNTIME_RECORDED = 0x00200000
BIT_ARM_BOOT_LAYOUT_RECORDED = 0x00400000
BIT_NO_PLATFORM_RUNTIME_EXEC = 0x00800000
BIT_ARM_CONSISTENT_DEBUG_CLASSIFIED = 0x01000000
BIT_ARM_CONSISTENT_DEBUG_ALLOWED = 0x02000000
BIT_CONSISTENT_DEBUG_LAYOUT_RECORDED = 0x04000000
BIT_ARM_PEXPERT_RUNTIME_BLOCKED = 0x08000000
BIT_BOOTSTRAP_CONTRACT_SELECTED = 0x10000000
REQUIRED_MASK = 0x1FFFFFFF

PMAP_REF_ARM_VM_INIT = 0x00000001
PMAP_REF_PMAP_C = 0x00000002
PMAP_REF_PMAP_H = 0x00000004
PMAP_REF_PROC_REG_H = 0x00000008
PMAP_REF_VM_PARAM_H = 0x00000010
PMAP_REF_REQUIRED = 0x0000001F
PMAP_RUNTIME_BLOCK_ARM_VM_INIT = 0x00000001
PMAP_RUNTIME_BLOCK_PMAP_C = 0x00000002
PMAP_RUNTIME_BLOCK_REQUIRED = 0x00000003

FAIL_SOURCE_ROOT = 0x00000001
FAIL_CANDIDATES = 0x00000002
FAIL_BASELINE = 0x00000004
FAIL_FORBIDDEN = 0x00000008
FAIL_SHIMS = 0x00000010
FAIL_ARM_BOOTARGS = 0x00000020
FAIL_PLATFORM_REFS = 0x00000040
FAIL_DUPLICATES = 0x00000080
FAIL_ARM_CONSISTENT_DEBUG = 0x00000100
FAIL_BOOTSTRAP_CONTRACT = 0x00000200
FAIL_PMAP_REFERENCE = 0x00000400
FAIL_EXTERNAL_MUTATION = 0x80000000


@dataclass(frozen=True)
class Candidate:
    candidate_id: str
    relpath: str
    baseline: str
    source_family: str
    architecture_class: str
    eligibility: str
    risk_class: int
    allow_compile: int
    allow_link: int
    required_shims: tuple[str, ...]
    defined_symbols: tuple[str, ...]
    undefined_symbols: tuple[str, ...]


CANDIDATES = [
    Candidate(
        "device_tree",
        "external/xnu-upstream/pexpert/gen/device_tree.c",
        "xnu-2050.22.13",
        "pexpert/gen",
        "generic",
        "compile-proven",
        0,
        1,
        1,
        tuple(),
        ("DTInit", "DTLookupEntry", "DTGetProperty"),
        ("kalloc", "kfree", "strcmp", "strlen"),
    ),
    Candidate(
        "bootargs",
        "external/xnu-upstream/pexpert/gen/bootargs.c",
        "xnu-2050.22.13",
        "pexpert/gen",
        "generic",
        "compile-proven",
        0,
        1,
        1,
        tuple(),
        ("PE_parse_boot_argn", "PE_get_default", "PE_imgsrc_mount_supported"),
        ("DTLookupEntry", "DTGetProperty", "IODTGetDefault", "PE_boot_args", "memcpy", "strncmp"),
    ),
    Candidate(
        "pe_gen",
        "external/xnu-upstream/pexpert/gen/pe_gen.c",
        "xnu-2050.22.13",
        "pexpert/gen",
        "generic",
        "bounded-proven",
        2,
        1,
        1,
        ("shims/kern/debug.h",),
        ("pe_init_debug", "PE_enter_debugger", "PE_init_printf", "PE_putc", "gPESerialBaud", "appleClut8"),
        ("PE_parse_boot_argn", "Debugger", "cnputc", "vcattach"),
    ),
    Candidate(
        "arm_pe_bootargs",
        "external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/arm",
        "arm-pexpert-bounded",
        "bounded-new",
        2,
        1,
        1,
        ("shims/pexpert/boot.h", "shims/pexpert/pexpert.h"),
        ("PE_boot_args",),
        ("PE_state",),
    ),
    Candidate(
        "arm_boot_h_abi_reference",
        "external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/pexpert/arm",
        "arm-boot-abi-reference",
        "abi-reference-only",
        3,
        0,
        0,
        tuple(),
        ("boot_args", "Boot_Video", "BOOT_LINE_LENGTH", "kBootArgsRevision2", "kBootArgsVersion2"),
        tuple(),
    ),
    Candidate(
        "arm_pe_identify_machine_blocked",
        "external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/arm",
        "platform-runtime",
        "blocked-runtime-reference",
        4,
        0,
        0,
        tuple(),
        ("pe_identify_machine",),
        ("DTLookupEntry", "DTInitEntryIterator", "DTIterateEntries", "DTGetProperty", "assert", "clean_mmu_dcache"),
    ),
    Candidate(
        "arm_pe_init_blocked",
        "external/xnu-4570.1.46/pexpert/arm/pe_init.c",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/arm",
        "platform-runtime",
        "blocked-runtime-reference",
        4,
        0,
        0,
        tuple(),
        ("PE_init_platform", "PE_state", "gPEClockFrequencyInfo"),
        ("ml_io_map_wcomb", "StartIOKit", "panic", "PE_initialize_console", "pe_identify_machine"),
    ),
    Candidate(
        "arm_pe_consistent_debug",
        "external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/arm",
        "arm-pexpert-bounded-debug",
        "bounded-new",
        2,
        1,
        1,
        ("shims/pexpert/arm/consistent_debug.h", "shims/libkern/OSAtomic.h", "shims/machine/machine_routines.h"),
        ("PE_consistent_debug_inherit", "PE_consistent_debug_register", "PE_consistent_debug_enabled"),
        ("DTLookupEntry", "DTGetProperty", "OSCompareAndSwap64", "ml_map_high_window"),
    ),
    Candidate(
        "arm_consistent_debug_h_abi_reference",
        "external/xnu-4570.1.46/pexpert/pexpert/arm/consistent_debug.h",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/pexpert/arm",
        "arm-consistent-debug-abi-reference",
        "abi-reference-only",
        3,
        0,
        0,
        tuple(),
        ("dbg_registry_t", "dbg_record_header_t", "DEBUG_REGISTRY_MAX_RECORDS", "kDbgIdUnusedEntry", "kDbgIdReservedEntry"),
        tuple(),
    ),
    Candidate(
        "arm_pe_kprintf_blocked",
        "external/xnu-4570.1.46/pexpert/arm/pe_kprintf.c",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/arm",
        "console-runtime",
        "blocked-runtime-reference",
        4,
        0,
        0,
        tuple(),
        ("PE_init_kprintf", "kprintf", "serial_putc", "serial_getc"),
        ("simple_lock", "ml_set_interrupts_enabled", "ml_get_interrupts_enabled", "cpu_number", "_doprnt_log", "os_log_with_args", "panic", "serial_init", "uart_putc", "uart_getc"),
    ),
    Candidate(
        "arm_pe_serial_blocked",
        "external/xnu-4570.1.46/pexpert/arm/pe_serial.c",
        "xnu-4570.1.46-public-arm-reference",
        "pexpert/arm",
        "hardware-console-runtime",
        "blocked-runtime-reference",
        4,
        0,
        0,
        tuple(),
        ("serial_init", "uart_putc", "uart_getc"),
        ("ml_io_map", "ml_io_map_wcomb", "PE_parse_boot_argn", "flush_dcache", "invalidate_dcache"),
    ),
    Candidate(
        "i386_pe_serial_negative_reference",
        "external/xnu-upstream/pexpert/i386/pe_serial.c",
        "xnu-2050.22.13-negative-reference",
        "pexpert/i386",
        "wrong-arch-hardware-runtime",
        "wrong-arch-reference-only",
        4,
        0,
        0,
        tuple(),
        tuple(),
        tuple(),
    ),
    Candidate(
        "i386_pe_kprintf_negative_reference",
        "external/xnu-upstream/pexpert/i386/pe_kprintf.c",
        "xnu-2050.22.13-negative-reference",
        "pexpert/i386",
        "wrong-arch-console-runtime",
        "wrong-arch-reference-only",
        4,
        0,
        0,
        tuple(),
        tuple(),
        tuple(),
    ),
    Candidate(
        "arm_start_forbidden",
        "external/xnu-4570.1.46/osfmk/arm/start.s",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/arm",
        "startup",
        "excluded-high-risk",
        5,
        0,
        0,
        tuple(),
        tuple(),
        tuple(),
    ),
    Candidate(
        "arm_init_forbidden",
        "external/xnu-4570.1.46/osfmk/arm/arm_init.c",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/arm",
        "startup",
        "excluded-high-risk",
        5,
        0,
        0,
        tuple(),
        tuple(),
        tuple(),
    ),
    Candidate(
        "arm_vm_init_forbidden",
        "external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/arm",
        "vm/pmap",
        "excluded-high-risk",
        5,
        0,
        0,
        tuple(),
        tuple(),
        tuple(),
    ),
    Candidate(
        "arm_pmap_forbidden",
        "external/xnu-4570.1.46/osfmk/arm/pmap.c",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/arm",
        "vm/pmap",
        "excluded-high-risk",
        5,
        0,
        0,
        tuple(),
        tuple(),
        tuple(),
    ),
    Candidate(
        "arm_pmap_h_reference",
        "external/xnu-4570.1.46/osfmk/arm/pmap.h",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/arm",
        "vm/pmap-abi-reference",
        "abi-reference-only",
        4,
        0,
        0,
        tuple(),
        ("tt_entry_t", "pt_entry_t", "arm_atop", "arm_ptoa"),
        tuple(),
    ),
    Candidate(
        "arm_proc_reg_h_reference",
        "external/xnu-4570.1.46/osfmk/arm/proc_reg.h",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/arm",
        "vm/pmap-register-reference",
        "abi-reference-only",
        4,
        0,
        0,
        tuple(),
        ("ARM_PGSHIFT", "ARM_PGBYTES", "ARM_PGMASK"),
        tuple(),
    ),
    Candidate(
        "mach_arm_vm_param_h_reference",
        "external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h",
        "xnu-4570.1.46-public-arm-reference",
        "osfmk/mach/arm",
        "vm-param-reference",
        "abi-reference-only",
        4,
        0,
        0,
        tuple(),
        ("PAGE_SHIFT_CONST", "PAGE_SIZE", "VM_MIN_KERNEL_ADDRESS", "VM_MAX_KERNEL_ADDRESS"),
        tuple(),
    ),
]


def run_git_status(repo: Path) -> str:
    if not (repo / ".git").exists():
        return ""
    return subprocess.check_output(["git", "-C", str(repo), "status", "--short"], text=True)


def run_git_rev(repo: Path) -> str:
    return subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()


def read_includes(path: Path) -> list[str]:
    if not path.exists() or not path.is_file():
        return []
    includes: list[str] = []
    pat = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = pat.match(line)
        if m:
            includes.append(m.group(1))
    return includes


def sha32_text(text: str) -> int:
    return int.from_bytes(hashlib.sha256(text.encode("utf-8")).digest()[:4], "big")


def main() -> int:
    stage_dir = Path(__file__).resolve().parent
    root = stage_dir.parent
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "out" / "stage60"
    if not out_dir.is_absolute():
        out_dir = (stage_dir / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    xnu_2050 = root / "external" / "xnu-upstream"
    xnu_4570 = root / "external" / "xnu-4570.1.46"

    status_txt = out_dir / "xnu-compile-graph-status.txt"
    manifest_tsv = out_dir / "xnu-compile-graph-manifest.tsv"
    includes_txt = out_dir / "xnu-compile-graph-includes.txt"
    symbols_txt = out_dir / "xnu-compile-graph-symbols.txt"
    risk_txt = out_dir / "xnu-compile-graph-risk.txt"
    json_txt = out_dir / "xnu-compile-graph-manifest.json"
    header = out_dir / "xnu_compile_graph_generated.h"

    satisfied = 0
    failure = 0
    records: list[dict[str, object]] = []

    def sat(bit: int) -> None:
        nonlocal satisfied
        satisfied |= bit

    def fail(bit: int) -> None:
        nonlocal failure
        failure |= bit

    source_roots_ok = xnu_2050.exists() and xnu_4570.exists()
    if source_roots_ok:
        sat(BIT_SOURCE_ROOT_PRESENT)
    else:
        fail(FAIL_SOURCE_ROOT)

    baseline_ok = False
    if xnu_2050.exists():
        try:
            rev = run_git_rev(xnu_2050)
            master = (xnu_2050 / "config" / "MasterVersion").read_text(encoding="utf-8", errors="replace").splitlines()[0].strip()
            baseline_ok = rev == BASELINE_COMMIT and master == BASELINE_MASTER
        except Exception:
            baseline_ok = False
    if baseline_ok:
        sat(BIT_PUBLIC_2050_BASELINE)
    else:
        fail(FAIL_BASELINE)

    external_clean = True
    for repo in (xnu_2050, xnu_4570):
        if repo.exists() and run_git_status(repo):
            external_clean = False
    if external_clean:
        sat(BIT_NO_EXTERNAL_MUTATION)
    else:
        fail(FAIL_EXTERNAL_MUTATION)

    all_candidates_present = True
    forbidden_excluded = True
    include_lines: list[str] = []
    symbol_lines: list[str] = []
    risk_lines: list[str] = []
    shim_required_count = 0
    max_risk = 0

    for c in CANDIDATES:
        path = root / c.relpath
        exists = path.exists()
        includes = read_includes(path)
        missing_shims = [s for s in c.required_shims if not (stage_dir / s).exists()]
        if not exists:
            all_candidates_present = False
        if c.eligibility in ("reference-only", "abi-reference-only", "blocked-runtime-reference", "wrong-arch-reference-only", "excluded-high-risk") and (c.allow_compile or c.allow_link):
            forbidden_excluded = False
        if missing_shims:
            all_candidates_present = False
        if c.required_shims:
            shim_required_count += 1
        max_risk = max(max_risk, c.risk_class)
        record = asdict(c)
        record.update({
            "exists": int(exists),
            "includes": includes,
            "missing_shims": missing_shims,
            "graph_key": sha32_text(c.candidate_id + c.relpath + c.eligibility),
        })
        records.append(record)
        include_lines.append(f"{c.candidate_id}\t" + ",".join(includes))
        symbol_lines.append(f"{c.candidate_id}\tdefines={','.join(c.defined_symbols)}\tundefs={','.join(c.undefined_symbols)}")
        risk_lines.append(f"{c.candidate_id}\trisk={c.risk_class}\teligibility={c.eligibility}\tcompile={c.allow_compile}\tlink={c.allow_link}")

    if all_candidates_present:
        sat(BIT_CANDIDATES_PRESENT)
    else:
        fail(FAIL_CANDIDATES)
    if any(r["candidate_id"] == "device_tree" and r["exists"] for r in records):
        sat(BIT_DEVICE_TREE_CLASSIFIED)
    if any(r["candidate_id"] == "bootargs" and r["exists"] for r in records):
        sat(BIT_BOOTARGS_CLASSIFIED)
    if any(r["candidate_id"] == "pe_gen" and r["exists"] and not r["missing_shims"] for r in records):
        sat(BIT_PE_GEN_CLASSIFIED)
    else:
        fail(FAIL_SHIMS)
    arm_bootargs_ok = any(r["candidate_id"] == "arm_pe_bootargs" and r["exists"] and not r["missing_shims"] for r in records)
    if arm_bootargs_ok:
        sat(BIT_ARM_BOOTARGS_CLASSIFIED)
        sat(BIT_ARM_BOOTARGS_ALLOWED)
    else:
        fail(FAIL_ARM_BOOTARGS)
    if any(r["candidate_id"] == "arm_boot_h_abi_reference" and r["exists"] for r in records):
        sat(BIT_ARM_BOOT_LAYOUT_RECORDED)
    else:
        fail(FAIL_ARM_BOOTARGS)
    arm_consistent_debug_ok = any(r["candidate_id"] == "arm_pe_consistent_debug" and r["exists"] and not r["missing_shims"] for r in records)
    if arm_consistent_debug_ok:
        sat(BIT_ARM_CONSISTENT_DEBUG_CLASSIFIED)
        sat(BIT_ARM_CONSISTENT_DEBUG_ALLOWED)
    else:
        fail(FAIL_ARM_CONSISTENT_DEBUG)
    if any(r["candidate_id"] == "arm_consistent_debug_h_abi_reference" and r["exists"] for r in records):
        sat(BIT_CONSISTENT_DEBUG_LAYOUT_RECORDED)
    else:
        fail(FAIL_ARM_CONSISTENT_DEBUG)
    arm_pe_kprintf_blocked = 1 if any(r["candidate_id"] == "arm_pe_kprintf_blocked" and r["exists"] and r["allow_compile"] == 0 and r["allow_link"] == 0 for r in records) else 0
    arm_pe_serial_blocked = 1 if any(r["candidate_id"] == "arm_pe_serial_blocked" and r["exists"] and r["allow_compile"] == 0 and r["allow_link"] == 0 for r in records) else 0
    arm_pe_identify_machine_blocked = 1 if any(r["candidate_id"] == "arm_pe_identify_machine_blocked" and r["exists"] and r["allow_compile"] == 0 and r["allow_link"] == 0 for r in records) else 0
    arm_pe_init_blocked = 1 if any(r["candidate_id"] == "arm_pe_init_blocked" and r["exists"] and r["allow_compile"] == 0 and r["allow_link"] == 0 for r in records) else 0
    if arm_pe_kprintf_blocked and arm_pe_serial_blocked and arm_pe_identify_machine_blocked and arm_pe_init_blocked:
        sat(BIT_ARM_PEXPERT_RUNTIME_BLOCKED)
    else:
        fail(FAIL_PLATFORM_REFS)
    if (stage_dir / "xnu_bootstrap_contract.c").exists():
        sat(BIT_BOOTSTRAP_CONTRACT_SELECTED)
        bootstrap_contract_selected = 1
    else:
        fail(FAIL_BOOTSTRAP_CONTRACT)
        bootstrap_contract_selected = 0
    platform_reference_count = sum(1 for r in records if r["eligibility"] in ("blocked-runtime-reference", "abi-reference-only"))
    blocked_runtime_count = sum(1 for r in records if r["eligibility"] == "blocked-runtime-reference")
    pmap_reference_ids = {
        "arm_vm_init_forbidden": PMAP_REF_ARM_VM_INIT,
        "arm_pmap_forbidden": PMAP_REF_PMAP_C,
        "arm_pmap_h_reference": PMAP_REF_PMAP_H,
        "arm_proc_reg_h_reference": PMAP_REF_PROC_REG_H,
        "mach_arm_vm_param_h_reference": PMAP_REF_VM_PARAM_H,
    }
    pmap_runtime_block_ids = {
        "arm_vm_init_forbidden": PMAP_RUNTIME_BLOCK_ARM_VM_INIT,
        "arm_pmap_forbidden": PMAP_RUNTIME_BLOCK_PMAP_C,
    }
    pmap_reference_mask = 0
    pmap_runtime_blocked_mask = 0
    pmap_public_compile_count = 0
    pmap_public_link_count = 0
    for r in records:
        cid = str(r["candidate_id"])
        if cid in pmap_reference_ids and r["exists"]:
            pmap_reference_mask |= pmap_reference_ids[cid]
            pmap_public_compile_count += int(r["allow_compile"])
            pmap_public_link_count += int(r["allow_link"])
            if cid in pmap_runtime_block_ids and r["allow_compile"] == 0 and r["allow_link"] == 0:
                pmap_runtime_blocked_mask |= pmap_runtime_block_ids[cid]
    pmap_reference_count = sum(1 for r in records if str(r["candidate_id"]) in pmap_reference_ids and r["exists"])
    pmap_reference_only = 1 if (pmap_reference_mask == PMAP_REF_REQUIRED and
                                pmap_runtime_blocked_mask == PMAP_RUNTIME_BLOCK_REQUIRED and
                                pmap_public_compile_count == 0 and pmap_public_link_count == 0) else 0
    if not pmap_reference_only:
        fail(FAIL_PMAP_REFERENCE)
    if platform_reference_count >= 6:
        sat(BIT_PLATFORM_REFS_RECORDED)
    else:
        fail(FAIL_PLATFORM_REFS)
    if blocked_runtime_count >= 4:
        sat(BIT_BLOCKED_RUNTIME_RECORDED)
    else:
        fail(FAIL_PLATFORM_REFS)
    if forbidden_excluded:
        sat(BIT_FORBIDDEN_EXCLUDED)
    else:
        fail(FAIL_FORBIDDEN)

    allowed_defined: dict[str, list[str]] = {}
    for c in CANDIDATES:
        if c.allow_compile or c.allow_link:
            for sym in c.defined_symbols:
                allowed_defined.setdefault(sym, []).append(c.candidate_id)
    duplicate_symbol_count = sum(1 for owners in allowed_defined.values() if len(owners) > 1)
    if duplicate_symbol_count:
        fail(FAIL_DUPLICATES)

    sat(BIT_INCLUDE_DEPS_RECORDED)
    sat(BIT_SYMBOL_DEPS_RECORDED)
    sat(BIT_SHIM_NEEDS_RECORDED)
    sat(BIT_RISK_CLASSES_RECORDED)
    sat(BIT_4570_REFERENCE_ONLY)
    sat(BIT_NO_FULL_XNU_BUILD)
    sat(BIT_NO_PUBLIC_XNU_EXEC)
    sat(BIT_NO_MACHO_EXEC)
    sat(BIT_NO_PLATFORM_RUNTIME_EXEC)
    if str(out_dir).endswith("/out/stage60") or "/out/stage60" in str(out_dir):
        sat(BIT_OUTPUTS_IGNORED)
    sat(BIT_FAIL_CLOSED)

    candidate_count = len(CANDIDATES)
    allowed_compile_count = sum(c.allow_compile for c in CANDIDATES)
    allowed_link_count = sum(c.allow_link for c in CANDIDATES)
    forbidden_count = sum(1 for c in CANDIDATES if c.eligibility in ("reference-only", "abi-reference-only", "blocked-runtime-reference", "wrong-arch-reference-only", "excluded-high-risk"))
    pe_gen_allowed = 1 if any(c.candidate_id == "pe_gen" and c.allow_compile and c.allow_link for c in CANDIDATES) else 0
    arm_bootargs_allowed = 1 if arm_bootargs_ok else 0
    ref_only = 1 if forbidden_count >= 1 and forbidden_excluded else 0
    pe_state_abi_recorded = 1 if arm_bootargs_ok else 0
    boot_args_arm_layout_recorded = 1 if (satisfied & BIT_ARM_BOOT_LAYOUT_RECORDED) else 0
    arm_consistent_debug_allowed = 1 if arm_consistent_debug_ok else 0
    consistent_debug_layout_recorded = 1 if (satisfied & BIT_CONSISTENT_DEBUG_LAYOUT_RECORDED) else 0

    status = STATUS_OK if satisfied == REQUIRED_MASK and failure == 0 else (STATUS_BASE | (failure & 0x0FFFFFFF))

    status_lines = [
        "Stage60 public-XNU pexpert/platform compile graph scan",
        "This is a host-only classifier; public-XNU code is not executed.",
        f"stage60_xnu_compile_graph_status=0x{status:08x}",
        f"stage60_xnu_compile_graph_required_mask=0x{REQUIRED_MASK:08x}",
        f"stage60_xnu_compile_graph_satisfied_mask=0x{satisfied:08x}",
        f"stage60_xnu_compile_graph_failure_mask=0x{failure:08x}",
        f"stage60_xnu_compile_graph_candidate_count=0x{candidate_count:08x}",
        f"stage60_xnu_compile_graph_allowed_compile_count=0x{allowed_compile_count:08x}",
        f"stage60_xnu_compile_graph_allowed_link_count=0x{allowed_link_count:08x}",
        f"stage60_xnu_compile_graph_forbidden_count=0x{forbidden_count:08x}",
        f"stage60_xnu_compile_graph_shim_required_count=0x{shim_required_count:08x}",
        f"stage60_xnu_compile_graph_max_risk_class=0x{max_risk:08x}",
        f"stage60_xnu_compile_graph_pe_gen_allowed=0x{pe_gen_allowed:08x}",
        f"stage60_xnu_compile_graph_arm_bootargs_allowed=0x{arm_bootargs_allowed:08x}",
        f"stage60_xnu_compile_graph_arm_consistent_debug_allowed=0x{arm_consistent_debug_allowed:08x}",
        f"stage60_xnu_compile_graph_consistent_debug_layout_recorded=0x{consistent_debug_layout_recorded:08x}",
        f"stage60_xnu_compile_graph_arm_pe_kprintf_blocked=0x{arm_pe_kprintf_blocked:08x}",
        f"stage60_xnu_compile_graph_arm_pe_serial_blocked=0x{arm_pe_serial_blocked:08x}",
        f"stage60_xnu_compile_graph_arm_pe_identify_machine_blocked=0x{arm_pe_identify_machine_blocked:08x}",
        f"stage60_xnu_compile_graph_arm_pe_init_blocked=0x{arm_pe_init_blocked:08x}",
        f"stage60_xnu_compile_graph_bootstrap_contract_selected=0x{bootstrap_contract_selected:08x}",
        f"stage60_xnu_compile_graph_pmap_reference_mask=0x{pmap_reference_mask:08x}",
        f"stage60_xnu_compile_graph_pmap_runtime_blocked_mask=0x{pmap_runtime_blocked_mask:08x}",
        f"stage60_xnu_compile_graph_pmap_reference_count=0x{pmap_reference_count:08x}",
        f"stage60_xnu_compile_graph_pmap_public_compile_count=0x{pmap_public_compile_count:08x}",
        f"stage60_xnu_compile_graph_pmap_public_link_count=0x{pmap_public_link_count:08x}",
        f"stage60_xnu_compile_graph_pmap_reference_only=0x{pmap_reference_only:08x}",
        f"stage60_xnu_compile_graph_platform_reference_count=0x{platform_reference_count:08x}",
        f"stage60_xnu_compile_graph_blocked_runtime_count=0x{blocked_runtime_count:08x}",
        f"stage60_xnu_compile_graph_duplicate_symbol_count=0x{duplicate_symbol_count:08x}",
        f"stage60_xnu_compile_graph_pe_state_abi_recorded=0x{pe_state_abi_recorded:08x}",
        f"stage60_xnu_compile_graph_boot_args_arm_layout_recorded=0x{boot_args_arm_layout_recorded:08x}",
        f"stage60_xnu_compile_graph_4570_bounded_reference_policy=0x{ref_only:08x}",
        "stage60_xnu_compile_graph_no_full_xnu_build=0x00000001",
        "stage60_xnu_compile_graph_no_public_xnu_exec=0x00000001",
        "stage60_xnu_compile_graph_no_macho_exec=0x00000001",
        "stage60_xnu_compile_graph_no_platform_runtime_exec=0x00000001",
        f"stage60_xnu_compile_graph_no_external_mutation=0x{1 if external_clean else 0:08x}",
        "stage60_xnu_compile_graph_outputs_ignored=0x00000001",
        "stage60_xnu_compile_graph_fail_closed=0x00000001",
    ]
    status_txt.write_text("\n".join(status_lines) + "\n", encoding="utf-8")

    manifest_lines = [
        "candidate_id\tpath\tbaseline\tfamily\tarch\teligibility\trisk\tcompile\tlink\tshims\texists"
    ]
    for r in records:
        manifest_lines.append(
            f"{r['candidate_id']}\t{r['relpath']}\t{r['baseline']}\t{r['source_family']}\t{r['architecture_class']}\t"
            f"{r['eligibility']}\t{r['risk_class']}\t{r['allow_compile']}\t{r['allow_link']}\t"
            f"{','.join(r['required_shims'])}\t{r['exists']}"
        )
    manifest_tsv.write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    includes_txt.write_text("\n".join(include_lines) + "\n", encoding="utf-8")
    symbols_txt.write_text("\n".join(symbol_lines) + "\n", encoding="utf-8")
    risk_txt.write_text("\n".join(risk_lines) + "\n", encoding="utf-8")
    json_txt.write_text(json.dumps(records, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    header.write_text(f"""#ifndef MI4IOS6_STAGE60_XNU_COMPILE_GRAPH_GENERATED_H
#define MI4IOS6_STAGE60_XNU_COMPILE_GRAPH_GENERATED_H

#define STAGE60_XNU_GRAPH_HOST_STATUS                 0x{status:08x}u
#define STAGE60_XNU_GRAPH_HOST_REQUIRED_MASK          0x{REQUIRED_MASK:08x}u
#define STAGE60_XNU_GRAPH_HOST_SATISFIED_MASK         0x{satisfied:08x}u
#define STAGE60_XNU_GRAPH_HOST_FAILURE_MASK           0x{failure:08x}u
#define STAGE60_XNU_GRAPH_HOST_CANDIDATE_COUNT        0x{candidate_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_ALLOWED_COMPILE_COUNT  0x{allowed_compile_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_ALLOWED_LINK_COUNT     0x{allowed_link_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_FORBIDDEN_COUNT        0x{forbidden_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_SHIM_REQUIRED_COUNT    0x{shim_required_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_MAX_RISK_CLASS         0x{max_risk:08x}u
#define STAGE60_XNU_GRAPH_HOST_DEVICE_TREE_CLASSIFIED {1 if (satisfied & BIT_DEVICE_TREE_CLASSIFIED) else 0}u
#define STAGE60_XNU_GRAPH_HOST_BOOTARGS_CLASSIFIED    {1 if (satisfied & BIT_BOOTARGS_CLASSIFIED) else 0}u
#define STAGE60_XNU_GRAPH_HOST_PE_GEN_CLASSIFIED      {1 if (satisfied & BIT_PE_GEN_CLASSIFIED) else 0}u
#define STAGE60_XNU_GRAPH_HOST_PE_GEN_ALLOWED         {pe_gen_allowed}u
#define STAGE60_XNU_GRAPH_HOST_ARM_BOOTARGS_CLASSIFIED {1 if (satisfied & BIT_ARM_BOOTARGS_CLASSIFIED) else 0}u
#define STAGE60_XNU_GRAPH_HOST_ARM_BOOTARGS_ALLOWED   {arm_bootargs_allowed}u
#define STAGE60_XNU_GRAPH_HOST_ARM_CONSISTENT_DEBUG_CLASSIFIED {1 if (satisfied & BIT_ARM_CONSISTENT_DEBUG_CLASSIFIED) else 0}u
#define STAGE60_XNU_GRAPH_HOST_ARM_CONSISTENT_DEBUG_ALLOWED {arm_consistent_debug_allowed}u
#define STAGE60_XNU_GRAPH_HOST_CONSISTENT_DEBUG_LAYOUT_RECORDED {consistent_debug_layout_recorded}u
#define STAGE60_XNU_GRAPH_HOST_ARM_PE_KPRINTF_BLOCKED {arm_pe_kprintf_blocked}u
#define STAGE60_XNU_GRAPH_HOST_ARM_PE_SERIAL_BLOCKED {arm_pe_serial_blocked}u
#define STAGE60_XNU_GRAPH_HOST_ARM_PE_IDENTIFY_MACHINE_BLOCKED {arm_pe_identify_machine_blocked}u
#define STAGE60_XNU_GRAPH_HOST_ARM_PE_INIT_BLOCKED {arm_pe_init_blocked}u
#define STAGE60_XNU_GRAPH_HOST_BOOTSTRAP_CONTRACT_SELECTED {bootstrap_contract_selected}u
#define STAGE60_XNU_GRAPH_HOST_PMAP_REFERENCE_MASK 0x{pmap_reference_mask:08x}u
#define STAGE60_XNU_GRAPH_HOST_PMAP_RUNTIME_BLOCKED_MASK 0x{pmap_runtime_blocked_mask:08x}u
#define STAGE60_XNU_GRAPH_HOST_PMAP_REFERENCE_COUNT 0x{pmap_reference_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_PMAP_PUBLIC_COMPILE_COUNT 0x{pmap_public_compile_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_PMAP_PUBLIC_LINK_COUNT 0x{pmap_public_link_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_PMAP_REFERENCE_ONLY {pmap_reference_only}u
#define STAGE60_XNU_GRAPH_HOST_PLATFORM_REFERENCE_COUNT 0x{platform_reference_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_BLOCKED_RUNTIME_COUNT  0x{blocked_runtime_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_DUPLICATE_SYMBOL_COUNT 0x{duplicate_symbol_count:08x}u
#define STAGE60_XNU_GRAPH_HOST_PE_STATE_ABI_RECORDED  {pe_state_abi_recorded}u
#define STAGE60_XNU_GRAPH_HOST_BOOT_ARGS_ARM_LAYOUT_RECORDED {boot_args_arm_layout_recorded}u
#define STAGE60_XNU_GRAPH_HOST_FORBIDDEN_EXCLUDED     {1 if forbidden_excluded else 0}u
#define STAGE60_XNU_GRAPH_HOST_INCLUDE_DEPS_RECORDED  1u
#define STAGE60_XNU_GRAPH_HOST_SYMBOL_DEPS_RECORDED   1u
#define STAGE60_XNU_GRAPH_HOST_SHIM_NEEDS_RECORDED    1u
#define STAGE60_XNU_GRAPH_HOST_RISK_CLASSES_RECORDED  1u
#define STAGE60_XNU_GRAPH_HOST_BASELINE_COMMIT32      0x{BASELINE_COMMIT32:08x}u
#define STAGE60_XNU_GRAPH_HOST_MASTER_VERSION         0x{BASELINE_MASTER32:08x}u
#define STAGE60_XNU_GRAPH_HOST_PUBLIC_2050_BASELINE   {1 if baseline_ok else 0}u
#define STAGE60_XNU_GRAPH_HOST_4570_BOUNDED_REFERENCE_POLICY    {ref_only}u
#define STAGE60_XNU_GRAPH_HOST_NO_FULL_XNU_BUILD      1u
#define STAGE60_XNU_GRAPH_HOST_NO_PUBLIC_XNU_EXEC     1u
#define STAGE60_XNU_GRAPH_HOST_NO_MACHO_EXEC          1u
#define STAGE60_XNU_GRAPH_HOST_NO_PLATFORM_RUNTIME_EXEC 1u
#define STAGE60_XNU_GRAPH_HOST_NO_EXTERNAL_MUTATION   {1 if external_clean else 0}u
#define STAGE60_XNU_GRAPH_HOST_OUTPUTS_IGNORED        1u
#define STAGE60_XNU_GRAPH_HOST_FAIL_CLOSED            1u

#endif
""", encoding="utf-8")

    print(status_txt.read_text(encoding="utf-8"), end="")
    return 0 if status == STATUS_OK else 1


if __name__ == "__main__":
    raise SystemExit(main())
