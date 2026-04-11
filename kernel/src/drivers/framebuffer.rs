//! UEFI framebuffer driver.
//!
//! Provides pixel-level rendering over the bootloader-supplied framebuffer.
//! When the bootloader does not provide a framebuffer (e.g., headless), all
//! operations are silently skipped.

use bootloader_api::info::{FrameBuffer, FrameBufferInfo, PixelFormat};
use spin::Mutex;

static FB: Mutex<Option<FramebufferDriver>> = Mutex::new(None);

struct FramebufferDriver {
    buffer: &'static mut [u8],
    info: FrameBufferInfo,
}

impl FramebufferDriver {
    fn new(fb: &'static mut FrameBuffer) -> Self {
        let info = fb.info();
        FramebufferDriver {
            buffer: fb.buffer_mut(),
            info,
        }
    }

    /// Draw a filled rectangle.
    fn fill_rect(&mut self, x: usize, y: usize, w: usize, h: usize, r: u8, g: u8, b: u8) {
        for row in y..(y + h).min(self.info.height) {
            for col in x..(x + w).min(self.info.width) {
                self.put_pixel(col, row, r, g, b);
            }
        }
    }

    /// Write a single pixel.
    fn put_pixel(&mut self, x: usize, y: usize, r: u8, g: u8, b: u8) {
        let offset = y * self.info.stride + x;
        let bpp = self.info.bytes_per_pixel;
        let base = offset * bpp;
        if base + bpp > self.buffer.len() {
            return;
        }
        match self.info.pixel_format {
            PixelFormat::Rgb => {
                self.buffer[base] = r;
                self.buffer[base + 1] = g;
                self.buffer[base + 2] = b;
            }
            PixelFormat::Bgr => {
                self.buffer[base] = b;
                self.buffer[base + 1] = g;
                self.buffer[base + 2] = r;
            }
            PixelFormat::U8 => {
                // Grayscale approximation
                self.buffer[base] = ((r as u16 + g as u16 + b as u16) / 3) as u8;
            }
            _ => {}
        }
    }

    /// Clear the framebuffer to black.
    fn clear(&mut self) {
        for byte in self.buffer.iter_mut() {
            *byte = 0;
        }
    }
}

/// Initialize the framebuffer driver with the bootloader-supplied framebuffer.
pub fn init(fb: &'static mut FrameBuffer) {
    *FB.lock() = Some(FramebufferDriver::new(fb));
    clear();
}

/// Clear the framebuffer to black.
pub fn clear() {
    if let Some(ref mut drv) = *FB.lock() {
        drv.clear();
    }
}

/// Draw a filled rectangle.
pub fn fill_rect(x: usize, y: usize, w: usize, h: usize, r: u8, g: u8, b: u8) {
    if let Some(ref mut drv) = *FB.lock() {
        drv.fill_rect(x, y, w, h, r, g, b);
    }
}

/// Return framebuffer dimensions, or `None` if unavailable.
pub fn dimensions() -> Option<(usize, usize)> {
    FB.lock()
        .as_ref()
        .map(|d| (d.info.width, d.info.height))
}
