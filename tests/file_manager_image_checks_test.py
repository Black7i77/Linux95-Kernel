from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import file_manager_image_checks


class FileManagerImageChecksTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.test_image = self.root / "test.img"
        self.boot_image = self.root / "boot.img"
        self.test_image.write_bytes(b"disposable FAT image")
        self.boot_image.write_bytes(b"boot image")
        self.boot_digest = file_manager_image_checks.image_digest(self.boot_image)
        self.present = {
            "::FMTEST",
            "::FMTEST/PROTECT",
            "::FMTEST/PROTECT/CHILD.TXT",
            "::FMTEST/KEEP.TXT",
        }
        self.contents = {
            "::FMTEST/PROTECT/CHILD.TXT": b"",
            "::FMTEST/KEEP.TXT": b"",
        }

    def tearDown(self):
        self.temp.cleanup()

    def run_mtools(self, command, **_kwargs):
        if command[0] == "mdir":
            return SimpleNamespace(returncode=0 if command[-1] in self.present else 1,
                                   stdout="", stderr="")
        if command[0] == "mcopy":
            source, destination = command[-2:]
            if source not in self.contents:
                return SimpleNamespace(returncode=1, stdout="", stderr="missing")
            Path(destination).write_bytes(self.contents[source])
            return SimpleNamespace(returncode=0, stdout="", stderr="")
        raise AssertionError(f"unexpected command: {command}")

    def test_expected_persisted_state_and_boot_digest_are_required(self):
        with patch.object(file_manager_image_checks.subprocess, "run", self.run_mtools):
            self.assertEqual(file_manager_image_checks.verify(
                self.test_image, self.boot_image, self.boot_digest), [])

            self.present.remove("::FMTEST/KEEP.TXT")
            self.assertTrue(any("KEEP.TXT" in error for error in
                                file_manager_image_checks.verify(
                                    self.test_image, self.boot_image,
                                    self.boot_digest)))

            self.present.add("::FMTEST/KEEP.TXT")
            self.boot_image.write_bytes(b"modified boot image")
            self.assertTrue(any("Boot image" in error for error in
                                file_manager_image_checks.verify(
                                    self.test_image, self.boot_image,
                                    self.boot_digest)))

    def test_deleted_entries_and_exact_file_contents_are_checked(self):
        with patch.object(file_manager_image_checks.subprocess, "run", self.run_mtools):
            self.present.add("::FMTEST/EMPTYDIR")
            self.assertTrue(any("EMPTYDIR" in error for error in
                                file_manager_image_checks.verify(
                                    self.test_image, self.boot_image,
                                    self.boot_digest)))

            self.present.remove("::FMTEST/EMPTYDIR")
            self.contents["::FMTEST/KEEP.TXT"] = b"not empty"
            self.assertTrue(any("KEEP.TXT" in error for error in
                                file_manager_image_checks.verify(
                                    self.test_image, self.boot_image,
                                    self.boot_digest)))


if __name__ == "__main__":
    unittest.main()
