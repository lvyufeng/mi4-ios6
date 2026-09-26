#!/usr/bin/env python3
"""
Check the Mach-O the entry image carries as the first userland process's executable.

    ./tools/host_ramdisk_macho_check.py                       # uses out/stage90/xnu_arm_entry.elf
    ./tools/host_ramdisk_macho_check.py path/to/entry.elf
    ./tools/host_ramdisk_macho_check.py --selftest            # prove the checks bite

Since experiment 468, `g_stage90_ramdisk` (`src/entry/entry_ramdisk.s`) is a
Mach-O at offset 0, and the bytes at that offset **are** `/sbin/launchd` - `mockfs` maps its one file
node directly onto the device's physical pages (`mockfs_fsnode.c:333-344`) and takes the file's size
from the device (`:80`). So this object is read by `exec_mach_imgact` -> `get_macho_vnode` ->
`parse_machfile` -> `load_segment` -> `load_unixthread`, and a field that is wrong is a *load*
failure on the device that no log names: `load_return_to_errno` maps `LOAD_BADMACHO` to `EBADMACHO`
and `LOAD_FAILURE` to its own errno, so the report is a number and not a cause.

What this checks, and it checks the artifact rather than the source:

  - the bytes **emitted into the linked ELF**, at `g_stage90_ramdisk`'s address, so what is tested is
    what the device will read;
  - that the section holding them is `PROGBITS` and `ALLOC` - i.e. in the part of the image the
    payload *copies* and not the part it zeroes. A `.bss` disk would be erased between the copy and
    the jump, and that is a whole-class failure this asserts rather than assumes;
  - every load command's fields against the rules `parse_machfile` and `load_machfile` apply, each
    one quoted from the source where it is applied, and the two rules that are *shapes* rather than
    values: `sizeofcmds` must be the commands' own extent (walked with their own `cmdsize` chains)
    and the entry point must be inside the R+X segment it is loaded from (`validentry`);
  - the numbers that come from Apple's headers are **read out of those headers** - `mach-o/loader.h`,
    `mach/machine.h`, `mach/vm_prot.h`, `mach/arm/thread_status.h` and `mach/arm/_structs.h` - so the
    assembler's spelling and the kernel's cannot drift apart silently. The one thing transcribed here
    is the *struct layout* of `mach_header`/`segment_command`/`thread_command`, which is the same
    concession `host_entry_macho_check.sh` makes and records: XNU's own reader compiling against that
    layout is what the device run tests.

What it does not do, stated rather than implied: it does not run XNU's parser, and it cannot say
whether the *entry point* is reachable in the user's address space - only that the file describes it
the way the loader requires. `--selftest` mutates a copy of the blob one field at a time and requires
every one of those mutations to be refused, because a checker that cannot fail proves nothing.

Exit 0 clean, 1 a check failed, 2 a tool problem.
"""

import argparse
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
XNU = os.environ.get("XNU_TREE", os.path.join(REPO_ROOT, "external", "xnu-4570.1.46"))
DEFAULT_ELF = os.path.join(REPO_ROOT, "out", "stage90", "xnu_arm_entry.elf")
MMAN = os.path.join(XNU, "bsd/sys/mman.h")
FCNTL = os.path.join(XNU, "bsd/sys/fcntl.h")

PAGE = 0x1000

# The one transcription: `mach-o/loader.h`'s 32-bit struct layouts. `struct mach_header` is seven
# `uint32_t`s; `struct segment_command` and `struct thread_command` are checked field by field, so the
# offsets below are what they are *used* at rather than a second definition of the struct - a wrong
# one here fails the build instead of reaching the device.
SEG = {
    "cmd": 0, "cmdsize": 4, "segname": 8, "vmaddr": 24, "vmsize": 28, "fileoff": 32,
    "filesize": 36, "maxprot": 40, "initprot": 44, "nsects": 48, "flags": 52,
}
SEG_SIZE = 56
THREAD_HDR = 8       # cmd, cmdsize
THREAD_STATE = 8     # flavor, count - the two words load_unixthread skips over
THREAD_CMD_MIN = 16  # the header plus flavor and count

failures = []
notes = []


def fail(msg):
    failures.append(msg)


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def u64(blob, off):
    return struct.unpack_from("<Q", blob, off)[0]


def hdr_define(path, name, base=0):
    """The value of `#define name ...` in a header, as an int.

    Apple spells these several ways - `0xfeedface`, `12`, `((vm_prot_t) 0x01)`, an expression over
    other defines - so the extractor takes the first `0x`-or-decimal literal on the line and refuses
    rather than guesses when there is none.
    """
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError as e:
        sys.exit(f"cannot read {path}: {e}")
    for line in text.split("\n"):
        m = re.match(r"\s*#\s*define\s+%s\b(.*)$" % re.escape(name), line)
        if not m:
            continue
        rest = m.group(1)
        lit = re.search(r"0[xX][0-9a-fA-F]+|\b\d+\b", rest)
        if lit:
            return int(lit.group(0), 0)
        # An expression such as `(1 << 2)`: evaluate it with only the earlier defines in scope.
        return None
    sys.exit(f"{path} does not define {name} - the header this check reads has moved")


def syscall_getpid():
    """The fixture's syscall, from the place XNU writes it down.

    `bsd/kern/syscalls.master`'s own line - `20 AUE_GETPID ALL { int getpid(void); }` - is the number
    the BSD dispatcher indexes `sysent` with, and the prototype is what makes `sy_narg` zero:
    `unix_syscall` only calls `arm_get_syscall_args` when `callp->sy_narg != 0`
    (`bsd/dev/arm/systemcalls.c:117`), so the program's argument registers are read by nothing at all.
    The *sign* is the ABI and it is checked here: `osfmk/arm/locore.s`'s `fleh_swi` computes
    `r5 = -r12` and branches to `fleh_swi_unix` when that is `<= 0`, so a positive number in r12 is the
    unix path and a negative one is a mach trap - 478's fixture called the mach side (`-61`,
    `thread_switch`), and this one calls the BSD side.

    The other end of the same decision is the image's own table, and it is checked where it can be:
    `tools/check_sysent_table.py` reads `sysent[20].sy_call` out of the linked image and requires it to
    be `--wrap=getpid`'s wrapper on `getpid`. A number here that disagreed with the image's table would
    otherwise be a fixture calling a syscall the image does not dispatch where this file says.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+AUE_GETPID\s+ALL\s+\{\s*int\s+getpid\s*\(\s*void\s*\)\s*;", master,
                  re.M)
    if not m:
        sys.exit("bsd/kern/syscalls.master no longer has an `AUE_GETPID ALL { int getpid(void); }` "
                 "line - the fixture's syscall number cannot be checked against the master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts getpid at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    return number


def syscall_mmap():
    """The program's second syscall, and the prototype that decides how its arguments arrive.

    `bsd/kern/syscalls.master`'s own line - `197 AUE_MMAP ALL { user_addr_t mmap(caddr_t addr,
    size_t len, int prot, int flags, int fd, off_t pos) NO_SYSCALL_STUB; }` - is the line Apple
    generates `sysent[197]` from, and the parameter list is what decides the **munger**:
    `munge_wwwwwl`, five 4-byte arguments and one 8-byte `off_t`, which is the function
    `arm_get_u32_syscall_args` (`bsd/dev/arm/systemcalls.c:337`) calls to turn the saved registers
    into the eight words the wrapper in `entry_trace.c` reads. The number is returned, and the
    parameter list is checked against the one the fixture's header comment was written for - because a
    prototype change moves every argument word, and none of it would show on the device: `MAP_ANON`
    makes `fd` and `pos` inoperative, so a wrong argument arrangement is still a working `mmap`.

    The two ends of that bridge are both machine-checked - this line, and the munger the *image* names
    in `sysent[197].sy_arg_munge32` (`tools/check_sysent_table.py`). Only Apple's own rule for
    spelling a munger's name (`w` for a 4-byte argument, `l` for an 8-byte one, in order) is stated in
    prose rather than derived.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+AUE_MMAP\s+ALL\s+\{\s*user_addr_t\s+mmap\s*\(([^)]*)\)", master, re.M)
    if not m:
        sys.exit("bsd/kern/syscalls.master no longer has the `AUE_MMAP ALL { user_addr_t mmap(...) }` "
                 "line - the fixture's second syscall number and its argument layout cannot be "
                 "checked against the master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts mmap at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    params = " ".join(m.group(2).split())
    want = "caddr_t addr, size_t len, int prot, int flags, int fd, off_t pos"
    if params != want:
        sys.exit(f"syscalls.master's mmap takes `{params}`, and the armv7k reading this check encodes "
                 f"was written for `{want}`: the munger's name, the register order in "
                 f"entry_ramdisk.s, and the eight words the wrapper in entry_trace.c reads all have "
                 f"to be re-derived from the new prototype before the fixture can be believed")
    return number


def syscall_poll():
    """The program's third syscall, the one whose *effect* is to make the kernel wait.

    `bsd/kern/syscalls.master`'s own line is
    `230 AUE_POLL ALL { int poll(struct pollfd *fds, u_int nfds, int timeout); }`, and the prototype
    is what decides two separate things here:

      - **the three arguments are three 4-byte words, so the slot's munger is `munge_www`.** The
        armv7k `arm_get_u32_syscall_args` (`bsd/dev/arm/systemcalls.c:337`) calls
        `sysent[230].sy_arg_munge32`, and `munge_www` (`bsd/dev/arm/munge.c:129`) is `munge_wlll`
        reduced - it copies `r0..r2` and nothing else. So the fixture's three register writes are
        `fds`, `nfds` and `timeout`, in that order, with **no padding word** - which is the difference
        between this call and `mmap`'s, whose 8-byte `off_t` puts one there.
        `tools/check_sysent_table.py` reads that munger word back out of the linked image; this
        function is the other end, the prototype the word is generated from.
      - **the return type is `int`**, so `poll` has one return word and the fixture's `r0` after the
        `svc` is its value (`0` when the timeout expires with nothing ready) - not an address like
        `mmap`'s.

    This function is why the count above `nfds` matters: `poll(NULL, 0, ms)` is Apple's own way to
    sleep on a deadline, and Apple's comment says so in Apple's words (`bsd/kern/sys_generic.c:1785`:
    "If user space passed 0 FDs, then respect any timeout value passed. This is an extremely
    inefficient sleep"). A prototype that had grown a fourth argument would put the timeout in a
    different register than the fixture writes it in.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+AUE_POLL\s+ALL\s+\{\s*int\s+poll\s*\(([^)]*)\)\s*;", master, re.M)
    if not m:
        sys.exit("bsd/kern/syscalls.master no longer has an `AUE_POLL ALL { int poll(...) }` line - "
                 "the fixture's third syscall cannot be checked against the master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts poll at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    params = [q.strip() for q in m.group(2).split(",") if q.strip()]
    if len(params) != 3 or "off_t" in m.group(2):
        sys.exit(f"syscalls.master's poll takes `{m.group(2)}`, and the armv7k reading this check "
                 f"encodes - three 4-byte arguments, so `munge_www` and r0..r2 - is derived from the "
                 f"three-word prototype it has had since 4570. A fourth argument, or an 8-byte one, "
                 f"would put an argument in a register the fixture does not write")
    return number


def syscall_three_words(audit, name):
    """A syscall 504's program makes, from `syscalls.master`'s own line.

    Both of this step's calls are three 4-byte arguments on this target -
    `3 AUE_NULL ALL { user_ssize_t read(int fd, user_addr_t cbuf, user_size_t nbyte); }` and
    `5 AUE_OPEN_RWTC ALL { int open(user_addr_t path, int flags, int mode) NO_SYSCALL_STUB; }` - so
    both slots name `munge_www`, the same munger 503's `poll` has, and both wrappers in
    `entry_trace.c` read three words of `uap`. That is a claim about the *prototype*, so it is read
    here and not written down: a parameter list that grew a fourth argument, or an 8-byte one, would
    move a register the fixture does not write and the run would show the wrong word in a key.

    `read`'s **return** type is 64-bit (`user_ssize_t`), and that is deliberately not part of what this
    function refuses: the munger is generated from the *argument* types alone, and the 64-bit return
    has its own, separate consequence - `unix_syscall` writes `save_r1` as well as `save_r0` for it,
    which is why the fixture keeps its page address in `r9` rather than in `r1`. The three arguments
    are `int`, `user_addr_t` and `user_size_t`, four bytes each on this 32-bit kernel.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+%s\s+ALL\s+\{\s*\w+\s+%s\s*\(([^)]*)\)" % (re.escape(audit),
                                                                        re.escape(name)),
                  master, re.M)
    if not m:
        sys.exit(f"bsd/kern/syscalls.master no longer has an `{audit} ALL {{ ... {name}(...) }}` "
                 f"line - the fixture's call cannot be checked against the master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts {name} at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    args = m.group(2)
    params = [q.strip() for q in args.split(",") if q.strip()]
    if len(params) != 3 or "off_t" in args:
        sys.exit(f"syscalls.master's {name} takes `{args}`, and the armv7k reading this check encodes "
                 f"- three 4-byte arguments, so `munge_www` and r0..r2 - is derived from a three-word "
                 f"prototype. A fourth argument, or an 8-byte one, would put an argument in a register "
                 f"the fixture does not write")
    return number


def syscall_fork_and_exit():
    """505's pair, from `syscalls.master`'s own lines - and the *shapes* are what the step rests on.

    `2 AUE_FORK ALL { int fork(void) NO_SYSCALL_STUB; }` and
    `1 AUE_EXIT ALL { void exit(int rval) NO_SYSCALL_STUB; }` are two different shapes and both of them
    are load-bearing:

      - **`fork` takes nothing**, so `sysent[2].sy_narg` is 0 and `unix_syscall`
        (`bsd/dev/arm/systemcalls.c:118`) never calls `arm_get_syscall_args` for it. That is the fact
        the fixture's `mov r0, #0` before the `svc` depends on: the word is *not* read as an argument,
        and what the two processes return with is written by the kernel - the pid in `r0` for both, and
        the flag in `r1`: 0 for the parent (`retval[1]`, `bsd/kern/kern_fork.c:895`) and 1 for the child
        (`thread_set_child`, `osfmk/arm/status.c:722`; 505's run measured both ends of that - the
        parent's `xnu_live_fork_ret_hi` 0, and the child walking the parent's arm when the branch tested
        `r0`). `tools/check_sysent_table.py` checks the slot's own three zero words; this is the
        prototype they are generated from.
      - **`exit` takes exactly one 4-byte word** (`int rval`), so its munger is `munge_w` and the
        fixture's `r0` is that word - the number the kernel composes into the status with
        `W_EXITCODE` (see `exit_status_word` below).

    A `fork` prototype that grew an argument, or an `exit` one that grew an 8-byte one, would move a
    register the fixture does not write - and neither would show on the device: `fork` ignores its
    argument buffer entirely, and `exit`'s status word is not read back by anything in this run.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    fork = re.search(r"^(\d+)\s+AUE_FORK\s+ALL\s+\{\s*int\s+fork\s*\(([^)]*)\)", master, re.M)
    exit_ = re.search(r"^(\d+)\s+AUE_EXIT\s+ALL\s+\{\s*void\s+exit\s*\(([^)]*)\)", master, re.M)
    if not fork or not exit_:
        sys.exit("bsd/kern/syscalls.master no longer has the `AUE_FORK ALL { int fork(void) }` and "
                 "`AUE_EXIT ALL { void exit(int rval) }` lines - 505's two syscall numbers and their "
                 "argument shapes cannot be checked against the master")
    fork_args = " ".join(fork.group(2).split())
    if fork_args != "void":
        sys.exit(f"syscalls.master's fork takes `{fork_args}` and 505's fixture is built on it taking "
                 f"nothing: with a non-zero `sy_narg` the kernel would marshal the fixture's registers "
                 f"into `uu_arg`, and the `mov r0, #0` before the `svc` would be a word the kernel reads "
                 f"- which changes nothing observable, which is why the slot's own `sy_narg` is checked "
                 f"in tools/check_sysent_table.py as well")
    exit_args = " ".join(exit_.group(2).split())
    if exit_args != "int rval":
        sys.exit(f"syscalls.master's exit takes `{exit_args}` and 505's fixture writes exactly one "
                 f"4-byte word into r0 before the `svc`: a second argument, or an 8-byte one, would put "
                 f"the status in a register the fixture does not write and the kernel would compose "
                 f"`W_EXITCODE` out of something else")
    fork_number, exit_number = int(fork.group(1)), int(exit_.group(1))
    for name, number in (("fork", fork_number), ("exit", exit_number)):
        if number <= 0:
            sys.exit(f"syscalls.master puts {name} at {number}: with a non-positive number `fleh_swi` "
                     f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    return fork_number, exit_number


def syscall_wait4():
    """508's call, from `syscalls.master`'s own line - and the *order* of its four arguments is the claim.

    `7 AUE_WAIT4 ALL { int wait4(int pid, user_addr_t status, int options, user_addr_t rusage)
    NO_SYSCALL_STUB; }` puts four arguments in four registers, and the fixture's `r0`..`r3` are the
    four fields of `struct wait4_args` **in this order**: the pid to ask about, the address the status
    is written to, the options, and the address a `rusage` would be written to. All four are one word
    on this target - which is not a reading of `user_addr_t`'s `sizeof` but the thing the object says:
    `wait4_nocancel` loads the argument struct at byte offsets 0, 4, 8 and 12
    (`ldr r0, [r6]`, `ldr r1, [r6, #4]`, `ldrb r0, [r6, #8]`, `ldr r1, [r6, #12]` in
    `out/xnu_kernel_obj/bsd_kern_kern_exit.o`), and the slot's `munge_wwww` copies exactly four words
    contiguously over them. `tools/check_sysent_table.py` checks that side - the munger's name and the
    two counts - and this function is the other side of the same claim from the master, so a prototype
    that grew an argument, or swapped `status` and `options`, fails the build instead of moving a
    register the fixture does not write.

    What it cannot check is that the *kernel* reads them in that order; that is the disassembly above,
    and it is why this docstring carries the four offsets rather than a count of the parameters.
    """
    master = open(os.path.join(XNU, "bsd/kern/syscalls.master"), encoding="utf-8",
                  errors="replace").read()
    m = re.search(r"^(\d+)\s+AUE_WAIT4\s+ALL\s+\{\s*int\s+wait4\s*\(([^)]*)\)", master, re.M)
    if not m:
        sys.exit("bsd/kern/syscalls.master no longer has the `AUE_WAIT4 ALL { int wait4(...) }` line - "
                 "508's syscall number and its four argument registers cannot be checked against the "
                 "master")
    number = int(m.group(1))
    if number <= 0:
        sys.exit(f"syscalls.master puts wait4 at {number}: with a non-positive number `fleh_swi` "
                 f"routes it to the mach path, so the fixture would not be calling a BSD syscall")
    args = " ".join(m.group(2).split())
    params = [q.strip() for q in args.split(",") if q.strip()]
    shape = [q.split()[0] for q in params if q.split()]
    want = ["int", "user_addr_t", "int", "user_addr_t"]
    if shape != want:
        sys.exit(f"syscalls.master's wait4 takes `{args}`, whose types are {shape}, and 508's fixture "
                 f"is built on the prototype being {want} - the four words r0..r3 are the pid, the "
                 f"status address, the options and the rusage address *in that order*, so a "
                 f"prototype whose parameter list differs puts one of the fixture's registers "
                 f"somewhere the kernel does not read it (and the rusage word, which the fixture sets "
                 f"to 0, would be a stale pointer `wait4_nocancel` copies out to)")
    return number


def exit_status_word(rval):
    """`W_EXITCODE(rval, 0)`: the status the kernel composes out of the fixture's own number.

    The macro is `bsd/sys/wait.h:157` - `#define W_EXITCODE(ret, sig) ((ret) << 8 | (sig))` - and the
    shift is read out of that line rather than written here, because the *composition* is the claim:
    `exit` hands `exit1` the expression `W_EXITCODE(uap->rval, 0)` (`bsd/kern/kern_exit.c:682`), so the
    value the kernel works with is derived from the fixture's register and is not equal to it. That is
    what makes the fixture's status a number worth choosing: **3 becomes 0x300**, which a zero-filled
    field, a stale register and a wrapper publishing its own arguments cannot produce - and which is
    why the check refuses a 0 there (see `check_program`).

    What this check does *not* do is observe the composed word on the device: 505's run has no reaping
    parent, so nothing reads that status back. It is here because the fixture's number has to be one the
    kernel's own composition makes visible rather than one that survives it unchanged.
    """
    src = open(os.path.join(XNU, "bsd/sys/wait.h"), encoding="utf-8", errors="replace").read()
    m = re.search(r"#define\s+W_EXITCODE\s*\(\s*ret\s*,\s*sig\s*\)\s*\(\s*\(\s*ret\s*\)\s*<<\s*(\d+)"
                  r"\s*\|\s*\(\s*sig\s*\)\s*\)", src)
    if not m:
        sys.exit("bsd/sys/wait.h no longer defines `W_EXITCODE(ret, sig)` as `((ret) << N | (sig))` - "
                 "the composition 505's exit status rests on cannot be read from the kernel's own "
                 "header")
    return (rval << int(m.group(1))) | 0


def devfs_mount_point():
    """Where devfs is mounted, from the line that mounts it.

    `bsd/kern/bsd_init.c`'s `char mounthere[] = "/dev";` is the string `devfs_kernel_mount` is handed,
    and it is the *prefix* of the path the fixture opens - so it is read here rather than written as
    `/dev`, because a fixture whose prefix was right only by coincidence would still pass a check that
    compared it against a literal in this file.
    """
    src = open(os.path.join(XNU, "bsd/kern/bsd_init.c"), encoding="utf-8", errors="replace").read()
    m = re.search(r'char\s+mounthere\[\s*\]\s*=\s*"([^"]+)"', src)
    if not m:
        sys.exit("bsd/kern/bsd_init.c no longer has a `char mounthere[] = \"...\"` beside "
                 "devfs_kernel_mount - the mount point the fixture's path is built on cannot be "
                 "checked against the kernel's own source")
    return m.group(1)


def mdev_node_names():
    """The two names `mdevadd` makes devfs nodes under, as `(block, char)`, from Apple's own call.

    `bsd/dev/memdev.c` makes *two* nodes per memory device and the format string is the difference:
    `devfs_make_node(..., DEVFS_BLOCK, ..., 0600, "md%d", devid)` and the same with `DEVFS_CHAR` and
    `"rmd%d"`. The fixture opens the **character** one, and this function is where that choice is
    checked against the file that made the node rather than against a comment: the name it opens has to
    be the one the *character* format produces for the device the boot added, which the run's own log
    line (`Added memory device md0/rmd0 ...`) is the other end of.

    The minor is 0 and it is not written here: `mdevadd` is called with -1 from
    `iokit/bsddev/IOKitBSDInit.cpp:447`, which assigns the first free id, and the boot has exactly one
    memory device - so 0 is the *first* number the formats can produce, and a fixture that opened
    `/dev/md1` would name a device this boot never made.
    """
    src = open(os.path.join(XNU, "bsd/dev/memdev.c"), encoding="utf-8", errors="replace").read()
    found = {}
    for m in re.finditer(r"DEVFS_(BLOCK|CHAR)\b(.*?);", src, re.S):
        fmt = re.search(r'"([^"]*)"', m.group(2))
        if fmt:
            found[m.group(1)] = fmt.group(1)
    if "BLOCK" not in found or "CHAR" not in found:
        sys.exit("bsd/dev/memdev.c no longer has two `devfs_make_node` calls, one per DEVFS_BLOCK and "
                 "DEVFS_CHAR - the node the fixture opens cannot be checked against the driver that "
                 "makes it")
    return found["BLOCK"] % 0, found["CHAR"] % 0


def poll_timeouts(decoded):
    """The two timeouts the program asks for, as `(short_ms, long_ms)`, or `(None, None)`.

    **These are the one pair of numbers in this program that no header defines**, because they are the
    fixture's own choice and the property they have to satisfy is a *ratio* and not a value. So this
    function does not return constants for the caller to compare against: it reads the two `movw r2`
    immediates and returns them, and `check_program` states the properties. The `None`s are what make
    a word that is not a `movw` into r2 fail the word-by-word comparison with a message that shows
    `movw r2, #None` beside the word that is really there, rather than being accepted because it
    happens to be in the same register.
    """
    out = []
    for index in (POLL_SHORT_WORD, POLL_LONG_WORD):
        ins = decoded[index]
        if ins[0] == "movw" and len(ins[1]) == 3 and ins[1][0] == 2 and isinstance(ins[1][1], int):
            out.append(ins[1][1])
        else:
            out.append(None)
    return tuple(out)


def read_length(decoded):
    """The byte count 504's `read` asks for, or `None` when that word is not the `mov r2, #imm` it is.

    Read out of the instruction stream for `poll_timeouts`' reason, one step further on: the number is
    the fixture's own choice and its property is a *bound*, not a value - it has to be big enough for
    the word the wrapper publishes and small enough to stay inside the page the buffer is - so the
    clause that states the bound is in `check_program` and the value is read back here.
    """
    ins = decoded[READ_LEN_WORD]
    if ins[0] == "mov" and len(ins[1]) == 3 and ins[1][0] == 2 and isinstance(ins[1][1], int):
        return ins[1][1]
    return None


def park_timeout(decoded):
    """512's park timeout, as a number of milliseconds, or `None` when that word is not a `movw r2`.

    The third of this file's three *read* numbers, and the reason is the same each time: the value is
    the fixture's own choice and the property is a bound, so `check_program` states the bound (nonzero,
    and no test on the answer) and this reads the value back. `None` is what makes a park whose timeout
    moved to another register fail the word-by-word comparison with `movw r2, #None` beside the word
    that is really there, rather than being accepted because the number is right.
    """
    ins = decoded[PARK_MS_WORD]
    if ins[0] == "movw" and len(ins[1]) == 3 and ins[1][0] == 2 and isinstance(ins[1][1], int):
        return ins[1][1]
    return None


def park_threshold():
    """`ENTRY_PARK_MIN_MS` as the number the C wrapper really uses, read out of the wrapper's own file.

    **This is the one value that lives in C and is checked from here, and it is read rather than
    restated on purpose.** `entry_trace.c`'s `__wrap_poll` prints this step's console line on the first
    `poll` whose timeout is at or above this number, so the number is the *premise* of a reading: if it
    did not sit between the program's two asks and its park, the line would either never be printed or
    be printed for an ask, and the log would say the OS idled when it had not. So the fixture's three
    timeouts are compared against this number below - the asks must be under it and the park at or over
    it - and a park retuned below the threshold fails the build here instead of silently losing the
    artifact.

    The file is read as text and the define is required to be there: a reader that quietly returned a
    default when the name changed would make this clause pass by not existing, which is the defect 447
    recorded about a check that measured the label instead of the branch.
    """
    path = os.path.join(REPO_ROOT, "src/entry/entry_trace.c")
    src = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"^#define\s+ENTRY_PARK_MIN_MS\s+(\d+)\s*$", src, re.M)
    if not m:
        sys.exit("src/entry/entry_trace.c no longer defines ENTRY_PARK_MIN_MS as a "
                 "plain decimal number - the `__wrap_poll` guard that prints 512's console line reads "
                 "it, and this check cannot compare the fixture's three timeouts against a threshold "
                 "it cannot find")
    return int(m.group(1))


def dev_path_strings(blob, K):
    """Every `/dev/...` NUL-terminated string in the RAM disk, as `[(offset, text), ...]`.

    Found by scanning the bytes rather than by being told where they are, because that is what makes
    the clause about them a measurement of the file: the fixture is *required* to contain exactly the
    two paths its two `adr`s point at, and a third one - a path written somewhere a future step forgot -
    would make the count wrong instead of being invisible.
    """
    prefix = (K["DEV_MOUNT"] + "/").encode()
    out = []
    at = 0
    while True:
        at = blob.find(prefix, at)
        if at < 0:
            return out
        end = blob.find(b"\0", at)
        if end < 0:
            return out
        out.append((at, blob[at:end].decode("latin-1")))
        at += 1


def arm_pgshift():
    """The kernel's page shift, from the file that defines it for this architecture.

    `mmap`'s length is rounded up to a page by `vm_map_enter`, so the fixture's request is "one page"
    only if the kernel's page is 4096 - and on this target it is: `osfmk/arm/proc_reg.h`'s
    `ARM_PGSHIFT` is 12 for `__arm__`, where the arm64 file's is 14. The length is read from here
    rather than written as `0x1000` so that the fixture's claim is compared with the kernel's own
    definition of a page instead of with a literal that happens to agree with it.
    """
    return hdr_define(os.path.join(XNU, "osfmk/arm/proc_reg.h"), "ARM_PGSHIFT")


def init_pid():
    """The pid the kernel gives the process the RAM disk is exec'd into, from Apple's own source.

    `bsd_utaskbootstrap` clones the init process out of `kernproc` and then holds it by name:
    `initproc = proc_find(1)` (`bsd/kern/bsd_init.c`), with a `panic("bsd_utaskbootstrap: initproc not
    set")` in the same block for the case where it is not there. `load_init_program(p)` in
    `bsd_do_post` execs this Mach-O into that process. The number is read out of the source rather
    than written here because the fixture's `cmp r0, #N` *is* the claim, and a literal in this file
    would be a second definition of it with nothing comparing the two - the shape this project's
    check-everything rule exists for. 478's console measured the same identity from the other side
    (`pid 1 exited -- exit reason namespace 2 subcode 0x4`).
    """
    src = open(os.path.join(XNU, "bsd/kern/bsd_init.c"), encoding="utf-8", errors="replace").read()
    m = re.search(r"initproc\s*=\s*proc_find\(\s*(\d+)\s*\)", src)
    if not m:
        sys.exit("bsd/kern/bsd_init.c no longer has an `initproc = proc_find(N)` line - the pid the "
                 "fixture requires getpid to return cannot be checked against the kernel's own source")
    return int(m.group(1))


def sign24(word):
    """The 24-bit branch offset of an ARM `b`/`bl`, sign-extended and scaled."""
    imm = word & 0xFFFFFF
    if imm & 0x800000:
        imm -= 0x1000000
    return imm * 4


# How many instructions the program at the entry point is. `entry_ramdisk.s` asserts the same length
# in an `.if` over its own labels, so the two are a pair: a program that grew would fail to assemble
# and a program that shrank would fail here.
PROGRAM_WORDS = 79

# Where the two `poll` calls' timeouts are, and where the two calls start. The word numbers are the
# program's own layout - `entry_ramdisk.s`'s listing counts the same offsets - and they are named here
# rather than written into the table below because three separate clauses read them: the expectation
# list, the ratio property, and the mutation that breaks the ratio.
POLL_SHORT_WORD, POLL_LONG_WORD = 24, 29
POLL_CALL_WORDS = (22, 27)          # the first word of each ask: `mov r0, #0`
# 512: the loop is a *park* and these are its five words. `PARK_WORD` is the first (`mov r0, #0`), the
# word `PARK_BACK_WORD` branches to - which is the first and not the ask, because a syscall's return
# writes r0 and a loop that did not reload it would ask with a stale argument. `PARK_MS_WORD` is the
# timeout, and like the two asks' it is read rather than fixed: the property is that it is nonzero.
PARK_WORD, PARK_MS_WORD, PARK_ASK_WORD, PARK_SVC_WORD, PARK_BACK_WORD = 72, 74, 75, 76, 77
FAILED_WORD = 78                    # the `udf #1` every check in the program shares

# The words 504 adds, named for the same reason: `PAGE_WORD` keeps 480's mapping in r9, the two `adr`s
# are the paths the two opens pass, and the read's three words are the call the driver answers.
PAGE_WORD = 21                      # `mov r9, r0` - the page, kept across the calls below
OPEN_PATH_WORD, CONTROL_PATH_WORD = 32, 41   # `adr r0, path_rmd0` / `adr r0, path_missing`
READ_FD_WORD, READ_LEN_WORD, READ_CALL_WORD = 37, 38, 39

# And the words 505 adds. `FORK_CALL_WORD` is the `svc` that returns twice, `CHILD_WORD` is the first
# word of the child's half - which is also the *target* of the branch at `FORK_BRANCH_WORD`, so the
# two are a pair the way `PARK_WORD` and its back-branch are: the parent's `b park` and the child's
# branch are what split one return into two processes, and naming them here is what lets the table
# below say which half each branch goes to instead of saying a number.
FORK_ARG_WORD, FORK_CALL_WORD = 46, 47   # 505's dead word, and the number `2`
FORK_SVC_WORD = 48                       # the `svc` whose single instruction has two continuations
FORK_TEST_WORD, FORK_BRANCH_WORD = 49, 50  # 506's `cmp r1, #CHILD_FLAG` and its `beq entry_child`
PARENT_BRANCH_WORD = 51                  # 508: `b entry_parent` - the half the parent keeps
CHILD_WORD = 52                          # `entry_child`'s first word, and the branch's target
EXIT_ARG_WORD, EXIT_CALL_WORD, EXIT_SVC_WORD = 52, 53, 54

# And the words 508 adds, named for the same reason again: the parent's branch above is the *target*
# `PARENT_WORD` for the first time in this program - until 508 the parent's half was the loop, so the
# branch and the loop's first word were one number - and the two `wait4` blocks are two calls whose
# arguments, answers and checks the table has to be able to name. `WAIT_STATUS_WORD` is the load of
# the word the kernel *wrote through* the pointer the fixture gave, and `WAIT_STATUS_TEST_WORD` is the
# comparison whose immediate is `W_EXITCODE(EXIT_RVAL, 0)`.
PARENT_WORD = 55                         # `entry_parent`'s first word: `mov r1, r9`, the status pointer
WAIT_STATUS_WORD = 62                    # `ldr r3, [r9]` - the answer, read from the page
WAIT_STATUS_TEST_WORD = 63               # `cmp r3, #EXIT_STATUS`
WAIT_PID_TEST_WORD = 60                  # `cmp r0, #WAIT_PID` - the pid the kernel answered with
WAIT_CALL_WORD, WAIT_SVC_WORD = 58, 59   # the first call: `mov r12, #SYS_WAIT4` and its `svc`
WAIT2_ARG_WORD = 65                      # the second call's `mov r0, #WAIT_PID`
WAIT2_CALL_WORD, WAIT2_SVC_WORD, WAIT_BACK_WORD = 69, 70, 71

# The ARM condition codes the program's branches use, by name: `bne`, `bcs` and `b`. The names are what
# the *reading* rests on for one of them - `unix_syscall`'s error convention is the carry bit
# (`arm_prepare_u32_syscall_return`, `bsd/dev/arm/systemcalls.c:279`), so the test after `mmap` has to
# be `bcs` and not `blt`.
COND = {"eq": 0x0, "ne": 0x1, "cs": 0x2, "mi": 0x4, "lt": 0xB, "al": 0xE}


def imm12(word):
    """The 12-bit immediate of an ARM data-processing form, rotated as the encoding says.

    `mov r0, #N` is not always `0xE3A00000 | N`: bits 11:8 are a rotate count, and the value is
    `imm8 ROR (2 * rot)`. Returning the *rotated* value is what makes a `cmp r0, #0x100` written as a
    rotated `1` fail the comparison below instead of passing as the number it stands for.
    """
    imm, rot = word & 0xFF, (word >> 8) & 0xF
    return ((imm >> (2 * rot)) | (imm << (32 - 2 * rot))) & 0xFFFFFFFF if rot else imm, rot


def decode_arm(word, index):
    """The instruction `word` is, as `(mnemonic, operands)`, for the forms `entry_ramdisk.s` uses.

    Not a disassembler: it recognises the shapes the program is written in - `svc`, `udf`, `b`, the
    three `mov` forms, `cmp` against a register and against an immediate, and `ldr`/`str` with a
    zero-offset immediate addressing mode - and returns `("?", (word,))` for anything else. That is
    deliberate: a program word outside those shapes has to fail the comparison below rather than be
    described by a guess, and a *disassembler* here would be a second opinion about ARM that could be
    wrong in the same direction as the fixture.

    Branches are decoded with their target as an **index into the program** rather than a byte
    address, because that is what makes "the failure branch lands on the `udf`" a statement the table
    below can make; a target that is unaligned, negative or past the last word is returned as a string
    so that it compares unequal to every index and the message shows the address the device would have
    jumped to.
    """
    cond = word >> 28

    if word & 0xFFFFFF00 == 0xEF000000:                       # svc #imm24
        return ("svc", (word & 0xFF,))

    if word & 0x0FF00000 == 0x07F00000 and word & 0xF0 == 0xF0:         # udf #imm16
        # `cond 0111 1111 imm12 1111 imm4`, so the 16-bit immediate is `imm12:imm4` - bits 19:8 and
        # bits 3:0 - and the `1111` in between is what tells a `udf` from any other data-processing
        # word. Read as `(word >> 8) & 0xFFF << 4 | word & 0xF`, and not as the top byte of the word:
        # `udf #1` is 0xe7f000f1, whose immediate is in the last nibble.
        return ("udf", ((((word >> 8) & 0xFFF) << 4) | (word & 0xF),))

    if word & 0x0E000000 == 0x0A000000:                       # b (24-bit offset, not bl)
        target = index * 4 + 8 + sign24(word)
        if target % 4 or target < 0 or target >= PROGRAM_WORDS * 4:
            return ("b", (cond, f"0x{target:x} outside the program"))
        return ("b", (cond, target // 4))

    if word & 0x0FF00000 == 0x03A00000:                       # mov rd, #imm
        value, rot = imm12(word)
        return ("mov", ((word >> 12) & 0xF, value, rot))

    if word & 0x0FF00000 == 0x03000000:                       # movw rd, #imm16
        return ("movw", ((word >> 12) & 0xF,
                         (((word >> 4) & 0xF000) | (word & 0xFFF)), 0))

    if word & 0x0FF00000 == 0x03E00000:                       # mvn rd, #imm
        value, rot = imm12(word)
        return ("mvn", ((word >> 12) & 0xF, value, rot))

    if word & 0x0FF00000 == 0x03500000:                       # cmp rn, #imm
        value, rot = imm12(word)
        return ("cmp_i", ((word >> 16) & 0xF, value, rot))

    if word & 0x0FF00FF0 == 0x01500000:                       # cmp rn, rm
        return ("cmp_r", ((word >> 16) & 0xF, word & 0xF))

    if word & 0x0FEF0FF0 == 0x01A00000:                       # mov rd, rm
        # The register form, which 504 is the first step to need: `mov r9, r0` keeps 480's page
        # across the calls below, and `mov r1, r9` hands it to `read` as the buffer. The encoding is a
        # data-processing word with opcode 1101 and **`Rn` zero**, so the mask fixes bits 19:16 to zero
        # as well as the opcode, Rd and Rm - and leaves S and the shift bits out, so `movs rd, rm` or a
        # shifted `mov` is not this shape.
        return ("mov_r", ((word >> 12) & 0xF, word & 0xF))

    if word & 0x0FFF0000 == 0x028F0000 or word & 0x0FFF0000 == 0x024F0000:
        # `adr rd, label`, which the assembler writes as `add rd, pc, #imm` (or `sub` for a target
        # behind it). **This is the one instruction in the program whose operand is an *address in the
        # file***, so it is returned as the signed byte offset the encoding holds and the caller
        # compares it against where the path strings really are - not against a number here.
        value, rot = imm12(word)
        return ("adr", ((word >> 12) & 0xF, value, rot,
                        1 if word & 0x0FFF0000 == 0x028F0000 else -1))

    if word & 0x0FB00000 == 0x05900000 or word & 0x0FB00000 == 0x05800000:
        # `ldr`/`str rd, [rn, #imm12]`: P=1, U=1, B=0, W=0, L = bit 20. Bit 25 (register offset) is
        # excluded by the mask, so a register-addressed load is not this shape.
        return ("ldr" if word & 0x00100000 else "str",
                ((word >> 12) & 0xF, (word >> 16) & 0xF, word & 0xFFF))

    return ("?", (word,))


def describe(instruction):
    """A decoded instruction as one line, for a failure message."""
    mnemonic, operands = instruction

    def num(value):
        # An operand is an int for every instruction this decoder recognises, and a decoded *word*
        # for the one it does not - so the formatter has to survive a value it cannot print as a
        # number, or a failure message about an unrecognised word would raise instead of reporting.
        return f"#{value:#x}" if isinstance(value, int) else f"#{value!r}"

    if mnemonic == "?":
        return f"the word 0x{operands[0]:08x}"
    if mnemonic in ("svc", "udf"):
        return f"{mnemonic} {num(operands[0])}"
    if mnemonic == "b":
        names = {0x0: "eq", 0x1: "ne", 0x2: "cs", 0x4: "mi", 0xB: "lt", 0xE: ""}
        target = operands[1] if isinstance(operands[1], str) else f"word {operands[1]}"
        return f"b{names.get(operands[0], '?')} to {target}"
    if mnemonic in ("mov", "movw", "mvn"):
        return f"{mnemonic} r{operands[0]}, {num(operands[1])}"
    if mnemonic == "mov_r":
        return f"mov r{operands[0]}, r{operands[1]}"
    if mnemonic == "adr":
        # The target as the instruction encodes it: `pc` is the instruction's address plus eight on
        # this architecture, so the immediate is a signed offset from this word to the string. The
        # expected value can be `None` - `check_device_paths` leaves it out when it has already failed
        # to find the string an `adr` has to point at - and this has to survive printing that, or the
        # failure message about a wrong target would raise instead of naming it.
        rd, value, _rot, sign = operands
        if not isinstance(value, int):
            return f"adr r{rd}, an offset into this file (not computed: see the failure above)"
        return (f"adr r{rd}, pc{'+' if sign > 0 else '-'}{value:#x} (the target's offset from "
                f"this word)")
    if mnemonic == "cmp_i":
        return f"cmp r{operands[0]}, {num(operands[1])}"
    if mnemonic == "cmp_r":
        return f"cmp r{operands[0]}, r{operands[1]}"
    if mnemonic in ("ldr", "str"):
        return f"{mnemonic} r{operands[0]}, [r{operands[1]}, #{operands[2]:#x}]"
    return f"{mnemonic} {operands}"


def program_expectations(decoded, K, adr_targets):
    """What each of the program's 78 words must decode to, in the order they are loaded.

    The values come from the headers and from Apple's source (`K`), never from a literal here: the pids
    from `bsd_init.c`'s `initproc = proc_find(N)`, the syscall numbers from `syscalls.master`, the
    page length from the kernel's own `ARM_PGSHIFT`, and `prot`/`flags` from `bsd/sys/mman.h`'s
    `PROT_READ|PROT_WRITE` and `MAP_PRIVATE|MAP_ANON`. **Four words are deliberately not fixed as
    values** - see the marker, the two timeouts and the read's length below - because the properties
    they have to have are properties and not numbers in a header. `adr_targets` is the exception to the
    exception: the two `adr`s' operands are *offsets into this file*, so their expected values are
    computed by `check_program` from where the path strings really are and passed in here, rather than
    being left free - a free operand would make the two most important addresses in this program the
    only two the check does not compare.

    Word by word, and what a wrong value there would cost on the device:

      - 0..2 ask `getpid` and check the answer, which is 479's program unchanged: the `udf` behind the
        compare is what makes a wrong answer loud, and 478's run is what a `udf` here costs.
      - 3..11 are `mmap`'s six arguments in the registers the armv7k munger reads - `addr`, `len`,
        `prot`, `flags`, `fd`, and the 64-bit `pos` as the pair `r6`/`r8` - with `r5` in the word of
        `struct mmap_args` that is padding (see the fixture's header). `fd` is -1 because `MAP_ANON`
        needs no vnode, and `addr` is 0 so the kernel chooses.
      - 12..13 make the call and test **the carry**: `unix_syscall`'s error convention is
        `regs->cpsr |= PSR_CF`, so a `cmp`/`blt` here would read the same flags it just destroyed and
        a failed `mmap` would be taken for a valid address.
      - 14..20 are the two accesses that are the point of that step: a load of a page nothing has
        touched, then a store to a page the load mapped read-only, each checked afterwards. The `str`
        writes the address into the address it names, so 18..19 are the whole proof that the page is
        the process's own and writable.
      - **21 is 504's first word and it is a register move, not a call**: `mov r9, r0` keeps that page
        for the read at 37, and the register is r9 because the return path of a syscall whose return is
        64-bit - `read` is - writes `save_r1` as well as `save_r0`.
      - **22..31 are 503's two asks**, and they are the only two places in this program where a
        register is loaded from something that is not a header: `fds` and `nfds` are zero because a
        zero-descriptor `poll` is a pure deadline, and the `timeout` is a *ratio* (checked below).
        25 and 30 carry the syscall number in r12 - the same register and the same convention as the
        other calls - and 23 and 28 are the `mov r1, #0` that make it a sleep rather than a wait on a
        descriptor, which is the one argument in the program whose *value* is the whole semantic.
      - **32..45 are 504's two opens with the read between them.** 32 and 41 are `adr`s whose targets
        are checked against the bytes of the two path strings below (that is the one clause in this
        function that is about a *file offset* rather than a value); 33/34 and 42/43 are the flags and
        the mode, both zero, and zero is `O_RDONLY` read out of `bsd/sys/fcntl.h`; 35 and 44 are the
        syscall number, which is the same 5 in both; and 37..40 are the read - the buffer is the page
        in r9, the count is a *bound* (checked below), and the call is number 3.
      - **46..54 are 505's fork and the child's exit.** 46 and 47 are the two words either side of the
        one call in this program that returns twice: 47 is the syscall number - 2, and a slot whose
        argument munger is NULL because `fork` takes no argument - and 46 is a word `fork` never reads
        on this path, which is checked here precisely because 505's run measured that nothing else reads
        it either (the child's `r0` is the pid, and the run's `xnu_live_fork_uap0` is the word the
        argument buffer kept). 49..51 are the split: `cmp r1, #CHILD_FLAG` - the register 506 switched
        to, because `thread_set_child` writes 1 there for the child and 0 is the parent's - and the two
        branches whose targets are checked as word indices, with 50 landing on `entry_child` (word 52)
        and 51 on the parent's half, so a program with the halves exchanged fails here rather than
        passing on matching numbers. (Until 508 that second target was `PARK_WORD`: the parent's half
        *was* the loop, and the branch and the loop's first word were one number. 508 is the step that
        gave the parent something to do before it goes back - and 512 is the step that turned that loop
        from a `getpid` spin into a park, which is why the word is `PARK_WORD` now and not `SPIN_WORD`.)
        52..54 are the child's three words, whose
        last does not return.
      - **55..71 are 508's two `wait4`s and the two answers between them.** 55..58 are the first call's
        arguments - the status pointer is 504's page in r9, `options` and `rusage` are both zero, and
        zero is load-bearing for the last of them: `wait4_nocancel` reads the argument struct's word at
        offset 12 and a stale pointer there would make it `copyout` a `rusage` to an address this
        process does not own. 59..61 are the call, the pid it is answered with and the branch that
        refuses a different one; 62..64 are **the reading** - a load of the word the kernel wrote
        through the pointer, compared with `W_EXITCODE(EXIT_RVAL, 0)` - and 65..70 are the same call
        again on the same pid, whose answer (`ECHILD`) is deliberately not tested: word 71 is the
        unconditional `b` into the park and nothing between 70 and the failure marker is allowed to be a
        conditional branch, which is a property `check_program` states as an absence below.
      - 72..78 are **512's park and the failure marker**: `poll(NULL, 0, PARK_MS)` with its three
        arguments reloaded on every turn, an unconditional branch back to the first of them, and the
        `udf #1` behind it. The `b` at 71 lands on 72, so 508's parent goes straight from its second
        `wait4` into the park - and from 479 until this step those five words were a `getpid` loop,
        which is why every run before this one ended with the CPU busy and the kernel never idle.
    """
    # The one value this check does not fix: the word the program puts in r5. It has to be a marker -
    # nonzero, and different from every argument the program loads - because its whole job is to be
    # recognisable in the word of `struct mmap_args` that `mmap` never reads, and "recognisable" is a
    # property of the program rather than a number in a header. The properties are checked below; the
    # shape (a `movw` into r5) is checked here. `None` when word 8 is not that shape at all, which is
    # what makes the `movw` claim a comparison instead of an assumption - the failure message then
    # reads `movw r5, #None` beside the word that is really there.
    word8 = decoded[8]
    marker = word8[1][1] if (word8[0] == "movw" and len(word8[1]) == 3 and
                             isinstance(word8[1][1], int)) else None
    # And the second: the two timeouts. They are read out of the instruction stream by
    # `poll_timeouts` for the same reason - what this file can assert about them is a relation and not
    # a value - and they are `None` here when the word at that offset is not the `movw r2, #imm16` the
    # program's shape requires, so that a `poll` whose timeout moved to another register fails the
    # comparison here rather than passing because the number is right.
    #
    # **The relation is order, not the words' own 8:1 ratio.** The words are 5 and 40 ms, and from 503
    # until 512's host check this line said their "property is an 8:1 ratio", which no run has ever
    # produced: what the wrapper publishes is the kernel's tick count across the call, and that count is
    # the deadline *plus* the wake, so the ratio the device reports is diluted by a per-wake cost that
    # does not scale with the ask. The two runs measured are 160755 : 898180 (5.6:1) and 361154 :
    # 1060380 (2.9:1), against the words' 8:1. So the clause below checks the only thing that survives
    # both runs - the second ask is strictly longer than the first - and the ratio itself is a reading of
    # the run, not a property of this file.
    short_ms, long_ms = poll_timeouts(decoded)
    # And the third: 504's read length, read the same way for the same reason - the property is a
    # bound (`check_program`'s) and the number is the fixture's.
    read_bytes = read_length(decoded)
    # And the fourth: 512's park timeout, read the same way again. Its property is a *bound* too - the
    # process has to still be parked when the watchdog fires, and the loop has to re-park if the
    # timeout ever expires - so the value is the fixture's and the clause is `check_program`'s.
    park_ms = park_timeout(decoded)

    return [
        (0, ("svc", (0x80,))),
        (1, ("cmp_i", (0, K["INIT_PID"], 0))),
        (2, ("b", (COND["ne"], FAILED_WORD))),
        (3, ("mov", (0, 0, 0))),
        (4, ("movw", (1, K["MMAP_LENGTH"], 0))),
        (5, ("mov", (2, K["MMAP_PROT"], 0))),
        (6, ("movw", (3, K["MMAP_FLAGS"], 0))),
        (7, ("mvn", (4, 0, 0))),
        (8, ("movw", (5, marker, 0))),
        (9, ("mov", (6, 0, 0))),
        (10, ("mov", (8, 0, 0))),
        (11, ("mov", (12, K["SYSCALL_MMAP"], 0))),
        (12, ("svc", (0x80,))),
        (13, ("b", (COND["cs"], FAILED_WORD))),
        (14, ("ldr", (3, 0, 0))),
        (15, ("cmp_i", (3, 0, 0))),
        (16, ("b", (COND["ne"], FAILED_WORD))),
        (17, ("str", (0, 0, 0))),
        (18, ("ldr", (1, 0, 0))),
        (19, ("cmp_r", (1, 0))),
        (20, ("b", (COND["ne"], FAILED_WORD))),
        # 504's first word: the page `mmap` returned, kept in r9 for the read below. It is *not*
        # checked against a value - nothing in a header says where the kernel will put a mapping - it is
        # checked as the move it is, and the run's `xnu_live_read_buf` is the number it held.
        (PAGE_WORD, ("mov_r", (9, 0))),
        # 503's first ask: `poll(NULL, 0, short)`. `fds` and `nfds` are both `mov r_, #0` and not a
        # `movw`, which is not a stylistic choice - a `movw r1, #0` would decode as a different shape
        # and fail here, so "nfds is zero" is stated as the instruction that writes zero.
        (22, ("mov", (0, 0, 0))),
        (23, ("mov", (1, 0, 0))),
        (24, ("movw", (2, short_ms, 0))),
        (25, ("mov", (12, K["SYSCALL_POLL"], 0))),
        (26, ("svc", (0x80,))),
        (27, ("mov", (0, 0, 0))),
        (28, ("mov", (1, 0, 0))),
        (29, ("movw", (2, long_ms, 0))),
        (30, ("mov", (12, K["SYSCALL_POLL"], 0))),
        (31, ("svc", (0x80,))),
        # 504's first open. The `adr` is checked for its shape here and for its *target* in
        # `check_program`, because its operand is an offset into this file and not a value in a header.
        (OPEN_PATH_WORD, ("adr", (0, adr_targets.get(OPEN_PATH_WORD), 0, 1))),
        (33, ("mov", (1, K["OPEN_RDONLY"], 0))),
        (34, ("mov", (2, 0, 0))),
        (35, ("mov", (12, K["SYSCALL_OPEN"], 0))),
        (36, ("svc", (0x80,))),
        # And the read: the buffer is the page in r9, the count is the fixture's own bound, and the
        # call is `read` - three words the munger copies from r0..r2, exactly as `poll`'s are.
        (READ_FD_WORD, ("mov_r", (1, 9))),
        (READ_LEN_WORD, ("mov", (2, read_bytes, 0))),
        (READ_CALL_WORD, ("mov", (12, K["SYSCALL_READ"], 0))),
        (40, ("svc", (0x80,))),
        # The control: the same three arguments and the same call, on a path no driver can have made a
        # node for. It is called *after* the read, and deliberately not before: the fd the read uses is
        # the first open's, so a control that overwrote r0 first would move the read onto its errno.
        (CONTROL_PATH_WORD, ("adr", (0, adr_targets.get(CONTROL_PATH_WORD), 0, 1))),
        (42, ("mov", (1, K["OPEN_RDONLY"], 0))),
        (43, ("mov", (2, 0, 0))),
        (44, ("mov", (12, K["SYSCALL_OPEN"], 0))),
        (45, ("svc", (0x80,))),
        # 505's ask that makes a second process. 46 is a word the kernel **does not read on this call
        # at all** - `fork`'s sysent slot is `{ int fork(void) }`, so its `sy_narg` is 0 and its munger
        # word is NULL, and `unix_syscall` marshals nothing - and that is exactly why the value is
        # checked here: it is not an argument to `fork`, and 505's run measured the other half of the
        # claim too (it is not either process's return value either: `xnu_live_fork_uap0` names the
        # word the argument buffer kept, and both processes' `r0` are written by the kernel). Leaving
        # it out of this table would make the one word nothing reads the one word nothing compares.
        (FORK_ARG_WORD, ("mov", (0, K["FORK_ARG_VALUE"], 0))),
        (FORK_CALL_WORD, ("mov", (12, K["SYSCALL_FORK"], 0))),
        (FORK_SVC_WORD, ("svc", (0x80,))),
        # The test and the two branches: `cmp r1, #CHILD_FLAG`, `beq entry_child`, `b entry_parent`.
        # **The register and the number are the step's subject and both are checked as facts about the
        # instruction**, because 505's run is what a wrong pair costs: with `cmp r0, #0` / `beq`, the
        # child - whose `r0` is the pid too - took the parent's arm and died on the program's own
        # `udf #1`. 506's *first* run inverted the condition instead (`cmp r1, #CHILD_FLAG` / `bne`,
        # which is true of the parent) and `initproc` took the child's arm: the device measured that
        # as `xnu_live_exit_pid` = 1, a panic, and `pid 1 exited` on the console. **The table alone
        # could not refuse it, which is why `check_fork_branch_sense` below reads the immediate and
        # the condition as one claim about the child's flag rather than as two checked fields.** The
        # child's branch is checked as a *target* as well - word `CHILD_WORD`, which is where
        # `entry_child` is - because the other failure this pair can have is the two halves swapped,
        # and a table of numbers would accept a program that gave the parent the exit and the child
        # the loop. The parent's target is `PARENT_WORD` since 508, and *where* it lands is a clause
        # of its own below for the same reason: until this step it was the loop, and a program that
        # left it there would still satisfy every row of this table.
        (FORK_TEST_WORD, ("cmp_i", (1, K["CHILD_FLAG"], 0))),
        (FORK_BRANCH_WORD, ("b", (COND["eq"], CHILD_WORD))),
        (PARENT_BRANCH_WORD, ("b", (COND["al"], PARENT_WORD))),
        # And the child's half: three words, the last of which is the only `svc` in this program that
        # does not return to the instruction after it.
        (EXIT_ARG_WORD, ("mov", (0, K["EXIT_RVAL"], 0))),
        (EXIT_CALL_WORD, ("mov", (12, K["SYSCALL_EXIT"], 0))),
        (EXIT_SVC_WORD, ("svc", (0x80,))),
        # **508's two `wait4`s and the two answers between them.** The first three words are the
        # arguments: the status pointer is the page `mmap` gave this process and the read filled (the
        # same `r9` 504 kept it in, which is why the load at `WAIT_STATUS_WORD` is about the *page* and
        # not about a register the kernel returned), `options` is 0 - not `WNOHANG`, because the child
        # is a zombie and the call is supposed to return rather than to park - and `rusage` is 0, which
        # is the one argument no run can show being read: a stale word there would make
        # `wait4_nocancel` copy a `struct user32_rusage` to an address this process does not own, and
        # the EFAULT would arrive as `_error` rather than as a fault. `WAIT_STATUS_TEST_WORD` is the
        # step's whole reading in one instruction: a load of the word the kernel *wrote through* the
        # pointer, compared with the number 506 derived from `W_EXITCODE` and the fixture's own 3.
        (PARENT_WORD, ("mov_r", (1, 9))),
        (56, ("mov", (2, 0, 0))),
        (57, ("mov", (3, 0, 0))),
        (WAIT_CALL_WORD, ("mov", (12, K["SYSCALL_WAIT4"], 0))),
        (WAIT_SVC_WORD, ("svc", (0x80,))),
        (WAIT_PID_TEST_WORD, ("cmp_i", (0, K["WAIT_PID"], 0))),
        (61, ("b", (COND["ne"], FAILED_WORD))),
        (WAIT_STATUS_WORD, ("ldr", (3, 9, 0))),
        # **The step's subject in one instruction**, and the `rot` in the tuple is not decoration:
        # 0x300 is an eight-bit immediate rotated by twelve, so a `cmp r3, #0x300` written as any
        # other encoding of the same number fails here - which is the defect this check was built with
        # in 205 (`imm12` returns the rotated value *and* the count for exactly this row).
        (WAIT_STATUS_TEST_WORD, ("cmp_i", (3, K["EXIT_STATUS"], 12))),
        (64, ("b", (COND["ne"], FAILED_WORD))),
        # And the second call: the same four arguments and the same number, on the pid of a child that
        # has been reaped. It is followed by a `b` to the loop and by **no comparison at all**, which
        # is the one place in this program where an answer is deliberately not branched on - see the
        # fixture's header: `ECHILD` is the expected answer, and a `udf #1` here would kill `initproc`
        # for a fact about the reap path rather than about this call.
        (WAIT2_ARG_WORD, ("mov", (0, K["WAIT_PID"], 0))),
        (66, ("mov_r", (1, 9))),
        (67, ("mov", (2, 0, 0))),
        (68, ("mov", (3, 0, 0))),
        (WAIT2_CALL_WORD, ("mov", (12, K["SYSCALL_WAIT4"], 0))),
        (WAIT2_SVC_WORD, ("svc", (0x80,))),
        (WAIT_BACK_WORD, ("b", (COND["al"], PARK_WORD))),
        # **512's park, and the three arguments are written out rather than inherited.** `poll(NULL, 0,
        # PARK_MS)` is the same call the two asks above make; what makes this one different is that it
        # is a *loop*, so every turn has to be the call the listing says. A syscall's return writes r0,
        # so a loop that branched back to `PARK_ASK_WORD` instead of `PARK_WORD` would ask
        # `poll(0, 0, PARK_MS)` - the same call by luck - and the same mistake after any other call
        # would ask something else entirely. The back-branch's target is therefore checked as
        # `PARK_WORD` and not as a number (see the clause in `check_program`), and the timeout is read
        # out of the instruction like the two asks' (`park_timeout`) because its property is a bound.
        (PARK_WORD, ("mov", (0, 0, 0))),
        (73, ("mov", (1, 0, 0))),
        (PARK_MS_WORD, ("movw", (2, park_ms, 0))),
        (PARK_ASK_WORD, ("mov", (12, K["SYSCALL_POLL"], 0))),
        (PARK_SVC_WORD, ("svc", (0x80,))),
        # And the loop closes on itself: the only branch in the park goes back to its first word, and
        # **nothing tests the answer** - 503's rule, and the reason is that a `poll` that came back with
        # an errno is a reading while a `udf #1` here kills `initproc`.
        (PARK_BACK_WORD, ("b", (COND["al"], PARK_WORD))),
        (FAILED_WORD, ("udf", (1,))),
    ]


def check_device_paths(blob, fpc, decoded, K, p):
    """504's two paths: that the file holds exactly the two strings the two `adr`s point at, and that
    the first of them is the character device `mdevadd` made a node for.

    **This is the clause that ties three files together, and it is the reason the fixture's addresses
    are `adr`s rather than literals.** The path the program opens is built from Apple's own source in
    two places - the mount point devfs is mounted at (`bsd/kern/bsd_init.c`) and the format string the
    node was made with (`bsd/dev/memdev.c`) - and the *address* of that string is computed once, by the
    assembler, from the string's position in this file. So the chain is: the driver's own format
    string says the node is called `rmd0`, `devfs_kernel_mount("/dev")` says where it lives, the string
    in this object is that concatenation, the `adr` at `OPEN_PATH_WORD` points at it, and the run's
    `xnu_live_open_path` is the address the kernel received. Any link being a literal in this file
    would make the check agree with itself.

    The **control** is the other string, and what is checked about it is what can be checked statically:
    that it is a `/dev/` path, that it is not the character device's, and that it is not the *block*
    device's either - `mdevadd` makes both, and a fixture that opened the block one would take a
    different read path (`spec_read` into the buffer cache rather than `mdevrw`'s own copy), which is
    the kind of difference that shows up as a different number in a key rather than as a failure. What
    cannot be checked here is that no *other* driver in this image has a node by that name; that is
    what the run's `ENOENT` is for, and the docstring says so rather than implying the file proves it.

    Returns the two `adr` operands' expected values, as offsets from the words that encode them -
    `pc` is the instruction's address plus eight - so that `program_expectations` can compare them
    like every other word. A path that could not be found, or an `adr` that is not one, leaves the
    entry out: the failure is recorded here and the comparison then fails with `#None` beside the word.
    """
    found = dev_path_strings(blob, K)
    want = K["DEV_MOUNT"] + "/" + K["CHAR_DEV_NAME"]
    block = K["DEV_MOUNT"] + "/" + K["BLOCK_DEV_NAME"]
    if len(found) != 2:
        fail(f"{p}the RAM disk holds {len(found)} `/dev/`-prefixed strings ({found}) and this step's "
             f"program has exactly two paths - the open of the character device `mdevadd` made and the "
             f"control. A third string would be a path some future step wrote and nothing points at")
        return {}

    expect_offsets = {}
    real = [off for off, text in found if text == want]
    if len(real) != 1:
        fail(f"{p}none of the file's `/dev/` strings is `{want}`, which is what `memdev.c`'s character "
             f"device format string makes for the device the boot added (`{found}`): the fixture is "
             f"opening either the block device - whose `read` is a `spec_read` into the buffer cache "
             f"rather than `mdevrw`'s own `uiomove64` - or a name no driver in this image can have "
             f"made a node for")
        return {}
    expect_offsets[OPEN_PATH_WORD] = real[0]
    for off, text in found:
        if text != want:
            if text == block:
                fail(f"{p}the control path `{text}` is the *block* device's name, which `mdevadd` "
                     f"makes a node for too: a control that resolves is not a control, and the run's "
                     f"two `xnu_live_open_error` words would not separate a working lookup from an "
                     f"open that always answers")
            if not text.startswith(K["DEV_MOUNT"] + "/"):
                fail(f"{p}the control path `{text}` is not under `{K['DEV_MOUNT']}`, where devfs is "
                     f"mounted, so it is not a name this namespace could hold in any case")
            expect_offsets[CONTROL_PATH_WORD] = off

    out = {}
    for index in (OPEN_PATH_WORD, CONTROL_PATH_WORD):
        ins = decoded[index]
        if ins[0] != "adr" or len(ins[1]) != 4 or ins[1][0] != 0 or ins[1][2] != 0:
            fail(f"{p}word {index} is {describe(ins)}, and both of this step's paths have to be "
                 f"`adr r0, <label>`: the instruction's own target is the address the string is at, "
                 f"and an address written as a number here would be a second definition of it")
            continue
        target = fpc + index * 4 + 8 + ins[1][3] * ins[1][1]
        if index not in expect_offsets:
            continue
        if target != expect_offsets[index]:
            fail(f"{p}the `adr` at word {index} points at file offset 0x{target:x} and the string it "
                 f"has to name is at 0x{expect_offsets[index]:x}: the address the device hands `open` "
                 f"is not this file's own bytes for that path")
            continue
        out[index] = expect_offsets[index] - (fpc + index * 4 + 8)
    return out


def check_program(blob, fpc, pc, reg, K):
    """The 79 words of `entry_ramdisk.s`'s program, decoded against what they are for.

    This is the assertion experiment 468 wrote for one word, applied to the program 479 replaced it
    with and 480, 503, 504, 505, 506, 508 and 512 grew: the one-word version asked "is the entry point
    a `udf #0`", which measured only that the user's mapping was where the file said it was. Every word
    of a program this size is a way to be wrong - a `cmp` against the wrong register or the wrong pid,
    a branch that lands one instruction away, a syscall number in the wrong register, an argument in
    the wrong register, the sign convention instead of the carry - and every one of them is a *silent*
    difference whose only symptom on the device would be a process that runs when it should have
    stopped, or an init death where 478 already had one.

    Nine of the program's properties are not word-for-word comparisons and are checked here because
    no single word holds them: the marker's (that word 8 is nonzero and unlike every argument), the
    branch targets' (that each lands on another instruction of the program), 503's ratio (that word 29
    is larger than word 24 and neither is zero), 504's two paths (`check_device_paths`: that the file
    holds exactly the two strings the two `adr`s point at, and that the first is the character device
    the kernel's own `devfs_make_node` call names), 504's read length (that it is at least the word
    the wrapper publishes and no longer than the page the buffer is in), **512's park** (that its
    timeout is nonzero, that the loop closes on the park's *own first word* so every turn reloads its
    arguments, and that nothing in it tests the call's answer), **505's split** (that the
    fork, the child's half, 508's wait and the loop are in that order, and that the two branches out of
    the one `svc` land on different halves - the one relation in this function that is about the
    program's *shape*, because a per-word table is blind to a program whose fork block was moved inside
    the loop with every constant renumbered to match), **the sense of the fork's comparison** (that
    the immediate and the condition are one claim about the child's flag - the clause 506 added, and
    the one that could have refused its first run), **where the parent's branch lands** (508's half of
    that clause: a program whose parent branch went to the loop would satisfy every row above and never
    call `wait4`, which is a *missing* reading rather than a wrong one), and **the absence after the
    second `wait4`** (508's: no conditional branch between that `svc` and the loop, because `ECHILD` is
    an answer this step wants recorded and not faulted on).
    The
    ordering of the two asks is the reading 503 exists for - a wake whose length does not follow the
    ask that produced it is a latency and not a deadline - and the pair of paths is the reading 504
    exists for, and both have to be properties a mutation can break rather than numbers compared with a
    header. What the ordering clause deliberately does *not* check is the ratio: see the note in
    `program_expectations` for the two runs that measured it at 5.6:1 and 2.9:1 against the words' 8:1.
    """
    p = f"{pc:#x}: "
    words = [u32(blob, fpc + i * 4) for i in range(PROGRAM_WORDS)]
    decoded = [decode_arm(word, i) for i, word in enumerate(words)]

    adr_targets = check_device_paths(blob, fpc, decoded, K, p)

    for index, expect in program_expectations(decoded, K, adr_targets):
        if decoded[index] != expect:
            fail(f"{p}word {index} at 0x{pc + index * 4:x} is {describe(decoded[index])}, and this "
                 f"check requires {describe(expect)} - the program the device runs is not the one "
                 f"`entry_ramdisk.s` describes, and nothing else in this build would say so")

    # **The marker's properties, which are the reason word 8 is not fixed above.** Two things make the
    # word in r5 a *reading* rather than a value: it must be nonzero (a zero there is
    # indistinguishable from a register the program never set, which is the defect this project has
    # found in stand-ins three times) and it must differ from every argument the program loads (a
    # marker equal to `len`, `prot`, `flags` or `fd` would make the log's word ambiguous exactly where
    # it is supposed to be unambiguous).
    marker = decoded[8][1][1] if decoded[8][0] == "movw" else None
    arguments = {index: decoded[index][1][1]
                 for index in (4, 5, 6, 7, 9, 10) if decoded[index][0] in ("mov", "movw")}
    if marker == 0:
        fail(f"{p}the word the program puts in r5 is zero, and a zero is what a register nothing "
             f"wrote holds: the marker would stop being a marker, and the log's word of "
             f"`struct mmap_args` would read the same whether the munger copied r5 or nothing")
    for index, value in arguments.items():
        if value == marker:
            fail(f"{p}the word in r5 ({marker:#x}) is also the argument loaded by word {index} "
                 f"({value:#x}), so the marker cannot be told apart from a real argument in the "
                 f"munger's output - which is the only place it is read")

    # **And 503's ratio, which is the whole reason there are two asks.** `entry_trace.c`'s wrapper times
    # both calls with the kernel's own counter, so what the run publishes is a pair of tick counts that
    # the *user* program chose the ratio of. Nothing on this machine has ever produced a user-visible
    # interval before, so there is no baseline for the pair and the only thing that can be asserted in
    # the file is the relation between them. A `poll` whose two timeouts were equal would publish a
    # ratio of about 1 either way and would read identically whether the wake came from the deadline or
    # from some fixed latency; a zero timeout would not block at all (`kqueue_scan` would never arm a
    # wait timer), so it would publish a tick count that says nothing about the timer path while
    # looking like a reading of it. Both are mutations `--selftest` makes.
    short_ms, long_ms = poll_timeouts(decoded)
    read_bytes = read_length(decoded)
    park_ms = park_timeout(decoded)
    if short_ms is not None and long_ms is not None:
        if min(short_ms, long_ms) == 0:
            fail(f"{p}one of the two `poll` timeouts is 0 ({short_ms} and {long_ms}): a zero timeout "
                 f"makes `kqueue_scan` return without arming the thread's wait timer, so the pair "
                 f"would record two calls that never blocked and the timer path would be untested")
        if long_ms <= short_ms:
            fail(f"{p}the second `poll` asks for {long_ms} ms and the first {short_ms} ms: the first "
                 f"ask has to be the shorter one, because the reading these two calls produce is their "
                 f"*ratio* and a wake that did not come from the countdown would give the same one for "
                 f"both. An equal or reversed pair cannot separate a deadline from a latency")

    # Every branch lands inside the program, stated once for all of them: a branch out would leave the
    # user's `__TEXT` for unmapped memory and fault, which the run would show as an abort rather than
    # as a syscall - and the `b` at word `WAIT_BACK_WORD` has to come back to the park's first word, so
    # that 508's parent goes from its second `wait4` straight into the park rather than into anything
    # else.
    for index, instruction in enumerate(decoded):
        if instruction[0] == "b" and not isinstance(instruction[1][1], int):
            fail(f"{p}the branch at word {index} (0x{pc + index * 4:x}) targets "
                 f"{instruction[1][1]} - every branch in this program has to land on another "
                 f"instruction of it")

    # **And 504's read length, which is a bound and not a value.** The word the wrapper publishes is
    # read out of the buffer *after* the call, so a count below four would leave the word it publishes
    # partly old and partly new - a number in the log that is neither what was there nor what the
    # driver wrote, which is the worst kind of reading: one that looks like data. The upper bound is the
    # mapping: the buffer is the *start* of a page `mmap` gave this process and nothing here has mapped
    # anything past it, so a longer read is `mdevrw`'s `uiomove64` writing into the page after the
    # mapping ends - a kernel-side copyout that faults, in the one process whose death panics the boot.
    if read_bytes is not None:
        if read_bytes < 4:
            fail(f"{p}the read asks for {read_bytes} bytes, and the wrapper publishes the buffer's "
                 f"first word: a count below four leaves that word part old and part new, so the log's "
                 f"`xnu_live_read_word_after` would be a mixture rather than what the driver wrote")
        if read_bytes > K["MMAP_LENGTH"]:
            fail(f"{p}the read asks for {read_bytes} bytes and the buffer is one {K['MMAP_LENGTH']:#x}"
                 f"-byte mapping: everything past it is unmapped, so the driver's copy would run off "
                 f"the end of this process's own page and fault in the middle of a copyout")

    # **And 512's park, which is three claims about a loop rather than about a word.** The table pins
    # its five instructions, and a program can satisfy every one of those rows and still never park the
    # process: the timeout could be zero, which makes `poll` return at once (so the loop would be a spin
    # again, with the kernel never idle - the very state this step exists to leave), or the back-branch
    # could go to the ask word instead of the park's first word, which is the same spin with a reloaded
    # syscall number, or a test could be added on the call's answer, which turns a runtime fact about
    # the timer path into `udf #1` and kills `initproc`.
    if park_ms is not None:
        if park_ms == 0:
            fail(f"{p}512's park asks for a {park_ms} ms timeout: `poll` with a zero timeout returns "
                 f"without arming a wait timer, so the loop would be a spin with the same shape as the "
                 f"park - the kernel would have something runnable forever and `machine_idle` would "
                 f"never be called, which is the state every run before this step was in")
    else:
        # Not a `movw r2, #imm16`, which `program_expectations` has already refused word by word - this
        # branch exists so that the two clauses below are not reached with a value that is not a number.
        pass
    if park_ms is not None and long_ms is not None and short_ms is not None:
        threshold = park_threshold()
        if not (short_ms < threshold and long_ms < threshold):
            fail(f"{p}the program's two asks ask for {short_ms} ms and {long_ms} ms and the park's "
                 f"console line is printed from the wrapper only when a timeout is at least "
                 f"{threshold} ms (`ENTRY_PARK_MIN_MS` in `entry_trace.c`): an ask at or above that "
                 f"number would be named as the park, and the durable artifact would say the OS had "
                 f"nothing to run when it was only waiting on a deadline this program asked for")
        if park_ms < threshold:
            fail(f"{p}512's park asks for {park_ms} ms and the wrapper's threshold is {threshold} "
                 f"(`ENTRY_PARK_MIN_MS` in `entry_trace.c`): the park is the call this step exists for, "
                 f"and a park below the threshold is one whose return prints nothing - the run would be "
                 f"up to the reader to notice, and the console line this step adds would be silently "
                 f"absent from every log it produces")

    if decoded[PARK_BACK_WORD][0] == "b" and isinstance(decoded[PARK_BACK_WORD][1][1], int):
        target = decoded[PARK_BACK_WORD][1][1]
        if target != PARK_WORD:
            fail(f"{p}512's park branches back to word {target} and its first word is "
                 f"{PARK_WORD}: every turn has to re-write all three arguments, because a syscall's "
                 f"return writes r0 - a loop that went back to the ask (`mov r12, #SYS_POLL`) would ask "
                 f"`poll(0, 0, {park_ms})`, which is this call only by luck, and the same edit after "
                 f"any other call would ask something else entirely")

    # **And 505's split, which is the one property of this program no per-word row above can state:
    # the *order* of the three parts.** The table pins every word of the fork block, the child's half
    # and the loop, but each row is about one word at one offset - and the same program with the fork
    # block moved *inside* the loop, every constant above renumbered to match, would satisfy every row
    # and be wrong in the worst way this step has: the process would fork again on every turn of the
    # loop, so the log would fill with `xnu_live_fork_seq` and the `exit` at `CHILD_WORD` would never
    # be reached at all. So the three parts have to be in the order the design needs - the fork, then
    # the child's half, then 508's wait, then the loop - which is a relation between the constants and
    # not a fact about any word. **No `--selftest` mutation constructs its violation, and that is not an
    # oversight:** a mutation changes one word, and this failure is a reordering, so the only way to
    # reach it is to edit the program and the constants together - which is exactly the edit this
    # clause is here to refuse.
    if not (FORK_SVC_WORD < CHILD_WORD <= EXIT_SVC_WORD < PARENT_WORD < WAIT2_SVC_WORD
            < PARK_WORD < FAILED_WORD):
        fail(f"{p}the program's parts are out of order: the `fork`'s `svc` is word "
             f"{FORK_SVC_WORD}, the child's half words {CHILD_WORD}..{EXIT_SVC_WORD}, 508's wait words "
             f"{PARENT_WORD}..{WAIT2_SVC_WORD}, and the loop words {PARK_WORD}..{FAILED_WORD}. The "
             f"fork has to come before the child's half, the child's half before the parent's wait, "
             f"and the wait before the loop - a loop the fork can be reached from would fork on every "
             f"turn, a wait inside the child's half would be a call the dying process makes, and a "
             f"wait after the loop is a wait that never runs")
    # And the two branches out of that `svc`, which is the second opinion about a failure the table is
    # blind to for the same structural reason: a program in which *both* branches go to the loop. Its
    # every word would be right - `fork` would be called, the `cmp` would be the `cmp`, the number
    # would be 2 - and on the device the child would run the loop instead of exiting, so no `exit` and
    # no `psignal(pp, SIGCHLD)` would ever run and the step's reading would be missing rather than
    # wrong. So the two targets have to be different words, the child's has to be outside the loop,
    # and the child's half has to contain no branch at all: the only way out of the three words
    # `entry_child` heads is the kernel's own `exit` path, which is the claim the SIGCHLD reading
    # rests on.
    if decoded[FORK_BRANCH_WORD][0] == "b" and decoded[PARENT_BRANCH_WORD][0] == "b":
        child_target = decoded[FORK_BRANCH_WORD][1][1]
        parent_target = decoded[PARENT_BRANCH_WORD][1][1]
        if isinstance(child_target, int) and isinstance(parent_target, int):
            if child_target == parent_target:
                fail(f"{p}the two branches out of the `fork` at word {FORK_SVC_WORD} both land on "
                     f"word {child_target}: one `svc` returns twice and these two branches are the "
                     f"only thing that tells the two returns apart, so a program whose branches agree "
                     f"gives both processes the same half - the loop would fork again from the child, "
                     f"no `exit` would ever run, and the run's `xnu_live_sigchld` would have nothing "
                     f"to record")
            if PARK_WORD <= child_target <= FAILED_WORD:
                fail(f"{p}the child's branch out of the `fork` lands on word {child_target}, which is "
                     f"inside the loop ({PARK_WORD}..{FAILED_WORD}): the child would run the loop its "
                     f"parent already runs and never reach the `exit` at words {EXIT_ARG_WORD}"
                     f"..{EXIT_SVC_WORD}, so the step's one reading - the second process's death - "
                     f"could not happen")
            # And 508's half of the same clause: the parent's branch is the *other* thing that one
            # `svc` decides, so where it lands is the claim that the parent reaches the wait at all.
            # A program whose parent branch went to the loop - every word of the wait still present and
            # correct, the counters renumbered to match - would pass every row of the table above and
            # would never call `wait4`: the child would still die and still send SIGCHLD, the loop would
            # still be answered, and the only difference in the log would be the *absence* of seven
            # keys, which is the failure mode this file exists to make loud. The target is compared
            # with `PARENT_WORD`'s own neighbourhood rather than with the number: the parent's half has
            # to start where `PARENT_WORD` says and to be outside the loop.
            if PARK_WORD <= parent_target <= FAILED_WORD:
                fail(f"{p}the parent's branch out of the `fork` lands on word {parent_target}, which is "
                     f"inside the loop ({PARK_WORD}..{FAILED_WORD}): the parent would never reach the "
                     f"wait at words {PARENT_WORD}..{WAIT2_SVC_WORD}, so the child would die unclaimed "
                     f"and the run would have no `xnu_live_wait_*` records at all - a missing reading "
                     f"rather than a wrong one")
        for index in range(CHILD_WORD, EXIT_SVC_WORD + 1):
            if decoded[index][0] == "b":
                fail(f"{p}word {index} in the child's half is a branch, and that half is three words "
                     f"whose last one does not return: a branch here would be a way for the child to "
                     f"leave through user mode, which would make the child's half a copy of the "
                     f"parent's rather than a process that ends")

    # **And the two answers in this program that must *not* be branched on, checked as an absence.**
    # 503 established the rule for an answer whose wrongness is a fact about the kernel rather than
    # about the fixture - neither of its two `poll`s tests the return, because a `poll` that came back
    # with an errno is a reading and a `udf #1` here would kill `initproc` and end the boot 478's way -
    # and 508's *second* `wait4` is the same kind of call for the same reason: `ECHILD` is what the
    # source says it answers, and an un-reaped child would make it answer 2 and *block*, which is a
    # finding this step wants in the log and not in a fault. **512's park is the third, and the range
    # below now reaches the failure marker rather than stopping at the loop, because the park's own
    # answer is unchecked for exactly the two asks' reason**: a `poll` that returned an errno while the
    # process was parking is *the* reading this step is for - it would say the process never parked at
    # all - and a `udf` on it would end the run by killing `initproc` instead of recording that.
    #
    # So from the second `wait4`'s `svc` to the failure marker there must be no conditional branch at
    # all: the unconditional `b` at `WAIT_BACK_WORD` and the park's own back-branch are the only two
    # branches allowed, and both are `al`. The failure this clause refuses is the tempting edit: a
    # `cmp`/`bne` pair added under either call, every word of the table above still satisfied, and the
    # run's ending changed from a park the log can read to a panic on the one path whose whole purpose
    # was to report something. It is an absence claim, so no mutation can construct it from a single
    # word (both halves of the pair would have to be added).
    for index in range(WAIT2_SVC_WORD + 1, FAILED_WORD):
        if decoded[index][0] == "b" and decoded[index][1][0] != COND["al"]:
            fail(f"{p}word {index} is a conditional branch between the second `wait4`'s `svc` at word "
                 f"{WAIT2_SVC_WORD} and the failure marker at word {FAILED_WORD}, and neither answer is "
                 f"deliberately tested: `ECHILD` is what `wait4_nocancel` answers for a reaped "
                 f"child (`kern_exit.c:1888`) and 512's park is a `poll` whose return is the reading - a "
                 f"wrong answer there is a fact about the timer path or the reap path rather than about "
                 f"this program, and `udf #1` here would kill `initproc` - 478's outcome - instead of "
                 f"recording it")

    # **And the *sense* of the comparison, which is the one field 506's first run got wrong while this
    # file agreed with it.** `cmp rX, #imm` sets `Z` exactly when `rX == imm`, and `beq` branches
    # exactly when `Z` is set - so the pair (immediate, condition) is a *single* claim about what the
    # child's `r1` holds, and it has two spellings: `ne` with a zero immediate ("the child is the one
    # whose r1 is not zero") and `eq` with a nonzero immediate ("the child is the one whose r1 is that
    # value"). The other two combinations are the same two instructions asserting the opposite - `eq`
    # with zero, and `ne` with `CHILD_FLAG` - and the second of those is run 1's program, whose reading
    # was `xnu_live_exit_pid` = 1 and a panic. So this clause is a relation between two fields of the
    # encoding and not a copy of the table's expectation, which is what makes it a second opinion: the
    # table says *which instruction* is there, and this says the instruction means what the constant's
    # name says it means. It is the only clause in this file that could have refused run 1 without the
    # table having to be right about the condition in the first place.
    if decoded[FORK_TEST_WORD][0] == "cmp_i" and decoded[FORK_BRANCH_WORD][0] == "b":
        immediate = decoded[FORK_TEST_WORD][1][1]
        condition = decoded[FORK_BRANCH_WORD][1][0]
        if isinstance(immediate, int) and isinstance(condition, int) and condition in (COND["eq"], COND["ne"]):
            want = COND["ne"] if immediate == 0 else COND["eq"]
            if condition != want:
                fail(f"{p}the `fork`'s test at word {FORK_TEST_WORD} compares against {immediate} and "
                     f"the branch at word {FORK_BRANCH_WORD} is taken on "
                     f"{'equality' if condition == COND['eq'] else 'inequality'}, which is the claim "
                     f"that the child is the thread whose r1 is "
                     f"{'zero' if condition == COND['eq'] else 'not ' + str(immediate)} - the opposite "
                     f"of what `thread_set_child` writes (`r[0] = pid; r[1] = 1`, "
                     f"`osfmk/arm/status.c:722`). Both relations are legal ARM, which is why 506's "
                     f"first run built and booted: `initproc` took the child's arm and the console's "
                     f"`pid 1 exited -- no exit reason available -- (signal 0, exit 3)` is what it "
                     f"cost. The child's value is the one this program must test for")

    # What the two asks will read as, said once at the end of the program's check: not a claim about
    # the device but the shape the wrapper in `entry_trace.c` publishes, so that a run whose two
    # `xnu_live_poll_ticks` are in this relation can be read without the reader re-deriving it. The
    # `short_ms > 0` is not decoration: `fail` above records the zero-timeout case and *continues*, and
    # the ratio this line prints is a division by `short_ms` - so without the guard the selftest's own
    # mutation would end this check in a `ZeroDivisionError` traceback instead of the failure message
    # it is made to produce.
    if short_ms is not None and long_ms is not None and short_ms > 0:
        notes.append(f"{p}words {POLL_SHORT_WORD} and {POLL_LONG_WORD} ask for {short_ms} ms and "
                     f"{long_ms} ms ({long_ms / short_ms:.0f}:1); `entry_note_poll` records each call's "
                     f"tick count as `xnu_live_poll_ticks`, so the run's reading is that the second is "
                     f"longer than the first rather than about equal. **The runs so far measure a ratio "
                     f"of about 5.6:1 and not {long_ms / short_ms:.0f}:1** - 160755 ticks for the 5 ms ask "
                     f"and 898180 for the 40 ms one (511's run) - so the tick count carries a component "
                     f"that does not scale with the timeout, and what the pair separates a deadline from "
                     f"is *monotonicity* in the timeout rather than the exact multiple this note used to "
                     f"claim. The fixture's 8:1 is a property of the two words; the device's ratio is the "
                     f"kernel's own answer and it is the one that counts")

    # **And 512's park, which is the one note here whose reading is about the *kernel* rather than
    # about this process.** The park's own evidence is `xnu_live_poll_*`: the wrapper publishes its
    # first four calls in full, so 503's two asks are records 1 and 2 and the park's first two turns are
    # records 3 and 4, each with `_nfds` = 0 and a `_ticks` near the timeout below; after them
    # `xnu_live_poll_over` counts the rest at powers of two. What the park is *for* is the other
    # instrument: `machine_idle` is called from `processor_idle` only when the processor has nothing
    # runnable, so `xnu_live_idle_*` - published at powers of two, with the idle thread's pointer, its
    # pid (0, `kernel_task`) and the live CPSR - is the kernel saying so, and its `_now` places each
    # entry between two poll records. The console line `__wrap_poll` prints on the first park return
    # carries that count, which is what makes the durable artifact a cross-check between the two.
    if park_ms is not None:
        notes.append(f"{p}512's park at words {PARK_WORD}..{PARK_BACK_WORD} asks for {park_ms} ms with "
                     f"no descriptor (`nfds` = 0), so the timeout is the whole of the call and the "
                     f"answer is deliberately untested: `xnu_live_poll_seq` 3 and 4 are its first two "
                     f"turns, `xnu_live_poll_over` counts the rest at powers of two, and "
                     f"`xnu_live_idle_*` is the kernel's own record of having nothing to run while it "
                     f"waited (`xnu_live_idle_pid` = 0 - the idle thread belongs to `kernel_task` - and "
                     f"`xnu_live_idle_cpsr` is the live register, not 511's saved-state one). A run with "
                     f"the park records and **no** idle records is falsifier (a) of the step: the process "
                     f"parked and the kernel still had something runnable")

    # And 504's two, said the same way: what the two paths are and what the read is a prediction of.
    # The pair of opens is the step's control - the same three arguments and the same syscall on a name
    # devfs cannot hold - and the read is the only user-mode instruction in this image whose result
    # depends on a device's own copy, so the notes name the key each one is read back from.
    paths = dev_path_strings(blob, K)
    if len(paths) == 2 and read_bytes is not None:
        real = K["DEV_MOUNT"] + "/" + K["CHAR_DEV_NAME"]
        control = [text for _off, text in paths if text != real]
        notes.append(f"{p}the two opens ask for `{real}` and `{control[0] if control else '?'}` "
                     f"(`xnu_live_open_path` carries each address), and the read asks for {read_bytes} "
                     f"bytes into the page `mmap` returned (`xnu_live_read_buf`): `mdevrw`'s "
                     f"`uiomove64` copies the RAM disk's first bytes there, so "
                     f"`xnu_live_read_word_after` should be this file's own MH_MAGIC=0x%(MH_MAGIC)x "
                     f"while `xnu_live_read_word_before` is the mapping's address" % K)

    # And 505's, which is the first note in this file that predicts a *sequence* rather than a value:
    # the call at word 47 has two returns and the run should show both, so the note says which keys each
    # half is read from and what separates them. **The two predictions that are numbers are the pair the
    # kernel composes (`retval[0]`/`retval[1]` from `fork`) and the status the child's own number
    # composes into (`EXIT_STATUS`)** - and the one thing the note says out loud is that neither is
    # *user* mode's: the fixture's r0 words are what the wrapper compares the kernel's answer against.
    #
    # **505 branched on the wrong register and its run measured the cost; 506 is the step that switches
    # to the right one.** `cmp r0, #0` is the pid in both processes, so 505's child walked the parent's
    # arm and died on the fixture's own `udf #1` (`xnu_live_undef_pc` = `0x11d0`, and
    # `xnu_live_getpid_change_value` = 2 from the child's own first `getpid`) with no `xnu_live_exit_*`
    # key in the log at all. `cmp r1, #CHILD_FLAG` is the flag the kernel writes for the child:
    # `fork1` calls `thread_set_child(child_thread, child_proc->p_pid)` after `thread_dup`
    # (`kern_fork.c:590`, `:636`) and ARM's body writes `r[0] = pid; r[1] = 1`
    # (`osfmk/arm/status.c:722`, and the ARM64 port's is the same pair, `arm64/status.c:1253`), so the
    # child's r1 is 1 and the parent's is 0 - the number `fork` puts in `retval[1]`
    # (`kern_fork.c:895`), which the run publishes as `xnu_live_fork_ret_hi`.
    if K["SYSCALL_FORK"] and K["SYSCALL_EXIT"]:
        notes.append(f"{p}word {FORK_CALL_WORD} asks for {K['SYSCALL_FORK']} (fork) and word "
                     f"{FORK_SVC_WORD} is the only `svc` in this program that returns twice: the parent "
                     f"comes back at word {FORK_TEST_WORD} with `xnu_live_fork_ret_lo` = the child's pid "
                     f"and `xnu_live_fork_ret_hi` = 0 (its r1), the child comes back at the same word "
                     f"with r0 = the pid too and r1 = {K['CHILD_FLAG']}, and the branch at word "
                     f"{FORK_BRANCH_WORD} tests *that* - so the word at {FORK_ARG_WORD}, "
                     f"{K['FORK_ARG_VALUE']}, is read by nobody: not as an argument (`sy_narg` 0, munger "
                     f"NULL) and not as either process's return value. The child's half at word "
                     f"{CHILD_WORD} is the `exit` at word {EXIT_CALL_WORD} carrying {K['EXIT_RVAL']}, "
                     f"which `exit1` composes into 0x%(EXIT_STATUS)x inside the kernel - the argument "
                     f"{K['EXIT_RVAL']} is `xnu_live_exit_rval` and the composed word is `p->p_xstat`, "
                     f"which this instrument does not publish (run 2 of 506 measured the difference) - "
                     f"with `xnu_live_exit_pid` = `xnu_live_fork_ret_lo` = 2, and then the OS tells the "
                     f"parent: `psignal(pp, SIGCHLD)` = 20 (`xnu_live_sigchld_signal`) to "
                     f"`xnu_live_sigchld_to` = 1. The parent never runs `exit` - the wait at word "
                     f"{PARENT_WORD} is its arm, and 508 is the step that reads the reaped child back "
                     f"out of it - so "
                     f"`xnu_live_getpid_count` climbing with `_last` = 1 beside an `exit` record is the "
                     f"pair that says two processes were alive at once **(until 512: the parent's own "
                     f"`getpid` loop is what made that count climb, and this step replaced it with a "
                     f"park, so the count now reaches only the calls the two processes make as they "
                     f"start - what says the parent is alive afterwards is `xnu_live_poll_seq` 3 and 4 "
                     f"with `_timeout_ms` = the park's, returning one after the other)**, and 508's "
                     f"`xnu_live_wait_*` records beside that same `exit` are what say the process it "
                     f"names is gone for good - the wait answers with the pid and `ECHILD` is what the "
                     f"second ask gets back" % K)


def check_thread_registers(pc, reg, K):
    """The registers `LC_UNIXTHREAD` starts the process with, which the program's words do not cover.

    Two claims, and they are different in kind:

      - **r12**, which is the one register of the twelve the kernel reads for the *first* instruction:
        `fleh_swi` turns it into the index and `arm_get_syscall_number` reads the same word back out of
        the saved state as the `sysent` index. The program reloads it twice more before the syscalls
        that follow, so this is the first call's number and only that.
      - **r0..r11 are zero**, and here that is a statement about the *first* call only: `getpid`'s
        `sy_narg` is 0, so `arm_get_syscall_args` is not called and these registers are read by
        nothing. 480's program then loads them itself - r0..r8 are `mmap`'s arguments by word 11 - so a
        nonzero value *here* would be a stale value the kernel never reads, which is why the check is a
        note rather than a failure and why it is worth reading: it says the file describes a process
        that starts with nothing rather than one that starts mid-conversation.
    """
    p = f"{pc:#x}: "
    number = K["SYSCALL_GETPID"]
    if reg[12] != number:
        fail(f"{p}r12 is {reg[12]}, not {number} (SYS_getpid, from bsd/kern/syscalls.master) - "
             f"`fleh_swi` computes `-r12` = {number} and takes the *unix* path for it, so a different "
             f"number here is a different syscall or the mach path")
    for k in range(13):
        if k != 12 and reg[k] != 0:
            notes.append(f"{p}r{k} is {reg[k]}, which the kernel reads as no argument at all for the "
                         f"first call (sy_narg is 0 for getpid); 480's program loads r0..r8 itself "
                         f"before its second")


def arm_thread_state_count():
    """sizeof(arm_thread_state_t)/4, from the struct rather than from a number here.

    `ARM_THREAD_STATE_COUNT` is `sizeof (arm_thread_state_t)/sizeof(uint32_t)` in
    `osfmk/mach/arm/thread_status.h`, which is an expression and not a literal - so the check counts
    the struct's own members: `__r[13]` plus `__sp`, `__lr`, `__pc`, `__cpsr`. The kernel's
    `machine_thread_set_state` copies exactly `sizeof(struct arm_thread_state)` bytes out of the
    Mach-O's thread state (`osfmk/arm/status.c:286-296`), so this is the number that bounds it.
    """
    text = open(os.path.join(XNU, "osfmk/mach/arm/_structs.h"), encoding="utf-8",
                errors="replace").read()
    m = re.search(r"_STRUCT_ARM_THREAD_STATE\s*\{(.*?)\}", text, re.S)
    if not m:
        sys.exit("osfmk/mach/arm/_structs.h has no _STRUCT_ARM_THREAD_STATE")
    # The comments in this struct sit *after* each member's semicolon, so splitting the body on
    # semicolons first gives every member but the first a leading `/* ... */` - and a member that
    # starts with a comment is still a member. Comments go first; the count was 13 until they did.
    body = re.sub(r"/\*.*?\*/", " ", m.group(1), flags=re.S)
    total = 0
    for member in body.split(";"):
        member = member.strip()
        if not member:
            continue
        arr = re.search(r"\[(\d+)\]", member)
        if arr:
            total += int(arr.group(1))
        elif re.search(r"uint32_t", member):
            total += 1
    if total == 0:
        sys.exit("could not count the members of _STRUCT_ARM_THREAD_STATE")
    return total


def elf_section_for(elf, addr, size):
    """(name, type, file offset, alloc flag) of the section containing [addr, addr+size).

    The disk must be in a section that is both `PROGBITS` and `ALLOC`: `ALLOC` is what puts it in
    the bytes `objcopy` produces, and `PROGBITS` is what keeps it out of the range the payload
    zeroes. `g_stage90_ramdisk` was in `.bss` until 468, so this is a check with a history rather
    than a formality. A section is matched on the address range alone and the type is reported, so a
    `NOBITS` disk is named as such instead of being called missing.
    """
    out = subprocess.run(["readelf", "-S", "-W", elf], capture_output=True, text=True)
    if out.returncode:
        sys.exit(f"readelf failed on {elf}: {out.stderr.strip()}")
    sections = []
    for line in out.stdout.split("\n"):
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
                     r"[0-9a-f]+\s+([A-Z]*)\s", line)
        if not m:
            continue
        sections.append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16),
                         int(m.group(5), 16), "A" in m.group(6)))
    for name, typ, saddr, soff, ssize, alloc in sections:
        if alloc and saddr <= addr and addr + size <= saddr + max(ssize, 1) and ssize:
            return name, typ, soff + (addr - saddr), alloc
    for name, typ, saddr, soff, ssize, alloc in sections:
        if saddr <= addr and addr + size <= saddr + max(ssize, 1) and ssize:
            return name, typ, soff + (addr - saddr), alloc
    fail("no section of %s contains 0x%x + %d - the RAM disk is not in this image at all"
         % (elf, addr, size))
    return None


def nm_value(elf, symbol):
    out = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True, text=True)
    if out.returncode:
        sys.exit(f"nm failed on {elf}: {out.stderr.strip()}")
    for line in out.stdout.split("\n"):
        parts = line.split()
        if len(parts) == 4 and parts[3] == symbol:
            return int(parts[0], 16), int(parts[1], 16)
    for line in out.stdout.split("\n"):
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16), 0
    sys.exit(f"{symbol} is not in {elf}")


def parse(blob, K, size, label=""):
    """Every check, over one blob. `label` names the mutation when --selftest runs it."""
    p = (label + ": ") if label else ""

    if len(blob) < size:
        fail(f"{p}the blob is {len(blob)} bytes and the symbol is {size}")
        return

    magic, cputype, cpusubtype = u32(blob, 0), u32(blob, 4), u32(blob, 8)
    filetype, ncmds, sizeofcmds, flags = u32(blob, 12), u32(blob, 16), u32(blob, 20), u32(blob, 24)

    # --- the header, and what claims the file ------------------------------------------------
    # `exec_mach_imgact` (bsd/kern/kern_exec.c:855) claims by magic, refuses anything but MH_EXECUTE
    # (:859) and refuses MH_CIGAM explicitly (:845). `parse_machfile`'s first test
    # (bsd/kern/mach_loader.c:611-614) is the cputype masked against the host's, its second is
    # `grade_binary` - which for a V7K host accepts only V7K (bsd/dev/arm/kern_machdep.c:105-113).
    if magic != K["MH_MAGIC"]:
        fail(f"{p}magic is 0x{magic:08x}, not MH_MAGIC (0x{K['MH_MAGIC']:08x}) - exec_mach_imgact "
             f"declines the file and the exec is ENOEXEC, which is exactly 467's stop")
    if cputype != K["CPU_TYPE_ARM"]:
        fail(f"{p}cputype is {cputype}, not CPU_TYPE_ARM ({K['CPU_TYPE_ARM']}) - parse_machfile's "
             f"first test fails and the exec is EBADARCH")
    if cpusubtype != K["CPU_SUBTYPE_ARM_V7K"]:
        fail(f"{p}cpusubtype is {cpusubtype}, not CPU_SUBTYPE_ARM_V7K "
             f"({K['CPU_SUBTYPE_ARM_V7K']}) - grade_binary grades this 0 on a V7K host, so the exec "
             f"is EBADARCH")
    if filetype != K["MH_EXECUTE"]:
        fail(f"{p}filetype is {filetype}, not MH_EXECUTE ({K['MH_EXECUTE']})")
    if flags & K["MH_PIE"]:
        fail(f"{p}flags 0x{flags:x} set MH_PIE (0x{K['MH_PIE']:x}): parse_machfile then slides every "
             f"segment by the ASLR offset, and the addresses this check asserts are the unslid ones")
    if flags & K["MH_DYLDLINK"]:
        fail(f"{p}flags 0x{flags:x} set MH_DYLDLINK (0x{K['MH_DYLDLINK']:x}): the file claims to be "
             f"dynamic, which needs a dylinker vnode (mach_loader.c:1155) and CS_DYLD_PLATFORM "
             f"(:1173)")
    if flags != 0:
        notes.append(f"{p}flags are 0x{flags:x}; neither PIE nor DYLDLINK is set, which is what "
                     f"makes this a static executable - legal only because DEVELOPMENT is on "
                     f"(mach_loader.c:631-636)")

    # --- the commands, walked by their own cmdsize chain -------------------------------------
    # `parse_machfile` reads `mach_header_sz + sizeofcmds` bytes from the file (:654-657) and walks
    # `ncmds` commands, rejecting any `cmdsize < sizeof(struct load_command)` or any that runs past
    # the reserved section (:806-812). `sizeofcmds` is asserted against the walk rather than trusted,
    # because a header whose extent is larger than its commands is bytes the kernel reads as a
    # command that is not there.
    end = 28 + sizeofcmds
    if 28 + sizeofcmds > size:
        fail(f"{p}sizeofcmds {sizeofcmds} puts the commands' end at {end}, past the file's {size} "
             f"bytes - parse_machfile refuses that (:654-657)")
    off = 28
    seen = []
    for _ in range(ncmds):
        if off + 8 > end:
            fail(f"{p}command at +{off} runs past sizeofcmds' end {end}")
            break
        cmd, cmdsize = u32(blob, off), u32(blob, off + 4)
        if cmdsize < 8 or off + cmdsize > end:
            fail(f"{p}command 0x{cmd:x} at +{off} has cmdsize {cmdsize}, which is not a command the "
                 f"walk can take (mach_loader.c:806-812)")
            break
        seen.append((off, cmd, cmdsize))
        off += cmdsize
    if off != end:
        fail(f"{p}the {ncmds} commands walk to +{off} and sizeofcmds says +{end}: the header "
             f"describes {end - off} bytes that are not a command")

    kinds = [c for _o, c, _s in seen]
    if kinds != [K["LC_SEGMENT"], K["LC_SEGMENT"], K["LC_UNIXTHREAD"]]:
        fail(f"{p}the commands are {[hex(k) for k in kinds]}, not two LC_SEGMENTs and one "
             f"LC_UNIXTHREAD - the three this file is built from")

    # --- the two segments --------------------------------------------------------------------
    want = [
        # (index, segname, vmaddr, vmsize, fileoff, filesize, maxprot, initprot)
        (0, b"__PAGEZERO", 0, PAGE, 0, 0, 0, 0),
        (1, b"__TEXT", PAGE, PAGE, 0, PAGE, 0x7, 0x5),
    ]
    for idx, name, vmaddr, vmsize, fileoff, filesize, maxprot, initprot in want:
        if idx >= len(seen):
            fail(f"{p}LC_SEGMENT {idx} ({name.decode()}) is missing")
            continue
        o = seen[idx][0]
        got = (blob[o + SEG["segname"]:o + SEG["segname"] + 16].split(b"\0")[0],
               u32(blob, o + SEG["vmaddr"]), u32(blob, o + SEG["vmsize"]),
               u32(blob, o + SEG["fileoff"]), u32(blob, o + SEG["filesize"]),
               u32(blob, o + SEG["maxprot"]), u32(blob, o + SEG["initprot"]),
               u32(blob, o + SEG["nsects"]), u32(blob, o + SEG["flags"]))
        gname, gvmaddr, gvmsize, gfileoff, gfilesize, gmaxprot, ginitprot, gnsects, gflags = got
        if gname != name:
            fail(f"{p}segment {idx} is named {gname!r}, not {name!r}")
        for what, g, w in (("vmaddr", gvmaddr, vmaddr), ("vmsize", gvmsize, vmsize),
                           ("fileoff", gfileoff, fileoff), ("filesize", gfilesize, filesize),
                           ("maxprot", gmaxprot, maxprot), ("initprot", ginitprot, initprot)):
            if g != w:
                fail(f"{p}{name.decode()} {what} is 0x{g:x}, not 0x{w:x}")
        if gnsects != 0:
            # load_segment's own check is `total_section_size / sizeof(section) < nsects`
            # (mach_loader.c:1583-1585), so sections that do not fit are LOAD_BADMACHO.
            fail(f"{p}{name.decode()} declares {gnsects} sections in a {SEG_SIZE}-byte command")
        if gflags != 0:
            notes.append(f"{p}{name.decode()} flags are 0x{gflags:x}; SG_PROTECTED_VERSION_1 would "
                         f"route the segment through the DSMOS decrypt path")

    if len(seen) == 3:
        # `map_segment` panics for a 32-bit binary on a mapping that is not page-aligned in both
        # address spaces (mach_loader.c:1339-1350), and `load_segment` refuses a file offset that is
        # not page-aligned (:1610-1622). Both are panics/refusals rather than warnings, so they are
        # asserted for both segments rather than for `__TEXT` alone.
        for idx, (o, _c, _s) in enumerate(seen[:2]):
            for what, field in (("vmaddr", "vmaddr"), ("vmsize", "vmsize"),
                                ("fileoff", "fileoff"), ("filesize", "filesize")):
                v = u32(blob, o + SEG[field])
                if v % PAGE:
                    fail(f"{p}segment {idx}'s {what} is 0x{v:x}, which is not page-aligned: "
                         f"map_segment panics on that for a 32-bit binary")
        # `load_segment`'s bounds test (:1570-1573): fileoff + filesize must be inside the file.
        for idx, (o, _c, _s) in enumerate(seen[:2]):
            fo, fs = u32(blob, o + SEG["fileoff"]), u32(blob, o + SEG["filesize"])
            if fo + fs > size:
                fail(f"{p}segment {idx} reads file [{fo}, {fo + fs}) and the file is {size} bytes")
        # The header segment's vmsize is its filesize. A larger vmsize is legal Mach-O - the loader
        # maps it and reads only filesize bytes - but the tail would be zero fill *inside* a segment
        # this check requires to be R|X, i.e. executable zeros after the one instruction, which is a
        # different experiment from the one this file is.
        o2 = seen[1][0]
        if u32(blob, o2 + SEG["vmsize"]) != u32(blob, o2 + SEG["filesize"]):
            fail(f"{p}__TEXT maps 0x{u32(blob, o2 + SEG['vmsize']):x} bytes and reads "
                 f"0x{u32(blob, o2 + SEG['filesize']):x} from the file: the difference is zero fill "
                 f"inside an R|X segment")
        # `vm_map_has_hard_pagezero(map, 0x1000)` must be true after the parse
        # (mach_loader.c:452-479) and the only thing that raises the map's min_offset is a segment
        # with `vmaddr == 0 && filesize == 0 && vmsize != 0` and both protections NONE (:1645-1650).
        o = seen[0][0]
        if not (u32(blob, o + SEG["vmaddr"]) == 0 and u32(blob, o + SEG["filesize"]) == 0
                and u32(blob, o + SEG["vmsize"]) >= PAGE
                and u32(blob, o + SEG["initprot"]) == 0 and u32(blob, o + SEG["maxprot"]) == 0):
            fail(f"{p}the first segment is not a page zero: without it vm_map_has_hard_pagezero is "
                 f"false and load_machfile answers LOAD_BADMACHO before mapping anything")
        # `found_header_segment` (:906-916): exactly one segment with fileoff 0, filesize > 0 and
        # R|X, and no second one.
        headers = [i for i, (o, _c, _s) in enumerate(seen[:2])
                   if u32(blob, o + SEG["fileoff"]) == 0 and u32(blob, o + SEG["filesize"]) > 0]
        if headers != [1]:
            fail(f"{p}segments with a non-empty mapping at file offset 0 are {headers}, not [1]")
        else:
            o = seen[1][0]
            prot = u32(blob, o + SEG["initprot"])
            if (prot & (K["VM_PROT_READ"] | K["VM_PROT_EXECUTE"])) != (K["VM_PROT_READ"]
                                                                       | K["VM_PROT_EXECUTE"]):
                fail(f"{p}__TEXT's initprot 0x{prot:x} is not R|X, which found_header_segment and "
                     f"validentry both require")

    # --- the thread state ---------------------------------------------------------------------
    if len(seen) == 3:
        o = seen[2][0]
        flavor = u32(blob, o + THREAD_STATE)
        count = u32(blob, o + THREAD_STATE + 4)
        if flavor != K["ARM_THREAD_STATE"]:
            # thread_entrypoint (osfmk/arm/status.c:683-705) accepts ARM_THREAD_STATE and nothing
            # else: any other flavor is KERN_INVALID_ARGUMENT, which load_threadentry turns into
            # LOAD_FAILURE.
            fail(f"{p}the thread flavor is {flavor}, not ARM_THREAD_STATE "
                 f"({K['ARM_THREAD_STATE']}) - thread_entrypoint refuses every other flavor")
        if count < K["ARM_THREAD_STATE_COUNT"]:
            # thread_userstack (status.c:580) and machine_thread_set_state (status.c:281) both
            # refuse a count smaller than the state struct.
            fail(f"{p}the thread state count is {count}, less than ARM_THREAD_STATE_COUNT "
                 f"({K['ARM_THREAD_STATE_COUNT']}) - thread_userstack and "
                 f"machine_thread_set_state both refuse it")
        elif count != K["ARM_THREAD_STATE_COUNT"]:
            notes.append(f"{p}the thread state count is {count}, not "
                         f"{K['ARM_THREAD_STATE_COUNT']}")
        if o + THREAD_CMD_MIN + count * 4 > 28 + sizeofcmds:
            fail(f"{p}the thread state's {count} words run past the command's own cmdsize")
        else:
            # struct arm_thread_state: r[13], sp, lr, pc, cpsr.
            reg = [u32(blob, o + THREAD_STATE + 8 + k * 4) for k in range(13)]
            sp = u32(blob, o + THREAD_STATE + 8 + 13 * 4)
            pc = u32(blob, o + THREAD_STATE + 8 + 15 * 4)
            cpsr = u32(blob, o + THREAD_STATE + 8 + 16 * 4)
            if sp != 0:
                notes.append(f"{p}sp is 0x{sp:x}: thread_userstack then sets customstack and the "
                             f"kernel does not allocate the stack itself")
            # validentry (:1878-1890): inside an R+X segment or 0.
            o2 = seen[1][0]
            vmaddr, vmsize = u32(blob, o2 + SEG["vmaddr"]), u32(blob, o2 + SEG["vmsize"])
            if not (vmaddr <= pc < vmaddr + vmsize):
                fail(f"{p}pc is 0x{pc:x}, outside __TEXT [0x{vmaddr:x}, 0x{vmaddr + vmsize:x}) - "
                     f"validentry would be 0 and parse_machfile's pass 3 answers LOAD_FAILURE")
            else:
                fo = u32(blob, o2 + SEG["fileoff"])
                fs = u32(blob, o2 + SEG["filesize"])
                fpc = fo + (pc - vmaddr)
                if fpc + PROGRAM_WORDS * 4 > fo + fs:
                    fail(f"{p}pc 0x{pc:x} is at file offset 0x{fpc:x}, past __TEXT's filesize: the "
                         f"program is {PROGRAM_WORDS} instructions and this leaves room for fewer")
                else:
                    check_program(blob, fpc, pc, reg, K)
                    check_thread_registers(pc, reg, K)
                    notes.append(f"{p}__TEXT's file [{fo}, {fo + fs}) maps to "
                                 f"[0x{vmaddr:x}, 0x{vmaddr + vmsize:x}); "
                                 f"pc 0x{pc:x} is file offset 0x{fpc:x}, and the {PROGRAM_WORDS} "
                                 f"words there are the program this file describes")
                    notes.append(f"{p}cpsr 0x{cpsr:x} - machine_thread_set_state keeps only the flags "
                                 f"from this word and takes the mode from PSR_USERDFLT (0x10), so "
                                 f"the thread starts in User mode whatever it says")

    return len(seen)


def load(elf):
    """The disk's bytes, or None if the section that holds them is the wrong kind.

    The type is checked before the bytes are read rather than after: for a `NOBITS` section there are
    no bytes in the file at the symbol's address, so parsing whatever is at that file offset would
    report a dozen failures derived from the ELF header and bury the one that matters.
    """
    addr, size = nm_value(elf, "g_stage90_ramdisk")
    sec = elf_section_for(elf, addr, size)
    if sec is None:
        return None, size
    name, typ, off, alloc = sec
    if typ != "PROGBITS":
        fail(f"the section holding g_stage90_ramdisk ({name}) is {typ}, not PROGBITS - a NOBITS "
             f"region is zeroed by the payload after the copy and would erase the executable")
        return None, size
    if not alloc:
        fail(f"the section holding g_stage90_ramdisk ({name}) is not ALLOC, so it is not in the "
             f"bytes objcopy produces and the device's page would hold whatever was there before")
        return None, size
    blob = open(elf, "rb").read()[off:off + size]
    notes.append(f"g_stage90_ramdisk is 0x{addr:x} + {size} (0x{size:x}), in {name} ({typ}, ALLOC) "
                 f"at file offset 0x{off:x}")
    return blob, size


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("elf", nargs="?", default=DEFAULT_ELF)
    ap.add_argument("--selftest", action="store_true",
                    help="mutate the blob one field at a time and require every mutation to be refused")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        sys.exit(f"no {args.elf} - build the entry image first")

    K = {
        "MH_MAGIC": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"), "MH_MAGIC"),
        "MH_EXECUTE": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                 "MH_EXECUTE"),
        "LC_SEGMENT": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                 "LC_SEGMENT"),
        "LC_UNIXTHREAD": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                    "LC_UNIXTHREAD"),
        "MH_PIE": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"), "MH_PIE"),
        "MH_DYLDLINK": hdr_define(os.path.join(XNU, "EXTERNAL_HEADERS/mach-o/loader.h"),
                                  "MH_DYLDLINK"),
        "CPU_TYPE_ARM": hdr_define(os.path.join(XNU, "osfmk/mach/machine.h"), "CPU_TYPE_ARM"),
        "CPU_SUBTYPE_ARM_V7K": hdr_define(os.path.join(XNU, "osfmk/mach/machine.h"),
                                          "CPU_SUBTYPE_ARM_V7K"),
        "VM_PROT_READ": hdr_define(os.path.join(XNU, "osfmk/mach/vm_prot.h"), "VM_PROT_READ"),
        "VM_PROT_WRITE": hdr_define(os.path.join(XNU, "osfmk/mach/vm_prot.h"), "VM_PROT_WRITE"),
        "VM_PROT_EXECUTE": hdr_define(os.path.join(XNU, "osfmk/mach/vm_prot.h"), "VM_PROT_EXECUTE"),
        "ARM_THREAD_STATE": hdr_define(os.path.join(XNU, "osfmk/mach/arm/thread_status.h"),
                                       "ARM_THREAD_STATE"),
        "ARM_THREAD_STATE_COUNT": arm_thread_state_count(),
        # 479: the program's syscall number and the pid it compares the answer with - each read out of
        # the file that defines it, never written here. See `syscall_getpid` and `init_pid`.
        "SYSCALL_GETPID": syscall_getpid(),
        "INIT_PID": init_pid(),
        # 480: the second syscall, the page its length comes from, and the two mman flags by name.
        "SYSCALL_MMAP": syscall_mmap(),
        "MMAP_LENGTH": 1 << arm_pgshift(),
        "MMAP_PROT": hdr_define(MMAN, "PROT_READ") | hdr_define(MMAN, "PROT_WRITE"),
        "MMAP_FLAGS": hdr_define(MMAN, "MAP_PRIVATE") | hdr_define(MMAN, "MAP_ANON"),
        # 503: the third syscall, the one whose effect is to make the kernel wait. Its number comes
        # from `syscalls.master` through the same reader shape as the other two; the two timeouts
        # deliberately have no entry here, because they are the program's own choice of a *ratio* and
        # `program_expectations` reads them back out of the instruction stream instead of comparing
        # them with a number in this file (there is no header that defines how long a fixture should
        # sleep). See `poll_timeouts`.
        "SYSCALL_POLL": syscall_poll(),
        # 504: the two calls that reach a *driver*. Both numbers come from `syscalls.master` through the
        # same reader shape as the other three, and the path the first one opens is not written here at
        # all - it is the kernel's own devfs mount point and the driver's own node format, joined. See
        # `devfs_mount_point` and `mdev_node_names`.
        "SYSCALL_READ": syscall_three_words("AUE_NULL", "read"),
        "SYSCALL_OPEN": syscall_three_words("AUE_OPEN_RWTC", "open"),
        "OPEN_RDONLY": hdr_define(FCNTL, "O_RDONLY"),
        "DEV_MOUNT": devfs_mount_point(),
        "BLOCK_DEV_NAME": mdev_node_names()[0],
        "CHAR_DEV_NAME": mdev_node_names()[1],
        # 505: the pair that ends a process and makes another. Both numbers come from the master through
        # the same reader shape as the other five, with their *argument shapes* checked in the same
        # function (none for `fork`, one 4-byte word for `exit`), and the status the kernel composes out
        # of the child's own number is derived from `bsd/sys/wait.h`'s `W_EXITCODE` rather than written
        # here. See `syscall_fork_and_exit` and `exit_status_word`. `FORK_ARG_VALUE` is the word 505
        # wrote before the `svc` and 506 kept: it is checked against the *instruction* and is not
        # any process's return value - 505's run measured that - and `CHILD_FLAG` is the value of the
        # register the branch tests, from `thread_set_child`'s own write (`osfmk/arm/status.c:722`).
        "SYSCALL_FORK": syscall_fork_and_exit()[0],
        "SYSCALL_EXIT": syscall_fork_and_exit()[1],
        "FORK_ARG_VALUE": 0,
        "CHILD_FLAG": 1,
        "EXIT_RVAL": 3,
        "EXIT_STATUS": exit_status_word(3),
        # 508: the third of the three calls a Unix process's life is, from the master through the same
        # reader shape as the other six - with the *order* of its four arguments checked, because the
        # fixture's r0..r3 are those four fields - and the pid the parent asks about, which is derived
        # rather than written: the system has one user process when this program starts
        # (`initproc = proc_find(1)`), so the first process a user request can be given is the next
        # one. 505's run measured that number at three independent points (`xnu_live_fork_ret_lo`,
        # `xnu_live_exit_pid`, and the proc `xnu_live_pth_delete_p`), which is why the fixture's `cmp`
        # and the second call can both name it.
        "SYSCALL_WAIT4": syscall_wait4(),
        "WAIT_PID": init_pid() + 1,
    }
    notes.append("from the headers: MH_MAGIC=0x%(MH_MAGIC)x MH_EXECUTE=%(MH_EXECUTE)d "
                 "MH_PIE=0x%(MH_PIE)x MH_DYLDLINK=0x%(MH_DYLDLINK)x "
                 "LC_SEGMENT=0x%(LC_SEGMENT)x LC_UNIXTHREAD=0x%(LC_UNIXTHREAD)x "
                 "CPU_TYPE_ARM=%(CPU_TYPE_ARM)d CPU_SUBTYPE_ARM_V7K=%(CPU_SUBTYPE_ARM_V7K)d "
                 "VM_PROT=R|X=0x5 ARM_THREAD_STATE=%(ARM_THREAD_STATE)d "
                 "ARM_THREAD_STATE_COUNT=%(ARM_THREAD_STATE_COUNT)d "
                 "SYSCALL_GETPID=%(SYSCALL_GETPID)d (bsd/kern/syscalls.master) "
                 "INIT_PID=%(INIT_PID)d (bsd/kern/bsd_init.c's `initproc = proc_find(N)`) "
                 "SYSCALL_MMAP=%(SYSCALL_MMAP)d (bsd/kern/syscalls.master) "
                 "SYSCALL_POLL=%(SYSCALL_POLL)d (bsd/kern/syscalls.master) "
                 "SYSCALL_READ=%(SYSCALL_READ)d SYSCALL_OPEN=%(SYSCALL_OPEN)d "
                 "OPEN_RDONLY=%(OPEN_RDONLY)d (bsd/sys/fcntl.h) "
                 "DEV_MOUNT=%(DEV_MOUNT)s (bsd/kern/bsd_init.c) "
                 "BLOCK_DEV_NAME=%(BLOCK_DEV_NAME)s CHAR_DEV_NAME=%(CHAR_DEV_NAME)s "
                 "(bsd/dev/memdev.c's devfs_make_node formats) "
                 "MMAP_LENGTH=0x%(MMAP_LENGTH)x (1 << ARM_PGSHIFT) "
                 "MMAP_PROT=0x%(MMAP_PROT)x MMAP_FLAGS=0x%(MMAP_FLAGS)x (bsd/sys/mman.h) "
                 "SYSCALL_FORK=%(SYSCALL_FORK)d SYSCALL_EXIT=%(SYSCALL_EXIT)d "
                 "(bsd/kern/syscalls.master, the master's `fork(void)` and `exit(int rval)` lines) "
                 "FORK_ARG_VALUE=%(FORK_ARG_VALUE)d CHILD_FLAG=%(CHILD_FLAG)d "
                 "EXIT_RVAL=%(EXIT_RVAL)d "
                 "EXIT_STATUS=0x%(EXIT_STATUS)x (W_EXITCODE(%(EXIT_RVAL)d, 0), bsd/sys/wait.h) "
                 "SYSCALL_WAIT4=%(SYSCALL_WAIT4)d (bsd/kern/syscalls.master, whose `wait4(int pid, "
                 "user_addr_t status, int options, user_addr_t rusage)` line is the four registers "
                 "r0..r3 in that order) "
                 "WAIT_PID=%(WAIT_PID)d (INIT_PID + 1: the first pid a user request can be given)" % K)

    blob, size = load(args.elf)
    if blob is None:
        return report()
    parse(blob, K, size)
    if failures:
        return report()

    if args.selftest:
        # Every mutation below is a field this check has an opinion about. If any of them survives,
        # the check is decorative and this is the only place that can say so. The offsets are the
        # file's own: the header is 28 bytes, each segment command 56, the thread command 84 with its
        # state starting 16 bytes in.
        S1 = 28 + SEG_SIZE             # the second command's first byte
        T = S1 + SEG_SIZE              # the thread command's first byte
        STATE = T + THREAD_HDR + THREAD_STATE   # r[0]
        # 504's three string mutations are byte-valued rather than word-valued, so they need the two
        # strings' offsets - and they are found the same way the check finds them, by scanning the
        # file, rather than by being written as 0x1B0 and 0x1BC here. A baseline that does not hold two
        # `/dev/` strings has already failed above, and the selftest stops rather than mutating a file
        # it cannot describe.
        strings = dev_path_strings(blob, K)
        if len(strings) != 2:
            sys.exit("--selftest: this RAM disk holds %d `/dev/` strings and the three mutations "
                     "about the paths cannot be constructed - the baseline has already failed"
                     % len(strings))
        mutations = [
            ("magic", 0, K["MH_MAGIC"] ^ 0x1),
            ("cputype", 4, K["CPU_TYPE_ARM"] + 1),
            ("cpusubtype", 8, K["CPU_SUBTYPE_ARM_V7K"] + 1),
            ("filetype", 12, K["MH_EXECUTE"] + 1),
            ("ncmds", 16, 2),
            ("flags: MH_PIE", 24, K["MH_PIE"]),
            ("flags: MH_DYLDLINK", 24, K["MH_DYLDLINK"]),
            ("sizeofcmds", 20, 0xC4 - 4),
            ("__PAGEZERO vmsize", 28 + SEG["vmsize"], 0),
            ("__PAGEZERO filesize", 28 + SEG["filesize"], PAGE),
            ("__PAGEZERO initprot", 28 + SEG["initprot"], 0x1),
            ("__TEXT vmaddr", S1 + SEG["vmaddr"], 0),
            ("__TEXT vmsize", S1 + SEG["vmsize"], 0x2000),
            ("__TEXT fileoff", S1 + SEG["fileoff"], PAGE),
            ("__TEXT filesize", S1 + SEG["filesize"], 0),
            ("__TEXT initprot", S1 + SEG["initprot"], 0x1),
            ("__TEXT nsects", S1 + SEG["nsects"], 4),
            ("__TEXT prot not page aligned", S1 + SEG["vmaddr"], PAGE + 1),
            ("thread flavor", STATE - 8, 2),
            ("thread count", STATE - 4, 4),
            ("entry pc", STATE + 15 * 4, PAGE),
            ("r12: the syscall number", STATE + 12 * 4, K["SYSCALL_GETPID"] + 1),
            # 479's program: the five words at the entry point, one at a time, and the register the
            # syscall number lives in. A mutation that survived any of these would mean the device run
            # could differ from the program `entry_ramdisk.s` describes without this check saying so.
            # The first two are kept from that version because they are the two *defects the checks
            # themselves had*: the compare's register (a mask that forgot `Rn`, 205) and a rotated
            # immediate written as the value it stands for.
            ("the instruction", 0xE0, 0xE1A00000),
            ("the syscall immediate", 0xE0, 0xEF000081),
            ("the compare's register", 0xE4, 0xE3510001),
            ("the compare with a rotated immediate", 0xE4, 0xE3500101),
            ("the failure marker", 0xE0 + FAILED_WORD * 4, 0xE7F000F2),
            # 480's program, as *every one of its words except the marker and the two timeouts*: the
            # low bit is the cheapest change that leaves a word a word, and it moves the `svc`'s
            # immediate, a branch's target, a `mov`'s value or a load's register - so no word of the
            # program can differ from what the check requires without the check saying so.
            # `entry_ramdisk.s`'s `.if` fixes the program's length, so this list cannot silently stop
            # covering the program.
            #
            # **Five words are left out on purpose, and they are the places this list is not
            # complete.** Word 8 is the marker in r5, whose value the check deliberately leaves free (a
            # nonzero `movw` into r5 that repeats no argument - properties, not a value), so a low-bit
            # flip there changes only the property that is free and *should* be accepted (see
            # `program_expectations`). Words `POLL_SHORT_WORD` and `POLL_LONG_WORD` are 503's two
            # timeouts, and they are free for the same reason one step further on: the property is the
            # *relation* between them, so 5 ms and 40 ms, 4 ms and 40 ms and 5 ms and 41 ms are all
            # programs this check has no business refusing. What has to be refused is a change of shape
            # or of the relation, and the mutations below are exactly those: a nop, a zero word, a
            # repeated argument and a rotated `mov` where the `movw` is, for the marker; the syscall
            # number moved, and the relation broken two ways, for the asks. `READ_LEN_WORD` is 504's
            # read length, free for the same reason again - 4, 5 and 8 bytes are all reads whose first
            # word the wrapper can publish - and its mutations below are the two ends of the bound.
            # `PARK_MS_WORD` is 512's park timeout, free the same way and checked by the same kind of
            # clause: any number at or above `entry_trace.c`'s threshold makes the call a park, 500 ms
            # and 40000 ms are both programs this check has no business refusing, and what has to be
            # refused is a park below the threshold or a *conditional branch* on its answer. The first
            # of those is a mutation below; the second is an absence no single-word change can build.
            *[(f"program word {i}", 0xE0 + i * 4, u32(blob, 0xE0 + i * 4) ^ 1)
              for i in range(PROGRAM_WORDS)
              if i not in (8, POLL_SHORT_WORD, POLL_LONG_WORD, READ_LEN_WORD, PARK_MS_WORD)],
            # And the mutations that are about a *shape* rather than a value, each a plausible way to
            # write the same intent wrongly. The first is the one this step exists for:
            # `arm_prepare_u32_syscall_return` reports an error by setting the carry bit
            # (`regs->cpsr |= PSR_CF`), so a `blt` there tests a sign that means nothing and a failed
            # `mmap` would be taken for a valid address. The second drops the test altogether.
            ("the errno test is a sign test", 0xE0 + 13 * 4, 0xBA00000B),
            ("the errno test is unconditional", 0xE0 + 13 * 4, 0xEA00000B),
            ("the pad word is never set (a nop)", 0xE0 + 8 * 4, 0xE1A05005),
            ("the pad word is zero", 0xE0 + 8 * 4, 0xE3A05000),
            ("the pad word is a rotated mov", 0xE0 + 8 * 4, 0xE3A05C5A),
            ("the pad word repeats an argument", 0xE0 + 8 * 4, 0xE3015000),
            ("len in prot's register", 0xE0 + 4 * 4, 0xE3012000),
            ("the failure marker is 478's udf #0", 0xE0 + FAILED_WORD * 4, 0xE7F000F0),
            # 503's two asks, mutated four ways. The first is the pair that has to be *asymmetric*
            # (this is the mutation whose survival would make the step's reading worthless, because
            # equal timeouts give a ratio of about 1 whether the wake came from the countdown or from
            # a fixed latency); the second a timeout that never blocks, so the call would not arm the
            # thread's wait timer at all and the pair would be two measurements of nothing; and the
            # last two a call that is not the one the header describes - `poll` replaced by the
            # fixture's own first syscall, which would still make the log show two entries, and a
            # timeout in the register `nfds` lives in, which is the one mistake a reader of the
            # program's listing could make without noticing.
            ("the second ask is as long as the first", 0xE0 + POLL_LONG_WORD * 4, 0xE3002005),
            ("the first ask never waits", 0xE0 + POLL_SHORT_WORD * 4, 0xE3002000),
            ("the ask is not poll's syscall number", 0xE0 + 24 * 4, 0xE3A0C000 | K["SYSCALL_MMAP"]),
            ("the timeout is in nfds' register", 0xE0 + POLL_SHORT_WORD * 4, 0xE3001005),
            # **And the mutation only the threshold clause can refuse.** 1500 ms is a perfectly legal
            # second ask - it is still longer than the first, so the ratio clause is satisfied and every
            # word of the table above is too - but it is no longer *below* the number `__wrap_poll` uses
            # to tell an ask from 512's park, so that call would be named as the park on the console and
            # the artifact would claim the kernel had nothing to run while this program was waiting on a
            # deadline it asked for. Nothing else in this file looks at that number, which is why the
            # mutation is here rather than being left to the ratio clause.
            ("the second ask is as long as the park", 0xE0 + POLL_LONG_WORD * 4, 0xE30025DC),
            # 505's six, and they are the ways a program that *looks* like this one stops being it. The
            # first two are the fork itself - a `svc` that is not the call, and a call that is not
            # `fork`'s number - because a program that made a second process with the read's slot would
            # show `xnu_live_fork_seq` unchanged and a read's record instead. The next two are the
            # split: the child's branch sent back to the loop it must not run, and the *parent's*
            # branch sent into the child's half - which is the mutation whose device effect is the
            # worst in this file, because the parent here **is** `initproc` and its `exit` is the one
            # `proc_prepareexit` panics on (`launchd_crashed_panic`), so a program with these two
            # branches exchanged does not fail the run, it ends it. The last two are the child's half:
            # a status of zero, which the kernel's own composition would turn into a status
            # indistinguishable from a field nothing wrote, and an `exit` that is a nop, which drops
            # the child into the loop and leaves `xnu_live_sigchld` with nothing to record.
            ("the fork is a nop", 0xE0 + FORK_SVC_WORD * 4, 0xE1A00000),
            ("the fork is the read's syscall number", 0xE0 + FORK_CALL_WORD * 4,
             0xE3A0C000 | K["SYSCALL_READ"]),
            ("the child's branch goes to the loop too", 0xE0 + FORK_BRANCH_WORD * 4,
             0x0A000000 | ((PARK_WORD - FORK_BRANCH_WORD - 2) & 0xFFFFFF)),
            ("the parent's branch falls into the child's half", 0xE0 + PARENT_BRANCH_WORD * 4,
             0xEA000000 | ((CHILD_WORD - PARENT_BRANCH_WORD - 2) & 0xFFFFFF)),
            ("the child's status is zero", 0xE0 + EXIT_ARG_WORD * 4, 0xE3A00000),
            ("the child's exit is a nop", 0xE0 + EXIT_SVC_WORD * 4, 0xE1A00000),
            # 506's three, and they are the three ways this step's one edit can be undone without
            # changing the program's *shape*: the test reads the register 505 read (the pid in both
            # processes - the mutation that reproduces the run 505 measured), the test compares `r1`
            # against the value the dead word holds instead of the flag `thread_set_child` writes, and
            # the branch's *sense* is inverted - which is run 1's program, the one that panicked with
            # `xnu_live_exit_pid` = 1, and the one the sense clause above exists to refuse.
            ("the child test reads r0 again", 0xE0 + FORK_TEST_WORD * 4,
             0xE3500000 | K["FORK_ARG_VALUE"]),
            ("the child's test asks for zero", 0xE0 + FORK_TEST_WORD * 4,
             0xE3510000 | K["FORK_ARG_VALUE"]),
            ("the child's branch is taken on inequality", 0xE0 + FORK_BRANCH_WORD * 4,
             0x1A000000 | ((CHILD_WORD - FORK_BRANCH_WORD - 2) & 0xFFFFFF)),
            # 504's five, and they are of three kinds. The first two are the read length's bound at each
            # end - nothing read at all, and more than the mapping the buffer is the start of - and the
            # third is the *pair* of paths: the same instruction shape and a target four words away,
            # which is what a reader of the listing would write if they counted from the wrong string.
            # The last three are the strings themselves, which no value mutation above can reach: they
            # are bytes in the file and not words of the program, so they need the byte-valued form.
            ("the read asks for nothing", 0xE0 + READ_LEN_WORD * 4, 0xE3A02000),
            ("the read runs past the page", 0xE0 + READ_LEN_WORD * 4, 0xE3002101),
            ("the control opens the device", 0xE0 + CONTROL_PATH_WORD * 4,
             0xE28F0000 | ((strings[0][0] - (0xE0 + CONTROL_PATH_WORD * 4 + 8)) & 0xFFF)),
            ("the control path names the device too", strings[1][0], b"/dev/rmd0\0"),
            ("the path names the block device", strings[0][0], b"/dev/md0\0\0"),
            ("the path is not terminated", strings[0][0], b"/dev/rmd0X"),
        ]
        survived = []
        for name, off, value in mutations:
            width = len(value) if isinstance(value, bytes) else 4
            if off + width > len(blob):
                continue
            mutated = bytearray(blob)
            if isinstance(value, bytes):
                mutated[off:off + len(value)] = value
            else:
                struct.pack_into("<I", mutated, off, value)
            del failures[:]
            kept = len(notes)
            parse(bytes(mutated), K, size, label=f"selftest:{name}")
            del notes[kept:]   # a mutated blob's notes describe a file that does not exist
            if not failures:
                survived.append(name)
        # The last mutation's own failures are still in the list, and they are *expected* - every
        # mutation must be refused. They are dropped here rather than reported, or the selftest would
        # print a failure for the last thing it proved.
        del failures[:]
        if survived:
            fail("--selftest: these mutations were accepted, so the checks above do not see them: "
                 + ", ".join(survived))
        else:
            notes.append(f"--selftest: all {len(mutations)} mutations were refused")

    return report()


def report():
    if failures:
        print("the RAM disk is not a Mach-O parse_machfile would load:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        return 1
    print("ok: the RAM disk's Mach-O is one parse_machfile and load_machfile would load")
    for n in notes:
        print(f"    {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
