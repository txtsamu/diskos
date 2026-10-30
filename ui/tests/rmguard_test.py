#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
"""Exercise the real SD delete guard (rmguard.c) with temporary host paths, never /usr/data.

tests/rmguard_driver.c links rmguard.c itself; the guard directories are the module's overridable path macros, pointed
at a temp dir at compile time. Cases cover ordinary rm, altered wrappers and publication.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class GuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="diskos-rmguard-tests-")
        cls.root = Path(cls.tmp.name)
        cls.work = cls.root / "w"
        app = Path(__file__).resolve().parents[1]
        cls.exe = cls.root / "test"
        subprocess.run([os.environ.get("CC", "cc"), "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-g", "-I", str(app),
                        f'-DRMGUARD_ROOTFS="{cls.work}/ro"',
                        '-DRMGUARD_SYSTEM="/nonexistent-diskos-system-test"',
                        f'-DRMGUARD_FALLBACK_PARENT="{cls.work}/data"',
                        f'-DRMGUARD_FALLBACK="{cls.work}/data/bin"',
                        str(app / "tests" / "rmguard_driver.c"), str(app / "rmguard.c"),
                        "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, case, expected):
        shutil.rmtree(self.work, ignore_errors=True)
        self.work.mkdir()
        lines = subprocess.check_output([str(self.exe), case], text=True, timeout=5).splitlines()
        self.assertEqual(lines[0], "0 0 1" if expected is None else "1 1 1")
        if expected is None:
            self.assertEqual(lines[1], "/bin:/usr/bin")
        else:
            self.assertEqual(lines[1], f"{self.work}/{expected}:/bin:/usr/bin")
            guard = self.work / expected / "rm"
            payload = Path(__file__).resolve().parents[2] / "payload" / "diskos-rmguard"
            self.assertEqual(guard.read_bytes(), payload.read_bytes())

    def test_real_rm_does_not_count_as_guard(self):
        self.run_case("ordinary", "data/bin")

    def test_valid_rootfs_guard_is_preferred_and_idempotent(self):
        self.run_case("root", "ro")

    def test_invalid_rootfs_guards_are_rejected(self):
        for case in ("header", "changed", "nonexec", "symlink", "fifo"):
            with self.subTest(case=case):
                self.run_case(case, "data/bin")

    def test_existing_bad_fallback_is_replaced(self):
        self.run_case("fallback_changed", "data/bin")

    def test_no_guard_available_reports_failure(self):
        self.run_case("unwritable", None)


if __name__ == "__main__":
    unittest.main()
