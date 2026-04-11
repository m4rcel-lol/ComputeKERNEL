//! Global Descriptor Table (GDT) setup for x86_64.
//!
//! The GDT defines memory segments for the kernel and user space, and
//! registers the Task State Segment (TSS) which provides interrupt stacks.

use x86_64::{
    instructions::{segmentation::{CS, DS, Segment}, tables::load_tss},
    structures::{
        gdt::{Descriptor, GlobalDescriptorTable, SegmentSelector},
        tss::TaskStateSegment,
    },
    VirtAddr,
};

/// IST index used for the double-fault handler stack.
pub const DOUBLE_FAULT_IST_INDEX: u16 = 0;
/// IST index used for the general-protection-fault handler stack.
pub const GPF_IST_INDEX: u16 = 1;

/// Size of each IST stack (20 KiB).
const IST_STACK_SIZE: usize = 4096 * 5;

/// Dedicated stack for double-fault handler.
static mut DOUBLE_FAULT_STACK: [u8; IST_STACK_SIZE] = [0u8; IST_STACK_SIZE];
/// Dedicated stack for general-protection-fault handler.
static mut GPF_STACK: [u8; IST_STACK_SIZE] = [0u8; IST_STACK_SIZE];

/// Segment selectors created when loading the GDT.
pub struct Selectors {
    pub kernel_code: SegmentSelector,
    pub kernel_data: SegmentSelector,
    pub user_code: SegmentSelector,
    pub user_data: SegmentSelector,
    pub tss: SegmentSelector,
}

struct GdtHolder {
    gdt: GlobalDescriptorTable<8>,
    selectors: Selectors,
}

static TSS: spin::Once<TaskStateSegment> = spin::Once::new();
static GDT: spin::Once<GdtHolder> = spin::Once::new();

/// Initialize and load the GDT and TSS.
pub fn init() {
    // Build the TSS, pointing IST entries at dedicated stacks.
    let tss = TSS.call_once(|| {
        let mut tss = TaskStateSegment::new();

        tss.interrupt_stack_table[DOUBLE_FAULT_IST_INDEX as usize] = {
            let stack_start = VirtAddr::from_ptr(&raw const DOUBLE_FAULT_STACK);
            stack_start + IST_STACK_SIZE as u64
        };

        tss.interrupt_stack_table[GPF_IST_INDEX as usize] = {
            let stack_start = VirtAddr::from_ptr(&raw const GPF_STACK);
            stack_start + IST_STACK_SIZE as u64
        };

        tss
    });

    // Build the GDT with kernel/user segments and a TSS descriptor.
    let holder = GDT.call_once(|| {
        let mut gdt: GlobalDescriptorTable<8> = GlobalDescriptorTable::new();

        let kernel_code = gdt.append(Descriptor::kernel_code_segment());
        let kernel_data = gdt.append(Descriptor::kernel_data_segment());
        let user_data = gdt.append(Descriptor::user_data_segment());
        let user_code = gdt.append(Descriptor::user_code_segment());
        let tss_sel = gdt.append(Descriptor::tss_segment(tss));

        GdtHolder {
            gdt,
            selectors: Selectors {
                kernel_code,
                kernel_data,
                user_code,
                user_data,
                tss: tss_sel,
            },
        }
    });

    holder.gdt.load();

    unsafe {
        CS::set_reg(holder.selectors.kernel_code);
        DS::set_reg(holder.selectors.kernel_data);
        load_tss(holder.selectors.tss);
    }
}

/// Return the kernel code segment selector (for use in IDT entries).
pub fn kernel_code_selector() -> SegmentSelector {
    GDT.get()
        .expect("GDT not initialized")
        .selectors
        .kernel_code
}
