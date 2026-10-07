#!/usr/bin/env python3
"""Host-only regression tests for scripts/stage_boot_p19.sh. Never run it against a device.

Run: python3 tools/test_stage_boot_p19.py

Each invocation runs the PRODUCTION script (unmodified) in a temporary checkout, with a
PATH-leading fake `adb` which rejects unexpected commands and refuses any call that does not pin
the serial. There is deliberately no fastboot fake at all: the tool must not be able to invoke the
one command that writes to storage, and a missing `fastboot` in PATH means a hidden call fails
loudly instead of silently succeeding.

The script's own pin is an ARGUMENT (`--expect-sha256`), so there is no production hash to swap out:
every test passes the fixture's hash. The test that matters most is the BOUNDED write - the dd the
script issues must name the payload's own byte count and never the whole 32 MiB partition.
"""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/stage_boot_p19.sh"
SERIAL = "4a2fe00b"
FORBIDDEN = "33e80afe"
MAGIC = b"ANDROID!"
PARTITION_BYTES = 33554432            # p19 is exactly 32 MiB
PAYLOAD_BYTES = 512 * 32              # the fixture is 32 sectors, a valid but tiny boot image

# The shim models only the helper's protocol: it does not execute the remote shell text, does not
# invoke other tools, and touches no host /dev node. All bytes live under FAKE_STAGE_STATE_DIR.
FAKE_TOOL = r'''
import json
import os
from pathlib import Path
import re
import shutil
import signal
import sys
import time

root = Path(os.environ["FAKE_STAGE_STATE_DIR"])
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

if tool != "adb":
    error("this helper holds no %s call at all" % tool)
if args[:2] != ["-s", "4a2fe00b"]:
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
    if not re.fullmatch(r"/(tmp|data/local/tmp)/boot-p19-stage-[0-9]{8}T[0-9]{6}Z-[A-Za-z0-9]+/payload\.img", destination):
        error("unexpected push target: " + destination)
    if cfg.get("push_fail"):
        sys.exit(1)
    shutil.copyfile(source, root / "staged.bin")
    if cfg.get("corrupt_upload"):
        with (root / "staged.bin").open("r+b") as stream:
            stream.write(b"changed")
    if cfg.get("short_upload"):
        with (root / "staged.bin").open("r+b") as stream:
            stream.truncate(PAYLOAD_BYTES - 1)
    out("1 file pushed")
if operation not in ("shell", "exec-out") or len(args) != 2:
    error("unsupported adb operation: " + repr(args))
command = args[1]
if any(name in command for name in ("mmcblk0p9'", "mmcblk0p20", "of='/dev/block/mmcblk0'", "mmcblk0p1'")):
    error("forbidden partition mentioned: " + command)

GUARD = ["test \"$(id -u)\" = 0", "test -b '/dev/block/mmcblk0p19'",
         "test -b '/dev/block/platform/msm_sdcc.1/by-name/boot'", "test \"$(readlink -f",
         "test \"$(cat", "'/sys/class/block/mmcblk0p19/start'", "'/sys/class/block/mmcblk0p19/size'",
         "= '393216'", "= '65536'"]

def guard_present(command):
    if not all(part in command for part in GUARD):
        error("p19 action missing its in-command guard: " + command)

def guard_ok():
    return (str(cfg.get("uid", "0")) == "0"
            and cfg.get("node", "/dev/block/mmcblk0p19") == "/dev/block/mmcblk0p19"
            and str(cfg.get("start", "393216")) == "393216"
            and str(cfg.get("sectors", "65536")) == "65536")

def emit(path, amount=None, fail=False):
    size = path.stat().st_size
    remaining = size if amount is None else min(size, amount)
    if fail:
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
    sys.exit(1 if fail else 0)

def partition_region():
    # The first PAYLOAD_BYTES of p19, which the readback compares.
    with (root / "p19.bin").open("rb") as stream:
        return stream.read(PAYLOAD_BYTES)

if operation == "exec-out":
    bump("binary_dumps")
    if "if='/dev/block/mmcblk0p19'" in command:
        guard_present(command)
        if not guard_ok():
            sys.exit(0)  # emulate an old adb that loses remote failure status
        if "bs=512 count=" in command:
            phase = "readback"
            blob = partition_region()
            hang("adb:" + phase)
            if cfg.get("readback_bytes") is not None:
                blob = blob[:cfg["readback_bytes"]]
            elif cfg.get("readback_bytes_extra"):
                blob = blob + b"X" * cfg["readback_bytes_extra"]
            sys.stdout.buffer.write(blob)
            sys.stdout.buffer.flush()
            sys.exit(1 if cfg.get("readback_fail") else 0)
        phase = "before"
        hang("adb:" + phase)
        emit(root / "p19.bin", cfg.get("before_bytes"), cfg.get("before_fail", False))
    if "/payload.img' bs=1048576" in command:
        if not ("test -f" in command and "test ! -L" in command):
            error("upload dump lacks regular-file guard")
        emit(root / "staged.bin", cfg.get("upload_bytes"), cfg.get("upload_fail", False))
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
    # THE BOUNDED WRITE: the payload's own sector count, never the whole partition.
    if "of='/dev/block/mmcblk0p19' bs=512 count=%d conv=fsync" % (PAYLOAD_BYTES // 512) not in command:
        error("write is not bounded to the payload's own sector count: " + command)
    if "count=65536" in command or "count=0 " in command:
        error("write names the whole partition: " + command)
    if not guard_ok() or cfg.get("write_guard_fail") or cfg.get("write_missing_marker"):
        sys.exit(0)
    if cfg.get("write_fail"):
        sys.exit(1)
    region = (root / "staged.bin").read_bytes()
    with (root / "p19.bin").open("r+b") as stream:
        stream.write(region[:PAYLOAD_BYTES])
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

FAKE_TOOL = FAKE_TOOL.replace("PAYLOAD_BYTES", str(PAYLOAD_BYTES))


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            result.update(block)
    return result.hexdigest()


def make_payload():
    # A valid-looking boot image: the magic, then filler that includes CR/LF/NUL.
    pattern = bytes(range(256))
    body = (pattern * ((PAYLOAD_BYTES // len(pattern)) + 1))[:PAYLOAD_BYTES - len(MAGIC)]
    image = MAGIC + body
    assert len(image) == PAYLOAD_BYTES
    return image


class StageBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="stage-boot-fixtures-")
        fixtures = Path(cls.temp.name)
        cls.payload_fixture = fixtures / "payload.bin"
        cls.payload_fixture.write_bytes(make_payload())
        cls.payload_sha = digest(cls.payload_fixture)
        cls.before_fixture = fixtures / "before.bin"
        cls.before_fixture.write_bytes(bytes(reversed(range(256))) * (PARTITION_BYTES // 256))
        cls.before_sha = digest(cls.before_fixture)
        cls.production_source = SCRIPT.read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="stage-boot-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        (self.root / "out/stage90/backups").mkdir(parents=True)
        self.script = self.root / "scripts/stage_boot_p19.sh"
        shutil.copyfile(SCRIPT, self.script)
        self.payload = self.root / "out/stage90/stage90-qcdt.img"
        shutil.copyfile(self.payload_fixture, self.payload)
        self.bin = self.root / "fake-bin"
        self.bin.mkdir()
        executable = self.bin / "adb"
        executable.write_text(f"#!{sys.executable}\n" + FAKE_TOOL)
        executable.chmod(0o755)
        # A real timeout must be reachable; everything else comes from the host PATH (coreutils).
        for name in ("timeout", "stat", "sha256sum", "mktemp", "cp", "chmod", "mv", "date",
                     "sync", "flock", "head", "rm", "bash", "dirname", "mkdir", "readlink"):
            found = shutil.which(name)
            if found:
                (self.bin / name).symlink_to(found)
        self.state = self.root / "fake-state"
        self.state.mkdir()
        shutil.copyfile(self.before_fixture, self.state / "p19.bin")
        self.config = {}

    def invoke(self, *args, ok=None):
        (self.state / "config.json").write_text(json.dumps(self.config))
        env = {
            "PATH": str(self.bin),
            "HOME": str(self.root),
            "LC_ALL": "C",
            "FAKE_STAGE_STATE_DIR": str(self.state),
        }
        result = subprocess.run(
            ["/bin/bash", str(self.script), *args],
            cwd=self.root, env=env, capture_output=True, text=True, timeout=25,
        )
        errors = self.state / "protocol-errors.txt"
        self.assertFalse(errors.exists(), errors.read_text() if errors.exists() else "")
        if ok is True:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        elif ok is False:
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertTrue(any(line.startswith("REFUSED:") for line in result.stderr.splitlines()),
                            result.stderr)
            self.assertNotIn("SUCCESS:", result.stdout)
        for call in self.calls():
            self.assertEqual(call["tool"], "adb")
            self.assertEqual(call["args"][:2], ["-s", SERIAL])
        return result

    def staged_run(self, *extra, **kwargs):
        return self.invoke("--expect-sha256", self.payload_sha, *extra, **kwargs)

    def calls(self):
        path = self.state / "calls.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def counts(self):
        path = self.state / "counts.json"
        return json.loads(path.read_text()) if path.exists() else {}

    def run_dirs(self):
        return sorted((self.root / "out/stage90/backups").glob("boot-p19-stage-*"))

    def p19(self):
        return (self.state / "p19.bin").read_bytes()

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

    def test_tool_holds_no_fastboot_call(self):
        # The safety property stated in the header, as a check and not a comment: fastboot may be
        # NAMED in prose but must never appear in executable code. Strip comment lines, then assert.
        code = "\n".join(line for line in self.production_source.splitlines()
                         if not line.lstrip().startswith("#"))
        self.assertNotIn("fastboot", code)

    def test_help_has_no_device_contact(self):
        result = self.invoke("--help", ok=True)
        self.assertIn("Default DRY RUN", result.stdout)
        self.assertEqual(self.calls(), [])

    def test_missing_or_malformed_pin_refuses_before_device_contact(self):
        for args in ((), ("--expect-sha256",), ("--expect-sha256=",),
                     ("--expect-sha256", "ABC"), ("--expect-sha256", "0" * 63),
                     ("--expect-sha256", "0" * 65), ("--expect-sha256", "G" * 64)):
            with self.subTest(args=args):
                self.invoke(*args, ok=False)
                self.assertEqual(self.calls(), [])

    def test_unknown_and_unbounded_arguments_are_refused(self):
        for args in (("--execute", "--execute"),
                     ("--timeout-seconds",), ("--timeout-seconds", "0"),
                     ("--timeout-seconds", "121"), ("--timeout-seconds", "1s"),
                     ("--image",), ("--fastboot",)):
            with self.subTest(args=args):
                self.invoke("--expect-sha256", self.payload_sha, *args, ok=False)
                self.assertEqual(self.calls(), [])

    def test_wrong_hash_refuses_before_device_contact(self):
        result = self.invoke("--expect-sha256", "0" * 64, "--execute", ok=False)
        self.assertIn("nothing was sent", result.stderr)
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.run_dirs(), [])

    def test_payload_shape_refusals_happen_before_device_contact(self):
        cases = {
            "missing": lambda: self.payload.unlink(),
            "empty": lambda: self.payload.write_bytes(b""),
            "not-magic": lambda: self.payload.write_bytes(b"X" * PAYLOAD_BYTES),
            "not-sectors": lambda: self.payload.write_bytes(MAGIC + b"z" * 100),
            "too-large": lambda: self.payload.write_bytes(MAGIC + b"z" * PARTITION_BYTES),
        }
        for kind, mutate in cases.items():
            with self.subTest(kind=kind):
                shutil.copyfile(self.payload_fixture, self.payload)
                mutate()
                # The pin is the mutated file's own hash, so the SHAPE is what refuses it.
                if self.payload.exists():
                    sha = digest(self.payload)
                else:
                    sha = self.payload_sha
                self.invoke("--expect-sha256", sha, "--execute", ok=False)
                self.assertEqual(self.calls(), [])
                self.assertEqual(self.run_dirs(), [])

    def test_default_dry_run_is_read_only(self):
        result = self.staged_run(ok=True)
        self.assertIn("DRY RUN", result.stdout)
        self.assertNotIn("SUCCESS:", result.stdout)
        self.assert_no_push()
        self.assertEqual(self.run_dirs(), [])
        self.assertFalse((self.root / "out/stage90/backups/.stage_boot_p19.lock").exists())
        self.assertEqual(self.counts().get("stages", 0), 0)
        self.assertEqual(self.counts().get("binary_dumps", 0), 0)
        self.assertEqual(self.counts()["adb_inventory"], 1)

    def test_dry_run_accepts_recovery_and_crlf(self):
        self.config = {"bootmode": "recovery", "state": "recovery", "crlf": True,
                       "adb_devices": "List of devices attached\r\n4a2fe00b\trecovery\r\n"}
        result = self.staged_run(ok=True)
        self.assertIn("stage under /tmp", result.stdout)
        self.assert_no_push()

    def test_forbidden_serial_even_unauthorized(self):
        self.config = {"adb_devices": f"List of devices attached\n{SERIAL}\tdevice\n{FORBIDDEN}\tunauthorized\n"}
        result = self.staged_run("--execute", ok=False)
        self.assertIn(FORBIDDEN, result.stderr)
        self.assert_no_push()

    def test_absent_offline_unauthorized_or_duplicate_device(self):
        for config in ({"adb_devices": "List of devices attached\n"},
                       {"adb_devices": f"List of devices attached\n{SERIAL}\toffline\n"},
                       {"adb_devices": f"List of devices attached\n{SERIAL}\tunauthorized\n"},
                       {"adb_devices": f"List of devices attached\n{SERIAL}\tdevice\n{SERIAL}\tdevice\n"}):
            with self.subTest(config=config):
                self.config = config
                self.staged_run("--execute", ok=False)
                self.assert_no_push()

    def test_inventory_or_get_state_failure_stops(self):
        for config in ({"adb_fail": True}, {"state_fail": True}, {"state": "offline"}):
            with self.subTest(config=config):
                self.config = config
                self.staged_run("--execute", ok=False)
                self.assert_no_push()

    def test_root_is_exact_uid_zero(self):
        for uid in ("1000", "uid=0(root)", "1000 uid=0", "00", "0\nextra", ""):
            with self.subTest(uid=uid):
                self.config = {"uid": uid}
                result = self.staged_run("--execute", ok=False)
                self.assertIn("uid must be exactly 0", result.stderr)
                self.assert_no_push()

    def test_wrong_or_missing_geometry_stops(self):
        for config in ({"node": "/dev/block/mmcblk0p20"}, {"node": None},
                       {"start": "393215"}, {"sectors": "65535"}, {"sectors": "65536 junk"}):
            with self.subTest(config=config):
                self.config = config
                self.staged_run("--execute", ok=False)
                self.assert_no_push()
                self.assertEqual(self.run_dirs(), [])

    def test_temp_base_must_confirm_root_access(self):
        self.config = {"temp_unwritable": True}
        self.staged_run("--execute", ok=False)
        self.assert_no_push()
        self.assertEqual(self.run_dirs(), [])

    def test_execute_writes_only_the_payload_region_and_verifies_it(self):
        result = self.staged_run("--execute", ok=True)
        self.assertEqual(result.stdout.count("SUCCESS:"), 1)
        self.assertIn(self.payload_sha, result.stdout)
        region = self.p19()[:PAYLOAD_BYTES]
        self.assertEqual(hashlib.sha256(region).hexdigest(), self.payload_sha)
        # The tail past the payload was never touched.
        self.assertEqual(self.p19()[PAYLOAD_BYTES:], self.before_fixture.read_bytes()[PAYLOAD_BYTES:])
        directories = self.run_dirs()
        self.assertEqual(len(directories), 1)
        record = directories[0]
        self.assertRegex(record.name, r"^boot-p19-stage-\d{8}T\d{6}Z-[A-Za-z0-9]+$")
        self.assertEqual((record / "p19-before.img").stat().st_size, PARTITION_BYTES)
        self.assertEqual(digest(record / "p19-before.img"), self.before_sha)
        self.assertEqual(digest(record / "payload.img"), self.payload_sha)
        self.assertEqual(digest(record / "upload-check.img"), self.payload_sha)
        self.assertEqual(digest(record / "p19-readback-head.img"), self.payload_sha)
        counts = self.counts()
        self.assertEqual(counts["writes"], 1)
        self.assertEqual(counts["binary_dumps"], 3)
        self.assertEqual(counts["cleanups"], 1)
        # The write is bounded: exactly the payload's sectors, never the whole partition.
        write = [call for call in self.calls() if "BOOT_P19_WRITE_FSYNC_OK" in " ".join(call["args"])][0]
        self.assertIn(f"bs=512 count={PAYLOAD_BYTES // 512} conv=fsync", " ".join(write["args"]))
        self.assertNotIn("count=65536", " ".join(write["args"]))
        # Only the two partition reads use binary exec-out; the write is the only shell dd.
        p19_reads = [call for call in self.calls() if "dd if='/dev/block/mmcblk0p19'" in " ".join(call["args"])]
        self.assertEqual(len(p19_reads), 2)
        self.assertTrue(all(call["args"][2] == "exec-out" for call in p19_reads))

    def test_execute_recovery_uses_tmp(self):
        self.config = {"twrp_version": "3.7.0_9-0", "bootmode": "unknown"}
        self.staged_run("--execute", ok=True)
        pushes = [call for call in self.calls() if call["args"][2] == "push"]
        self.assertTrue(pushes[0]["args"][4].startswith("/tmp/"))

    def test_host_lock_blocks_concurrent_execute(self):
        import fcntl
        lock = self.root / "out/stage90/backups/.stage_boot_p19.lock"
        with lock.open("w") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.staged_run("--execute", ok=False)
        self.assertIn("holds the host lock", result.stderr)
        self.assert_no_push()

    def test_before_dump_short_long_or_failed_blocks_push(self):
        for config in ({"before_bytes": PARTITION_BYTES - 1}, {"before_bytes": PARTITION_BYTES + 1},
                       {"before_fail": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                self.staged_run("--execute", ok=False)
                self.assert_no_push()
                newest = self.run_dirs()[-1]
                self.assertTrue((newest / "p19-before.img.partial").exists())
                self.assertFalse((newest / "p19-before.img").exists())

    def test_fsync_probe_push_seal_or_upload_failure_blocks_partition_write(self):
        for config in ({"stage_fail": True}, {"push_fail": True}, {"seal_fail": True},
                       {"corrupt_upload": True}, {"short_upload": True}, {"upload_fail": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                shutil.copyfile(self.before_fixture, self.state / "p19.bin")
                self.staged_run("--execute", ok=False)
                self.assert_no_write()

    def test_failed_write_missing_marker_or_changed_guard_stops_without_readback(self):
        for config in ({"write_fail": True}, {"write_missing_marker": True}, {"write_guard_fail": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                result = self.staged_run("--execute", ok=False)
                self.assertIn("p19 stage NOT verified", result.stderr)
                # before image + upload check, and NEITHER a readback nor a cleanup.
                self.assertEqual(self.counts().get("binary_dumps"), 2)
                self.assertEqual(self.counts().get("cleanups", 0), 0)

    def test_readback_short_long_failed_or_wrong_hash_never_prints_success(self):
        for config in ({"readback_bytes": PAYLOAD_BYTES - 1}, {"readback_bytes_extra": 1},
                       {"readback_fail": True}, {"corrupt_write": True}):
            with self.subTest(config=config):
                self.config = config
                (self.state / "counts.json").unlink(missing_ok=True)
                shutil.copyfile(self.before_fixture, self.state / "p19.bin")
                result = self.staged_run("--execute", ok=False)
                self.assertIn("p19 stage NOT verified", result.stderr)
                self.assertEqual(self.counts()["writes"], 1)
                self.assertEqual(self.counts().get("cleanups", 0), 0)

    def test_cleanup_failure_is_not_reported_as_success(self):
        self.config = {"cleanup_fail": True}
        result = self.staged_run("--execute", ok=False)
        self.assertIn("verified readback, but temp cleanup", result.stderr)

    def test_real_timeout_bounds_hanging_adb_inventory(self):
        self.config = {"hang_at": "adb:devices"}
        started = time.monotonic()
        result = self.invoke("--expect-sha256", self.payload_sha, "--timeout-seconds", "1",
                             "--execute", ok=False)
        self.assertLess(time.monotonic() - started, 6)
        self.assertIn("bounded adb devices", result.stderr)
        self.assert_no_push()

    def test_timed_out_write_is_unknown_and_does_not_reboot_or_retry(self):
        self.config = {"hang_at": "adb:write"}
        result = self.invoke("--expect-sha256", self.payload_sha, "--timeout-seconds", "1",
                             "--execute", ok=False)
        self.assertIn("may have continued", result.stderr)
        self.assertEqual(self.counts()["write_attempts"], 1)
        self.assertEqual(self.counts().get("cleanups", 0), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)