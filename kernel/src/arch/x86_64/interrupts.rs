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

impl InterruptIndex {
    #[inline]
    fn as_u8(self) -> u8 {
        self as u8
    }
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
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::Timer.as_u8()) };
}

pub extern "x86-interrupt" fn keyboard_interrupt_handler(_frame: InterruptStackFrame) {
    crate::drivers::keyboard::handle_interrupt();
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::Keyboard.as_u8()) };
}

pub extern "x86-interrupt" fn primary_ata_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::PrimaryAta.as_u8()) };
}

pub extern "x86-interrupt" fn secondary_ata_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::SecondaryAta.as_u8()) };
}

pub extern "x86-interrupt" fn cascade_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Cascade.as_u8()) };
}

pub extern "x86-interrupt" fn com2_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Com2.as_u8()) };
}

pub extern "x86-interrupt" fn com1_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Com1.as_u8()) };
}

pub extern "x86-interrupt" fn lpt2_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Lpt2.as_u8()) };
}

pub extern "x86-interrupt" fn floppy_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_FloppyDisk.as_u8()) };
}

pub extern "x86-interrupt" fn lpt1_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Lpt1.as_u8()) };
}

pub extern "x86-interrupt" fn rtc_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_RtcTimer.as_u8()) };
}

pub extern "x86-interrupt" fn legacy1_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Legacy1.as_u8()) };
}

pub extern "x86-interrupt" fn legacy2_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Legacy2.as_u8()) };
}

pub extern "x86-interrupt" fn legacy3_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Legacy3.as_u8()) };
}

pub extern "x86-interrupt" fn mouse_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_Ps2Mouse.as_u8()) };
}

pub extern "x86-interrupt" fn fpu_interrupt_handler(_frame: InterruptStackFrame) {
    unsafe { PICS.lock().notify_end_of_interrupt(InterruptIndex::_FpuCoprocessor.as_u8()) };
}

/// Initialize the PIC and remap IRQs above CPU exceptions.
pub fn init_pics() {
    unsafe {
        let mut pics = PICS.lock();
        pics.initialize();

        // Explicitly unmask timer, keyboard, and cascade on PIC1 so shell input
        // and periodic timer interrupts work reliably on different firmware.
        //
        // PIC1 IRQ bits:
        // 0 = timer, 1 = keyboard, 2 = cascade to PIC2.
        let primary_mask: u8 = 0xFF & !((1 << 0) | (1 << 1) | (1 << 2));
        // Keep all PIC2 IRQs masked until dedicated drivers are enabled.
        let secondary_mask: u8 = 0xFF;
        pics.write_masks(primary_mask, secondary_mask);
    };
}
