#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import tempfile


def run(command):
    subprocess.run(command, check=True)


def main():
    process_fault = "--process-fault" in sys.argv[2:]
    process_preemption = "--process-preemption" in sys.argv[2:]

    if process_fault and process_preemption:
        print("process fixture modes are mutually exclusive")
        return 2

    if len(sys.argv) not in (2, 3) or any(
            argument not in ("--process-fault", "--process-preemption")
            for argument in sys.argv[2:]):
        print(
            "usage: prepare_fat32_image.py <output-image> "
            "[--process-fault|--process-preemption]"
        )
        return 2

    image = Path(sys.argv[1]).resolve()
    user_init = Path("build/user/init.elf").resolve()
    user_worker = Path("build/user/worker.elf").resolve()
    user_fault = Path("build/user/fault.elf").resolve()
    user_preempt_hog = Path("build/user/preempt_hog.elf").resolve()
    user_preempt_worker = Path("build/user/preempt_worker.elf").resolve()

    if process_preemption:
        if not user_preempt_hog.is_file() or not user_preempt_worker.is_file():
            print("missing preemption user ELF")
            return 1
    elif process_fault:
        if not user_fault.is_file() or not user_worker.is_file():
            print("missing fault-test user ELF")
            return 1
    elif not user_init.is_file() or not user_worker.is_file():
        print("missing build/user/init.elf or build/user/worker.elf")
        return 1

    init_image = (
        user_preempt_hog if process_preemption
        else user_fault if process_fault
        else user_init
    )
    worker_image = (
        user_preempt_worker if process_preemption
        else user_worker
    )
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
            str(init_image),
            "::USER/INIT.ELF",
        ])

        run([
            "mcopy",
            "-o",
            "-i", str(image),
            str(worker_image),
            "::USER/WORKER.ELF",
        ])

        if process_fault:
            run([
                "mcopy",
                "-o",
                "-i", str(image),
                str(user_fault),
                "::USER/FAULT.ELF",
            ])

        paths = ["::USER/INIT.ELF", "::USER/WORKER.ELF"]
        if process_fault:
            paths.append("::USER/FAULT.ELF")
        for path in paths:
            run(["mdir", "-i", str(image), path])

    print(f"FAT32 fixture created: {image}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
