//! Interrupt Descriptor Table (IDT) setup for x86_64.
//!
//! Registers handlers for all CPU exceptions and hardware IRQs.

use super::{
    gdt::{DOUBLE_FAULT_IST_INDEX, GPF_IST_INDEX},
    interrupts,
};
use x86_64::structures::idt::InterruptDescriptorTable;

/// Build and load the IDT.
pub fn init() {
    let idt = IDT.call_once(|| {
        let mut idt = InterruptDescriptorTable::new();

        // ── CPU exception handlers ──────────────────────────────────────────
        idt.divide_error.set_handler_fn(interrupts::divide_error_handler);
        idt.debug.set_handler_fn(interrupts::debug_handler);
        idt.non_maskable_interrupt.set_handler_fn(interrupts::nmi_handler);
        idt.breakpoint.set_handler_fn(interrupts::breakpoint_handler);
        idt.overflow.set_handler_fn(interrupts::overflow_handler);
        idt.bound_range_exceeded.set_handler_fn(interrupts::bound_range_exceeded_handler);
        idt.invalid_opcode.set_handler_fn(interrupts::invalid_opcode_handler);
        idt.device_not_available.set_handler_fn(interrupts::device_not_available_handler);

        unsafe {
            idt.double_fault
                .set_handler_fn(interrupts::double_fault_handler)
                .set_stack_index(DOUBLE_FAULT_IST_INDEX);
        }

        idt.invalid_tss.set_handler_fn(interrupts::invalid_tss_handler);
        idt.segment_not_present.set_handler_fn(interrupts::segment_not_present_handler);
        idt.stack_segment_fault.set_handler_fn(interrupts::stack_segment_fault_handler);

        unsafe {
            idt.general_protection_fault
                .set_handler_fn(interrupts::general_protection_fault_handler)
                .set_stack_index(GPF_IST_INDEX);
        }

        idt.page_fault.set_handler_fn(interrupts::page_fault_handler);
        idt.x87_floating_point.set_handler_fn(interrupts::x87_floating_point_handler);
        idt.alignment_check.set_handler_fn(interrupts::alignment_check_handler);
        idt.machine_check.set_handler_fn(interrupts::machine_check_handler);
        idt.simd_floating_point.set_handler_fn(interrupts::simd_floating_point_handler);
        idt.virtualization.set_handler_fn(interrupts::virtualization_handler);
        idt.security_exception.set_handler_fn(interrupts::security_exception_handler);

        // ── Hardware IRQ handlers (PIC remapped to 0x20–0x2F) ──────────────
        idt[interrupts::InterruptIndex::Timer as u8]
            .set_handler_fn(interrupts::timer_interrupt_handler);
        idt[interrupts::InterruptIndex::Keyboard as u8]
            .set_handler_fn(interrupts::keyboard_interrupt_handler);
        idt[interrupts::InterruptIndex::_Cascade as u8]
            .set_handler_fn(interrupts::cascade_interrupt_handler);
        idt[interrupts::InterruptIndex::_Com2 as u8]
            .set_handler_fn(interrupts::com2_interrupt_handler);
        idt[interrupts::InterruptIndex::_Com1 as u8]
            .set_handler_fn(interrupts::com1_interrupt_handler);
        idt[interrupts::InterruptIndex::_Lpt2 as u8]
            .set_handler_fn(interrupts::lpt2_interrupt_handler);
        idt[interrupts::InterruptIndex::_FloppyDisk as u8]
            .set_handler_fn(interrupts::floppy_interrupt_handler);
        idt[interrupts::InterruptIndex::_Lpt1 as u8]
            .set_handler_fn(interrupts::lpt1_interrupt_handler);
        idt[interrupts::InterruptIndex::_RtcTimer as u8]
            .set_handler_fn(interrupts::rtc_interrupt_handler);
        idt[interrupts::InterruptIndex::_Legacy1 as u8]
            .set_handler_fn(interrupts::legacy1_interrupt_handler);
        idt[interrupts::InterruptIndex::_Legacy2 as u8]
            .set_handler_fn(interrupts::legacy2_interrupt_handler);
        idt[interrupts::InterruptIndex::_Legacy3 as u8]
            .set_handler_fn(interrupts::legacy3_interrupt_handler);
        idt[interrupts::InterruptIndex::_Ps2Mouse as u8]
            .set_handler_fn(interrupts::mouse_interrupt_handler);
        idt[interrupts::InterruptIndex::_FpuCoprocessor as u8]
            .set_handler_fn(interrupts::fpu_interrupt_handler);
        idt[interrupts::InterruptIndex::PrimaryAta as u8]
            .set_handler_fn(interrupts::primary_ata_handler);
        idt[interrupts::InterruptIndex::SecondaryAta as u8]
            .set_handler_fn(interrupts::secondary_ata_handler);

        idt
    });

    idt.load();
}

static IDT: spin::Once<InterruptDescriptorTable> = spin::Once::new();
