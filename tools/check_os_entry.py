#!/usr/bin/env python3
"""The boot reached the OS, and the readings that say so cannot mean anything else.

    python3 tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf
    python3 tools/check_os_entry.py --image ... --verbose
    python3 tools/check_os_entry.py --image ... --selftest

Why this exists, and it is experiment 489's finding.

487 published a *cause* that its own log contradicts. It read the OS console's last line -
`load_init_program: attempting to load /sbin/launchd`, with no `failed loading` after it and no
`panic("Process 1 exec of %s failed, errno %d")` - as "`load_init_program_at_path` never returned, so
`execve` is still running and process 1 is still executing the RAM-disk Mach-O". That inference reads
a **success** path's silence as if it were a **failure** path's signature, and Apple's loop has no
message on the success arm at all (`kern_exec.c:5163-5173`: the failure arm prints, the success arm is
a bare `return`). So the same console is the reading for "the exec succeeded and the boot went on".

And the boot did go on, in the same runs 487 was reading, on four separate readings:

  - `load_machfile` returned **0** (`xnu_live_lmf_ret`), and no `os_reason_create` was ever made
    (`xnu_live_osr_*` is absent from the log), so the exec did not fail at the first place it can;
  - all five of `kernel_bootstrap_thread`'s tail calls ran in Apple's order
    (`xnu_live_tail_seq = 5`, `xnu_live_tail_idx = 4` = `vm_pageout` = `0x80071ef4`), and the two
    registry censuses live inside `__wrap_vm_pageout` - so the image *cannot* have read the registry
    before `bsd_init()` returned;
  - process 1's first two syscalls answered: `xnu_live_getpid_value = 1` with `error = 0`, and
    `xnu_live_mmap_value = 0x00102000` - and the fixture's `svc #0x80` is only executable *after*
    `execve` returned and the thread was returned to user mode;
  - the fixture then took the two faults it is written to take, at `pc = entry + 0x38` (a **load**,
    `fsr = 0x007` = page translation fault, read) and `pc = entry + 0x44` (a **store**, `fsr = 0x80f`
    = page permission fault, write) on `far = 0x00102000`, the page `mmap` had just returned - and the
    getpid loop *ran on* afterwards to `0x00800000` calls, which is a fault that was serviced.

The claims below are what makes that a structural reading rather than a re-reading of one log. They
are about the **image and Apple's sources**, and five of them are load-bearing:

1. **The chain that execs process 1 is `bsd_ast` -> `bsdinit_task` -> `load_init_program` ->
   `execve`, and `/sbin/launchd` is one of `init_programs[]`.** Read out of `kern_sig.c:3445`,
   `bsd_init.c` and `kern_exec.c`; the call is guarded by a once-only `bsd_init_done`, so the fixture
   runs on the thread that called `bsd_init`.
2. **`load_init_program` prints on every failure and on nothing else.** Exactly two
   `attempting to load` printfs, exactly two `failed loading %s: errno %d`, no other `printf`, one
   `panic(` after the loop, and no `if (!error)` arm that prints. That is the rule that turns the
   console's silence into a reading - and if the rule ever stops holding, this claim is what fails
   instead of a conclusion.
3. **`bsd_init()` precedes the five tail calls** (`startup.c:628` against `:633-646`), the image
   `--wrap`s exactly those five, and the censuses are called from exactly one place in
   `entry_trace.c` - inside `__wrap_vm_pageout`, before `__real_vm_pageout()`.
4. **The two PCs the fixture is written to fault on are a load and a store**, computed from the RAM
   disk's own bytes as they are linked into this image: `entry = __TEXT.vmaddr + 28 + sizeofcmds`,
   the word at `+0x38` a load through `r0` into `r3`, the word at `+0x44` a store of `r0` through
   `r0`. So the run's read-then-write pair is *predicted by the bytes*, not matched to them after the
   fact.
5. **The readings this rests on are published by the image**: the `load_machfile` wrapper publishes
   the real call's return, the `getpid` wrapper publishes the first answer with its error and its
   caller, `__wrap_vm_pageout` passes index 4, and `--wrap=load_machfile` is in this build's wrap
   list.

**What this check deliberately does not own.** The *shape* of the fixture's twenty-seven
instructions - the syscall numbers, the munged argument registers, the entry's `LC_UNIXTHREAD` - is
`tools/host_ramdisk_macho_check.py`'s, and is not restated here. What is claimed here is only the two
words the boot's own fault records name, and their position, which nothing else reads.

**And what it cannot claim.** That `vm_pageout` being reached *implies* `load_init_program` returned
rests on `bsd_ast`'s once-only guard and on the tail call order, both of which are checked - but the
reading that needs no such argument is the fixture's own `getpid`, because a user-mode `svc` cannot
run before the exec that entered the image has returned. The two are kept as separate claims for
exactly that reason.
"""

import argparse
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.path.join(REPO_ROOT, "external", "xnu-4570.1.46")
ORDER_H = os.path.join(XNU, "osfmk", "mach", "arm", "_structs.h")
BOOT_DIR = os.path.join(REPO_ROOT, "stages", "stage90", "xnu_arm_boot")
ENTRY_TRACE_C = os.path.join(BOOT_DIR, "entry_trace.c")
ENTRY_STUBS_C = os.path.join(BOOT_DIR, "entry_stubs.c")
BUILD_ENTRY_SH = os.path.join(BOOT_DIR, "build_entry.sh")
STARTUP_C = os.path.join(XNU, "osfmk", "kern", "startup.c")
BSD_INIT_C = os.path.join(XNU, "bsd", "kern", "bsd_init.c")
KERN_EXEC_C = os.path.join(XNU, "bsd", "kern", "kern_exec.c")
KERN_SIG_C = os.path.join(XNU, "bsd", "kern", "kern_sig.c")

NM = os.environ.get("NM", "arm-none-eabi-nm")
READELF = os.environ.get("READELF", "arm-none-eabi-readelf")
OBJDUMP = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")

# `kernel_bootstrap_thread`'s tail, in Apple's order (`startup.c:628` and `:633-646`), as
# `(index, name)`. The index is the one `entry_note_boot_tail` is called with, and the fifth is the
# wrapper the censuses run inside.
BOOT_TAIL = (
    (0, "OSKextRemoveKextBootstrap"),
    (1, "kdebug_free_early_buf"),
    (2, "serial_keyboard_init"),
    (3, "vm_page_init_local_q"),
    (4, "vm_pageout"),
)
TAIL_CALLS = len(BOOT_TAIL)

# The two censuses, and the wrapper whose body must be their only caller.
CENSUSES = ("entry_probe_dt_children", "entry_probe_service_plane")
CENSUS_WRAPPER = "__wrap_vm_pageout"

# The fixture's two fault sites, as byte offsets from `entry_code`. Both are *measured* against the
# bytes below; the names are what the fault records are matched to.
FAULT_SITES = ((0x38, "ldr", "the read of the page mmap just returned"),
               (0x44, "str", "the write that follows it"))

# `g_stage90_ramdisk` is the whole of `/sbin/launchd` (`entry_ramdisk.s`), and these are the header
# fields this check reads before it trusts any offset. `LC_SEGMENT`, `LC_UNIXTHREAD` and the two
# sizes are from `mach-o/loader.h`.
MH_MAGIC = 0xFEEDFACE
LC_SEGMENT = 0x1
LC_UNIXTHREAD = 0x5
SEGMENT_CMD_SIZE = 56
THREAD_CMD_SIZE = 84
RAMDISK_BYTES = 0x2000
# `struct arm_thread_state` inside `LC_UNIXTHREAD`: r[13], sp, lr, pc, cpsr.
THREAD_PC_OFFSET = 76


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def run(command):
    return subprocess.run(command, capture_output=True, text=True).stdout


def strip_comments(text):
    """Remove C comments, and C++ `//` comments outside of strings."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\":
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append("\n")
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _matching(text, start, opener, closer):
    depth = 0
    i = start
    while i < len(text):
        if text[i] == opener:
            depth += 1
        elif text[i] == closer:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def function_body(text, name, parameters=None):
    """The body of a *definition* of `name` - the same reader 487's check uses, and for the same
    reason: a declaration or a call is skipped because what follows the closing parenthesis is not a
    `{`, and `parameters` disambiguates the two `IORegistryEntry::init`-shaped families."""
    for match in re.finditer(r"(?<![\w])%s\s*\(" % re.escape(name), text):
        open_paren = text.index("(", match.start())
        close_paren = _matching(text, open_paren, "(", ")")
        if close_paren < 0:
            continue
        if parameters is not None and parameters not in text[open_paren + 1:close_paren]:
            continue
        after = text[close_paren + 1:]
        qualifiers = re.match(r"[A-Za-z_0-9\s*]*\{", after)
        if qualifiers is None:
            continue
        brace = close_paren + 1 + qualifiers.end() - 1
        end = _matching(text, brace, "{", "}")
        if end < 0:
            continue
        return text[brace:end + 1]
    return None


def nm_addresses(path):
    """`name -> address`. A different question from `nm`'s kind, and 486's check records why."""
    addresses = {}
    for line in run([NM, path]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                addresses[parts[2]] = int(parts[0], 16)
            except ValueError:
                continue
    return addresses


def elf_bytes(path, addr, size):
    """`size` bytes at virtual address `addr`, out of the ELF's own section table.

    The blob's address and the file offset are two different numbers (the `.data` section is at
    0x80504000 in the image and 0x514000 in the file), so the section that contains the address is
    what supplies the difference - and a `.bss`-resident blob has no bytes at all, which is why this
    returns None rather than zeros.
    """
    for line in run([READELF, "-S", "--wide", path]).splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
                         line)
        if not match:
            continue
        _name, kind, section_addr, section_off, section_size = match.groups()
        if kind != "PROGBITS":
            continue
        section_addr, section_off, section_size = (int(section_addr, 16), int(section_off, 16),
                                                   int(section_size, 16))
        if section_addr <= addr and addr + size <= section_addr + section_size:
            with open(path, "rb") as handle:
                handle.seek(section_off + (addr - section_addr))
                return handle.read(size)
    return None


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def arm_transfer(word):
    """An ARM single data transfer, decoded into `(is_load, rd, rn, immediate)` - or None.

    Bits 27-26 are `01` for `LDR`/`STR` and bit 25 selects the register-offset form, which the
    fixture does not use; bit 20 is `L` (1 = load, 0 = store); bits 19-16 are `Rn` and 15-12 `Rd`.
    Decoding the encoding rather than reading the mnemonic is the point: the mnemonics are in the
    source's comments, and a claim that reads the comment tests the comment.
    """
    if (word >> 26) & 0x3 != 0x1:
        return None
    if (word >> 25) & 0x1 != 0x0:
        return None
    is_load = 1 if (word >> 20) & 0x1 else 0
    rn = (word >> 16) & 0xF
    rd = (word >> 12) & 0xF
    return (is_load, rd, rn, word & 0xFFF)


def parse_ramdisk(blob):
    """The header fields this check reads, and the two fault words - or None and a reason.

    Walks the load commands the way `parse_machfile` does, by `cmdsize`, so a command whose size is
    wrong is found here rather than becoming a wrong entry point.
    """
    out = {"blob": blob}
    if len(blob) != RAMDISK_BYTES:
        return None, "the RAM disk is %d bytes, not the %d the device's sector count makes it" % (
            len(blob), RAMDISK_BYTES)
    for field, want, where in (("magic", MH_MAGIC, "exec_mach_imgact claims a file by"),
                               ("ncmds", 3, "the three commands below"),
                               ("sizeofcmds", 0xC4, "the extent the kernel reads the commands with")):
        got = u32(blob, 0 if field == "magic" else (16 if field == "ncmds" else 20))
        if got != want:
            return None, "the RAM disk's %s is 0x%x, not 0x%x (%s)" % (field, got, want, where)
        out[field] = got
    if u32(blob, 24) != 0:
        return None, ("the RAM disk's `flags` is 0x%x: a non-zero one is `MH_DYLDLINK` (needs a "
                      "dylinker and a platform signature) or `MH_PIE` (a slide, so the vmaddrs stop "
                      "being the addresses the fault records name)" % u32(blob, 24))

    off = 28
    text_vmaddr = None
    thread_pc = None
    for _ in range(out["ncmds"]):
        cmd = u32(blob, off)
        cmdsize = u32(blob, off + 4)
        if cmd == LC_SEGMENT:
            if cmdsize != SEGMENT_CMD_SIZE:
                return None, "a LC_SEGMENT command is %d bytes, not %d" % (cmdsize, SEGMENT_CMD_SIZE)
            name = blob[off + 8:off + 24].split(b"\0")[0].decode("ascii", "replace")
            if name == "__TEXT":
                text_vmaddr = u32(blob, off + 24)
        elif cmd == LC_UNIXTHREAD:
            if cmdsize != THREAD_CMD_SIZE:
                return None, ("a LC_UNIXTHREAD command is %d bytes, not %d - so the pc is not at "
                              "offset %d within it" % (cmdsize, THREAD_CMD_SIZE, THREAD_PC_OFFSET))
            thread_pc = u32(blob, off + THREAD_PC_OFFSET)
        off += cmdsize
    if off != 28 + out["sizeofcmds"]:
        return None, ("the commands end at 0x%x and `sizeofcmds` says 0x%x: the entry point below is "
                      "`28 + sizeofcmds`, so the two must be the same number" % (off, 28 + out["sizeofcmds"]))
    if text_vmaddr is None:
        return None, "the RAM disk has no `__TEXT` segment, so there is no vmaddr to add the offset to"
    out["text_vmaddr"] = text_vmaddr
    out["entry_offset"] = off
    out["entry_pc"] = text_vmaddr + off

    if thread_pc != out["entry_pc"]:
        return None, ("the header's own `LC_UNIXTHREAD` pc is 0x%x and `__TEXT.vmaddr + 28 + "
                      "sizeofcmds` is 0x%x: the entry the kernel will use and the entry the file "
                      "offset is computed from disagree, so every `+0x...` below names the wrong "
                      "instruction" % (thread_pc, out["entry_pc"]))

    words = {}
    for delta, mnemonic, why in FAULT_SITES:
        word = u32(blob, out["entry_offset"] + delta)
        decoded = arm_transfer(word)
        if decoded is None:
            return None, ("the word at entry+0x%x (0x%08x) is not a single data transfer, so it is "
                          "not the `%s` of %s" % (delta, word, mnemonic, why))
        is_load, rd, rn, immediate = decoded
        words[delta] = {"word": word, "is_load": is_load, "rd": rd, "rn": rn,
                        "immediate": immediate, "mnemonic": mnemonic, "why": why}
    out["words"] = words
    return out, None


def gather(image):
    facts = {"image": image}
    for key, path in (("startup", STARTUP_C), ("bsd_init", BSD_INIT_C), ("kern_exec", KERN_EXEC_C),
                      ("kern_sig", KERN_SIG_C), ("trace", ENTRY_TRACE_C), ("stubs", ENTRY_STUBS_C),
                      ("build_entry", BUILD_ENTRY_SH)):
        facts[key] = strip_comments(read(path))
    facts["entries_source"] = read(BOOT_DIR + "/entry_ramdisk.s")
    facts["addresses"] = nm_addresses(image)
    facts["boot_tail_body"] = function_body(facts["trace"], CENSUS_WRAPPER)
    facts["load_init_program_body"] = function_body(facts["kern_exec"], "load_init_program")
    facts["execve_body"] = function_body(facts["kern_exec"], "load_init_program_at_path")
    blob = None
    addr = facts["addresses"].get("g_stage90_ramdisk")
    if addr is not None:
        blob = elf_bytes(image, addr, RAMDISK_BYTES)
    if blob is None:
        facts["ramdisk"], facts["ramdisk_error"] = None, (
            "no `g_stage90_ramdisk` in the image, or no bytes for it: the fixture is the only thing "
            "that says which two addresses the run's user faults are at")
    else:
        facts["ramdisk"], facts["ramdisk_error"] = parse_ramdisk(blob)
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_chain(facts, failures, notes):
    """1. `bsd_ast` -> `bsdinit_task` -> `load_init_program` -> `execve`, once, and `/sbin/launchd`."""
    sig = facts["kern_sig"]
    m = re.search(r"if\s*\(\s*!bsd_init_done\s*\)\s*\{([^}]*)\}", sig)
    if not m or "bsdinit_task()" not in m.group(1):
        failures.append("`kern_sig.c` no longer runs `bsdinit_task()` from `bsd_ast` under a "
                        "once-only `bsd_init_done`: the chain that execs process 1 is then not the "
                        "one this reading rests on, and `bsd_init` returning would stop saying "
                        "anything about the exec")
    else:
        notes.append("`bsd_ast` runs `bsdinit_task()` once, guarded by `bsd_init_done` "
                     "(`kern_sig.c:3443-3446`)")

    body = function_body(facts["bsd_init"], "bsdinit_task")
    if body is None or "load_init_program(p)" not in body:
        failures.append("`bsd_init.c` no longer calls `load_init_program(p)` from `bsdinit_task`: "
                        "the exec of process 1 has moved and every claim below is about a function "
                        "that no longer runs")
    else:
        after = body[body.index("load_init_program(p)") + len("load_init_program(p)"):]
        calls = re.findall(r"(?<![\w])[A-Za-z_][A-Za-z_0-9]*\s*\(", after)
        if calls:
            failures.append("`bsdinit_task` calls %s after `load_init_program(p)`: the exec is no "
                            "longer the last thing that happens before the function returns, so "
                            "reaching the boot's tail no longer says the exec returned"
                            % ", ".join(sorted(set(calls))[:4]))
        else:
            notes.append("`load_init_program(p)` is the last call in `bsdinit_task`'s body - only "
                         "an assignment follows it")

    if "/sbin/launchd" not in facts["kern_exec"]:
        failures.append("`/sbin/launchd` is not in `kern_exec.c`: the console line this reading is "
                        "about names a path the kernel no longer looks for")
    else:
        table = re.search(r"init_programs\[\]\s*=\s*\{(.*?)\};", facts["kern_exec"], re.S)
        names = re.findall(r'"([^"]+)"', table.group(1)) if table else []
        if "/sbin/launchd" not in names:
            failures.append("`/sbin/launchd` is no longer one of `init_programs[]`: the console's "
                            "last line would then come from the boot-arg block, which panics on "
                            "failure instead of falling through (`kern_exec.c:5146`)")
        elif names[0] == "/sbin/launchd":
            failures.append("`/sbin/launchd` is the *first* entry of `init_programs[]`: then the run "
                            "would never print the `/usr/local/sbin/launchd.development` line this "
                            "step's log shows, so the log being read is not this table's")
        else:
            notes.append("`init_programs[]` is %s, so `/sbin/launchd` is the last name tried"
                         % ", ".join(names))

    body = facts["execve_body"]
    if body is None or "return execve(p, &init_exec_args, retval);" not in body:
        failures.append("`load_init_program_at_path` no longer returns `execve(...)`: the errno the "
                        "console prints on a failure would then not be execve's, and 'no failure "
                        "line' would stop meaning 'execve did not fail'")
    else:
        notes.append("`load_init_program_at_path` returns `execve`'s own value, so the errno in a "
                     "`failed loading` line is execve's")


def claim_silence(facts, failures, notes):
    """2. `load_init_program` prints on failure and on nothing else - the rule that makes silence a
    reading.

    The rule is not "the loop prints twice". It is: **every attempt has a failure arm that prints or
    panics, no success arm prints anything, and the body's printing is exhausted by three `attempting
    to load` lines and two `failed loading ... errno` lines.** That is what makes the console
    readable, and it is stated over the whole function because both blocks can print.

    Which block the log's lines come from is a separate fact, and it is pinned by the *count*, not by
    a guess. `init_programs[]` in this configuration (`DEBUG` off, `DEVELOPMENT` on) is
    `{"/usr/local/sbin/launchd.development", "/sbin/launchd"}`, and the boot-argument block above the
    loop can only run when `PE_parse_boot_argn("launchdsuffix", ...)` finds the argument. The log
    shows `launchd.development` and no `launchdsuffix` line, so the loop ran and the block did not -
    which is a reading only if the two blocks' printfs are separable, hence the count.
    """
    body = facts["load_init_program_body"]
    if body is None:
        failures.append("no `load_init_program` definition in `kern_exec.c`: the function whose "
                        "console output this reading interprets is not there")
        return

    # (a) The body's printing, exhausted. A new diagnostic, a success printf, or a failure arm that
    #     stopped printing all move one of these three numbers.
    printfs = len(re.findall(r"(?<![\w])printf\(", body))
    attempting = len(re.findall(r'printf\("load_init_program: attempting to load', body))
    failed = len(re.findall(r'printf\("load_init_program: failed loading', body))
    if (attempting, failed, printfs) != (3, 2, 5):
        failures.append("`load_init_program`'s printing is %d `attempting to load` and %d `failed "
                        "loading ... errno` out of %d `printf(` calls in all, not 3 and 2 out of 5: "
                        "the console's silence is only a reading while the function's output is "
                        "exhausted by those two spellings, and the counts are what say which of the "
                        "two blocks the log's lines came from" % (attempting, failed, printfs))
    else:
        notes.append("`load_init_program`'s whole output is three `attempting to load` lines and two "
                     "`failed loading ... errno` lines - two of each in the `launchdsuffix` block "
                     "and one of each in the loop")

    # (b) Every attempt's failure arm prints or panics. Stated per call site: the text between one
    #     `load_init_program_at_path` and the next (or the end of the function) must contain the
    #     `if (!error)` that reads its result and, after that, either a `failed loading` line or a
    #     `panic`.
    sites = [m.start() for m in re.finditer(r"(?<![\w])load_init_program_at_path\(", body)]
    if len(sites) != 3:
        failures.append("`load_init_program` calls `load_init_program_at_path` %d times, not 3 (the "
                        "release-suffix branch, the suffixed branch and the loop): an attempt with no "
                        "call site is an attempt the console cannot account for" % len(sites))
    for i, at in enumerate(sites):
        nxt = sites[i + 1] if i + 1 < len(sites) else len(body)
        arm = body[at:nxt]
        if re.search(r"if\s*\(\s*!error\s*\)", arm) is None:
            failures.append("the attempt at offset %d does not read its result with `if (!error)`: "
                            "the errno it prints, if it prints one, is not that call's" % at)
        elif not (re.search(r'printf\("load_init_program: failed loading', arm)
                  or re.search(r"(?<![\w])panic\(", arm)):
            failures.append("the attempt at offset %d has no failure arm that prints a `failed "
                            "loading ... errno` line or panics: a path that neither reports nor dies "
                            "is one whose silence means nothing, and the whole reading is that "
                            "silence means success" % at)

    # (c) No success arm prints. Both spellings are here - `if (!error)\n\treturn;` and
    #     `if (!error) { return; }` - so the arm is taken by shape: a braced block, or a statement.
    for m in re.finditer(r"if\s*\(\s*!error\s*\)", body):
        rest = body[m.end():]
        stripped = rest.lstrip()
        if stripped.startswith("{"):
            brace = m.end() + len(rest) - len(stripped)
            end = _matching(body, brace, "{", "}")
            arm = body[brace:end + 1] if end >= 0 else stripped[:120]
        else:
            stop = rest.find(";")
            arm = stripped[:stop + 1] if stop >= 0 else stripped[:120]
        if "printf" in arm or "panic" in arm:
            failures.append("a success arm (`if (!error)`, at offset %d) prints: %s - success is then "
                            "not silent and the console's silence stops being the success reading"
                            % (m.start(), " ".join(arm.split())[:70]))
    if not any("success arm" in f for f in failures):
        notes.append("no `if (!error)` arm of `load_init_program` prints")

    # (d) The loop cannot fall out. Its exhaustion is the panic, and the panic is the only statement
    #     after it - so a console that stops at a `load` line cannot be a loop that ran off the end.
    loop_at = body.find("for (i = 0; i < sizeof(init_programs)")
    if loop_at < 0:
        failures.append("`load_init_program` has no `for (i = 0; i < sizeof(init_programs)...)` "
                        "loop: the console lines this reading interprets come from that loop")
    else:
        end = _matching(body, body.index("{", loop_at), "{", "}")
        if end < 0:
            failures.append("`load_init_program`'s loop has no matching closing brace")
        else:
            after = body[end + 1:]
            panics = re.findall(r"(?<![\w])panic\(", after)
            if len(panics) != 1 or "printf" in after:
                failures.append("what follows the loop has %d `panic(` calls and %s printf: the "
                                "exhausted loop has to reach exactly the one panic, and nothing "
                                "between them, or a silent fall-out of the loop would look like the "
                                "console's silence"
                                % (len(panics), "a" if "printf" in after else "no"))
            else:
                notes.append("the loop's success arm is a bare `return`, and the only statement after "
                             "the loop is the `panic` - so a console that stops at `attempting to "
                             "load /sbin/launchd` is the success reading, not a silent failure")

    # (e) The guard on the block that can print without the loop running.
    if re.search(r'PE_parse_boot_argn\("launchdsuffix"', body) is None:
        failures.append("`load_init_program` no longer reads the `launchdsuffix` boot argument: the "
                        "two printfs outside the loop have another guard, and 'the log shows one "
                        "launchd.development line and no suffix line' stops pinning which branch ran")


def claim_order(facts, failures, notes):
    """3. `bsd_init()` precedes the five tail calls, the image wraps those five, and the censuses are
    only called from inside the fifth's wrapper."""
    startup = facts["startup"]
    body = function_body(startup, "kernel_bootstrap_thread")
    if body is None:
        failures.append("no `kernel_bootstrap_thread` in `startup.c`: the boot thread's tail is the "
                        "only reason reaching `vm_pageout` says the boot got past BSD init")
    else:
        at = [(body.index("%s();" % name), name) for _i, name in BOOT_TAIL
              if ("%s();" % name) in body]
        missing = [name for _i, name in BOOT_TAIL if ("%s();" % name) not in body]
        if missing:
            failures.append("`kernel_bootstrap_thread` no longer calls %s: the tail this step's "
                            "reading is anchored to has changed shape" % ", ".join(missing))
        if "bsd_init();" not in body:
            failures.append("`kernel_bootstrap_thread` no longer calls `bsd_init()`: the ordering "
                            "below has no left-hand side")
        elif not missing:
            bsd_at = body.index("bsd_init();")
            after = [(off, name) for off, name in at if off < bsd_at]
            if after:
                failures.append("`kernel_bootstrap_thread` calls %s *before* `bsd_init()`: reaching "
                                "the tail no longer implies `bsd_init` returned, and with it no "
                                "longer implies `load_init_program` returned"
                                % ", ".join(name for _o, name in after))
            else:
                notes.append("`bsd_init()` is called before all five of %s in "
                             "`kernel_bootstrap_thread`" % ", ".join(n for _i, n in BOOT_TAIL))

    wraps = facts["build_entry"]
    unwrapped = [name for _i, name in BOOT_TAIL if "--wrap=%s" % name not in wraps]
    if unwrapped:
        failures.append("this build does not `--wrap` %s, so `entry_note_boot_tail` can never be "
                        "called for it and the run cannot say the boot reached it"
                        % ", ".join(unwrapped))
    else:
        notes.append("all %d tail calls are wrapped by `build_entry.sh`" % TAIL_CALLS)

    if "ENTRY_TAIL_CALLS 5u" not in facts["stubs"]:
        failures.append("`entry_stubs.c` no longer defines `ENTRY_TAIL_CALLS` as 5: the count the "
                        "array and the guard share is then not this tail's length")
    if "entry_note_boot_tail(%du," % (TAIL_CALLS - 1) not in (facts["boot_tail_body"] or ""):
        failures.append("`%s` does not call `entry_note_boot_tail(%du, ...)`: the censuses are then "
                        "not positionally tied to `%s`, which is the tail's last call"
                        % (CENSUS_WRAPPER, TAIL_CALLS - 1, BOOT_TAIL[-1][1]))

    body = facts["boot_tail_body"]
    if body is None:
        failures.append("no `%s` in `entry_trace.c`: the wrapper the censuses run inside is absent, "
                        "and a census that runs anywhere else can run before `bsd_init` returns"
                        % CENSUS_WRAPPER)
        return
    for name in CENSUSES:
        calls = len(re.findall(r"(?<![\w])%s\s*\([^;]*\);" % re.escape(name), body))
        if calls != 1:
            failures.append("`%s` calls `%s` %d times, not once: the census has to be in exactly "
                            "one place for its position to mean anything" % (CENSUS_WRAPPER, name, calls))
    # A declaration is not a call site. `extern void *entry_probe_dt_children(void);` matches the
    # same shape as a call - a name, parentheses, a semicolon - and counting it as one would report a
    # second definition of the census where there is only a type. So declarations come out first, by
    # the keyword, exactly as `strip_comments` removes comments by the comment syntax rather than by
    # pattern-matching what is around them. What is left is a call count over the whole file; the
    # calls inside the wrapper are subtracted because that is where they are supposed to be.
    callable_trace = "\n".join(line for line in facts["trace"].split("\n")
                               if not line.strip().startswith("extern "))
    for name in CENSUSES:
        total = len(re.findall(r"(?<![\w])%s\s*\([^;]*\);" % re.escape(name), callable_trace))
        inside = len(re.findall(r"(?<![\w])%s\s*\([^;]*\);" % re.escape(name), body))
        if total - inside != 0:
            failures.append("`%s` is called %d time(s) outside `%s` as well: a second call site is a "
                            "second, uncompared definition of 'the state the driver layer settled to'"
                            % (name, total - inside, CENSUS_WRAPPER))
    if "__real_vm_pageout()" not in body or body.index("__real_vm_pageout()") < body.index(CENSUSES[0]):
        failures.append("`%s` no longer calls the real `vm_pageout` *after* the censuses: `vm_pageout` "
                        "never returns, so a census after it would never run" % CENSUS_WRAPPER)
    elif body[body.index("__real_vm_pageout();") + len("__real_vm_pageout();"):].strip() != "}":
        failures.append("`%s` has a statement *after* the real `vm_pageout` call (%s): "
                        "`vm_pageout` is followed by Apple's own NOTREACHED marker "
                        "(`startup.c:647`), so anything written after it either never runs - a "
                        "record whose presence would say the call returned - or says the wrapper "
                        "returned, which the kernel says cannot happen"
                        % (CENSUS_WRAPPER,
                           " ".join(body[body.index("__real_vm_pageout();")
                                         + len("__real_vm_pageout();"):].split())[:60]))
    else:
        notes.append("both censuses are called from `%s`'s body only, between the tail record and "
                     "`__real_vm_pageout()`, which is its last statement - so an image whose "
                     "registry is read at all has already returned from `bsd_init`" % CENSUS_WRAPPER)


def claim_fault_sites(facts, failures, notes):
    """4. The two sites the fixture is written to fault on are a load and a store, read out of the
    RAM disk's own bytes as this image carries them."""
    ram = facts["ramdisk"]
    if ram is None:
        failures.append("the RAM disk could not be read: %s" % facts["ramdisk_error"])
        return
    notes.append("the RAM disk's entry is `__TEXT.vmaddr` 0x%x + 28 + sizeofcmds 0x%x = **0x%x**, "
                 "and its own `LC_UNIXTHREAD` pc agrees" % (ram["text_vmaddr"], ram["sizeofcmds"],
                                                            ram["entry_pc"]))
    for delta, mnemonic, why in FAULT_SITES:
        w = ram["words"][delta]
        want_load = 1 if mnemonic == "ldr" else 0
        if w["is_load"] != want_load:
            failures.append("entry+0x%x is %s, not `%s` (%s): the fault at that address would then "
                            "be a %s, and the pair of fault kinds the run shows - one read and one "
                            "write - would not be the pair these bytes can produce"
                            % (delta, "a load" if w["is_load"] else "a store", mnemonic, why,
                               "read" if w["is_load"] else "write"))
        elif w["rn"] != 0:
            failures.append("entry+0x%x is a `%s` through r%d, not r0 (%s): the address that faults "
                            "is then not the register `mmap` returned in, so `far` would not name "
                            "the page the fixture asked for" % (delta, mnemonic, w["rn"], why))
        else:
            notes.append("entry+0x%x (0x%08x) is %s r%d, [r0] - %s - fault `far` names the page "
                         "`mmap` returned, and the fault is a %s"
                         % (delta, w["word"], mnemonic, w["rd"], why,
                            "read" if want_load else "write"))
        if mnemonic == "str" and w["rd"] != w["rn"]:
            failures.append("entry+0x%x stores r%d through r0, not r0 through r0 (%s): the "
                            "load-and-compare after it would compare two different things, so the "
                            "program would not prove the page is the process's own"
                            % (delta, w["rd"], why))
    if ram["entry_pc"] != (ram["text_vmaddr"] + 28 + ram["sizeofcmds"]):
        failures.append("the RAM disk's entry point is not `__TEXT.vmaddr + 28 + sizeofcmds`")
    notes.append("entry+0x38 = **0x%x** and entry+0x44 = **0x%x** are the two addresses the run's "
                 "user-mode fault records must name"
                 % (ram["entry_pc"] + FAULT_SITES[0][0], ram["entry_pc"] + FAULT_SITES[1][0]))


def claim_readings(facts, failures, notes):
    """5. The readings this rests on are published by this image, not merely asserted here."""
    trace = facts["trace"]
    for needle, why in (
        ("entry_note_loadmachfile(caller, (uint32_t)(uintptr_t)header, (uint32_t)r);",
         "the `load_machfile` wrapper does not publish the real call's return, so `lmf_ret = 0` "
         "cannot be read"),
    ):
        if needle not in trace:
            failures.append(why)
        else:
            notes.append("`__wrap_load_machfile` publishes `header` and the real call's return")
    if "--wrap=load_machfile" not in facts["build_entry"]:
        failures.append("`load_machfile` is not in this build's wrap list: the return value the "
                        "`lmf_ret` reading comes from would then be the un-wrapped call's, "
                        "unpublished")

    stubs = facts["stubs"]
    for key in ("xnu_live_getpid_value", "xnu_live_getpid_error", "xnu_live_getpid_caller"):
        if "entry_live_write(\"%s\"" % key not in stubs:
            failures.append("`entry_stubs.c` does not publish `%s`: the first answer's value, its "
                            "error and its caller are the reading that says process 1 ran and the "
                            "kernel answered it" % key)
    if re.search(r"if\s*\(\s*g_getpid_calls\s*==\s*1u\s*\)", stubs) is None:
        failures.append("`entry_note_getpid` no longer has a first-call arm: the keys above would "
                        "then be written on a doubling schedule instead, and the *first* answer - "
                        "the one that ties the instrument to the fixture's first instruction - "
                        "would not survive")
    else:
        notes.append("the `getpid` and `mmap` wrappers publish their first call's value, error and "
                     "caller, so `xnu_live_getpid_value = 1` is a reading rather than an inference")


CLAIMS = (claim_chain, claim_silence, claim_order, claim_fault_sites, claim_readings)


def compare(facts, mutate=None):
    if mutate is not None:
        facts = mutate_facts(dict(facts), mutate)
    failures, notes = [], []
    for claim in CLAIMS:
        claim(facts, failures, notes)
    return failures, notes


# ------------------------------------------------------------------------------------------------
# The selftest
# ------------------------------------------------------------------------------------------------

def _bump(text, needle, replacement):
    assert text.count(needle) >= 1, needle
    return text.replace(needle, replacement, 1)


def _drop(text, needle):
    assert needle in text, needle
    return text.replace(needle, "", 1)


def mutate_facts(facts, mutate):
    facts = dict(facts)
    facts["ramdisk"] = dict(facts["ramdisk"]) if facts["ramdisk"] else None
    if facts["ramdisk"]:
        facts["ramdisk"]["words"] = {k: dict(v) for k, v in facts["ramdisk"]["words"].items()}

    def src(key, needle, replacement):
        facts[key] = _bump(facts[key], needle, replacement)

    if mutate == "bsd_init_runs_after_the_tail":
        # The whole ordering argument, inverted: the init runs after the tail has already run.
        facts["startup"] = _bump(facts["startup"], "bsd_init();", "vm_pageout();")
    elif mutate == "the_init_runs_after_the_tail_calls":
        src("startup", "\tbsd_init();", "\tvm_pageout();\n\tbsd_init();")
    elif mutate == "the_census_leaves_the_wrapper":
        facts["boot_tail_body"] = _bump(facts["boot_tail_body"], "entry_probe_dt_children()",
                                        "entry_probe_dt_children_elsewhere()")
        src("trace", "entry_probe_dt_children()", "entry_probe_dt_children_elsewhere()")
    elif mutate == "the_census_gains_a_second_caller":
        src("trace", "void __real_vm_pageout(void);",
            "void entry_probe_second_call(void) { entry_probe_dt_children(); }\n"
            "void __real_vm_pageout(void);")
    elif mutate == "the_census_runs_after_the_real_call":
        src("trace", "    __real_vm_pageout();",
            "    __real_vm_pageout();\n    (void)entry_probe_dt_children();")
    elif mutate == "a_statement_runs_after_the_real_call":
        # Nothing the census rules can see: the counts and the call sites are unchanged, and the only
        # property this breaks is that the real call is the wrapper's last statement.
        src("trace", "    __real_vm_pageout();",
            "    __real_vm_pageout();\n    (void)iokit_root;")
    elif mutate == "the_success_arm_prints":
        # The loop's arm, not the boot-argument block's: two tabs is the loop's, three the block's.
        src("kern_exec", "\t\tif (!error) {\n\t\t\treturn;",
            '\t\tif (!error) {\n\t\t\tprintf("load_init_program: loaded %s\\n", init_programs[i]);\n'
            '\t\t\treturn;')
    elif mutate == "the_failure_arm_stops_printing":
        src("kern_exec",
            'printf("load_init_program: failed loading %s: errno %d\\n", init_programs[i], error);',
            "(void)error;")
    elif mutate == "a_failure_line_moves_to_the_other_arm":
        # The body still prints 3 and 2, and the panic after the loop is untouched - so only the
        # per-call-site rule can see that one attempt no longer reports anything. The line moves to
        # the release-suffix branch, which already panics, so every other count is unchanged.
        src("kern_exec",
            'printf("load_init_program: failed loading %s: errno %d\\n", launchd_path, error);',
            "(void)error;")
        src("kern_exec", 'panic("Process 1 exec of launchd.release failed, errno %d", error);',
            'printf("load_init_program: failed loading %s: errno %d\\n", launchd_path, error);\n'
            '\t\t\tpanic("Process 1 exec of launchd.release failed, errno %d", error);')
    elif mutate == "the_panic_is_gone":
        src("kern_exec",
            'panic("Process 1 exec of %s failed, errno %d", ((i == 0) ? "<null>" : '
            'init_programs[i-1]), error);', "return;")
    elif mutate == "a_third_diagnostic_appears":
        src("kern_exec", "\n\tpanic(\"Process 1 exec of %s failed",
            "\n\tprintf(\"load_init_program: falling out of the loop\\n\");\n\tpanic(\"Process 1 exec "
            "of %s failed")
    elif mutate == "the_boot_argument_block_loses_its_guard":
        src("kern_exec", 'PE_parse_boot_argn("launchdsuffix"', 'PE_parse_boot_argn("ldsuffix"')
    elif mutate == "the_release_branch_stops_printing":
        src("kern_exec", 'printf("load_init_program: attempting to load /sbin/launchd\\n");',
            "(void)scratch_addr;")
    elif mutate == "the_attempt_gains_a_fourth_call_site":
        src("kern_exec", "\terror = ENOENT;\n\tfor (i = 0;",
            '\tprintf("load_init_program: attempting to load %s\\n", "/sbin/launchd");\n'
            '\tload_init_program_at_path(p, (user_addr_t)scratch_addr, "/sbin/launchd");\n'
            "\terror = ENOENT;\n\tfor (i = 0;")
    elif mutate == "the_census_wrapper_loses_its_index":
        src("trace", "entry_note_boot_tail(4u,", "entry_note_boot_tail(3u,")
    elif mutate == "the_tail_calls_stop_being_wrapped":
        src("build_entry", "--wrap=vm_pageout", "--wrap=not_vm_pageout")
    elif mutate == "the_header_enters_at_the_fault":
        facts["ramdisk"]["entry_pc"] = facts["ramdisk"]["entry_pc"] + FAULT_SITES[0][0]
    elif mutate == "the_read_is_a_store":
        w = facts["ramdisk"]["words"][0x38]
        w["is_load"] = 0
    elif mutate == "the_write_is_a_load":
        w = facts["ramdisk"]["words"][0x44]
        w["is_load"] = 1
    elif mutate == "the_read_uses_the_wrong_register":
        facts["ramdisk"]["words"][0x38]["rn"] = 3
    elif mutate == "the_store_is_not_its_own_address":
        facts["ramdisk"]["words"][0x44]["rd"] = 1
    elif mutate == "the_ramdisk_is_not_there":
        facts["ramdisk"] = None
        facts["ramdisk_error"] = "mutation"
    elif mutate == "the_load_machfile_return_stops_being_published":
        src("trace", "entry_note_loadmachfile(caller, (uint32_t)(uintptr_t)header, (uint32_t)r);",
            "entry_note_loadmachfile(caller, (uint32_t)(uintptr_t)header, 0u);")
    elif mutate == "the_load_machfile_wrap_is_gone":
        src("build_entry", "--wrap=load_machfile", "--no-wrap=load_machfile")
    elif mutate == "the_first_getpid_answer_stops_being_published":
        src("stubs", 'entry_live_write("xnu_live_getpid_value", value);', "/* removed */")
    elif mutate == "the_getpid_first_call_arm_is_gone":
        src("stubs", "if (g_getpid_calls == 1u) {", "if (g_getpid_calls == 0u) {")
    elif mutate == "the_boot_tail_count_changes":
        src("stubs", "#define ENTRY_TAIL_CALLS 5u", "#define ENTRY_TAIL_CALLS 6u")
    elif mutate == "the_init_program_table_moves_launchd_first":
        src("kern_exec", 'init_programs[] = {\n#if DEBUG',
            'init_programs[] = {\n\t"/sbin/launchd",\n#if DEBUG')
    elif mutate == "the_exec_result_stops_being_the_errno":
        src("kern_exec", "return execve(p, &init_exec_args, retval);",
            "execve(p, &init_exec_args, retval);\n\treturn 0;")
    elif mutate == "bsdinit_task_leaves_the_ast":
        src("kern_sig", "if (!bsd_init_done) {", "if (bsd_init_done) {")
    else:
        raise SystemExit("unknown mutation %s" % mutate)

    # Every mutation above edits source text; the bodies the claims read are re-derived from it, so a
    # mutation cannot change a body the source no longer produces.
    facts["boot_tail_body"] = function_body(facts["trace"], CENSUS_WRAPPER)
    facts["load_init_program_body"] = function_body(facts["kern_exec"], "load_init_program")
    facts["execve_body"] = function_body(facts["kern_exec"], "load_init_program_at_path")
    return facts


MUTATIONS = (
    "bsd_init_runs_after_the_tail", "the_init_runs_after_the_tail_calls",
    "the_census_leaves_the_wrapper", "the_census_gains_a_second_caller",
    "the_census_runs_after_the_real_call", "a_statement_runs_after_the_real_call",
    "the_success_arm_prints",
    "the_failure_arm_stops_printing", "a_failure_line_moves_to_the_other_arm",
    "the_panic_is_gone", "a_third_diagnostic_appears",
    "the_boot_argument_block_loses_its_guard", "the_release_branch_stops_printing",
    "the_attempt_gains_a_fourth_call_site",
    "the_census_wrapper_loses_its_index", "the_tail_calls_stop_being_wrapped",
    "the_header_enters_at_the_fault", "the_read_is_a_store", "the_write_is_a_load",
    "the_read_uses_the_wrong_register", "the_store_is_not_its_own_address",
    "the_ramdisk_is_not_there", "the_load_machfile_return_stops_being_published",
    "the_load_machfile_wrap_is_gone", "the_first_getpid_answer_stops_being_published",
    "the_getpid_first_call_arm_is_gone", "the_boot_tail_count_changes",
    "the_init_program_table_moves_launchd_first", "the_exec_result_stops_being_the_errno",
    "bsdinit_task_leaves_the_ast",
)


def selftest(facts):
    baseline, _notes = compare(facts)
    if baseline:
        print("FAIL: the selftest's own baseline fails %d claim(s), so 'every mutation was refused' "
              "would be true for the wrong reason. Fix these first:" % len(baseline), file=sys.stderr)
        for failure in baseline:
            print("      " + failure, file=sys.stderr)
        return 1
    accepted = []
    for name in MUTATIONS:
        failures, _notes = compare(facts, mutate=name)
        if not failures:
            accepted.append(name)
            print("      ACCEPTED: %s" % name, file=sys.stderr)
    if accepted:
        print("FAIL: %d of %d mutations were not refused: %s"
              % (len(accepted), len(MUTATIONS), ", ".join(accepted)), file=sys.stderr)
        return 1
    say("  --selftest: all %d mutations were refused" % len(MUTATIONS))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--image", required=True, help="the linked entry image")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if not os.path.exists(args.image):
        print("FAIL: no %s: claim 4 reads the RAM disk out of the linked image, and claims 3 and 5 "
              "read the image's own transfers" % args.image, file=sys.stderr)
        return 1
    facts = gather(args.image)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if args.verbose:
        for note in notes:
            say("    " + note)
    if failures:
        print("FAIL: the reading that the boot reached the OS does not hold for this image:",
              file=sys.stderr)
        for failure in failures:
            print("      " + failure, file=sys.stderr)
        return 1
    say("  xnu_entry_489: the boot reached the OS - `bsd_ast` execs `init_programs[]`'s last name "
        "into process 1, `load_init_program` prints on failure and on nothing else so the console's "
        "silence is the success reading, the image can only read the registry after `bsd_init` has "
        "returned, and the two fault sites in the RAM disk's own bytes are one load and one store "
        "through r0 - so the run's read-then-write pair is predicted by the file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
