//! x86_64 interrupt handlers for CPU exceptions and hardware IRQs.

use crate::serial_println;
use pic8259::ChainedPics;
use x86_64::structures::idt::{InterruptStackFrame, PageFaultErrorCode};

// ── PIC constants ──────────────────────────────────────────────────────────

pub const PIC_1_OFFSET: u8 = 0x20;
pub const PIC_2_OFFSET: u8 = PIC_1_OFFSET + 8;

pub static PICS: spin::Mutex<ChainedPics> =
    spin::Mutex::new(unsafe { ChainedPics::new(PIC_1_OFFSET, PIC_2_OFFSET) });

/// Numeric IRQ offsets after PIC remapping.
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum InterruptIndex {
    Timer = PIC_1_OFFSET,
    Keyboard,
    _Cascade,
    _Com2,
    _Com1,
    _Lpt2,
    _FloppyDisk,
    _Lpt1,
    _RtcTimer,
    _Legacy1,
    _Legacy2,
    _Legacy3,
    _Ps2Mouse,
    _FpuCoprocessor,
    PrimaryAta,
    SecondaryAta,
}

// ── CPU Exception Handlers ─────────────────────────────────────────────────

pub extern "x86-interrupt" fn divide_error_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: DIVIDE ERROR\n{:#?}", frame);
}

pub extern "x86-interrupt" fn debug_handler(frame: InterruptStackFrame) {
    serial_println!("EXCEPTION: DEBUG\n{:#?}", frame);
}

pub extern "x86-interrupt" fn nmi_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: NON-MASKABLE INTERRUPT\n{:#?}", frame);
}

pub extern "x86-interrupt" fn breakpoint_handler(frame: InterruptStackFrame) {
    serial_println!("EXCEPTION: BREAKPOINT\n{:#?}", frame);
}

pub extern "x86-interrupt" fn overflow_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: OVERFLOW\n{:#?}", frame);
}

pub extern "x86-interrupt" fn bound_range_exceeded_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: BOUND RANGE EXCEEDED\n{:#?}", frame);
}

pub extern "x86-interrupt" fn invalid_opcode_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: INVALID OPCODE\n{:#?}", frame);
}

pub extern "x86-interrupt" fn device_not_available_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: DEVICE NOT AVAILABLE\n{:#?}", frame);
}

pub extern "x86-interrupt" fn double_fault_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) -> ! {
    panic!("EXCEPTION: DOUBLE FAULT (error={:#x})\n{:#?}", error_code, frame);
}

pub extern "x86-interrupt" fn invalid_tss_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) {
    panic!("EXCEPTION: INVALID TSS (selector={:#x})\n{:#?}", error_code, frame);
}

pub extern "x86-interrupt" fn segment_not_present_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) {
    panic!("EXCEPTION: SEGMENT NOT PRESENT (selector={:#x})\n{:#?}", error_code, frame);
}

pub extern "x86-interrupt" fn stack_segment_fault_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) {
    panic!("EXCEPTION: STACK SEGMENT FAULT (error={:#x})\n{:#?}", error_code, frame);
}

pub extern "x86-interrupt" fn general_protection_fault_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) {
    panic!("EXCEPTION: GENERAL PROTECTION FAULT (error={:#x})\n{:#?}", error_code, frame);
}

pub extern "x86-interrupt" fn page_fault_handler(
    frame: InterruptStackFrame,
    error_code: PageFaultErrorCode,
) {
    use x86_64::registers::control::Cr2;
    let addr = Cr2::read();
    panic!(
        "EXCEPTION: PAGE FAULT\n  Accessed address: {:?}\n  Error code: {:?}\n{:#?}",
        addr, error_code, frame
    );
}

pub extern "x86-interrupt" fn x87_floating_point_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: x87 FLOATING POINT\n{:#?}", frame);
}

pub extern "x86-interrupt" fn alignment_check_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) {
    panic!("EXCEPTION: ALIGNMENT CHECK (error={:#x})\n{:#?}", error_code, frame);
}

pub extern "x86-interrupt" fn machine_check_handler(frame: InterruptStackFrame) -> ! {
    panic!("EXCEPTION: MACHINE CHECK\n{:#?}", frame);
}

pub extern "x86-interrupt" fn simd_floating_point_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: SIMD FLOATING POINT\n{:#?}", frame);
}

pub extern "x86-interrupt" fn virtualization_handler(frame: InterruptStackFrame) {
    panic!("EXCEPTION: VIRTUALIZATION\n{:#?}", frame);
}

pub extern "x86-interrupt" fn security_exception_handler(
    frame: InterruptStackFrame,
    error_code: u64,
) {
    panic!("EXCEPTION: SECURITY EXCEPTION (error={:#x})\n{:#?}", error_code, frame);
}

// ── Hardware IRQ Handlers ──────────────────────────────────────────────────

pub extern "x86-interrupt" fn timer_interrupt_handler(_frame: InterruptStackFrame) {
    // Increment tick counter
    crate::process::scheduler::tick();
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::Timer as u8) };
}

pub extern "x86-interrupt" fn keyboard_interrupt_handler(_frame: InterruptStackFrame) {
    crate::drivers::keyboard::handle_interrupt();
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::Keyboard as u8) };
}

pub extern "x86-interrupt" fn primary_ata_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::PrimaryAta as u8) };
}

pub extern "x86-interrupt" fn secondary_ata_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::SecondaryAta as u8) };
}

/// Initialize the PIC and remap IRQs above CPU exceptions.
pub fn init_pics() {
    unsafe { PICS.lock().initialize() };
}
