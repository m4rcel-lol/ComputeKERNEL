#![no_std]

use core::panic::PanicInfo;

const CK_RUST_STRLEN_MAX: usize = 4096;

#[no_mangle]
pub extern "C" fn ck_rust_strlen(ptr: *const u8) -> usize {
    if ptr.is_null() {
        return 0;
    }

    let mut len = 0usize;
    // SAFETY: The caller provides a valid pointer to readable memory containing
    // a NUL-terminated C string. The caller also guarantees that reading up to
    // CK_RUST_STRLEN_MAX bytes from `ptr` stays within valid addressable memory.
    // We enforce this hard upper bound to avoid unbounded scanning if malformed.
    unsafe {
        while len < CK_RUST_STRLEN_MAX && *ptr.add(len) != 0 {
            len += 1;
        }
    }
    len
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
