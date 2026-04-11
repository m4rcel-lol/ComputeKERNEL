//! Network driver subsystem.
//!
//! Currently provides an Intel e1000 driver stub.

pub mod e1000;

pub use e1000::E1000Driver;
