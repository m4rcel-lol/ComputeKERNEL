#![no_std]

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn ck_rust_strlen(ptr: *const u8) -> usize {
    if ptr.is_null() {
        return 0;
    }

    let mut len = 0usize;
    // SAFETY: The caller provides a valid NUL-terminated C string pointer.
    unsafe {
        while *ptr.add(len) != 0 {
            len += 1;
        }
    }
    len
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
