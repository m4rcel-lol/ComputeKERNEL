# ComputeKERNEL

A production-ready, UNIX-compatible monolithic kernel written in 100% Rust, targeting x86_64 and booting via UEFI.

## Features

- **UEFI boot** via the `bootloader` crate (v0.11)
- **x86_64** bare-metal (`no_std`) kernel
- **GDT** with kernel/user segments and TSS
- **IDT** with full CPU exception + hardware IRQ handlers
- **Physical frame allocator** backed by the bootloader memory map
- **Virtual memory** via `x86_64::structures::paging::OffsetPageTable`
- **Kernel heap** using `linked_list_allocator`
- **Drivers**: UART 16550 serial, VGA text mode, UEFI framebuffer, PCI enumeration, PS/2 keyboard, ACPI stub, NVMe stub, AHCI stub, e1000 NIC stub, Intel HDA stub, xHCI USB stub, Bluetooth HCI stub
- **Process management**: PCB, round-robin scheduler, baseline `fork`/`execve`/`exit`/`wait4`, default signal delivery
- **Virtual Filesystem (VFS)**: ext4 stub, `/proc`, `/sys`, `/dev`, initramfs (cpio newc)
- **System calls**: Linux x86_64 ABI — `read`, `write`, `open`, `close`, `fork`, `execve`, `wait4`, `exit`, `getpid`, `kill`, and core pseudo-filesystem I/O (`/proc`, `/sys`, `/dev`)
- **TCP/IP stack**: Ethernet, IPv4, TCP, UDP parsing layers (send stubs)
- **PID 1 init** process (`init` crate): mounts filesystems, spawns `/bin/sh`

## Project Structure

```
ComputeKERNEL/
├── kernel/               # Kernel crate (no_std)
│   ├── src/
│   │   ├── main.rs       # Entry point (bootloader_api::entry_point!)
│   │   ├── arch/x86_64/  # GDT, IDT, interrupt handlers
│   │   ├── memory/       # Frame allocator, paging, heap
│   │   ├── drivers/      # All device drivers
│   │   ├── process/      # Scheduler, signals
│   │   ├── fs/           # VFS, ext4, proc, sys, dev, initramfs
│   │   ├── syscall/      # Linux-compatible syscall table
│   │   └── net/          # TCP/IP stack
│   ├── linker.ld         # Kernel linker script
│   └── build.rs          # Build script (sets linker flags)
├── init/                 # PID 1 init process (userspace)
├── targets/              # Custom x86_64 target JSON
└── .cargo/config.toml    # Build configuration
```

## Prerequisites

- **Rust nightly** (required for `abi_x86_interrupt` and `alloc_error_handler`):
  ```sh
  rustup toolchain install nightly
  rustup override set nightly
  ```
- **`rust-src`** component (for `build-std`):
  ```sh
  rustup component add rust-src --toolchain nightly
  ```
- **`llvm-tools-preview`** (for `rust-lld`):
  ```sh
  rustup component add llvm-tools-preview --toolchain nightly
  ```

## Building

```sh
# Build the kernel (cross-compilation to bare-metal x86_64)
cargo +nightly build --package compute-kernel \
    -Z build-std=core,compiler_builtins,alloc \
    -Z build-std-features=compiler-builtins-mem \
    --target targets/x86_64-computekernel.json

# Release build
cargo +nightly build --package compute-kernel --release \
    -Z build-std=core,compiler_builtins,alloc \
    -Z build-std-features=compiler-builtins-mem \
    --target targets/x86_64-computekernel.json
```

## Creating a Bootable Image

Use the `bootloader` crate's builder to combine the kernel ELF with the UEFI bootloader:

```sh
cargo +nightly install bootloader --features uefi-binary
bootloader-tool kernel/target/.../compute-kernel --uefi
```

## Running in QEMU

```sh
qemu-system-x86_64 \
    -drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_CODE.fd,readonly=on \
    -drive format=raw,file=disk.img \
    -serial stdio \
    -m 256M \
    -no-reboot
```

## Nightly Features Used

| Feature | Why |
|---|---|
| `abi_x86_interrupt` | Required for the `extern "x86-interrupt"` calling convention used in interrupt handlers |
| `alloc_error_handler` | Required to define a custom handler for allocation failures in `no_std` |
| `naked_functions` | Reserved for future use in low-level context switch stubs |

## License

MIT — see [LICENSE](LICENSE).
