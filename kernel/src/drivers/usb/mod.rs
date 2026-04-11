//! USB subsystem stub.
//!
//! Future work: implement xHCI host controller driver.

pub mod xhci;
pub use xhci::XhciController;
