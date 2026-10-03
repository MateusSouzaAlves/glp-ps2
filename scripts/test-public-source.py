# SPDX-License-Identifier: MIT
"""Synthetic hash/path regressions; no firmware or game inputs."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("public_source", Path(__file__).with_name("verify-public-source.py"))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class SourceChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "input.cpp").write_bytes(b"synthetic native source")
        self.record = {"path": "input.cpp", "bytes": 23,
                       "sha256": hashlib.sha256(b"synthetic native source").hexdigest()}

    def test_exact_input(self):
        self.assertEqual(audit.verify_files(self.root, [self.record]), {"input.cpp"})

    def test_tampered_input(self):
        (self.root / "input.cpp").write_bytes(b"modified native source!")
        with self.assertRaisesRegex(ValueError, "Source bytes changed"):
            audit.verify_files(self.root, [self.record])

    def test_duplicate_input(self):
        with self.assertRaisesRegex(ValueError, "Invalid source path"):
            audit.verify_files(self.root, [self.record, self.record])

    def test_parent_path(self):
        self.record["path"] = "../input.cpp"
        with self.assertRaisesRegex(ValueError, "Invalid source path"):
            audit.verify_files(self.root, [self.record])

    def test_absolute_path(self):
        self.record["path"] = str(self.root / "input.cpp")
        with self.assertRaisesRegex(ValueError, "Invalid source path"):
            audit.verify_files(self.root, [self.record])

    def test_missing_source(self):
        self.record["path"] = "missing.cpp"
        with self.assertRaisesRegex(ValueError, "Invalid source path"):
            audit.verify_files(self.root, [self.record])


if __name__ == "__main__":
    unittest.main()
