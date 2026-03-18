ARCH ?= x86_64
PROFILE ?= debug

OUT := out
BUILD := build

.PHONY: all help kernel iso run qemu kvm vmware vbox gdb test lint rust-check size-report

all: kernel

help:
	@echo "ComputeKERNEL targets:"
	@echo "  make kernel       - build kernel (scaffold)"
	@echo "  make iso          - assemble ISO image (placeholder)"
	@echo "  make run|qemu     - run with QEMU"
	@echo "  make kvm          - run with QEMU+KVM accel"
	@echo "  make vmware       - print VMware instructions"
	@echo "  make vbox         - print VirtualBox instructions"
	@echo "  make gdb          - print gdb attach instructions"
	@echo "  make test         - run tooling checks for scaffold"
	@echo "  make lint         - shell/python syntax checks"
	@echo "  make rust-check   - run Rust check if rust crate exists"
	@echo "  make size-report  - report output directory size"

kernel:
	@mkdir -p $(OUT) $(BUILD)
	@echo "[CK] kernel scaffold build ($(ARCH),$(PROFILE))"
	@echo "This repository now includes starter files; real compilation is next phase."

iso: kernel
	@./scripts/build-iso.sh

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

