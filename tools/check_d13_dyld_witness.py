#!/usr/bin/env python3
"""Assert BY VALUE that the linked image does (ON) or does not (OFF) carry the dyld halt-message
witness staged by `tools/patch_d13_dyld_witness.py`.

Reads the disassembly (`arm-none-eabi-objdump -d`), the symbol table (`arm-none-eabi-nm`) and the
image's own bytes, and DECODES THE ARM ENCODINGS itself - it never trusts a source comment or an
objdump mnemonic (`mi4-a-claim-in-a-comment-is-not-a-check`).  The kernel pool is built with
`clang -marm` (`tools/build_xnu_arm_kernel.sh:1119`), so these are ARM, not Thumb, encodings.

WHAT THE WITNESS IS.  994's press halted inside dyld's own fatal `udf` with a message nothing could
print (evidence `out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt`).  The patch reads
it out of the halting task from BOTH pointers that carry it - `task->all_image_info_addr` A is dyld's
`dyld_all_image_infos` (mach_loader's `note_all_image_info_section`, `bsd/kern/mach_loader.c:249-269`),
whose word at +0x20 is the message pointer (`setAlImageInfosHalt` @ dyld 0x2be08d04 is
`str r0, [r3, #32]`), and `state->r[5]` is `dyld::halt`'s argument, still live at the `udf`.  Both are
`copyinstr`'d and compared; and `state->r[8]` is classified by the exception block's signature pair
0x432b2b00/0x434c4e47 (doc `docs/reference/d13-dyld-halt-message-recovery.md` 9.3).  Rev 3 (996)
also reads, in the same trap, the rest of the version-0x0e struct: the shared-cache words and dyld's
own referrer/target/symbol strings, each fail-soft.  The call sits
immediately after the `dyld_all_image_info_addr` printf in `sleh_undef`, i.e. on the same path that
prints the very A the witness dereferences.

CHECKS, ON (`--expect on`):
  (a) a text symbol `ios7wit_dyld_message` exists              - the function was compiled in;
  (b) `sleh_undef`'s region contains a `bl`/`blx` to it        - the call site is wired;
  (c) that call follows a `bl printf` in the same region       - it is on the dump path, not a stray;
  (d) the witness's own region calls `copyin` at least 9 times and `copyinstr` at least 6 times -
      the calls are the prescribed reads: the A+0x20 field, the two 16-byte r8 signature windows,
      the six rev-3 version-0x0e field reads (errorKind/slide/cacheBase and the three char*
      pointers), the A text, the r5 text, the r8 text and the three rev-3 strings; every user read is
      a copyin, so a build that dropped one of the reads FAILs here rather than silently probing
      less;
  (e) the witness's region names the immediates `#32`, `#48`, `#100` and `#80` - the +0x20 field
      offset / r8 back-offset, the 0x30 base-forward offset / 48-byte line stride, and the rev-3
      sharedCacheBaseAddress (+0x64) / sharedCacheSlide (+0x50) offsets are in the code.
      NECESSARY, NOT SUFFICIENT: they prove the constants are there, not that they are added to the
      right addresses; the call-site and string checks are what pin the read;
  (f) the image bytes carry the witness's whole vocabulary - `IOS7WITNESS dyld msg=`,
      `IOS7WITNESS r5 msg=`, the three verdicts (`matches errorMessage` / `DIFFERS from errorMessage`
      / `unreadable, no comparison`), the `dyld> `/`dyld5> `/`dyld8> ` line tags, the three r8
      classification lines, the `stale probe=` fallback, and (rev 3) the `IOS7WITNESS info ...` set
      (the cacheBase/slide/errKind line and its copyin fallback, `client=`/`target=`/`symbol=`,
      `<null>`) - so every branch exists in the image, not just the happy one
      (`mi4-silence-is-a-reading-only-if-success-is-silent`).

CHECKS, OFF (`--expect off`): the mirror image, asserted positively so an empty or truncated file
cannot pass it - `sleh_undef` must still exist and still call `printf` (the register dump survives),
while no `ios7wit_dyld_message`, no `IOS7WITNESS`, no `dyld> `/`dyld5> `/`dyld8> ` is anywhere in the
image.

NEGATIVE CONTROLS.  `--control ELF` (repeatable) runs BOTH modes against an image that predates the
switch: OFF must PASS and ON must FAIL on each, i.e. the checker tells the two shapes apart instead of
passing on everything.  The 994 image (`out/stage90/xnu_arm_entry.elf`, sha `8b4c9a3e...`) is such a
control, and the 995 rung should freeze its own ON image the same way before the next rebuild.

KNOWN LOUD FAILURE MODE.  The calls resolve through `bl` immediates.  If a link ever puts a callee
more than ±32 MiB from its caller, the linker inserts a veneer, the decoded `bl` target is the veneer
rather than the function, and the affected check FAILs on a call that is really there - it reports
the call rather than assuming it.  The failing message lists the region's calls to unnamed targets so
a veneer is visible rather than mysterious (`mi4-silence-is-a-reading-only-if-success-is-silent`).

Exit 0 iff every check of the asserted mode passes (and, with controls, each control discriminates);
1 if any check fails; 2 for a usage or tool error.
"""
import re
import subprocess
import sys

OBJDUMP = "arm-none-eabi-objdump"
NM = "arm-none-eabi-nm"

SLEH = "sleh_undef"
WITNESS = "ios7wit_dyld_message"
# Minimum call counts in the witness's region: copyin = the A+0x20 field read + the two r8 signature
# windows + the six rev-3 field reads (three u32 words, three char* pointers); copyinstr = the A text
# + the r5 text + the r8 text + the three rev-3 strings.
CALLEES = (("copyin", 9), ("copyinstr", 6), ("printf", 1))
# The field offsets the witness must materialise.  Matched as EXACT immediates: `#32` as a plain
# substring also matches `#32770` (measured on this image: `movt r2, #32770` at 0x8002c6f8), which
# would make the check pass on a coincidence.
FIELD_OFF_RE = re.compile(r"#32\b")
BASE_FWD_RE = re.compile(r"#48\b")
CACHE_BASE_OFF_RE = re.compile(r"#100\b")
CACHE_SLIDE_OFF_RE = re.compile(r"#80\b")
# Every string literal the ON image must carry.
TOKENS_ON = (b"IOS7WITNESS dyld msg=", b"IOS7WITNESS r5 msg=", b"matches errorMessage",
             b"DIFFERS from errorMessage", b"unreadable, no comparison",
             b"dyld> ", b"dyld5> ", b"dyld8> ",
             b"exception object (signature at r8-0x20)",
             b"exception block base (signature at r8+0x30)",
             b"message interior len=", b"stale probe=0x%08x",
             b"dyld> <copyinstr failed>",
             b"IOS7WITNESS info cacheBase=0x%08x slide=0x%08x errKind=0x%08x",
             b"IOS7WITNESS info cacheBase/slide/errKind copyin=",
             b"IOS7WITNESS info client=", b"IOS7WITNESS info target=",
             b"IOS7WITNESS info symbol=", b"<null>")
# Every byte string the OFF image must NOT carry.
TOKENS_OFF_ABSENT = (b"IOS7WITNESS", b"dyld> ", b"dyld5> ", b"dyld8> ")
AII_FMT = b"dyld_all_image_info_addr: 0x%08x"

# objdump left-pads the address column to the width of the section's addresses (`   4:` in a
# low-address object, `8046b72c:` in the linked image), so both patterns must allow leading blanks -
# without it a small object parses to zero instructions and every region check reports "no
# disassembly", which reads like a missing function rather than a parsing bug.
_LABEL = re.compile(r"^\s*([0-9a-f]+) <([^>]+)>:$")
_INSN = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)$")


def objdump_disasm(path):
    return subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True,
                          check=True).stdout


def symbols(path):
    """({name: va}, [va of every GLOBAL text symbol]) for the image."""
    out, globals_ = {}, []
    for line in subprocess.run([NM, path], capture_output=True, text=True,
                               check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("T", "t", "D", "d", "B", "b", "R", "r", "W", "w"):
            out[parts[2]] = int(parts[0], 16)
            if parts[1] == "T":
                globals_.append(int(parts[0], 16))
    return out, sorted(globals_)


def bodies(path):
    """{symbol: [(va, word, mnemonic, operands)]} in link order, from the disassembly."""
    funcs, cur = {}, None
    for line in objdump_disasm(path).splitlines():
        m = _LABEL.match(line)
        if m:
            cur = m.group(2)
            funcs.setdefault(cur, [])
            continue
        m = _INSN.match(line)
        if m and cur is not None:
            funcs[cur].append((int(m.group(1), 16), int(m.group(2), 16), m.group(3),
                               m.group(4).split(";")[0].strip()))
    return funcs


def _sign_extend24(imm24):
    return imm24 - (1 << 24) if imm24 & (1 << 23) else imm24


def branch_target(word, va):
    """Absolute target of a B / BL / BLX-immediate, or (None, None) if the word is not one."""
    cond = (word >> 28) & 0xF
    top = (word >> 24) & 0xF
    if top == 0xA:                                   # B
        return va + 8 + (_sign_extend24(word & 0xFFFFFF) << 2), "b"
    if top == 0xB and cond != 0xF:                   # BL
        return va + 8 + (_sign_extend24(word & 0xFFFFFF) << 2), "bl"
    if (word >> 25) & 0x7F == 0b1111101:             # BLX immediate (1111 101H imm24)
        h = (word >> 24) & 1
        return va + 8 + ((_sign_extend24(word & 0xFFFFFF) << 2) | (h << 1)), "blx"
    return None, None


def region_body(funcs, start_va, globals_):
    """Every instruction of the region beginning at `start_va`, up to the next GLOBAL text symbol.

    objdump emits a label for every local symbol, so a function's own sub-labels are separate
    `funcs` entries; ending the region at the next such label would cut it off mid-body
    (`mi4-linked-code-order-is-not-source-order`).
    """
    nxt = next((v for v in globals_ if v > start_va), None)
    out = []
    for b in funcs.values():
        if b and b[0][0] >= start_va and (nxt is None or b[0][0] < nxt):
            out.extend(b)
    out.sort()
    return out


def calls_of(body, vas):
    """[(VA, target_VA)] of every bl/blx in `body` whose decoded target is in `vas`."""
    return [(va, tgt) for va, word, _mn, _ops in body
            for tgt, kind in [branch_target(word, va)] if kind in ("bl", "blx") and tgt in vas]


def unnamed_calls(body, syms):
    return sorted({tgt for va, word, _mn, _ops in body
                   for tgt, kind in [branch_target(word, va)]
                   if kind in ("bl", "blx") and tgt is not None and tgt not in syms.values()})


def load_env(path):
    syms, globals_ = symbols(path)
    return {"path": path, "syms": syms, "globals": globals_, "funcs": bodies(path),
            "data": open(path, "rb").read()}


def check_on(env):
    res = []
    syms, globals_, funcs, data = env["syms"], env["globals"], env["funcs"], env["data"]
    if WITNESS not in syms:
        return [(False, f"no `{WITNESS}` symbol - the switch did not reach the pool, or the pool "
                        f"object was not rebuilt (`build_entry.sh` only LINKS "
                        f"out/xnu_kernel_obj_d13/osfmk_arm_trap.o)")]
    if SLEH not in syms:
        return [(False, f"no `{SLEH}` symbol - wrong image")]
    res.append((True, f"`{WITNESS}` is at {syms[WITNESS]:#010x}"))
    body = region_body(funcs, syms[SLEH], globals_)
    wit_va = syms[WITNESS]
    calls = calls_of(body, {wit_va})
    if not calls:
        unnamed = unnamed_calls(body, syms)
        res.append((False, f"{SLEH} ({syms[SLEH]:#010x}) does NOT call {WITNESS} ({wit_va:#010x}) - "
                           f"the call site is not on the dump path"
                           + (f"; its calls to UNNAMED targets (a veneer would show here): "
                              f"{', '.join(hex(t) for t in unnamed)}" if unnamed else "")))
    else:
        call_va = calls[0][0]
        res.append((True, f"{SLEH} ({syms[SLEH]:#010x}) calls {WITNESS} at {call_va:#010x}"))
        dump = calls_of(body, {syms["printf"]}) if "printf" in syms else []
        before = [va for va, _t in dump if va < call_va]
        res.append((bool(before), f"the witness call follows a `bl printf` ({max(before):#010x}) in "
                                  f"{SLEH} - it is on the dyld_all_image_info_addr dump path"
                    if before else f"no `bl printf` before the witness call in {SLEH} - it is not on "
                                   f"the dyld_all_image_info_addr dump path"))
    wbody = region_body(funcs, wit_va, globals_)
    if not wbody:
        res.append((False, f"no disassembly for the witness region at {wit_va:#010x}"))
    else:
        for callee, least in CALLEES:
            if callee not in syms:
                res.append((False, f"`{callee}` is not in the image - cannot assert the witness "
                                   f"calls it"))
                continue
            got = calls_of(wbody, {syms[callee]})
            res.append((len(got) >= least,
                        f"the witness ({wit_va:#010x}) calls {callee} {len(got)} time(s), "
                        f"{least} needed (at {', '.join(hex(v) for v, _t in got)})"
                        if got else
                        f"the witness region never calls {callee} - NOT found"))
        for rx, what in ((FIELD_OFF_RE, "+0x20 (the A field offset / the r8 back-offset)"),
                         (BASE_FWD_RE, "+0x30 (the r8 base-forward offset) / the 48-byte line"),
                         (CACHE_BASE_OFF_RE, "+0x64 (the sharedCacheBaseAddress field)"),
                         (CACHE_SLIDE_OFF_RE, "+0x50 (the sharedCacheSlide field)")):
            hits = [f"{va:#010x} {mn} {ops}" for va, _w, mn, ops in wbody if rx.search(ops)]
            res.append((bool(hits), f"the witness names the field offset {what}: {hits[0]}" if hits
                        else f"the witness region never names the field offset {what} - the "
                             f"corresponding constant is not in the code"))
    for tok in TOKENS_ON:
        res.append((tok in data, f"the image carries `{tok.decode()}`"))
    return res


def check_off(env):
    res = []
    syms, globals_, funcs, data = env["syms"], env["globals"], env["funcs"], env["data"]
    res.append((WITNESS not in syms, f"no `{WITNESS}` symbol in the image"
                if WITNESS not in syms else
                f"`{WITNESS}` IS a symbol in this image ({syms[WITNESS]:#010x}) while the switch is "
                f"supposed to be OFF"))
    for tok in TOKENS_OFF_ABSENT:
        res.append((tok not in data, f"the image carries no `{tok.decode()}` bytes"
                    if tok not in data else
                    f"the image carries `{tok.decode()}` while the switch is supposed to be OFF"))
    if SLEH not in syms:
        return res + [(False, f"`{SLEH}` is not in the image - wrong image, or the register dump the "
                              f"witness hangs off is gone")]
    body = region_body(funcs, syms[SLEH], globals_)
    dump = calls_of(body, {syms["printf"]}) if "printf" in syms else []
    res.append((len(dump) >= 3, f"{SLEH} ({syms[SLEH]:#010x}) still calls printf {len(dump)} time(s) "
                                f"- the register dump group survives the switch being OFF"
                if len(dump) >= 3 else
                f"{SLEH} calls printf {len(dump)} time(s), expected the three-dump group (the fault "
                f"header, the register block and dyld_all_image_info_addr)"))
    res.append((AII_FMT in data, "the image still carries the `dyld_all_image_info_addr` format "
                                 "string" if AII_FMT in data else
                "the image does NOT carry the `dyld_all_image_info_addr` format string"))
    return res


def run(path, expect):
    try:
        env = load_env(path)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"  FAIL {OBJDUMP}/{NM} could not read it: {exc}")
        return False
    res = (check_on if expect == "on" else check_off)(env)
    for good, msg in res:
        print(f"  {'ok  ' if good else 'FAIL'} {msg}")
    return all(good for good, _m in res)


def main() -> int:
    argv = sys.argv[1:]
    expect, args, controls, i = "on", [], [], 0
    while i < len(argv):
        a = argv[i]
        if a == "--expect" and i + 1 < len(argv):
            expect, i = argv[i + 1], i + 1
        elif a.startswith("--expect="):
            expect = a.split("=", 1)[1]
        elif a == "--control" and i + 1 < len(argv):
            controls.append(argv[i + 1])
            i += 1
        elif a == "--off":
            expect = "off"
        elif a == "--on":
            expect = "on"
        elif not a.startswith("--"):
            args.append(a)
        else:
            print(f"check_d13_dyld_witness: unknown option {a}", file=sys.stderr)
            return 2
        i += 1
    if expect not in ("on", "off"):
        print(f"check_d13_dyld_witness: --expect must be on or off, not [{expect}]", file=sys.stderr)
        return 2
    path = args[0] if args else "out/stage90/xnu_arm_entry.elf"

    print(f"check_d13_dyld_witness: {path} (expect switch {expect.upper()})")
    ok = run(path, expect)
    print(f"check_d13_dyld_witness: {'PASS' if ok else 'FAIL'} ({expect})")

    for ctl in controls:
        print(f"check_d13_dyld_witness: control {ctl} (must FAIL on and PASS off)")
        on = run(ctl, "on")
        off = run(ctl, "off")
        if on or not off:
            print(f"  FAIL the control does not discriminate: on={on} off={off} - it is not an image "
                  f"this checker can tell apart")
            ok = False
        else:
            print("  ok   control discriminates (on FAILs, off PASSes)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())