#!/usr/bin/env python3

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import TestCase, main, mock
import runpy


CHECKER = Path(__file__).with_name("filesystem_source_checks.py")


class WriterDiskCheckTest(TestCase):
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
