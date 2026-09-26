.RECIPEPREFIX := >

BUILD := build

NASM := nasm
CXX := g++
LD := ld
OBJCOPY := objcopy
READELF := readelf
PYTHON := python3
QEMU := qemu-system-x86_64

USER_CXXFLAGS := -std=c++17 -m64 -mcmodel=large -ffreestanding -fno-builtin \
	-fno-exceptions -fno-rtti -fno-stack-protector -fno-pie \
	-fno-threadsafe-statics -fno-use-cxa-atexit -mno-red-zone \
	-mno-mmx -mno-sse -mno-sse2 -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -Wall -Wextra -Werror -O2 -Iuser/include

SECTOR := 512
STAGE2_SECTORS := 16
KERNEL_LBA := 17
KERNEL_SECTORS := 256
IMAGE_SECTORS := 273
STORAGE_TEST_IMAGE := $(BUILD)/linux95-storage-test.img
FAULT_TEST_IMAGE := $(BUILD)/linux95-fault-test.img
PREEMPTION_TEST_IMAGE := $(BUILD)/linux95-preemption-test.img

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
	$(BUILD)/int80_entry.o \
	$(BUILD)/syscall_entry.o \
	$(BUILD)/context.o \
	$(BUILD)/paging_bootstrap.o \
	$(BUILD)/segments_asm.o \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o $(BUILD)/vga_output.o $(BUILD)/shell_session.o $(BUILD)/framebuffer.o $(BUILD)/renderer.o \
        $(BUILD)/terminal_model.o $(BUILD)/terminal_app.o \
        $(BUILD)/system_info_app.o \
        $(BUILD)/window_manager.o \
        $(BUILD)/desktop.o \
	$(BUILD)/panic.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/pic.o \
	$(BUILD)/pit.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/ps2.o \
	$(BUILD)/mouse.o \
	$(BUILD)/pci.o \
	$(BUILD)/rtl8139.o \
	$(BUILD)/ethernet.o \
	$(BUILD)/arp.o \
	$(BUILD)/ipv4.o \
	$(BUILD)/icmp.o \
	$(BUILD)/network.o \
	$(BUILD)/memory.o \
	$(BUILD)/virtual.o \
	$(BUILD)/physical.o \
	$(BUILD)/paging.o \
	$(BUILD)/user_space.o \
	$(BUILD)/elf.o \
	$(BUILD)/process.o \
	$(BUILD)/scheduler.o \
	$(BUILD)/syscall.o \
	$(BUILD)/segments.o \
	$(BUILD)/tss.o \
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

NETWORK_TEST_OBJS := $(subst $(BUILD)/kernel.o,$(BUILD)/kernel-network-test.o,$(KERNEL_OBJS))
NETWORK_TEST_IMAGE := $(BUILD)/linux95-kernel-network-test.img

.PHONY: all clean run run-debug test test-qemu prepare-storage-test-image test-host-segments test-host-process test-host-scheduler test-host-user-space test-host-elf test-host-elf-loader-plan test-host-syscall test-host-memory test-host-storage test-host-filesystem test-host-graphics test-host-pci test-host-rtl8139-helpers test-host-kernel-virtual-to-physical test-host-ethernet test-host-arp test-host-ipv4 test-host-icmp test-host-udp test-host-heap test-memory-source test-storage-source test-relocations check-tools

all: check-tools $(BUILD)/linux95-kernel.img $(BUILD)/user/init.elf $(BUILD)/user/worker.elf

check-tools:
>@for tool in $(NASM) $(CXX) $(LD) $(OBJCOPY) $(READELF) $(PYTHON); do \
	command -v $$tool >/dev/null || { \
		echo "Missing tool: $$tool"; \
		exit 1; \
	}; \
done

$(BUILD):
>mkdir -p $(BUILD)

$(BUILD)/user:
>mkdir -p $@

$(BUILD)/user/start.o: user/crt/start.asm | $(BUILD)/user
>$(NASM) -f elf64 $< -o $@

$(BUILD)/user/init.o: user/init/main.cpp user/include/linux95_syscall.hpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/worker.o: user/worker/main.cpp user/include/linux95_syscall.hpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/preempt_hog.o: user/preempt_hog/main.cpp user/include/linux95_syscall.hpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/preempt_worker.o: user/preempt_worker/main.cpp user/include/linux95_syscall.hpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/fault.o: user/fault/main.cpp | $(BUILD)/user
>$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(BUILD)/user/init.elf: $(BUILD)/user/start.o $(BUILD)/user/init.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/init.o

$(BUILD)/user/worker.elf: $(BUILD)/user/start.o $(BUILD)/user/worker.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/worker.o

$(BUILD)/user/preempt_hog.elf: $(BUILD)/user/start.o $(BUILD)/user/preempt_hog.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/preempt_hog.o

$(BUILD)/user/preempt_worker.elf: $(BUILD)/user/start.o $(BUILD)/user/preempt_worker.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/preempt_worker.o

$(BUILD)/user/fault.elf: $(BUILD)/user/start.o $(BUILD)/user/fault.o user/user.ld
>$(LD) -nostdlib -static -no-pie -z max-page-size=0x1000 -T user/user.ld -o $@ $(BUILD)/user/start.o $(BUILD)/user/fault.o

$(BUILD)/int80_entry.o: kernel/syscall/int80_entry.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/syscall_entry.o: kernel/syscall/syscall_entry.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/context.o: kernel/process/context.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/scheduler.o: kernel/process/scheduler.cpp kernel/process/scheduler.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

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

$(BUILD)/segments_asm.o: kernel/arch/x86_64/segments.asm | $(BUILD)
>$(NASM) -f elf64 $< -o $@

$(BUILD)/kernel.o: \
	kernel/kernel.cpp \
	kernel/boot_info.hpp \
	kernel/arch/x86_64/segments.hpp \
	kernel/arch/x86_64/tss.hpp \
	kernel/filesystem/vfs.hpp \
	kernel/net/network.hpp \
	kernel/filesystem/vfs_self_test.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel-network-test.o: \
	kernel/kernel.cpp \
	kernel/boot_info.hpp \
	kernel/arch/x86_64/segments.hpp \
	kernel/arch/x86_64/tss.hpp \
	kernel/filesystem/vfs.hpp \
	kernel/net/network.hpp \
	kernel/filesystem/vfs_self_test.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -DLINUX95_QEMU_NETWORK_SELF_TEST -c $< -o $@

$(BUILD)/renderer.o: kernel/graphics/renderer.cpp kernel/graphics/renderer.hpp kernel/graphics/font8x8.hpp kernel/graphics/framebuffer.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/terminal_model.o: kernel/gui/terminal_model.cpp kernel/gui/terminal_model.hpp kernel/terminal/output.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/terminal_app.o: kernel/gui/terminal_app.cpp kernel/gui/terminal_app.hpp kernel/gui/app.hpp kernel/gui/terminal_model.hpp kernel/terminal/shell_session.hpp kernel/terminal/shell.hpp kernel/graphics/renderer.hpp kernel/net/network.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/system_info_app.o: kernel/gui/system_info_app.cpp kernel/gui/system_info_app.hpp kernel/gui/app.hpp kernel/graphics/renderer.hpp kernel/arch/pit.hpp kernel/memory/memory.hpp kernel/memory/physical.hpp kernel/memory/heap.hpp kernel/storage/disk.hpp kernel/filesystem/filesystem.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/window_manager.o: kernel/gui/window_manager.cpp kernel/gui/window_manager.hpp kernel/gui/geometry.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/desktop.o: kernel/gui/desktop.cpp kernel/gui/desktop.hpp kernel/gui/window_manager.hpp kernel/gui/app.hpp kernel/gui/terminal_app.hpp kernel/gui/system_info_app.hpp kernel/graphics/renderer.hpp kernel/arch/debug.hpp kernel/arch/io.hpp kernel/arch/keyboard.hpp kernel/arch/mouse.hpp kernel/arch/pit.hpp kernel/net/network.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/framebuffer.o: kernel/graphics/framebuffer.cpp kernel/graphics/framebuffer.hpp kernel/graphics/framebuffer_helpers.hpp kernel/boot_info.hpp kernel/memory/paging.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vga.o: kernel/terminal/vga.cpp kernel/terminal/vga.hpp kernel/arch/io.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vga_output.o: kernel/terminal/vga_output.cpp kernel/terminal/vga_output.hpp kernel/terminal/output.hpp kernel/terminal/vga.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/shell_session.o: kernel/terminal/shell_session.cpp kernel/terminal/shell_session.hpp kernel/terminal/output.hpp kernel/net/network.hpp | $(BUILD)
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

$(BUILD)/ps2.o: kernel/arch/ps2.cpp kernel/arch/ps2.hpp kernel/arch/io.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mouse.o: kernel/arch/mouse.cpp kernel/arch/mouse.hpp kernel/arch/mouse_helpers.hpp kernel/arch/ps2.hpp kernel/arch/io.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pci.o: kernel/pci/pci.cpp kernel/pci/pci.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/rtl8139.o: kernel/drivers/rtl8139.cpp kernel/drivers/rtl8139.hpp kernel/drivers/rtl8139_helpers.hpp kernel/memory/memory.hpp kernel/pci/pci.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/ethernet.o: kernel/net/ethernet.cpp kernel/net/ethernet.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/arp.o: kernel/net/arp.cpp kernel/net/arp.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/ipv4.o: kernel/net/ipv4.cpp kernel/net/ipv4.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/icmp.o: kernel/net/icmp.cpp kernel/net/icmp.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/network.o: kernel/net/network.cpp kernel/net/network.hpp kernel/net/arp.hpp kernel/net/ethernet.hpp kernel/net/icmp.hpp kernel/net/ipv4.hpp kernel/drivers/rtl8139.hpp kernel/arch/debug.hpp kernel/arch/pit.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@


$(BUILD)/memory.o: kernel/memory/memory.cpp kernel/memory/memory.hpp kernel/memory/address.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/virtual.o: kernel/memory/virtual.cpp kernel/memory/virtual.hpp kernel/memory/address.hpp kernel/memory/memory.hpp kernel/arch/debug.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/physical.o: kernel/memory/physical.cpp kernel/memory/physical.hpp kernel/memory/page_bitmap.hpp kernel/memory/address.hpp kernel/memory/memory.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/paging.o: kernel/memory/paging.cpp kernel/memory/paging.hpp kernel/memory/physical.hpp kernel/memory/address.hpp kernel/arch/x86_64/control_regs.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/user_space.o: kernel/memory/user_space.cpp kernel/memory/user_space.hpp kernel/memory/paging.hpp kernel/memory/physical.hpp kernel/memory/address.hpp kernel/arch/x86_64/control_regs.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/elf.o: kernel/user/elf.cpp kernel/user/elf.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/process.o: kernel/process/process.cpp kernel/process/process.hpp kernel/process/context.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/syscall.o: kernel/syscall/syscall.cpp kernel/syscall/syscall.hpp kernel/process/process.hpp kernel/memory/user_space.hpp kernel/arch/x86_64/msr.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/segments.o: kernel/arch/x86_64/segments.cpp kernel/arch/x86_64/segments.hpp kernel/arch/x86_64/tss.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tss.o: kernel/arch/x86_64/tss.cpp kernel/arch/x86_64/tss.hpp kernel/arch/x86_64/segments.hpp kernel/syscall/syscall.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/self_test.o: kernel/memory/self_test.cpp kernel/memory/self_test.hpp kernel/memory/paging.hpp kernel/memory/physical.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(CXXFLAGS) -c $< -o $@


$(BUILD)/host-e820-limit-test: tests/host/e820_limit_test.cpp kernel/memory/memory.cpp kernel/memory/memory.hpp kernel/boot_info.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/e820_limit_test.cpp kernel/memory/memory.cpp -o $@

$(BUILD)/host-segments-test: tests/host/segments_test.cpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-segments: $(BUILD)/host-segments-test
>$(BUILD)/host-segments-test

$(BUILD)/host-process-test: tests/host/process_test.cpp kernel/process/process.cpp kernel/process/process.hpp kernel/process/context.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/process_test.cpp kernel/process/process.cpp -o $@

test-host-process: $(BUILD)/host-process-test
>$(BUILD)/host-process-test

$(BUILD)/host-scheduler-test: tests/host/scheduler_test.cpp kernel/process/scheduler.cpp kernel/process/scheduler.hpp kernel/process/process.hpp kernel/process/context.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections tests/host/scheduler_test.cpp kernel/process/scheduler.cpp -Wl,--gc-sections -o $@

test-host-scheduler: $(BUILD)/host-scheduler-test
>$(BUILD)/host-scheduler-test

$(BUILD)/host-user-space-test: tests/host/user_space_test.cpp kernel/memory/user_space.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-user-space: $(BUILD)/host-user-space-test
>$(BUILD)/host-user-space-test

$(BUILD)/host-elf-test: tests/host/elf_test.cpp kernel/user/elf.cpp kernel/user/elf.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections tests/host/elf_test.cpp kernel/user/elf.cpp -Wl,--gc-sections -o $@

test-host-elf: $(BUILD)/host-elf-test
>$(BUILD)/host-elf-test

$(BUILD)/host-elf-loader-plan-test: tests/host/elf_loader_plan_test.cpp kernel/user/elf.cpp kernel/user/elf.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections tests/host/elf_loader_plan_test.cpp kernel/user/elf.cpp -Wl,--gc-sections -o $@

test-host-elf-loader-plan: $(BUILD)/host-elf-loader-plan-test
>$(BUILD)/host-elf-loader-plan-test

$(BUILD)/host-syscall-test: tests/host/syscall_test.cpp kernel/syscall/syscall.cpp kernel/syscall/syscall.hpp kernel/memory/user_space.cpp kernel/memory/user_space.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections tests/host/syscall_test.cpp kernel/syscall/syscall.cpp kernel/memory/user_space.cpp -Wl,--gc-sections -o $@

test-host-syscall: $(BUILD)/host-syscall-test
>$(BUILD)/host-syscall-test

$(BUILD)/host-page-bitmap-test: tests/host/page_bitmap_test.cpp kernel/memory/page_bitmap.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-memory: $(BUILD)/host-page-bitmap-test $(BUILD)/host-e820-limit-test
>$(BUILD)/host-page-bitmap-test
>$(BUILD)/host-e820-limit-test

$(BUILD)/host-heap-test: tests/host/heap_test.cpp kernel/memory/heap.cpp kernel/memory/heap.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/heap_test.cpp kernel/memory/heap.cpp -o $@

test-host-heap: $(BUILD)/host-heap-test
>$(BUILD)/host-heap-test

$(BUILD)/host-pci-helpers-test: tests/host/pci_helpers_test.cpp kernel/pci/pci.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-pci: $(BUILD)/host-pci-helpers-test
>$(BUILD)/host-pci-helpers-test

$(BUILD)/host-rtl8139-helpers-test: tests/host/rtl8139_helpers_test.cpp kernel/drivers/rtl8139_helpers.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-rtl8139-helpers: $(BUILD)/host-rtl8139-helpers-test
>$(BUILD)/host-rtl8139-helpers-test

$(BUILD)/host-kernel-virtual-to-physical-test: tests/host/kernel_virtual_to_physical_test.cpp kernel/memory/memory.cpp kernel/memory/memory.hpp kernel/memory/address.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/kernel_virtual_to_physical_test.cpp kernel/memory/memory.cpp -o $@

test-host-kernel-virtual-to-physical: $(BUILD)/host-kernel-virtual-to-physical-test
>$(BUILD)/host-kernel-virtual-to-physical-test

$(BUILD)/host-ethernet-test: tests/host/ethernet_test.cpp kernel/net/ethernet.cpp kernel/net/ethernet.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/ethernet_test.cpp kernel/net/ethernet.cpp -o $@

test-host-ethernet: $(BUILD)/host-ethernet-test
>$(BUILD)/host-ethernet-test

$(BUILD)/host-arp-test: tests/host/arp_test.cpp kernel/net/arp.cpp kernel/net/arp.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/arp_test.cpp kernel/net/arp.cpp -o $@

test-host-arp: $(BUILD)/host-arp-test
>$(BUILD)/host-arp-test

$(BUILD)/host-ipv4-test: tests/host/ipv4_test.cpp kernel/net/ipv4.cpp kernel/net/ipv4.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/ipv4_test.cpp kernel/net/ipv4.cpp -o $@

test-host-ipv4: $(BUILD)/host-ipv4-test
>$(BUILD)/host-ipv4-test

$(BUILD)/host-icmp-test: tests/host/icmp_test.cpp kernel/net/icmp.cpp kernel/net/icmp.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/icmp_test.cpp kernel/net/icmp.cpp -o $@

test-host-icmp: $(BUILD)/host-icmp-test
>$(BUILD)/host-icmp-test

$(BUILD)/host-udp-test: tests/host/udp_test.cpp kernel/net/udp.cpp kernel/net/udp.hpp kernel/net/net_types.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/udp_test.cpp kernel/net/udp.cpp -o $@

test-host-udp: $(BUILD)/host-udp-test
>$(BUILD)/host-udp-test

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
	kernel/filesystem/vfs.hpp \
	kernel/net/network.hpp \
	kernel/terminal/shell_session.hpp | $(BUILD)
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

$(BUILD)/kernel-network-test.elf: $(NETWORK_TEST_OBJS) linker.ld
>$(LD) \
	-nostdlib \
	-z max-page-size=0x1000 \
	-T linker.ld \
	-o $@ \
	$(NETWORK_TEST_OBJS)
>@entry=$$($(READELF) -h $@ | awk '/Entry point address:/ {print $$4}'); \
	test "$$entry" = "0x100000" || { \
		echo "ERROR: bad network-test kernel entry: $$entry"; \
		exit 1; \
	}

$(BUILD)/kernel-network-test.bin: $(BUILD)/kernel-network-test.elf
>$(OBJCOPY) -O binary $< $@
>@size=$$(stat -c%s $@); \
	max=$$(( $(KERNEL_SECTORS) * $(SECTOR) )); \
	test $$size -le $$max || { \
		echo "ERROR: Network-test kernel too large: $$size > $$max"; \
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
>@echo "Linux95 Kernel v1.0 Graphics Desktop Foundation image built:"
>@ls -lh $@

$(NETWORK_TEST_IMAGE): \
	$(BUILD)/stage1.bin \
	$(BUILD)/stage2.bin \
	$(BUILD)/kernel-network-test.bin
>dd if=/dev/zero of=$@ bs=$(SECTOR) count=$(IMAGE_SECTORS) status=none
>dd if=$(BUILD)/stage1.bin of=$@ bs=$(SECTOR) seek=0 conv=notrunc status=none
>dd if=$(BUILD)/stage2.bin of=$@ bs=$(SECTOR) seek=1 conv=notrunc status=none
>dd if=$(BUILD)/kernel-network-test.bin of=$@ bs=$(SECTOR) seek=$(KERNEL_LBA) conv=notrunc status=none

$(STORAGE_TEST_IMAGE): tests/prepare_fat32_image.py $(BUILD)/user/init.elf $(BUILD)/user/worker.elf | $(BUILD)
>@for tool in mkfs.fat mmd mcopy; do \
	command -v $$tool >/dev/null || { echo "Missing tool: $$tool"; exit 1; }; \
done
>$(PYTHON) tests/prepare_fat32_image.py $@

$(FAULT_TEST_IMAGE): tests/prepare_fat32_image.py $(BUILD)/user/fault.elf $(BUILD)/user/worker.elf | $(BUILD)
>$(PYTHON) tests/prepare_fat32_image.py $@ --process-fault

$(PREEMPTION_TEST_IMAGE): tests/prepare_fat32_image.py $(BUILD)/user/preempt_hog.elf $(BUILD)/user/preempt_worker.elf | $(BUILD)
>$(PYTHON) tests/prepare_fat32_image.py $@ --process-preemption

prepare-storage-test-image: $(STORAGE_TEST_IMAGE)

test-memory-source:
>$(PYTHON) tests/memory_source_checks.py

test-relocations: all
>$(PYTHON) tests/relocation_checks.py

test-storage-source:
>$(PYTHON) tests/storage_source_checks.py

test-filesystem-source:
>$(PYTHON) tests/filesystem_source_checks.py

.PHONY: test-preemption-source
test-preemption-source:
>$(PYTHON) tests/preemption_source_checks.py

test: test-preemption-source
test: all test-host-memory test-host-storage test-host-heap test-host-segments test-host-process test-host-scheduler test-host-user-space test-host-elf test-host-elf-loader-plan test-host-syscall test-host-filesystem test-host-graphics test-host-pci test-host-rtl8139-helpers test-host-kernel-virtual-to-physical test-host-ethernet test-host-arp test-host-ipv4 test-host-icmp
>$(PYTHON) tests/source_checks.py
>$(PYTHON) tests/image_checks.py
>$(PYTHON) tests/memory_source_checks.py
>$(PYTHON) tests/storage_source_checks.py
>$(PYTHON) tests/filesystem_source_checks.py
>$(PYTHON) tests/relocation_checks.py

test-qemu: all $(NETWORK_TEST_IMAGE) prepare-storage-test-image
>@command -v $(QEMU) >/dev/null || { echo "Missing tool: $(QEMU)"; exit 1; }
>$(PYTHON) tests/qemu_smoke.py
>$(PYTHON) tests/qemu_smoke.py --without-network
>$(PYTHON) tests/qemu_smoke.py --process-preemption-test

run: all prepare-storage-test-image
>$(QEMU) \
	-machine pc \
	-m 128M \
	-boot c \
    -vga std \
	-drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/linux95-kernel.img \
	-drive if=ide,index=1,media=disk,format=raw,file=$(STORAGE_TEST_IMAGE) \
	-netdev user,id=net0 \
	-device rtl8139,netdev=net0

run-debug: all prepare-storage-test-image
>$(QEMU) \
	-machine pc \
	-m 128M \
	-boot c \
    -vga std \
	-drive if=ide,index=0,media=disk,format=raw,file=$(BUILD)/linux95-kernel.img \
	-drive if=ide,index=1,media=disk,format=raw,file=$(STORAGE_TEST_IMAGE) \
	-netdev user,id=net0 \
	-device rtl8139,netdev=net0 \
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

.PHONY: test-host-mouse-helpers

$(BUILD)/host-mouse-helpers-test: tests/host/mouse_helpers_test.cpp kernel/arch/mouse_helpers.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) $< -o $@

test-host-mouse-helpers: $(BUILD)/host-mouse-helpers-test
>$(BUILD)/host-mouse-helpers-test

test-host-graphics: test-host-mouse-helpers

.PHONY: test-host-shell-session

$(BUILD)/host-shell-session-test: tests/host/shell_session_test.cpp kernel/terminal/output.hpp kernel/terminal/shell_session.hpp kernel/terminal/shell_session.cpp kernel/net/network.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/shell_session_test.cpp kernel/terminal/shell_session.cpp -o $@

test-host-shell-session: $(BUILD)/host-shell-session-test
>$(BUILD)/host-shell-session-test

test-host-graphics: test-host-shell-session

.PHONY: test-host-window-manager

$(BUILD)/host-window-manager-test: tests/host/window_manager_test.cpp kernel/gui/geometry.hpp kernel/gui/window_manager.hpp kernel/gui/window_manager.cpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/window_manager_test.cpp kernel/gui/window_manager.cpp -o $@

test-host-window-manager: $(BUILD)/host-window-manager-test
>$(BUILD)/host-window-manager-test

test-host-graphics: test-host-window-manager

.PHONY: test-host-terminal-model

$(BUILD)/host-terminal-model-test: tests/host/terminal_model_test.cpp kernel/gui/terminal_model.hpp kernel/gui/terminal_model.cpp kernel/terminal/output.hpp | $(BUILD)
>$(CXX) $(HOST_CXXFLAGS) tests/host/terminal_model_test.cpp kernel/gui/terminal_model.cpp -o $@

test-host-terminal-model: $(BUILD)/host-terminal-model-test
>$(BUILD)/host-terminal-model-test

test-host-graphics: test-host-terminal-model
