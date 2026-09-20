#!/usr/bin/env python3
"""The configuration the ARM assembly is assembled with is the configuration the C is compiled with.

    python3 tools/check_asm_config.py                 # the STAGE90_XNU build, measured
    python3 tools/check_asm_config.py --config X      # another configuration
    python3 tools/check_asm_config.py --selftest      # mutate every fact this check reads

Why this exists, and it is experiment 488's defect.

`osfmk/arm/locore.s` consults `CONFIG_SKIP_PRECISE_USER_KERNEL_TIME` at nine sites. With the option
**unset** - which is what an assembly that got no options at all looks like - `return_to_user_now`
calls `timer_state_event_kernel_to_user` and the three user-abort vectors call
`timer_state_event_user_to_kernel`. This configuration sets the option to 1, and the same value is what
keeps those two functions out of `osfmk/arm/machine_routines.c`. So the C side has no definition of
them, and an assembly that calls them leaves two undefined symbols that `build_entry.sh`'s generated
stubs answer - and a generated stub is *terminal*: `entry_stub_hit` ends in `entry_epilogue`. The
symptom is the worst kind: the console is byte-identical to a good run all the way to
`load_init_program: attempting to load /sbin/launchd`, and the boot then stops at the **first return to
user mode**, so process 1 never runs and nothing in the log says which of the two is wrong.

How the two sides came apart. `XNU_MASTER_LOCAL` was the caller's shell's business:
`build_xnu_arm_kernel.sh` takes it as an argument and passes it on, and nothing else does. Without it
`STAGE90_XNU` is *not declared by any MASTER file*, and `expand.sh`'s selection then gives an undeclared
name the `+` attribute - which matches every untagged `options` line in MASTER and no tagged one. The
result was ten `-D` flags instead of 110: a plausible set, a kernel that links and a boot that runs,
which is why it survived. Both ends are now closed: `select_master.sh` finds the fragment by convention
(`tools/xnu_config/<dir>/<CONFIG>.local`), `expand.sh` refuses an undeclared configuration, and
`arm_asm_defines.sh` refuses to emit an empty list instead of handing its caller nothing.

What this check claims, and each one is a measurement rather than a restatement:

1. **The configuration's expansion is its declaration.** `STAGE90_XNU = [ RELEASE mockfs development ]`
   (`tools/xnu_config/boot/STAGE90_XNU.local`) means every `-D` in RELEASE's expansion must be in
   STAGE90_XNU's, plus the options the fragment names. That is exactly the relation a missing fragment
   breaks (10 of 108), and it is read out of the two expansions rather than from a count.
2. **The assembly's list is the configuration's, minus the named exceptions and nothing else.** The
   filter in `arm_asm_defines.sh` is compared against `make_defines.sh` filtered by the list
   `--exceptions` prints - so an exception that has stopped being one, or a filter that dropped a name
   nobody named, fails here rather than in an object's `.text` two links later.
3. **The option's two sides agree in the linked objects.** `locore.s`'s guard sites are read out of
   Apple's file; if the configuration sets the option to 1 then neither assembled `locore.o` may carry
   an *undefined* `timer_state_event_kernel_to_user` or `timer_state_event_user_to_kernel`, and - the
   control that keeps this from being an argument from absence - the same file's other branch must be
   *live*: `CONFIG_TELEMETRY=1` puts `telemetry_needs_record` in the object, so a rename or an option
   that went missing fails the same claim from the other side.
4. **The offsets the assembly is given are the offsets the kernel's own C uses.** `assym.s` publishes
   `TH_KSTACKPTR #1480` and `TH_CTH_SELF #1496`; `genassym.c:129,142` declares them from
   `offsetof(struct thread, machine.…)`; and each number must appear as an immediate **inside a named
   function** of a kernel object whose C reads that same field - `model_dep.c:826`, `trap.c:237`,
   `machdep_call.c:85`, `pcb.c:133`. This is the half 487 asked for: `assym.s` was checked against the
   assembler that reads it and against nothing that compiles C, so one offset could be wrong on one
   side and right on the other with every build line green, which is what 468's run died of.

Every mutation is "the check must still report a failure after this edit", and the baseline is checked
first: the facts as they are must pass before any mutation is asked for. Two of the twenty-one exist
because an earlier version of a *sibling* check accepted them: a mutation that removed one name of a
pair the claim names as a set (`the_object_loses_the_control`), and one written over a whole flag where
the claim compared names (`one_of_releases_options_is_missing`).
"""

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XNU = os.path.join(REPO, "external", "xnu-4570.1.46")
LOCORE = os.path.join(XNU, "osfmk", "arm", "locore.s")

# The option the two functions are guarded by, the functions themselves, and the positive control from
# the same file's other branch (`arm_asm_defines.sh`'s measurements, re-read here).
OPTION = "CONFIG_SKIP_PRECISE_USER_KERNEL_TIME"
CALLED_WHEN_UNSET = ("timer_state_event_kernel_to_user", "timer_state_event_user_to_kernel")
CALLED_WHEN_TELEMETRY = ("telemetry_needs_record", "telemetry_mark_curthread")

# The two objects that must agree: the ARM layer the kernel image is linked from, and the entry image's
# own assembly (a different script, the same `arm_asm_defines.sh`).
OBJECTS = {
    "kernel_layer": ("out/xnu_asm_obj/locore.o", "tools/assemble_arm_layer.sh"),
    "entry_image": ("out/stage90/xnu_arm_entry_locore.o", "stages/stage90/xnu_arm_assemble.sh"),
}

# The declaration this project's configuration is written with, and the file it lives in. Read as
# text rather than assumed: `claim_declaration` is what compares it with the expansion.
FRAGMENT = "tools/xnu_config/boot/STAGE90_XNU.local"

# **The offsets `genassym.c` declares from `offsetof(struct thread, machine.<field>)`, and the kernel
# objects whose compiled C reads or writes that same field.** This is the second half of the same
# claim, measured on a *value* instead of on a flag list: `assym.s` was checked against the assembler
# that reads it and against nothing that compiles C (experiment 487's task), so a TH_* offset could be
# wrong on one side and right on the other with every build line green - which is what 468's run died
# of, at `thread_get_cthread_self`'s `ldr r0, [r0, #1496]` against an `assym.s` that said 1480.
#
# Each site is a *specific function* rather than "the object contains the number": the immediate has to
# be inside the body the C line is in, or the agreement is a coincidence of two unrelated constants.
OFFSETS = (
    ("TH_KSTACKPTR", (
        ("osfmk_arm_model_dep.o", "DebuggerXCall",
         "model_dep.c:826 reads `current_thread()->machine.kstackptr`"),
        ("osfmk_arm_trap.o", "sleh_undef",
         "trap.c:237 reads `current_thread()->machine.kstackptr`"),
    )),
    ("TH_CTH_SELF", (
        ("osfmk_arm_machdep_call.o", "thread_get_cthread_self",
         "machdep_call.c:85 returns `current_thread()->machine.cthread_self`"),
        ("osfmk_arm_pcb.o", "machine_thread_create",
         "pcb.c:133 zeroes `thread->machine.cthread_self`, and the compiler does it as a 16-byte NEON "
         "store at that offset - which is why `TH_CTH_DATA`, the field 8 bytes above it, has no "
         "immediate of its own to be compared with"),
    )),
)

# The object directory `build_xnu_arm_kernel.sh` writes the C objects into, and the tool the claim reads
# them with. `arm-none-eabi-objdump` is the same toolchain `arm-none-eabi-nm` above is from.
KOBJ = "out/xnu_kernel_obj"


def sh(cmd, env=None):
    """Run a script and return (ok, stdout). A failure is a fact this check reports, not an exit."""
    full = dict(os.environ)
    if env:
        full.update(env)
    p = subprocess.run(cmd, cwd=REPO, env=full, capture_output=True, text=True)
    return p.returncode == 0, p.stdout


def define_lines(out):
    """A `make_defines.sh` output as its `-D` lines.

    One flag per line, and **the line is the unit** - not the whitespace-separated word. `expand.sh`
    prints `-DCONFIG_MSG_BSIZE=CONFIG_MSG_BSIZE_REL`, and a split on whitespace would answer the count
    with 112 for a 108-flag expansion (measured), which is the sort of off-by-four that reads like a
    real difference between two configurations.
    """
    return sorted(line.strip() for line in out.splitlines() if line.strip().startswith("-D"))


def name_of(define):
    """The flag's *name*, for the comparison that claim 1 makes.

    `CONFIG_MSG_BSIZE=CONFIG_MSG_BSIZE_REL` and `CONFIG_MSG_BSIZE=CONFIG_MSG_BSIZE_DEV` are the same
    option with two legal values: `config/MASTER:288,289` declares `CONFIG_MSG_BSIZE` twice, once
    tagged `<!development,debug>` and once `<development,debug>`, and this project's configuration
    adds `development` to RELEASE. So the value is *expected* to differ, and a claim written over whole
    flags would fail on a correct build - the defect it is looking for is a flag whose name is missing
    altogether, which is what an undeclared configuration produces (ten names out of a hundred and
    eight).
    """
    return define.split("=", 1)[0]


def nm_undefined(path):
    """The undefined names of an object, or None when the object is not there.

    `arm-none-eabi-nm -u` prints `         U name`; the GNU `nm` for the host prints `U name`. Both
    are read, because which toolchain answers is a property of the machine and not of the claim.
    """
    if not os.path.exists(os.path.join(REPO, path)):
        return None
    for tool in ("arm-none-eabi-nm", "llvm-nm", "nm"):
        try:
            p = subprocess.run([tool, "-u", path], cwd=REPO, capture_output=True, text=True)
        except FileNotFoundError:
            continue
        if p.returncode != 0:
            continue
        return sorted({line.split()[-1] for line in p.stdout.splitlines() if line.split()})
    return None


def read_assym(path):
    """`assym.s` as `{name: value}` for its plain `#define NAME #<n>` lines.

    Two forms come out of Apple's scrape per entry and both matter: `#define NAME #3` (the `#` inside
    the define, because ARM's addressing syntax is `[r4, #3]`) and `#define NAME_NUM 3`. Only the first
    is the assembly's spelling, so only the first is read - and a name whose plain form is missing is a
    failure the claim reports rather than an empty value it compares.
    """
    if not os.path.exists(os.path.join(REPO, path)):
        return None
    out = {}
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"#define\s+([A-Za-z0-9_]+)\s+#(-?\d+)\s*$", line)
            if m:
                out[m.group(1)] = int(m.group(2))
    return out


def symbol_immediates(obj, symbol):
    """The immediates inside one function of one object, or None when either is not there.

    `objdump -d --disassemble=<symbol>` prints just that body; `#-?\\d+` is how both the ARM immediate
    syntax and objdump's own comment spelling write them, so `#1496` and `#1496\t; 0x5d8` are read the
    same way.
    """
    path = os.path.join(REPO, KOBJ, obj)
    if not os.path.exists(path):
        return None
    p = subprocess.run(["arm-none-eabi-objdump", "-d", "--disassemble=%s" % symbol, path],
                       cwd=REPO, capture_output=True, text=True)
    if p.returncode != 0:
        return None
    body = p.stdout
    if "<%s>:" % symbol not in body:
        return None
    return [int(n) for n in re.findall(r"#(-?\d+)\b", body)]


def facts_for(config):
    facts = {"config": config, "locore_s": None, "releases": {}, "fragments": {}}
    for name, cfg in (("release", "RELEASE"), (config, config)):
        ok, out = sh(["./tools/xnu_config/make_defines.sh", cfg])
        facts["releases"][name] = define_lines(out) if ok else None
    ok, out = sh(["./tools/xnu_config/arm_asm_defines.sh", config])
    facts["asm_defines"] = define_lines(out) if ok else None
    ok, out = sh(["./tools/xnu_config/arm_asm_defines.sh", "--exceptions"])
    facts["exceptions"] = sorted(out.split()) if ok else None
    # The configuration names the tree declares, which is what tells a word in a declaration apart from
    # an option: `select_master.sh`'s output carries the declarations themselves as `NAME#<attrs>`.
    ok, out = sh(["./tools/xnu_config/select_master.sh", config])
    facts["declared_configs"] = (sorted(re.findall(r"^([A-Za-z0-9_]+)#", out, re.M))
                                 if ok else None)
    for name, (path, script) in OBJECTS.items():
        facts[name] = {"path": path, "script": script, "undefined": nm_undefined(path)}
    facts["assym"] = read_assym("out/xnu_assym/%s/assym.s" % config)
    facts["offsets"] = {(obj, sym): symbol_immediates(obj, sym)
                        for _name, sites in OFFSETS for obj, sym, _why in sites}
    path = os.path.join(REPO, FRAGMENT)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            facts["fragment_text"] = f.read()
    if os.path.exists(LOCORE):
        with open(LOCORE, encoding="utf-8") as f:
            facts["locore_s"] = f.read()
    return facts


# ------------------------------------------------------------------------------------------------
# The claims
# ------------------------------------------------------------------------------------------------

def claim_declaration(facts, failures, notes):
    """1. The expansion is the declaration: everything RELEASE has, plus what the fragment names."""
    cfg = facts["config"]
    release = facts["releases"].get("release")
    mine = facts["releases"].get(cfg)
    if release is None:
        failures.append("`make_defines.sh RELEASE` failed: the composition this configuration is "
                        "declared in terms of cannot be read, so claim 1 has no left-hand side")
        return
    if mine is None:
        failures.append("`make_defines.sh %s` failed: the configuration did not expand at all. Ten "
                        "`-D` flags is what an undeclared name expands to - every untagged `options` "
                        "line and no tagged one - so this is the defect this check exists for, one "
                        "step upstream of the objects" % cfg)
        return

    missing = sorted({name_of(d) for d in release} - {name_of(d) for d in mine})
    if missing:
        failures.append("%s's expansion is missing %d of RELEASE's %d option names (%s%s): the "
                        "configuration is declared as `[ RELEASE ... ]` and an undeclared name expands "
                        "to the always-on options only, so the fragment was not found"
                        % (cfg, len(missing), len({name_of(d) for d in release}),
                           ", ".join(missing[:4]), "..." if len(missing) > 4 else ""))
    revalued = sorted(name_of(d) for d in release
                      if d not in mine and name_of(d) not in missing)
    if revalued:
        notes.append("%s gives %s a different value from RELEASE (%s), which is what the "
                     "declaration's attributes are for: `config/MASTER` declares %s twice, once per "
                     "tag" % (cfg, ", ".join(revalued), ", ".join(revalued), revalued[0]))

    text = facts.get("fragment_text")
    if text is None:
        failures.append("no %s, so the declaration `%s = [ ... ]` this configuration is composed "
                        "from cannot be read and the expansion has nothing to be compared with"
                        % (FRAGMENT, cfg))
    else:
        m = re.search(r"^#\s*%s\s*=\s*\[([^\]]*)\]" % re.escape(cfg), text, re.M)
        if not m:
            failures.append("%s no longer declares `%s = [ ... ]`, so nothing says which "
                            "configuration this one is composed from" % (FRAGMENT, cfg))
        else:
            named = m.group(1).split()
            # A word in a declaration is either another configuration or an option, and only the tree
            # can say which: `select_master.sh`'s output carries every declaration as `NAME#<attrs>`
            # for exactly that reason. A word that is neither has no reader anywhere - and the failure
            # it would cause is an *added* attribute, not a missing one, so nothing else would notice.
            known = set(facts.get("declared_configs") or ())
            if not known:
                failures.append("`select_master.sh %s` produced no declarations, so which words of "
                                "`[ %s ]` are configurations cannot be told from options"
                                % (cfg, " ".join(named)))
                return
            def reaches(w):
                if w in known:
                    return True
                return any(d == "-D" + w or d.startswith("-D" + w + "=") for d in mine)
            orphans = [w for w in named if not reaches(w)]
            if orphans:
                failures.append("%s's declaration is `[ %s ]` and %s is neither a configuration the "
                                "tree declares nor an option %s expands to: a name that reaches "
                                "nothing is a word nobody reads, and it leaves the expansion missing "
                                "whatever it was meant to add"
                                % (cfg, " ".join(named), ", ".join(orphans), cfg))
            else:
                notes.append("%s expands to %d defines, every one of RELEASE's %d option names among "
                             "them, and every word of its declaration `[ %s ]` reaches either a "
                             "nested configuration or an option"
                             % (cfg, len(mine), len({name_of(d) for d in release}),
                                " ".join(named)))


def claim_filter(facts, failures, notes):
    """2. The assembly's list is the configuration's minus the exceptions, exactly."""
    mine = facts["releases"].get(facts["config"])
    asm = facts["asm_defines"]
    exceptions = facts["exceptions"]
    if mine is None or asm is None or exceptions is None:
        failures.append("the configuration's expansion or the assembly list or the exception list "
                        "could not be read, so whether the filter drops what it names is not a "
                        "question this run can answer")
        return

    def is_exception(d):
        return any(d == "-D" + e or d.startswith("-D" + e + "=") for e in exceptions)

    expected = sorted(d for d in mine if not is_exception(d))
    dropped = sorted(set(expected) - set(asm))
    added = sorted(set(asm) - set(expected))
    if dropped:
        failures.append("the assembly's define list is missing %d of the configuration's flags (%s): "
                        "an assembly compiled without the configuration's options is the defect this "
                        "check exists for, and it is invisible in the object" % (len(dropped),
                                                                                 ", ".join(dropped[:4])))
    if added:
        failures.append("the assembly's define list carries %d flags the configuration does not set "
                        "(%s): the filter is inventing options" % (len(added), ", ".join(added[:4])))
    if not dropped and not added:
        notes.append("the assembly is given %d of the configuration's %d flags, the %s named "
                     "exception(s) removed and nothing else" % (len(asm), len(mine),
                                                                ", ".join(exceptions)))


def claim_locore(facts, failures, notes):
    """3. The option's two sides agree, read out of Apple's file and out of the objects."""
    text = facts["locore_s"]
    if text is None:
        failures.append("no %s, so the guard this claim is about cannot be read at all" % LOCORE)
        return

    sites = text.count(OPTION)
    if sites < 2:
        failures.append("`%s` appears %d time(s) in `osfmk/arm/locore.s`; the two branches this "
                        "claim compares are guarded by it at nine sites in the version this project "
                        "measured, so a file with %d means the guard is gone or renamed and the "
                        "assembled objects no longer say which branch they took"
                        % (OPTION, sites, sites))
    unset_branch = [f for f in CALLED_WHEN_UNSET if "bl\t\tEXT(%s)" % f in text
                    or "bl EXT(%s)" % f in text]
    if len(unset_branch) != len(CALLED_WHEN_UNSET):
        failures.append("`locore.s` no longer calls %s in its un-guarded spelling (%d of %d found): "
                        "the two names this check looks for in the objects are then not the names the "
                        "file calls, and an object without them would pass for the wrong reason"
                        % (", ".join(CALLED_WHEN_UNSET), len(unset_branch), len(CALLED_WHEN_UNSET)))

    mine = facts["releases"].get(facts["config"]) or []
    skip = any(d == "-D" + OPTION or d.startswith("-D" + OPTION + "=") for d in mine)
    value_on = any(d == "-D" + OPTION + "=1" for d in mine)
    telemetry = any(d.startswith("-DCONFIG_TELEMETRY=") and not d.endswith("=0") for d in mine)
    if not skip:
        failures.append("this configuration does not set %s at all, and every claim below is about "
                        "what that option decides: either the configuration changed or the expansion "
                        "is wrong (which is claim 1)" % OPTION)
        return

    for name, entry in sorted(facts.items()):
        if name not in OBJECTS:
            continue
        undefined = entry.get("undefined")
        if undefined is None:
            failures.append("no %s (`%s`): the ARM layer object this claim is about is not on disk, "
                            "so whether the assembly took the guarded branch is unmeasured"
                            % (entry["path"], entry["script"]))
            continue
        if not value_on:
            continue
        called = [f for f in CALLED_WHEN_UNSET if f in undefined]
        if called:
            failures.append("%s has %s undefined, and this configuration sets %s=1: Apple's "
                            "`machine_routines.c` does not compile those two functions under that "
                            "option, so the object was assembled with the option *unset* - i.e. "
                            "without the configuration's options - and the boot's generated stub for "
                            "each of them is terminal, stopping the first return to user mode"
                            % (entry["path"], " and ".join(called), OPTION))
        # The control: the same file's other branch. `CONFIG_TELEMETRY=1` makes `locore.s` reference
        # `telemetry_needs_record` *and* `telemetry_mark_curthread`, so an object assembled without any
        # options lacks both - and a claim that only looked for absences would call an empty file
        # correct. **Both, not either**: written as "neither of" the claim was satisfied by an object
        # that referenced `telemetry_mark_curthread` alone, and this check's own selftest found it (a
        # mutation that removes one of a pair the claim names as a set).
        if telemetry:
            control = [f for f in CALLED_WHEN_TELEMETRY if f in undefined]
            if len(control) != len(CALLED_WHEN_TELEMETRY):
                failures.append("%s references %d of the %d names the telemetry branch of the same "
                                "`#if` produces (%s), and this configuration sets CONFIG_TELEMETRY: an "
                                "object that neither calls the guarded functions nor takes the "
                                "telemetry branch is not evidence that the guard worked - it is "
                                "evidence that the object is empty, stale or not this file's"
                                % (entry["path"], len(control), len(CALLED_WHEN_TELEMETRY),
                                   " and ".join(CALLED_WHEN_TELEMETRY)))


CLAIMS = (claim_declaration, claim_filter, claim_locore)


def claim_offsets(facts, failures, notes):
    """4. The offsets the assembly is given are the ones the kernel's compiled C uses."""
    assym = facts.get("assym")
    if not assym:
        failures.append("no `out/xnu_assym/%s/assym.s`, or it has no plain `#define NAME #<n>` line: "
                        "every TH_* offset the ARM layer addresses a `struct thread` field through "
                        "comes from this file, and the file it would have been compared with is the "
                        "only other place those offsets are written" % facts["config"])
        return

    for name, sites in OFFSETS:
        if name not in assym:
            failures.append("assym.s publishes no `%s`, so the assembly reaches that field with a "
                            "value the C never agreed to" % name)
            continue
        want = assym[name]
        read = []
        for obj, sym, why in sites:
            imms = facts["offsets"].get((obj, sym))
            if imms is None:
                failures.append("%s's `%s` is not on disk or is empty (`%s`): the second source for "
                                "%s = %d is then missing, and a claim with one source is a restatement"
                                % (obj, sym, why, name, want))
                continue
            if want not in imms:
                failures.append("assym.s says %s = %d and %s's `%s` materialises %s instead (`%s`): the "
                                "assembly and the C disagree about a `struct thread` field's offset, so "
                                "one of the two is addressing the wrong word and no build line sees it"
                                % (name, want, obj, sym,
                                   "nothing near it" if not imms else
                                   " and ".join(str(i) for i in sorted(set(imms))[:6]),
                                   why))
            else:
                read.append("%s's `%s` (%s)" % (obj, sym, why.split(",")[0]))
        if len(read) == len(sites):
            notes.append("assym.s's %s = %d is materialised by the kernel's own C in %s"
                         % (name, want, ", ".join(read)))


CLAIMS = (claim_declaration, claim_filter, claim_locore, claim_offsets)


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

def _drop_one(seq, item):
    out = list(seq)
    out.remove(item)
    return out


def _add(seq, item):
    return sorted(list(seq) + [item])


def mutate_facts(facts, mutate):
    facts = {k: (dict(v) if isinstance(v, dict) else v) for k, v in facts.items()}
    cfg = facts["config"]

    if mutate == "the_fragment_is_not_found":
        # The whole defect, one layer up: an undeclared name expands to the always-on set.
        facts["releases"][cfg] = facts["releases"]["release"][:10]
    elif mutate == "one_of_releases_options_is_missing":
        shared = [d for d in facts["releases"][cfg] if d in facts["releases"]["release"]]
        assert shared, "no flag is shared between RELEASE and the configuration"
        facts["releases"][cfg] = _drop_one(facts["releases"][cfg], shared[len(shared) // 2])
    elif mutate == "the_declaration_is_gone":
        facts["fragment_text"] = facts["fragment_text"].replace("#  STAGE90_XNU = [", "#  STAGE90_XX = [")
    elif mutate == "a_named_word_reaches_nothing":
        facts["fragment_text"] = re.sub(r"(^#\s*%s\s*=\s*\[[^\]]*)\]" % re.escape(cfg),
                                        r"\1 bogusword]" , facts["fragment_text"], count=1, flags=re.M)
    elif mutate == "a_named_configuration_stops_resolving":
        facts["declared_configs"] = _drop_one(facts["declared_configs"], "mockfs")
    elif mutate == "the_assembly_was_given_no_options":
        facts["asm_defines"] = []
    elif mutate == "the_exception_is_not_dropped":
        facts["exceptions"] = []
    elif mutate == "the_filter_drops_an_unnamed_flag":
        facts["asm_defines"] = _drop_one(facts["asm_defines"], sorted(facts["asm_defines"])[3])
    elif mutate == "the_filter_invents_a_flag":
        facts["asm_defines"] = _add(facts["asm_defines"], "-DCONFIG_IMAGINARY=1")
    elif mutate == "the_option_is_gone_from_locore":
        facts["locore_s"] = facts["locore_s"].replace(OPTION, "CONFIG_SOMETHING_ELSE")
    elif mutate == "the_unguarded_call_is_renamed":
        facts["locore_s"] = facts["locore_s"].replace("EXT(%s)" % CALLED_WHEN_UNSET[0],
                                                      "EXT(not_that_function)")
    elif mutate == "the_object_calls_the_guarded_functions":
        facts["kernel_layer"]["undefined"] = _add(facts["kernel_layer"]["undefined"],
                                                  CALLED_WHEN_UNSET[0])
    elif mutate == "the_entry_object_calls_the_guarded_functions":
        facts["entry_image"]["undefined"] = _add(facts["entry_image"]["undefined"],
                                                 CALLED_WHEN_UNSET[1])
    elif mutate == "the_object_loses_the_control":
        facts["kernel_layer"]["undefined"] = _drop_one(facts["kernel_layer"]["undefined"],
                                                       CALLED_WHEN_TELEMETRY[0])
    elif mutate == "the_object_is_not_there":
        facts["kernel_layer"]["undefined"] = None
    elif mutate == "the_configuration_stops_setting_the_option":
        facts["releases"][cfg] = _drop_one(facts["releases"][cfg], "-D" + OPTION + "=1")
    elif mutate == "assym_publishes_a_wrong_offset":
        # The smallest wrongness there is: one field, eight bytes out. This is 468's failure exactly -
        # `TH_CTH_SELF` 1480 against the `#1496` the C reads - and every build line was green.
        facts["assym"] = dict(facts["assym"], **{"TH_CTH_SELF": 1480})
    elif mutate == "the_kernel_object_reads_a_different_offset":
        facts["offsets"] = dict(facts["offsets"])
        key = ("osfmk_arm_machdep_call.o", "thread_get_cthread_self")
        facts["offsets"][key] = [i for i in facts["offsets"][key] if i != 1496] + [1480]
    elif mutate == "one_of_the_second_sources_is_gone":
        facts["offsets"] = dict(facts["offsets"])
        facts["offsets"][("osfmk_arm_trap.o", "sleh_undef")] = None
    elif mutate == "the_offset_is_gone_from_assym":
        facts["assym"] = {k: v for k, v in facts["assym"].items() if k != "TH_KSTACKPTR"}
    elif mutate == "assym_is_not_there":
        facts["assym"] = None
    else:
        raise SystemExit("unknown mutation %s" % mutate)
    return facts


MUTATIONS = (
    "the_fragment_is_not_found", "one_of_releases_options_is_missing", "the_declaration_is_gone",
    "a_named_word_reaches_nothing", "a_named_configuration_stops_resolving",
    "the_assembly_was_given_no_options", "the_exception_is_not_dropped",
    "the_filter_drops_an_unnamed_flag", "the_filter_invents_a_flag",
    "the_option_is_gone_from_locore", "the_unguarded_call_is_renamed",
    "the_object_calls_the_guarded_functions", "the_entry_object_calls_the_guarded_functions",
    "the_object_loses_the_control", "the_object_is_not_there",
    "the_configuration_stops_setting_the_option", "assym_publishes_a_wrong_offset",
    "the_kernel_object_reads_a_different_offset", "one_of_the_second_sources_is_gone",
    "the_offset_is_gone_from_assym", "assym_is_not_there",
)


def selftest(facts):
    failures, _notes = compare(facts)
    if failures:
        print("the baseline fails, so this selftest proves nothing:", file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        return 1
    accepted = []
    for name in MUTATIONS:
        failures, _notes = compare(facts, mutate=name)
        if not failures:
            accepted.append(name)
            print("  ACCEPTED: " + name)
    if accepted:
        print("FAIL: %d of %d mutations were not refused: %s"
              % (len(accepted), len(MUTATIONS), ", ".join(accepted)))
        return 1
    print("  --selftest: all %d mutations were refused" % len(MUTATIONS))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=os.environ.get("XNU_KERNEL_CONFIG", "STAGE90_XNU"))
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    facts = facts_for(args.config)
    if args.selftest:
        return selftest(facts)

    failures, notes = compare(facts)
    if failures:
        print("the ARM assembly and the C are not compiled for the same configuration:",
              file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        return 1
    if args.verbose:
        for n in notes:
            print("    " + n)
    print("  xnu_entry_488: the assembly is given the configuration's own options - the expansion is "
          "the declaration it is composed from, the filter drops only the names it declares, and "
          "neither assembled `locore.o` calls the two functions `%s=1` keeps out of the kernel, while "
          "both take the telemetry branch the same guard selects - and the two `struct thread` offsets "
          "they address fields through are the ones the kernel's own compiled C materialises"
          % OPTION)
    return 0


if __name__ == "__main__":
    sys.exit(main())
