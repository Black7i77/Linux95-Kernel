#!/usr/bin/env python3

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import TestCase, main, mock
import runpy


CHECKER = Path(__file__).with_name("filesystem_source_checks.py")
ROOT = CHECKER.parent.parent
SELF_TEST = ROOT / "kernel" / "filesystem" / "fat32_write_self_test.cpp"


def run_with_planned_self_test(source):
    read_text = Path.read_text
    glob = Path.glob
    rglob = Path.rglob

    def synthetic_read_text(path, *args, **kwargs):
        if path == SELF_TEST:
            return source
        return read_text(path, *args, **kwargs)

    def synthetic_glob(path, pattern, *args, **kwargs):
        yield from glob(path, pattern, *args, **kwargs)
        if path == SELF_TEST.parent and pattern == "*":
            yield SELF_TEST

    def synthetic_rglob(path, pattern):
        yield from rglob(path, pattern)
        if path == ROOT / "kernel" and pattern == "*.cpp":
            yield SELF_TEST

    with mock.patch.object(Path, "read_text", synthetic_read_text), \
            mock.patch.object(Path, "glob", synthetic_glob), \
            mock.patch.object(Path, "rglob", synthetic_rglob):
        with redirect_stdout(StringIO()):
            runpy.run_path(str(CHECKER), run_name="__main__")


class WriterDiskCheckTest(TestCase):
    def test_planned_self_test_allows_literal_boot_denial_probe(self):
        run_with_planned_self_test(
            "storage::write_sector(storage::DiskId::Boot, 0, sector);\n"
        )

    def test_planned_self_test_rejects_test_disk_write(self):
        with self.assertRaisesRegex(SystemExit, "self-test raw write is not a Boot denial probe"):
            run_with_planned_self_test(
                "storage::write_sector(storage::DiskId::Test, 0, sector);\n"
            )

    def test_planned_self_test_rejects_variable_disk_selector(self):
        with self.assertRaisesRegex(SystemExit, "self-test raw write is not a Boot denial probe"):
            run_with_planned_self_test(
                "storage::write_sector(disk, 0, sector);\n"
            )

    def test_rejects_extra_public_writable_api_with_cluster_parameter(self):
        read_text = Path.read_text
        facade = ROOT / "kernel" / "filesystem" / "filesystem.hpp"

        def with_extra_overload(path, *args, **kwargs):
            source = read_text(path, *args, **kwargs)
            if path == facade:
                return source + "\nStatus touch(const char* path, uint32_t cluster);\n"
            return source

        with mock.patch.object(Path, "read_text", with_extra_overload):
            with redirect_stdout(StringIO()):
                with self.assertRaisesRegex(SystemExit, "filesystem.*writable API"):
                    runpy.run_path(str(CHECKER), run_name="__main__")

    def test_rejects_raw_write_in_filesystem_facade(self):
        read_text = Path.read_text
        facade = ROOT / "kernel" / "filesystem" / "filesystem.cpp"

        def with_raw_write(path, *args, **kwargs):
            source = read_text(path, *args, **kwargs)
            if path == facade:
                return source + "\nstorage::write_sector(storage::DiskId::Test, 0, sector);\n"
            return source

        with mock.patch.object(Path, "read_text", with_raw_write):
            with redirect_stdout(StringIO()):
                with self.assertRaisesRegex(SystemExit, "filesystem write call found"):
                    runpy.run_path(str(CHECKER), run_name="__main__")

    def test_rejects_raw_write_in_terminal(self):
        read_text = Path.read_text
        shell = ROOT / "kernel" / "terminal" / "shell.cpp"

        def with_raw_write(path, *args, **kwargs):
            source = read_text(path, *args, **kwargs)
            if path == shell:
                return source + "\nstorage::write_sector(storage::DiskId::Test, 0, sector);\n"
            return source

        with mock.patch.object(Path, "read_text", with_raw_write):
            with redirect_stdout(StringIO()):
                with self.assertRaisesRegex(SystemExit, "raw sector write outside FAT32 writer"):
                    runpy.run_path(str(CHECKER), run_name="__main__")

    def test_rejects_an_additional_write_through_a_disk_variable(self):
        read_text = Path.read_text

        def with_extra_write(path, *args, **kwargs):
            source = read_text(path, *args, **kwargs)
            if path.name == "fat32_write.cpp":
                return source + "\nstorage::write_sector(disk, 32, sector);\n"
            return source

        with mock.patch.object(Path, "read_text", with_extra_write):
            with redirect_stdout(StringIO()):
                with self.assertRaisesRegex(SystemExit, "writer.*Test disk"):
                    runpy.run_path(str(CHECKER), run_name="__main__")


if __name__ == "__main__":
    main()
