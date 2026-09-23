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
KERNEL_SECTORS := 128
IMAGE_SECTORS := 145

CXXFLAGS := -std=c++17 \
	-m64 \
	-ffreestanding \
	-fno-builtin \
	-fno-exceptions \
	-fno-rtti \
	-fno-stack-protector \
	-fno-pie \
	-fno-pic \
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
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	$(BUILD)/panic.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/pic.o \
	$(BUILD)/pit.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/memory.o \
	$(BUILD)/heap.o \
	$(BUILD)/shell.o

.PHONY: all clean run run-debug test test-qemu check-tools

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

$(BUILD)/kernel.o: kernel/kernel.cpp kernel/boot_info.hpp | $(BUILD)
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

$(BUILD)/heap.o: kernel/memory/heap.cpp kernel/memory/heap.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/shell.o: kernel/terminal/shell.cpp kernel/terminal/shell.hpp | $(BUILD)
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
>@echo "Linux95 Kernel v0.2 image built:"
>@ls -lh $@

test: all
>$(PYTHON) tests/source_checks.py
>$(PYTHON) tests/image_checks.py

test-qemu: all
>@command -v $(QEMU) >/dev/null || { echo "Missing tool: $(QEMU)"; exit 1; }
>$(PYTHON) tests/qemu_smoke.py

run: all
>$(QEMU) \
	-machine pc \
	-m 128M \
	-drive format=raw,file=$(BUILD)/linux95-kernel.img

run-debug: all
>$(QEMU) \
	-machine pc \
	-m 128M \
	-drive format=raw,file=$(BUILD)/linux95-kernel.img \
	-no-reboot \
	-no-shutdown

clean:
>rm -rf $(BUILD)
