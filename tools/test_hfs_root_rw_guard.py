#!/usr/bin/env python3
"""Host-only regressions for build_entry.sh's actual linked HFS rw-root clause.

The shell clause and say/set helpers are extracted from the builder, not reimplemented here. Only
that clause runs, against fake ARM tools and a dummy image in a temporary directory. PATH exposes
those fakes plus host awk/grep; neither the rest of the builder nor any live out/device path runs.

Usage: python3 tools/test_hfs_root_rw_guard.py
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


BUILDER = Path(__file__).resolve().parents[1] / "src/entry/build_entry.sh"
BASH = shutil.which("bash")
START = "# **905 B3: THE READ-WRITE ROOT CLEAR, READ OUT OF THE LINKED IMAGE.**"
END = "\nif [[ $EMMC_STRATEGY -eq 1 ]]; then\n"
BASE = 0x80001000
SIZE = 0x10
NM_LEGACY = "80000000 00000010 T _start\n80002000 00000004 T vfs_clearflags\n"
NM_HFS = "80000000 00000010 T _start\n80001000 00000010 T hfs_mountroot\n80002000 00000004 T vfs_clearflags\n"
BODY_RO = """xnu_arm_entry.elf:     file format elf32-littlearm

Disassembly of section .text:

80001000 <hfs_mountroot>:
80001000: e92d4000 push {lr}
80001004: e1a00000 mov r0, r0
80001008: eb0007fc bl 80003000 <hfs_mountfs>
8000100c: e8bd8000 pop {pc}
"""
BODY_RW = BODY_RO.replace("e1a00000 mov r0, r0", "eb0003fd bl 80002000 <vfs_clearflags>")


# The fakes verify the tool arguments too: a whole-image dump or a guessed containing-symbol range
# cannot silently satisfy a test whose subject is the exact hfs_mountroot body.
FAKE_TOOL = """import json
import os
from pathlib import Path
import sys

fixture = json.loads(Path(os.environ['HMR_TEST_FIXTURE']).read_text())
tool = Path(sys.argv[0]).name.removeprefix('arm-none-eabi-')
args = sys.argv[1:]
with Path(os.environ['HMR_TEST_LOG']).open('a') as log:
    log.write(json.dumps({'tool': tool, 'args': args}) + '\\n')
if tool in ('nm', 'objdump'):
    image = fixture['image']
    try:
        Path(image).read_bytes()
    except OSError as exc:
        sys.stderr.write('fake ' + tool + ': unreadable image: ' + str(exc) + '\\n')
        sys.exit(1)
    if tool == 'nm':
        valid = args == ['-S', '-n', '--defined-only', image]
    else:
        valid = (len(args) == 5 and args[:2] == ['-d', '-z'] and args[-1] == image
                 and args[2].startswith('--start-address=')
                 and args[3].startswith('--stop-address='))
        if valid:
            valid = (int(args[2].split('=', 1)[1], 0) == fixture['start']
                     and int(args[3].split('=', 1)[1], 0) == fixture['stop'])
    if not valid:
        sys.stderr.write('fake ' + tool + ': unexpected arguments: ' + repr(args) + '\\n')
        sys.exit(97)
response = fixture[tool]
sys.stdout.write(response.get('stdout', ''))
sys.stderr.write(response.get('stderr', ''))
sys.exit(response.get('status', 0))
"""


def extract_shell(source):
    """Fail if the named clause/helpers disappear instead of testing a stale copy of their rule."""
    if source.count(START) != 1:
        raise ValueError("expected exactly one linked HFS rw-root clause")
    start = source.index(START)
    end = source.index(END, start)
    say = re.search(r"^say\(\) \{[^\n]*\}$", source, re.M)
    options = re.search(r"^set -euo pipefail$", source, re.M)
    if not say or not options:
        raise ValueError("cannot extract the builder's say/set helpers")
    return options.group() + "\n" + say.group() + "\n", source[start:end]


class HfsRootRwGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if BASH is None:
            raise RuntimeError("bash is required")
        cls.source = BUILDER.read_text()
        cls.prelude, cls.clause = extract_shell(cls.source)

    def run_clause(self, hdd=0, rw=0, *, nm=NM_HFS, body=BODY_RO, nm_status=0,
                   objdump_status=0, missing_tool=None, unreadable_tool=None,
                   missing_image=False, failing_host_tool=None):
        with tempfile.TemporaryDirectory(prefix="hfs-root-rw-guard-") as directory:
            root = Path(directory)
            image = root / "xnu_arm_entry.elf"
            if not missing_image:
                image.write_bytes(b"host-only dummy image; never passed to real ARM tools\n")
            bindir = root / "bin"
            bindir.mkdir()
            for name in ("awk", "grep"):
                executable = shutil.which(name)
                if executable is None:
                    raise RuntimeError(name + " is required")
                (bindir / name).symlink_to(executable)
            for name in ("arm-none-eabi-nm", "arm-none-eabi-objdump"):
                tool = bindir / name
                tool.write_text("#!" + sys.executable + "\n" + FAKE_TOOL)
                tool.chmod(0o755)
            fixture = {
                "image": str(image), "start": BASE, "stop": BASE + SIZE,
                "nm": {"stdout": nm, "status": nm_status},
                "objdump": {"stdout": body, "status": objdump_status},
            }
            if missing_tool:
                (bindir / missing_tool).unlink()
            if unreadable_tool:
                (bindir / unreadable_tool).chmod(0o644)
            if failing_host_tool:
                tool = bindir / failing_host_tool
                tool.unlink()  # Never write through the host-tool symlink.
                tool.write_text("#!" + sys.executable + "\n" + FAKE_TOOL)
                tool.chmod(0o755)
                fixture[failing_host_tool] = {"status": 2}
            fixture_path = root / "fixture.json"
            fixture_path.write_text(json.dumps(fixture))
            log = root / "calls.jsonl"
            # No inherited BASH_ENV, shell startup files, or real ARM/device commands in PATH.
            env = {
                "PATH": str(bindir), "LC_ALL": "C", "OUT": str(root),
                "HDD_WRITE": str(hdd), "HFS_ROOT_RW": str(rw),
                "HMR_TEST_FIXTURE": str(fixture_path), "HMR_TEST_LOG": str(log),
            }
            result = subprocess.run(
                [BASH, "--noprofile", "--norc", "-c", self.prelude + self.clause],
                cwd=root, env=env, text=True, capture_output=True, timeout=10,
            )
            calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            return result, calls

    def assert_pass(self, result, verdict):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn(verdict, result.stdout)

    def assert_refusal(self, result, reason):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("FAIL:", result.stderr)
        self.assertIn(reason, result.stderr)
        self.assertNotIn("agrees with the linked image", result.stdout)
        self.assertNotIn("not applicable", result.stdout)

    def test_bash_syntax(self):
        result = subprocess.run([BASH, "-n", str(BUILDER)], text=True, capture_output=True,
                                env={"PATH": os.defpath, "LC_ALL": "C"}, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_hdd_write_still_requires_hfs_root_rw(self):
        start = self.source.index("if [[ $HDD_WRITE -eq 1 && $HFS_ROOT_RW -ne 1 ]]; then\n")
        end = self.source.index("\nfi\n", start) + len("\nfi\n")
        result = subprocess.run(
            [BASH, "--noprofile", "--norc", "-c", self.prelude + self.source[start:end]],
            env={"PATH": os.defpath, "HDD_WRITE": "1", "HFS_ROOT_RW": "0"},
            text=True, capture_output=True, timeout=10,
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("STAGE90_XNU_HDD_WRITE=1 needs STAGE90_XNU_HFS_ROOT_RW=1", result.stderr)

    def test_ordinary_ro_record_refuses_stale_rw_pool(self):
        result, _ = self.run_clause(0, 0, body=BODY_RW)
        self.assert_refusal(result, "DOES call vfs_clearflags")

    def test_ordinary_ro_record_checks_ro_body_and_prints_verdict(self):
        result, calls = self.run_clause(0, 0)
        self.assert_pass(result, "STAGE90_XNU_HFS_ROOT_RW=0 agrees with the linked image")
        self.assertIn("has NO call to vfs_clearflags", result.stdout)
        self.assertEqual([call["tool"] for call in calls], ["nm", "objdump"])

    def test_rw_record_checks_rw_body_and_prints_verdict(self):
        result, _ = self.run_clause(1, 1, body=BODY_RW)
        self.assert_pass(result, "STAGE90_XNU_HFS_ROOT_RW=1 agrees with the linked image")
        self.assertIn("CALLS vfs_clearflags", result.stdout)

    def test_rw_root_without_hdd_write_still_checks_body(self):
        result, _ = self.run_clause(0, 1, body=BODY_RW)
        self.assert_pass(result, "CALLS vfs_clearflags")

    def test_rw_record_refuses_missing_clear(self):
        for hdd in (0, 1):
            with self.subTest(hdd=hdd):
                result, _ = self.run_clause(hdd, 1)
                self.assert_refusal(result, "does NOT call vfs_clearflags")

    def test_legacy_no_hfs_ro_link_skips_without_claiming_checked_body(self):
        result, calls = self.run_clause(0, 0, nm=NM_LEGACY)
        self.assert_pass(result, "no defined hfs_mountroot")
        self.assertIn("not applicable", result.stdout)
        self.assertNotIn("agrees with the linked image", result.stdout)
        self.assertNotIn("CALLS vfs_clearflags", result.stdout)
        self.assertEqual([call["tool"] for call in calls], ["nm"])

    def test_required_hfs_missing_definition_refuses(self):
        for hdd in (0, 1):
            with self.subTest(hdd=hdd):
                result, calls = self.run_clause(hdd, 1, nm=NM_LEGACY)
                self.assert_refusal(result, "not a defined (T) symbol")
                self.assertEqual([call["tool"] for call in calls], ["nm"])

    def test_wrong_symbol_kind_is_not_a_defined_body(self):
        result, _ = self.run_clause(0, 1, nm=NM_HFS.replace("T hfs_mountroot", "D hfs_mountroot"))
        self.assert_refusal(result, "not a defined (T) symbol")

    def test_missing_own_body_bound_cannot_borrow_containing_symbol_size(self):
        nm = ("80000000 00000010 T _start\n80000ff0 00000100 T containing_symbol\n"
              "80001000 T hfs_mountroot\n")
        for rw in (0, 1):
            with self.subTest(rw=rw):
                result, calls = self.run_clause(0, rw, nm=nm)
                self.assert_refusal(result, "cannot bound its body")
                self.assertEqual([call["tool"] for call in calls], ["nm"])

    def test_zero_and_overflowing_body_bounds_refuse(self):
        for row in ("80001000 00000000 T hfs_mountroot\n",
                    "fffffff0 00000020 T hfs_mountroot\n"):
            with self.subTest(row=row):
                result, _ = self.run_clause(nm=NM_LEGACY + row)
                self.assert_refusal(result, "cannot bound its body")

    def test_duplicate_definition_is_malformed_not_ro(self):
        result, _ = self.run_clause(nm=NM_HFS + "80001000 00000010 T hfs_mountroot\n")
        self.assert_refusal(result, "malformed or empty linked symbol table")

    def test_empty_and_malformed_symbol_results_fail_closed(self):
        for nm in ("", "not a symbol table\n", "nothex 00000010 T hfs_mountroot\n",
                   "80001000 nothex T hfs_mountroot\n", "80001000 00000010 TT hfs_mountroot\n"):
            with self.subTest(nm=nm):
                result, _ = self.run_clause(nm=nm)
                self.assert_refusal(result, "malformed or empty linked symbol table")

    def test_nm_error_cannot_impersonate_ro_or_legacy_result(self):
        for nm in (NM_HFS, NM_LEGACY):
            with self.subTest(nm=nm):
                result, _ = self.run_clause(nm=nm, nm_status=1)
                self.assert_refusal(result, "cannot read the linked")

    def test_missing_and_unexecutable_nm_fail_closed(self):
        for option in ("missing_tool", "unreadable_tool"):
            with self.subTest(option=option):
                result, _ = self.run_clause(**{option: "arm-none-eabi-nm"})
                self.assert_refusal(result, "cannot read the linked")

    def test_unreadable_image_fails_closed(self):
        result, _ = self.run_clause(missing_image=True)
        self.assert_refusal(result, "cannot read the linked")
        self.assertIn("unreadable image", result.stderr)

    def test_objdump_error_with_ro_stdout_still_refuses(self):
        result, _ = self.run_clause(objdump_status=1)
        self.assert_refusal(result, "cannot disassemble the linked hfs_mountroot body")

    def test_missing_and_unexecutable_objdump_fail_closed(self):
        for option in ("missing_tool", "unreadable_tool"):
            with self.subTest(option=option):
                result, _ = self.run_clause(**{option: "arm-none-eabi-objdump"})
                self.assert_refusal(result, "cannot disassemble the linked hfs_mountroot body")

    def test_empty_truncated_and_malformed_bodies_fail_closed(self):
        bodies = (
            "", "80001000 <hfs_mountroot>:\n", "tool succeeded but emitted junk\n",
            BODY_RO.replace("<hfs_mountroot>:", "<other_function>:"),
            BODY_RO.replace("80001000 <hfs_mountroot>:", "80001004 <hfs_mountroot>:"),
            BODY_RO.replace("e1a00000 mov", "nothex mov"),
            BODY_RO.replace("e1a00000 mov", "e1a00000 ???"),
            BODY_RO.replace("80001004: e1a00000 mov r0, r0\n", ""),
            BODY_RO.replace("8000100c: e8bd8000 pop {pc}\n", ""),
        )
        for body in bodies:
            with self.subTest(body=body):
                result, _ = self.run_clause(body=body)
                self.assert_refusal(result, "incomplete or malformed linked hfs_mountroot disassembly")

    def test_body_outside_exact_bounds_is_not_accepted(self):
        body = BODY_RO + "80001010 <next_function>:\n80001010: eb0003fc bl 80002000 <vfs_clearflags>\n"
        result, _ = self.run_clause(body=body)
        self.assert_refusal(result, "incomplete or malformed linked hfs_mountroot disassembly")

    def test_non_call_reference_does_not_count_as_rw_clear(self):
        body = BODY_RO.replace("mov r0, r0", "ldr r0, [pc, #0] ; bl <vfs_clearflags>")
        result, _ = self.run_clause(body=body)
        self.assert_pass(result, "has NO call to vfs_clearflags")
        result, _ = self.run_clause(0, 1, body=body)
        self.assert_refusal(result, "does NOT call vfs_clearflags")

    def test_call_scan_error_is_not_clear_off(self):
        result, _ = self.run_clause(failing_host_tool="grep")
        self.assert_refusal(result, "cannot test the linked hfs_mountroot calls")

    def test_symbol_parser_error_is_not_legacy_no_hfs(self):
        result, _ = self.run_clause(nm=NM_LEGACY, failing_host_tool="awk")
        self.assert_refusal(result, "malformed or empty linked symbol table")


if __name__ == "__main__":
    unittest.main(verbosity=2)
