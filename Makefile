.RECIPEPREFIX := >

BUILD := build

NASM := nasm
CXX := g++
LD := ld
OBJCOPY := objcopy
READELF := readelf
PYTHON := python3
QEMU := qemu-system-x86_64

SECTOR := 512
STAGE2_SECTORS := 16
KERNEL_LBA := 17
KERNEL_SECTORS := 256
IMAGE_SECTORS := 273
STORAGE_TEST_IMAGE := $(BUILD)/linux95-storage-test.img

HOST_CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -Ikernel

CXXFLAGS := -std=c++17 \
	-m64 \
	-ffreestanding \
	-fno-builtin \
	-fno-exceptions \
	-fno-rtti \
	-fno-stack-protector \
	-fpie \
	-fno-threadsafe-statics \
	-fno-use-cxa-atexit \
	-fno-tree-loop-distribute-patterns \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-Wall \
	-Wextra \
	-Werror \
	-O2 \
	-Ikernel

KERNEL_OBJS := \
	$(BUILD)/entry.o \
	$(BUILD)/interrupts_asm.o \
	$(BUILD)/paging_bootstrap.o \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o $(BUILD)/framebuffer.o $(BUILD)/renderer.o \
	$(BUILD)/panic.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/pic.o \
	$(BUILD)/pit.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/memory.o \
	$(BUILD)/virtual.o \
	$(BUILD)/physical.o \
	$(BUILD)/paging.o \
	$(BUILD)/self_test.o \
	$(BUILD)/heap.o \
	$(BUILD)/ata.o \
	$(BUILD)/disk.o \
	$(BUILD)/storage_self_test.o \
	$(BUILD)/fat32.o \
	$(BUILD)/filesystem.o \
	$(BUILD)/vfs.o \
	$(BUILD)/filesystem_self_test.o \
	$(BUILD)/vfs_self_test.o \
	$(BUILD)/shell.o

.PHONY: all clean run run-debug test test-qemu prepare-storage-test-image test-host-memory test-host-storage test-host-filesystem test-host-graphics test-memory-source test-storage-source test-relocations check-tools

all: check-tools $(BUILD)/linux95-kernel.img

check-tools:
>@for tool in $(NASM) $(CXX) $(LD) $(OBJCOPY) $(READELF) $(PYTHON); do \
	command -v $$tool >/dev/null || { \
		echo "Missing tool: $$tool"; \
		exit 1; \
	}; \
done

$(BUILD):
>mkdir -p $(BUILD)

$(BUILD)/stage1.bin: boot/stage1.asm | $(BUILD)
>$(NASM) -f bin $< -o $@
>@size=$$(stat -c%s $@); \
	test $$size -eq 512 || { \
		echo "ERROR: Stage 1 must be exactly 512 bytes, got $$size"; \
		exit 1; \
	}

$(BUILD)/stage2.bin: boot/stage2.asm | $(BUILD)
>$(NASM) -f bin $< -o $@
>@size=$$(stat -c%s $@); \
	max=$$(( $(STAGE2_SECTORS) * $(SECTOR) )); \
	test $$size -eq $$max || { \
		echo "ERROR: Stage 2 must be exactly $$max bytes, got $$size"; \
		exit 1; \
	}

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/interrupts_asm.o: kernel/arch/interrupts.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/paging_bootstrap.o: kernel/arch/x86_64/paging_bootstrap.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/kernel.o: \
	kernel/kernel.cpp \
	kernel/boot_info.hpp \
	kernel/filesystem/vfs.hpp \
	kernel/filesystem/vfs_self_test.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/renderer.o: kernel/graphics/renderer.cpp kernel/graphics/renderer.hpp kernel/graphics/font8x8.hpp kernel/graphics/framebuffer.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/framebuffer.o: kernel/graphics/framebuffer.cpp kernel/graphics/framebuffer.hpp kernel/graphics/framebuffer_helpers.hpp kernel/boot_info.hpp kernel/memory/paging.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vga.o: kernel/terminal/vga.cpp kernel/terminal/vga.hpp kernel/arch/io.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/panic.o: kernel/panic/panic.cpp kernel/panic/panic.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: kernel/arch/interrupts.cpp kernel/arch/interrupts.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pic.o: kernel/arch/pic.cpp kernel/arch/pic.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pit.o: kernel/arch/pit.cpp kernel/arch/pit.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: kernel/arch/keyboard.cpp kernel/arch/keyboard.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/memory.o: kernel/memory/memory.cpp kernel/memory/memory.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/virtual.o: kernel/memory/virtual.cpp kernel/memory/virtual.hpp kernel/memory/address.hpp kernel/memory/memory.hpp kernel/arch/debug.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/physical.o: kernel/memory/physical.cpp kernel/memory/physical.hpp kernel/memory/page_bitmap.hpp kernel/memory/address.hpp kernel/memory/memory.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/paging.o: kernel/memory/paging.cpp kernel/memory/paging.hpp kernel/memory/physical.hpp kernel/memory/address.hpp kernel/arch/x86_64/control_regs.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/self_test.o: kernel/memory/self_test.cpp kernel/memory/self_test.hpp kernel/memory/paging.hpp kernel/memory/physical.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@


$(BUILD)/host-e820-limit-test: tests/host/e820_limit_test.cpp kernel/memory/memory.cpp kernel/memory/memory.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/e820_limit_test.cpp kernel/memory/memory.cpp -o $@

$(BUILD)/host-page-bitmap-test: tests/host/page_bitmap_test.cpp kernel/memory/page_bitmap.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-memory: $(BUILD)/host-page-bitmap-test $(BUILD)/host-e820-limit-test
>$(BUILD)/host-page-bitmap-test
>$(BUILD)/host-e820-limit-test

$(BUILD)/host-ata-helpers-test: tests/host/ata_helpers_test.cpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-storage: $(BUILD)/host-ata-helpers-test
>$(BUILD)/host-ata-helpers-test
$(BUILD)/host-fat32-helpers-test: tests/host/fat32_helpers_test.cpp kernel/filesystem/fat32_helpers.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

$(BUILD)/host-fat32-mount-test: \
	tests/host/fat32_mount_test.cpp \
	kernel/filesystem/fat32.cpp \
	kernel/filesystem/fat32.hpp \
	kernel/filesystem/fat32_helpers.hpp \
	kernel/filesystem/filesystem.cpp \
	kernel/filesystem/filesystem.hpp \
	kernel/storage/disk.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/fat32_mount_test.cpp kernel/filesystem/fat32.cpp kernel/filesystem/filesystem.cpp -o $@

$(BUILD)/host-vfs-test: \
	tests/host/vfs_test.cpp \
	kernel/filesystem/vfs.cpp \
	kernel/filesystem/vfs.hpp \
	kernel/filesystem/filesystem.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/vfs_test.cpp kernel/filesystem/vfs.cpp -o $@

$(BUILD)/host-framebuffer-helpers-test: tests/host/framebuffer_helpers_test.cpp kernel/graphics/framebuffer_helpers.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-graphics: $(BUILD)/host-framebuffer-helpers-test
>$(BUILD)/host-framebuffer-helpers-test

test-host-filesystem: $(BUILD)/host-fat32-helpers-test $(BUILD)/host-fat32-mount-test $(BUILD)/host-vfs-test
>$(BUILD)/host-fat32-helpers-test
>$(BUILD)/host-fat32-mount-test
>$(BUILD)/host-vfs-test

$(BUILD)/heap.o: kernel/memory/heap.cpp kernel/memory/heap.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/ata.o: kernel/storage/ata.cpp kernel/storage/ata.hpp kernel/storage/ata_helpers.hpp kernel/arch/io.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/disk.o: kernel/storage/disk.cpp kernel/storage/disk.hpp kernel/storage/ata.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/fat32.o: kernel/filesystem/fat32.cpp kernel/filesystem/fat32.hpp kernel/filesystem/fat32_helpers.hpp kernel/filesystem/filesystem.hpp kernel/storage/disk.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/filesystem.o: kernel/filesystem/filesystem.cpp kernel/filesystem/filesystem.hpp kernel/filesystem/fat32.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vfs.o: kernel/filesystem/vfs.cpp kernel/filesystem/vfs.hpp kernel/filesystem/filesystem.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/filesystem_self_test.o: kernel/filesystem/filesystem_self_test.cpp kernel/filesystem/filesystem_self_test.hpp kernel/filesystem/filesystem.hpp kernel/arch/debug.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vfs_self_test.o: \
	kernel/filesystem/vfs_self_test.cpp \
	kernel/filesystem/vfs_self_test.hpp \
	kernel/filesystem/vfs.hpp \
	kernel/filesystem/filesystem.hpp \
	kernel/arch/debug.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/storage_self_test.o: kernel/storage/storage_self_test.cpp kernel/storage/storage_self_test.hpp kernel/storage/disk.hpp kernel/storage/ata_helpers.hpp kernel/arch/debug.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/shell.o: \
	kernel/terminal/shell.cpp \
	kernel/terminal/shell.hpp \
	kernel/filesystem/vfs.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(KERNEL_OBJS) linker.ld
>$(LD) \
	-nostdlib \
	-z max-page-size=0x1000 \
	-T linker.ld \
	-o $@ \
	$(KERNEL_OBJS)
>@entry=$$($(READELF) -h $@ | awk '/Entry point address:/ {print $$4}'); \
	test "$$entry" = "0x100000" || { \
		echo "ERROR: bad kernel entry: $$entry"; \
		exit 1; \
	}

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
>$(OBJCOPY) -O binary $< $@
>@size=$$(stat -c%s $@); \
	max=$$(( $(KERNEL_SECTORS) * $(SECTOR) )); \
	test $$size -le $$max || { \
		echo "ERROR: Kernel too large: $$size > $$max"; \
		exit 1; \
	}

$(BUILD)/linux95-kernel.img: \
	$(BUILD)/stage1.bin \
	$(BUILD)/stage2.bin \
	$(BUILD)/kernel.bin
>dd if=/dev/zero of=$@ bs=$(SECTOR) count=$(IMAGE_SECTORS) status=none
>dd if=$(BUILD)/stage1.bin of=$@ bs=$(SECTOR) seek=0 conv=notrunc status=none
>dd if=$(BUILD)/stage2.bin of=$@ bs=$(SECTOR) seek=1 conv=notrunc status=none
>dd if=$(BUILD)/kernel.bin of=$@ bs=$(SECTOR) seek=$(KERNEL_LBA) conv=notrunc status=none
>@echo
>@echo "Linux95 Kernel v1.0 VFS Foundation image built:"
>@ls -lh $@

$(STORAGE_TEST_IMAGE): | $(BUILD)
>$(PYTHON) tests/prepare_fat32_image.py $@

prepare-storage-test-image: | $(BUILD)
>@for tool in mkfs.fat mmd mcopy; do \
	command -v $$tool >/dev/null || { echo "Missing tool: $$tool"; exit 1; }; \
done
>$(PYTHON) tests/prepare_fat32_image.py $(STORAGE_TEST_IMAGE)

test-memory-source:
>$(PYTHON) tests/memory_source_checks.py

test-relocations: all
>$(PYTHON) tests/relocation_checks.py

test-storage-source:
>$(PYTHON) tests/storage_source_checks.py

test-filesystem-source:
>$(PYTHON) tests/filesystem_source_checks.py

test: all test-host-memory test-host-storage test-host-filesystem test-host-graphics test-host-graphics
>$(PYTHON) tests/source_checks.py
>$(PYTHON) tests/image_checks.py
>$(PYTHON) tests/memory_source_checks.py
>$(PYTHON) tests/storage_source_checks.py
>$(PYTHON) tests/filesystem_source_checks.py
>$(PYTHON) tests/relocation_checks.py

test-qemu: all prepare-storage-test-image
>@command -v $(QEMU) >/dev/null || { echo "Missing tool: $(QEMU)"; exit 1; }
>$(PYTHON) tests/qemu_smoke.py

run: all prepare-storage-test-image
>$(QEMU) \
	-machine pc \
	-m 128M \
	-boot c \
	-drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/linux95-kernel.img \
	-drive if=ide,index=1,media=disk,format=raw,file=$(STORAGE_TEST_IMAGE)

run-debug: all prepare-storage-test-image
>$(QEMU) \
	-machine pc \
	-m 128M \
	-boot c \
	-drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/linux95-kernel.img \
	-drive if=ide,index=1,media=disk,format=raw,file=$(STORAGE_TEST_IMAGE) \
	-no-reboot \
	-no-shutdown

clean:
>rm -rf $(BUILD)

$(BUILD)/host-renderer-test: tests/host/renderer_test.cpp kernel/graphics/renderer.cpp kernel/graphics/renderer.hpp kernel/graphics/font8x8.hpp kernel/graphics/framebuffer.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/renderer_test.cpp kernel/graphics/renderer.cpp -o $@

test-host-renderer: $(BUILD)/host-renderer-test
>$(BUILD)/host-renderer-test

.PHONY: test-host-renderer
test-host-graphics: test-host-renderer
