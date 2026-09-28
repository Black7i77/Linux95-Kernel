#!/usr/bin/env python3
"""Independent post-QEMU checks for the disposable File Manager FAT32 image."""

from pathlib import Path
import hashlib
import subprocess
import tempfile


PRESENT_PATHS = (
    "::FMTEST",
    "::FMTEST/PROTECT",
    "::FMTEST/PROTECT/CHILD.TXT",
    "::FMTEST/KEEP.TXT",
)
ABSENT_PATHS = (
    "::FMTEST/REMOVE.TXT",
    "::FMTEST/TEMP.TXT",
    "::FMTEST/EMPTYDIR",
)
EMPTY_FILES = (
    "::FMTEST/PROTECT/CHILD.TXT",
    "::FMTEST/KEEP.TXT",
)


def image_digest(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as image_file:
        for block in iter(lambda: image_file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _exists(image, path):
    result = subprocess.run(
        ["mdir", "-i", str(image), path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


def verify(test_image, boot_image, expected_boot_digest):
    errors = []
    for path in PRESENT_PATHS:
        if not _exists(test_image, path):
            errors.append(f"expected persisted Test-disk entry is missing: {path}")
    for path in ABSENT_PATHS:
        if _exists(test_image, path):
            errors.append(f"entry expected to be deleted remains on Test disk: {path}")

    with tempfile.TemporaryDirectory(prefix="file-manager-image-check-") as directory:
        for index, path in enumerate(EMPTY_FILES):
            destination = Path(directory) / f"persisted-{index}.bin"
            copied = subprocess.run(
                ["mcopy", "-i", str(test_image), path, str(destination)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            if copied.returncode != 0 or not destination.is_file():
                errors.append(f"could not read persisted Test-disk file: {path}")
            elif destination.read_bytes() != b"":
                errors.append(f"expected zero-byte persisted file: {path}")

    if image_digest(boot_image) != expected_boot_digest:
        errors.append("Boot image changed during File Manager QEMU test")
    return errors


def main(argv):
    if len(argv) != 4:
        print("usage: file_manager_image_checks.py TEST_IMAGE BOOT_IMAGE BOOT_SHA256")
        return 2
    errors = verify(argv[1], argv[2], argv[3])
    if errors:
        print("File Manager image checks: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1
    print("File Manager Test image persistence: PASS")
    print("Boot image unchanged: PASS")
    return 0


if __name__ == "__main__":
    import sys
    raise SystemExit(main(sys.argv))
