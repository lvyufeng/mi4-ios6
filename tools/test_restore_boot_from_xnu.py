#!/usr/bin/env python3
"""Host-only regression tests. Never run the production helper against a device.

Run: python3 tools/test_restore_boot_from_xnu.py
Each invocation runs a COPY of the shell script in a temporary checkout, with
PATH-leading fake adb/fastboot which reject unexpected commands. Success cases
pin a synthetic, full 32-MiB fixture's hash ONLY in that temporary script copy;
production has no hash/path override. An unmodified-copy test proves that the
production b2119252 identity rejects that fixture before contacting any device.
GNU timeout remains real, including the tests with deliberately hanging fakes.
"""

import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/restore_boot_from_xnu.sh"
PRODUCTION_SHA = "b2119252d046aa2e8e682674949bc5c60a9536358ad83a2c34dfb2de060b4874"
BYTES = 33554432
SERIAL = "4a2fe00b"
FORBIDDEN = "33e80afe"
HASH_ASSIGNMENT = f'readonly EXPECTED_SHA256="{PRODUCTION_SHA}"'

# This shim models only the helper's protocol; it does not execute remote shell
# text, invoke other tools, or touch any host /dev node. Binary files are all
# inside FAKE_RESTORE_STATE_DIR. Unexpected calls are logged as protocol errors.
FAKE_TOOL = r'''
import json
import os
from pathlib import Path
import re
import shutil
import signal
import sys
import time

root = Path(os.environ["FAKE_RESTORE_STATE_DIR"])
cfg = json.loads((root / "config.json").read_text())
counts_path = root / "counts.json"
counts = json.loads(counts_path.read_text()) if counts_path.exists() else {}
tool = Path(sys.argv[0]).name
args = sys.argv[1:]
with (root / "calls.jsonl").open("a") as log:
    log.write(json.dumps({"tool": tool, "args": args}) + "\n")

def error(message):
    with (root / "protocol-errors.txt").open("a") as log:
        log.write(message + "\n")
    print("FAKE PROTOCOL ERROR: " + message, file=sys.stderr)
    sys.exit(97)

def bump(key):
    counts[key] = counts.get(key, 0) + 1
    counts_path.write_text(json.dumps(counts))
    return counts[key]

def out(value):
    if value is not None:
        sys.stdout.write(str(value) + ("\r\n" if cfg.get("crlf") else "\n"))
    sys.exit(0)

def hang(label):
    if cfg.get("hang_at") == label:
        if cfg.get("ignore_term"):
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
        time.sleep(60)

def guard_present(command):
    required = [
        'test "$(id -u)" = 0',
        "test -b '/dev/block/mmcblk0p19'",
        "test -b '/dev/block/platform/msm_sdcc.1/by-name/boot'",
        'test "$(readlink -f',
        'test "$(cat',
        "'/sys/class/block/mmcblk0p19/start'",
        "'/sys/class/block/mmcblk0p19/size'",
        "= '393216'", "= '65536'",
    ]
    if not all(part in command for part in required):
        error("p19 action missing its in-command guard: " + command)

def guard_ok():
    return (str(cfg.get("uid", "0")) == "0"
            and cfg.get("node", "/dev/block/mmcblk0p19") == "/dev/block/mmcblk0p19"
            and str(cfg.get("start", "393216")) == "393216"
            and str(cfg.get("sectors", "65536")) == "65536")

def binary(path, phase):
    hang("adb:" + phase)
    amount = cfg.get(phase + "_bytes")
    size = path.stat().st_size
    remaining = size if amount is None else min(size, amount)
    if cfg.get(phase + "_fail"):
        remaining = min(remaining, 1048576)
    with path.open("rb") as stream:
        while remaining:
            block = stream.read(min(1048576, remaining))
            if not block:
                break
            sys.stdout.buffer.write(block)
            remaining -= len(block)
    if amount is not None and amount > size:
        sys.stdout.buffer.write(b"X" * (amount - size))
    sys.stdout.buffer.flush()
    sys.exit(1 if cfg.get(phase + "_fail") else 0)

if tool == "fastboot":
    if args != ["devices"]:
        error("fastboot may only inventory: " + repr(args))
    inventory = bump("fastboot_inventory")
    hang("fastboot:devices")
    if cfg.get("fastboot_fail"):
        sys.exit(1)
    listing = cfg.get("fastboot_devices", "")
    if inventory == cfg.get("forbidden_inventory"):
        listing += "33e80afe\tfastboot\n"
    sys.stdout.write(listing)
    sys.exit(0)

if tool != "adb" or args[:2] != ["-s", "4a2fe00b"]:
    error("every adb call must pin the serial: " + repr(args))
args = args[2:]
if not args:
    error("missing adb operation")
operation = args[0]
hang("adb:" + operation)
if operation == "devices" and len(args) == 1:
    bump("adb_inventory")
    if cfg.get("adb_fail"):
        sys.exit(1)
    sys.stdout.write(cfg.get("adb_devices", "List of devices attached\n4a2fe00b\tdevice\n\n"))
    sys.exit(0)
if operation == "get-state" and len(args) == 1:
    if cfg.get("state_fail"):
        sys.exit(1)
    out(cfg.get("state", "device"))
if operation == "push" and len(args) == 3:
    bump("pushes")
    source, destination = args[1:]
    if not re.fullmatch(r"/(tmp|data/local/tmp)/boot-p19-restore-[0-9]{8}T[0-9]{6}Z-[A-Za-z0-9]+/rollback\.img", destination):
        error("unexpected push target: " + destination)
    if cfg.get("push_fail"):
        sys.exit(1)
    shutil.copyfile(source, root / "staged.bin")
    if cfg.get("corrupt_upload"):
        with (root / "staged.bin").open("r+b") as stream:
            stream.write(b"changed")
    if cfg.get("short_upload"):
        with (root / "staged.bin").open("r+b") as stream:
            stream.truncate(33554431)
    out("1 file pushed")
if operation not in ("shell", "exec-out") or len(args) != 2:
    error("unsupported adb operation: " + repr(args))
command = args[1]
if any(name in command for name in ("mmcblk0p9'", "mmcblk0p20", "of='/dev/block/mmcblk0'")):
    error("forbidden partition mentioned: " + command)

if operation == "exec-out":
    bump("binary_dumps")
    if "if='/dev/block/mmcblk0p19'" in command:
        guard_present(command)
        if not guard_ok():
            sys.exit(0)  # emulate an old adb that loses remote failure status
        phase = "before" if bump("partition_reads") == 1 else "readback"
        binary(root / "p19.bin", phase)
    if "/rollback.img' bs=1048576" in command:
        if not ("test -f" in command and "test ! -L" in command):
            error("upload dump lacks regular-file guard")
        binary(root / "staged.bin", "upload")
    error("unexpected binary command: " + command)

if command == "id -u":
    out(cfg.get("uid", "0"))
if command.startswith("test -b '/dev/block/mmcblk0p19'") and "readlink -f" in command:
    out(cfg.get("node", "/dev/block/mmcblk0p19"))
if command == "cat '/sys/class/block/mmcblk0p19/start'":
    out(cfg.get("start", "393216"))
if command == "cat '/sys/class/block/mmcblk0p19/size'":
    out(cfg.get("sectors", "65536"))
if command == "getprop ro.bootmode":
    out(cfg.get("bootmode", "normal"))
if command == "getprop ro.twrp.version":
    out(cfg.get("twrp_version", ""))
if "BOOT_P19_TEMP_OK" in command:
    if not command.startswith("test -d ") or "test -w " not in command:
        error("temp access check must be read-only")
    out(None if cfg.get("temp_unwritable") else "BOOT_P19_TEMP_OK")
if "BOOT_P19_STAGE_OK" in command:
    bump("stages")
    if "mkdir '" not in command or "conv=fsync" not in command or "/fsync-probe'" not in command:
        error("invalid disposable fsync probe")
    out(None if cfg.get("stage_fail") else "BOOT_P19_STAGE_OK")
if "BOOT_P19_SEALED_OK" in command:
    if "test ! -L" not in command or "chmod 400" not in command:
        error("invalid seal guard")
    out(None if cfg.get("seal_fail") else "BOOT_P19_SEALED_OK")
if "BOOT_P19_WRITE_FSYNC_OK" in command:
    bump("write_attempts")
    guard_present(command)
    if "of='/dev/block/mmcblk0p19' bs=1048576 count=32 conv=fsync" not in command:
        error("invalid dd write output, size limit, or fsync")
    if not guard_ok() or cfg.get("write_guard_fail") or cfg.get("write_missing_marker"):
        sys.exit(0)
    if cfg.get("write_fail"):
        sys.exit(1)
    shutil.copyfile(root / "staged.bin", root / "p19.bin")
    bump("writes")
    if cfg.get("corrupt_write"):
        with (root / "p19.bin").open("r+b") as stream:
            stream.write(b"bad readback")
    hang("adb:write")
    out("BOOT_P19_WRITE_FSYNC_OK")
if "BOOT_P19_CLEAN_OK" in command:
    bump("cleanups")
    if "rm -f" not in command or "rmdir" not in command:
        error("invalid bounded cleanup")
    out(None if cfg.get("cleanup_fail") else "BOOT_P19_CLEAN_OK")
error("unsupported shell command: " + command)
'''


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            result.update(block)
    return result.hexdigest()


class RestoreBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixtures_temp = tempfile.TemporaryDirectory(prefix="restore-boot-fixtures-")
        fixtures = Path(cls.fixtures_temp.name)
        cls.trusted_fixture = fixtures / "trusted.bin"
        cls.before_fixture = fixtures / "before.bin"
        # Includes NUL, CR, LF and all byte values: text-mode binary corruption
        # would fail the byte-for-byte fingerprint checks in the happy path.
        for path, block in ((cls.trusted_fixture, bytes(range(256)) * 4096),
                            (cls.before_fixture, bytes(reversed(range(256))) * 4096)):
            with path.open("wb") as stream:
                for _ in range(32):
                    stream.write(block)
        cls.fixture_sha = digest(cls.trusted_fixture)
        cls.before_sha = digest(cls.before_fixture)
        cls.production_source = SCRIPT.read_text()
        if cls.production_source.count(HASH_ASSIGNMENT) != 1:
            raise AssertionError("production hash must have one fixed assignment")
        if "readonly EXPECTED_BYTES=33554432" not in cls.production_source:
            raise AssertionError("production full partition size is not pinned")

    @classmethod
    def tearDownClass(cls):
        cls.fixtures_temp.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="restore-boot-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        self.backups = self.root / "out/stage90/backups"
        self.backups.mkdir(parents=True)
        self.rollback = self.backups / "boot_now.img"
        shutil.copyfile(self.trusted_fixture, self.rollback)
        self.script = self.root / "scripts/restore_boot_from_xnu.sh"
        self.script.write_text(self.production_source.replace(
            HASH_ASSIGNMENT, f'readonly EXPECTED_SHA256="{self.fixture_sha}"'))
        self.bin = self.root / "fake-bin"
        self.bin.mkdir()
        for name in ("adb", "fastboot"):
            executable = self.bin / name
            executable.write_text(f"#!{sys.executable}\n" + FAKE_TOOL)
            executable.chmod(0o755)
        self.state = self.root / "fake-state"
        self.state.mkdir()
        shutil.copyfile(self.before_fixture, self.state / "p19.bin")
        self.config = {}

    def invoke(self, *args, ok=None):
        (self.state / "config.json").write_text(json.dumps(self.config))
        env = {
            "PATH": str(self.bin) + os.pathsep + os.defpath,
            "HOME": str(self.root),
            "LC_ALL": "C",
            "FAKE_RESTORE_STATE_DIR": str(self.state),
        }
        result = subprocess.run(
            ["/bin/bash", str(self.script), *args],
            cwd=self.root, env=env, capture_output=True, text=True, timeout=20,
        )
        protocol_errors = self.state / "protocol-errors.txt"
        self.assertFalse(protocol_errors.exists(),
                         protocol_errors.read_text() if protocol_errors.exists() else "")
        if ok is True:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        elif ok is False:
            # The refusal protocol is fixed: exit 1 with a REFUSED line on stderr.
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertTrue(
                any(line.startswith("REFUSED:") for line in result.stderr.splitlines()),
                result.stderr)
            self.assertNotIn("SUCCESS:", result.stdout)
        # ALL logged adb calls must specify the fixed serial, including devices.
        for call in self.calls():
            if call["tool"] == "adb":
                self.assertEqual(call["args"][:2], ["-s", SERIAL])
            else:
                self.assertEqual(call, {"tool": "fastboot", "args": ["devices"]})
        return result

    def calls(self):
        path = self.state / "calls.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def counts(self):
        path = self.state / "counts.json"
        return json.loads(path.read_text()) if path.exists() else {}

    def run_dirs(self):
        return sorted(self.backups.glob("boot-p19-restore-*"))

    def assert_no_write(self):
        self.assertEqual(self.counts().get("write_attempts", 0), 0)
        self.assertEqual(digest(self.state / "p19.bin"), self.before_sha)

    def assert_no_push(self):
        self.assertEqual(self.counts().get("pushes", 0), 0)
        self.assert_no_write()

    def test_script_parses_under_bash(self):
        result = subprocess.run(["/bin/bash", "-n", str(SCRIPT)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_every_adb_call_pins_the_fixed_serial(self):
        # Run a full execute success path, then pin the invariant explicitly.
        self.invoke("--execute", ok=True)
        adb_calls = [call for call in self.calls() if call["tool"] == "adb"]
        self.assertTrue(adb_calls, "expected at least one adb call")
        for call in adb_calls:
            self.assertEqual(call["args"][:2], ["-s", SERIAL])
        inline = [call for call in self.calls() if "-s" in call["args"]]
        self.assertEqual(inline, adb_calls)
        # fastboot devices is called WITHOUT -s.
        for call in self.calls():
            if call["tool"] == "fastboot":
                self.assertEqual(call["args"], ["devices"])

    def test_production_identity_refuses_synthetic_fixture_before_device_contact(self):
        self.script.write_text(self.production_source)
        result = self.invoke("--execute", ok=False)
        self.assertIn(PRODUCTION_SHA, result.stderr)
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.run_dirs(), [])

    def test_help_has_no_device_contact(self):
        self.rollback.unlink()
        result = self.invoke("--help", ok=True)
        self.assertIn("Default DRY RUN", result.stdout)
        self.assertEqual(self.calls(), [])

    def test_unknown_and_unbounded_arguments_are_refused(self):
        for args in (("--fastboot",), ("--execute", "--execute"),
                     ("--timeout-seconds",), ("--timeout-seconds", "0"),
                     ("--timeout-seconds", "121"), ("--timeout-seconds", "-1"),
                     ("--timeout-seconds", "1s"), ("--timeout-seconds", "999999999999999999")):
            with self.subTest(args=args):
                self.invoke(*args, ok=False)
                self.assertEqual(self.calls(), [])

    def test_missing_short_oversized_and_corrupt_rollback_stop_before_contact(self):
        for kind in ("missing", "short", "oversized", "changed"):
            with self.subTest(kind=kind):
                shutil.copyfile(self.trusted_fixture, self.rollback)
                if kind == "missing":
                    self.rollback.unlink()
                else:
                    with self.rollback.open("r+b") as stream:
                        if kind == "short":
                            stream.truncate(BYTES - 1)
                        elif kind == "oversized":
                            stream.truncate(BYTES + 1)
                        else:
                            stream.write(b"wrong")
                self.invoke("--execute", ok=False)
                self.assertEqual(self.calls(), [])
                self.assertEqual(self.run_dirs(), [])

    def test_default_dry_run_is_read_only(self):
        result = self.invoke(ok=True)
        self.assertIn("DRY RUN", result.stdout)
        self.assertNotIn("SUCCESS:", result.stdout)
        self.assert_no_push()
        self.assertEqual(self.run_dirs(), [])
        self.assertFalse((self.backups / ".restore_boot_p19.lock").exists())
        self.assertEqual(self.counts().get("stages", 0), 0)
        self.assertEqual(self.counts().get("binary_dumps", 0), 0)
        self.assertEqual(self.counts()["adb_inventory"], 1)
        self.assertEqual(self.counts()["fastboot_inventory"], 1)

    def test_dry_run_accepts_recovery_and_crlf(self):
        self.config = {"bootmode": "recovery", "state": "recovery", "crlf": True,
                       "adb_devices": "List of devices attached\r\n4a2fe00b\trecovery\r\n"}
        result = self.invoke(ok=True)
        self.assertIn("stage under /tmp", result.stdout)
        self.assert_no_push()

    def test_forbidden_serial_in_either_list_even_unauthorized(self):
        for transport in ("adb", "fastboot"):
            with self.subTest(transport=transport):
                self.config = ({"adb_devices": f"List of devices attached\n{SERIAL}\tdevice\n{FORBIDDEN}\tunauthorized\n"}
                               if transport == "adb" else {"fastboot_devices": f"{FORBIDDEN}\tfastboot\n"})
                result = self.invoke("--execute", ok=False)
                self.assertIn(FORBIDDEN, result.stderr)
                self.assert_no_push()
                # Both transports were inventoried even when adb had the peer.
                self.assertEqual(self.counts()["adb_inventory"], self.counts()["fastboot_inventory"])

    def test_absent_offline_unauthorized_duplicate_or_fastboot_target(self):
        scenarios = (
            {"adb_devices": "List of devices attached\n"},
            {"adb_devices": f"List of devices attached\n{SERIAL}\toffline\n"},
            {"adb_devices": f"List of devices attached\n{SERIAL}\tunauthorized\n"},
            {"adb_devices": f"List of devices attached\n{SERIAL}\tdevice\n{SERIAL}\tdevice\n"},
            {"fastboot_devices": f"{SERIAL}\tfastboot\n"},
        )
        for config in scenarios:
            with self.subTest(config=config):
                self.config = config
                self.invoke("--execute", ok=False)
                self.assert_no_push()

    def test_other_nonforbidden_device_cannot_change_adb_selection(self):
        self.config = {"adb_devices": f"List of devices attached\nother-phone\tdevice\n{SERIAL}\tdevice\n"}
        self.invoke(ok=True)
        self.assert_no_push()

    def test_inventory_or_get_state_failure_stops(self):
        for config in ({"adb_fail": True}, {"fastboot_fail": True},
                       {"state_fail": True}, {"state": "offline"}):
            with self.subTest(config=config):
                self.config = config
                self.invoke("--execute", ok=False)
                self.assert_no_push()

    def test_root_is_exact_uid_zero_not_substring(self):
        for uid in ("1000", "uid=0(root)", "1000 uid=0", "00", "0\nextra", ""):
            with self.subTest(uid=uid):
                self.config = {"uid": uid}
                result = self.invoke("--execute", ok=False)
                self.assertIn("uid must be exactly 0", result.stderr)
                self.assert_no_push()

    def test_wrong_or_missing_by_name_and_sysfs_geometry(self):
        for config in ({"node": "/dev/block/mmcblk0p20"}, {"node": None},
                       {"start": "393215"}, {"start": None},
                       {"sectors": "65535"}, {"sectors": None},
                       {"sectors": "65536 junk"}):
            with self.subTest(config=config):
                self.config = config
                self.invoke("--execute", ok=False)
                self.assert_no_push()
                self.assertEqual(self.run_dirs(), [])

    def test_temp_base_must_confirm_root_access(self):
        self.config = {"temp_unwritable": True}
        self.invoke("--execute", ok=False)
        self.assert_no_push()
        self.assertEqual(self.run_dirs(), [])

    def test_execute_android_preserves_full_before_and_matches_full_readback(self):
        result = self.invoke("--execute", ok=True)
        self.assertEqual(result.stdout.count("SUCCESS:"), 1)
        self.assertIn(self.fixture_sha, result.stdout)
        self.assertEqual(digest(self.rollback), self.fixture_sha)
        self.assertEqual(digest(self.state / "p19.bin"), self.fixture_sha)
        directories = self.run_dirs()
        self.assertEqual(len(directories), 1)
        record = directories[0]
        self.assertRegex(record.name, r"^boot-p19-restore-\d{8}T\d{6}Z-[A-Za-z0-9]+$")
        for name, expected in (("p19-before.img", self.before_sha),
                               ("p19-readback.img", self.fixture_sha),
                               ("trusted-rollback.img", self.fixture_sha),
                               ("upload-check.img", self.fixture_sha)):
            image = record / name
            self.assertEqual(image.stat().st_size, BYTES)
            self.assertEqual(digest(image), expected)
        self.assertIn(self.before_sha, (record / "p19-before.img.sha256").read_text())
        self.assertIn(self.fixture_sha, (record / "p19-readback.img.sha256").read_text())
        counts = self.counts()
        self.assertEqual(counts["writes"], 1)
        self.assertEqual(counts["partition_reads"], 2)
        self.assertEqual(counts["binary_dumps"], 3)
        self.assertEqual(counts["cleanups"], 1)
        pushes = [call for call in self.calls() if call["tool"] == "adb" and call["args"][2] == "push"]
        self.assertEqual(len(pushes), 1)
        self.assertEqual(pushes[0]["args"][3], str(record / "trusted-rollback.img"))
        self.assertTrue(pushes[0]["args"][4].startswith("/data/local/tmp/"))
        # All p19 reads MUST use binary exec-out. Only the fsynced write may be shell.
        p19_reads = [call for call in self.calls() if "dd if='/dev/block/mmcblk0p19'" in " ".join(call["args"])]
        self.assertEqual(len(p19_reads), 2)
        self.assertTrue(all(call["args"][2] == "exec-out" for call in p19_reads))

    def test_execute_recovery_uses_tmp_even_if_only_twrp_property_identifies_it(self):
        self.config = {"twrp_version": "3.7.0_9-0", "bootmode": "unknown"}
        self.invoke("--execute", ok=True)
        pushes = [call for call in self.calls() if call["tool"] == "adb" and call["args"][2] == "push"]
        self.assertTrue(pushes[0]["args"][4].startswith("/tmp/"))

    def test_repeated_execute_never_overwrites_original_or_before_record(self):
        self.invoke("--execute", ok=True)
        first = self.run_dirs()[0]
        first_hash = digest(first / "p19-before.img")
        first_push = [call for call in self.calls() if call["tool"] == "adb" and call["args"][2] == "push"][0]
        (self.state / "calls.jsonl").unlink()
        self.invoke("--execute", ok=True)
        self.assertEqual(len(self.run_dirs()), 2)
        self.assertEqual(digest(first / "p19-before.img"), first_hash)
        self.assertEqual(digest(self.rollback), self.fixture_sha)
        # Each run uses its own distinct remote temp directory.
        second_push = [call for call in self.calls() if call["tool"] == "adb" and call["args"][2] == "push"][0]
        self.assertNotEqual(first_push["args"][4], second_push["args"][4])

    def test_host_lock_blocks_concurrent_execute(self):
        with (self.backups / ".restore_boot_p19.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.invoke("--execute", ok=False)
        self.assertIn("holds the host lock", result.stderr)
        self.assert_no_push()
        self.assertEqual(self.run_dirs(), [])

    def test_before_dump_short_large_or_failed_blocks_push(self):
        for config in ({"before_bytes": BYTES - 1}, {"before_bytes": BYTES + 1}, {"before_fail": True}):
            with self.subTest(config=config):
                self.config = config
                # Reset the fake's phase counter between attempts in this case.
                (self.state / "counts.json").unlink(missing_ok=True)
                self.invoke("--execute", ok=False)
                self.assert_no_push()
                newest = self.run_dirs()[-1]
                self.assertTrue((newest / "p19-before.img.partial").exists())
                self.assertFalse((newest / "p19-before.img").exists())

    def test_fsync_probe_must_confirm_before_push(self):
        self.config = {"stage_fail": True}
        self.invoke("--execute", ok=False)
        self.assert_no_push()
        self.assertEqual(self.counts()["stages"], 1)
        self.assertTrue((self.run_dirs()[0] / "p19-before.img").exists())

    def test_push_seal_or_upload_failure_blocks_partition_write(self):
        for config in ({"push_fail": True}, {"seal_fail": True},
                       {"corrupt_upload": True}, {"short_upload": True}, {"upload_fail": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                self.invoke("--execute", ok=False)
                self.assert_no_write()

    def test_late_forbidden_device_blocks_push_or_write(self):
        for inventory in (2, 3):
            with self.subTest(inventory=inventory):
                self.config = {"forbidden_inventory": inventory}
                (self.state / "counts.json").unlink(missing_ok=True)
                result = self.invoke("--execute", ok=False)
                self.assertIn(FORBIDDEN, result.stderr)
                self.assert_no_write()
                self.assertEqual(self.counts().get("pushes", 0), 0 if inventory == 2 else 1)

    def test_failed_write_missing_marker_or_changed_guard_stops_without_readback(self):
        for config in ({"write_fail": True}, {"write_missing_marker": True}, {"write_guard_fail": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                result = self.invoke("--execute", ok=False)
                self.assertIn("p19 restore NOT verified", result.stderr)
                self.assertEqual(self.counts()["partition_reads"], 1)
                self.assertEqual(self.counts().get("cleanups", 0), 0)
                self.assertEqual(self.calls()[-1]["args"][2], "shell")
                self.assertIn("BOOT_P19_WRITE_FSYNC_OK", self.calls()[-1]["args"][3])

    def test_readback_short_long_failed_or_wrong_hash_never_prints_success(self):
        for config in ({"readback_bytes": BYTES - 1}, {"readback_bytes": BYTES + 1},
                       {"readback_fail": True}, {"corrupt_write": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                shutil.copyfile(self.before_fixture, self.state / "p19.bin")
                result = self.invoke("--execute", ok=False)
                self.assertIn("p19 restore NOT verified", result.stderr)
                self.assertEqual(self.counts()["writes"], 1)
                self.assertEqual(self.counts().get("cleanups", 0), 0)
                self.assertEqual(self.calls()[-1]["args"][2], "exec-out")

    def test_cleanup_failure_is_not_reported_as_success(self):
        self.config = {"cleanup_fail": True}
        result = self.invoke("--execute", ok=False)
        self.assertIn("verified readback, but temp cleanup", result.stderr)
        self.assertEqual(digest(self.state / "p19.bin"), self.fixture_sha)

    def test_real_timeout_bounds_hanging_adb_inventory(self):
        self.config = {"hang_at": "adb:devices"}
        started = time.monotonic()
        result = self.invoke("--timeout-seconds", "1", "--execute", ok=False)
        self.assertLess(time.monotonic() - started, 6)
        self.assertIn("bounded adb devices", result.stderr)
        self.assert_no_push()
        self.assertEqual(self.calls()[-1]["tool"], "adb")

    def test_real_timeout_kills_hanging_fastboot_even_if_term_is_ignored(self):
        self.config = {"hang_at": "fastboot:devices", "ignore_term": True}
        started = time.monotonic()
        result = self.invoke("--timeout-seconds", "1", "--execute", ok=False)
        self.assertLess(time.monotonic() - started, 7)
        self.assertIn("bounded fastboot devices", result.stderr)
        self.assert_no_push()
        self.assertEqual(self.calls()[-1]["tool"], "fastboot")

    def test_timed_out_write_is_unknown_and_does_not_reboot_or_retry(self):
        self.config = {"hang_at": "adb:write"}
        result = self.invoke("--timeout-seconds", "1", "--execute", ok=False)
        self.assertIn("may have continued", result.stderr)
        self.assertEqual(self.counts()["write_attempts"], 1)
        self.assertEqual(self.counts()["partition_reads"], 1)
        self.assertEqual(self.counts().get("cleanups", 0), 0)
        self.assertIn("BOOT_P19_WRITE_FSYNC_OK", self.calls()[-1]["args"][3])


if __name__ == "__main__":
    unittest.main(verbosity=2)
