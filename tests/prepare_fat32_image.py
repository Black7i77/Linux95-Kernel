#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import tempfile


def run(command):
    subprocess.run(command, check=True)


def main():
    if len(sys.argv) != 2:
        print("usage: prepare_fat32_image.py <output-image>")
        return 2

    image = Path(sys.argv[1]).resolve()
    user_init = Path("build/user/init.elf").resolve()
    user_worker = Path("build/user/worker.elf").resolve()
    if not user_init.is_file() or not user_worker.is_file():
        print("missing build/user/init.elf or build/user/worker.elf")
        return 1
    image.parent.mkdir(parents=True, exist_ok=True)
    image.unlink(missing_ok=True)

    with image.open("wb") as handle:
        handle.truncate(64 * 1024 * 1024)

    run([
        "mkfs.fat",
        "-F", "32",
        "-s", "1",
        "-n", "LINUX95",
        str(image),
    ])

    with tempfile.TemporaryDirectory() as temp_dir:
        fixture = Path(temp_dir)

        (fixture / "README.TXT").write_bytes(
            b"Linux95 FAT32 filesystem online.\r\n"
        )

        (fixture / "CHAIN.TXT").write_bytes(
            bytes(ord("A") + (index % 26) for index in range(1536))
        )

        (fixture / "KERNEL.TXT").write_bytes(
            b"Linux95 reads FAT32 subdirectories.\r\n"
        )

        run([
            "mmd",
            "-i", str(image),
            "::DOCS",
        ])

        run([
            "mmd",
            "-i", str(image),
            "::USER",
        ])

        run([
            "mcopy",
            "-o",
            "-i", str(image),
            str(fixture / "README.TXT"),
            "::README.TXT",
        ])

        run([
            "mcopy",
            "-o",
            "-i", str(image),
            str(fixture / "CHAIN.TXT"),
            "::CHAIN.TXT",
        ])

        run([
            "mcopy",
            "-o",
            "-i", str(image),
            str(fixture / "KERNEL.TXT"),
            "::DOCS/KERNEL.TXT",
        ])

        run([
            "mcopy",
            "-o",
            "-i", str(image),
            str(user_init),
            "::USER/INIT.ELF",
        ])

        run([
            "mcopy",
            "-o",
            "-i", str(image),
            str(user_worker),
            "::USER/WORKER.ELF",
        ])

        for path in ("::USER/INIT.ELF", "::USER/WORKER.ELF"):
            run(["mdir", "-i", str(image), path])

    print(f"FAT32 fixture created: {image}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
