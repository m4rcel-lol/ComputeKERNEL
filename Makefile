ARCH    ?= x86_64
PROFILE ?= debug

OUT   := out
BUILD := build

CC      := gcc
AS      := gcc
LD      := ld

CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -m64 -std=c11 -Iinclude -Wall -Wextra -g

ASFLAGS := -m64 -c

LDFLAGS := -T arch/x86_64/linker.ld -nostdlib -z max-page-size=0x1000

SRCS_C  := \
    kernel/init/main.c \
    kernel/printk.c \
    kernel/stubs.c \
    kernel/lib/string.c \
    kernel/drivers/serial.c \
    kernel/arch/x86_64/gdt.c \
    kernel/arch/x86_64/idt.c \
    kernel/arch/x86_64/pic.c \
    kernel/arch/x86_64/pit.c \
    kernel/arch/x86_64/keyboard.c \
    kernel/arch/x86_64/cpu.c \
    kernel/mm/pmm.c \
    kernel/mm/heap.c \
    kernel/sched/sched.c \
    kernel/vfs/vfs.c \
    kernel/vfs/ramfs.c
SRCS_S  := boot/x86_64/entry64.S \
           boot/x86_64/isr_stubs.S \
           boot/x86_64/switch.S

OBJS    := $(patsubst %.S, $(BUILD)/%.o, $(SRCS_S)) \
           $(patsubst %.c, $(BUILD)/%.o, $(SRCS_C))

KERNEL  := $(OUT)/computekernel.elf

.PHONY: all help kernel iso img run qemu kvm vmware vbox gdb test lint rust-check size-report

all: kernel

help:
	@echo "ComputeKERNEL targets:"
	@echo "  make kernel       - compile kernel ELF binary"
	@echo "  make iso          - build bootable ISO (requires grub-pc-bin + xorriso)"
	@echo "  make img          - build bootable .img (requires root + loop device)"
	@echo "  make run|qemu     - run with QEMU"
	@echo "  make kvm          - run with QEMU+KVM accel"
	@echo "  make vmware       - print VMware instructions"
	@echo "  make vbox         - print VirtualBox instructions"
	@echo "  make gdb          - print gdb attach instructions"
	@echo "  make test         - run tooling checks"
	@echo "  make lint         - shell/python syntax checks"
	@echo "  make rust-check   - run Rust check if rust crate exists"
	@echo "  make size-report  - report output directory size"

kernel: $(KERNEL)

$(KERNEL): $(OBJS)
	@mkdir -p $(OUT)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[CK] kernel → $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

iso: kernel
	@./scripts/build-iso.sh

img: kernel
	@./scripts/build-img.sh

run: qemu

qemu:
	@./scripts/run-qemu.sh

kvm:
	@./scripts/run-kvm.sh

vmware:
	@./scripts/run-vmware.sh

vbox:
	@./scripts/run-vbox.sh

gdb:
	@./scripts/debug-gdb.sh

test:
	@./scripts/test.sh

lint:
	@sh -n scripts/*.sh
	@python3 -m py_compile tools/*.py
	@echo "lint checks passed"

rust-check:
	@if [ -f rust/Cargo.toml ]; then \
		cd rust && cargo check --target x86_64-unknown-none; \
	else \
		echo "rust/Cargo.toml not present yet; skipping rust-check"; \
	fi

size-report:
	@du -h $(OUT) 2>/dev/null || echo "out/ does not exist yet"

