//! ComputeKERNEL - A UNIX-compatible monolithic kernel written in Rust.
//!
//! This is the kernel entry point. The bootloader invokes `kernel_main` after
//! setting up the UEFI environment and memory map.

#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]
#![feature(alloc_error_handler)]

extern crate alloc;

use bootloader_api::{entry_point, BootInfo};
use core::alloc::Layout;
use core::panic::PanicInfo;

// Module declarations
pub mod arch;
pub mod drivers;
pub mod fs;
pub mod memory;
pub mod net;
pub mod process;
pub mod shell;
pub mod syscall;

// Register the kernel entry point with the bootloader.
entry_point!(kernel_main);

/// Kernel main entry point, called by the bootloader after UEFI setup.
fn kernel_main(boot_info: &'static mut BootInfo) -> ! {
    // 1. Initialize serial port first (needed for early debug output before VGA).
    drivers::serial::init();
    serial_println!("\n[BOOT] ComputeKERNEL v1.0.0 booting...");

    // 2. Initialize VGA text buffer.
    drivers::vga::init();
    kprintln!("ComputeKERNEL v1.0.0");

    // 3. Initialize architecture-specific structures.
    serial_println!("[BOOT] Initializing GDT...");
    arch::x86_64::gdt::init();
    serial_println!("[OK]   GDT initialized");

    serial_println!("[BOOT] Initializing IDT...");
    arch::x86_64::idt::init();
    serial_println!("[OK]   IDT initialized");

    // 4. Initialize and remap the 8259 PIC (must happen before enabling IRQs).
    serial_println!("[BOOT] Initializing PIC...");
    arch::x86_64::interrupts::init_pics();
    serial_println!("[OK]   PIC initialized");

    // 5. Initialize memory management (frame allocator + kernel heap).
    serial_println!("[BOOT] Initializing memory management...");
    memory::init(boot_info);
    serial_println!("[OK]   Memory management initialized");

    // 6. Initialize device drivers.
    serial_println!("[BOOT] Initializing PCI bus...");
    drivers::pci::init();
    serial_println!("[OK]   PCI bus enumerated");

    serial_println!("[BOOT] Initializing keyboard...");
    drivers::keyboard::init();
    serial_println!("[OK]   Keyboard initialized");

    // 7. Initialize process scheduler.
    serial_println!("[BOOT] Initializing scheduler...");
    process::init();
    serial_println!("[OK]   Scheduler initialized");

    // 8. Enable hardware interrupts.
    x86_64::instructions::interrupts::enable();
    serial_println!("[OK]   Interrupts enabled");

    serial_println!("[BOOT] ComputeKERNEL v1.0.0 ready — starting shell");

    // 9. Drop into the interactive kernel shell.
    shell::run()
}

/// Panic handler — called on any unrecoverable kernel error.
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    x86_64::instructions::interrupts::disable();
    serial_println!("\n[PANIC] {}", info);
    kprintln!("\nKERNEL PANIC: {}", info);
    loop {
        x86_64::instructions::hlt();
    }
}

/// Allocation error handler (nightly feature: alloc_error_handler).
#[alloc_error_handler]
fn alloc_error_handler(layout: Layout) -> ! {
    panic!("allocation error: {:?}", layout)
}
