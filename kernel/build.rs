//! Build script for the ComputeKERNEL kernel crate.
//!
//! Instructs Cargo to re-link if the linker script changes, and to pass the
//! correct linker flags for the bare-metal x86_64 target.

use std::env;
use std::path::PathBuf;

fn main() {
    // Locate the linker script relative to the crate root.
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let linker_script = PathBuf::from(&manifest_dir).join("linker.ld");

    // Re-run this script if the linker script changes.
    println!("cargo:rerun-if-changed={}", linker_script.display());
    println!("cargo:rerun-if-changed=build.rs");

    // Pass the linker script to the linker.
    println!("cargo:rustc-link-arg=-T{}", linker_script.display());

    // Disable position-independent executable generation.
    println!("cargo:rustc-link-arg=-no-pie");

    // Ensure the linker uses the LLD linker (already set in target JSON, but
    // explicit here for safety).
    println!("cargo:rustc-link-arg=--no-relax");
}
