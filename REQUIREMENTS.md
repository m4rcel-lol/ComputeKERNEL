# ComputeKERNEL — Minimum Requirements

## Hardware (bare metal)

| Component | Minimum |
|-----------|---------|
| **Architecture** | x86\_64 (AMD64) — 64-bit long mode required |
| **CPU features** | PAE, NX/XD, SSE2 (hardware must expose; kernel disables SSE in kernel mode) |
| **RAM** | 256 MiB |
| **Storage** | CD/DVD or USB drive capable of booting El Torito ISO, or a GPT disk with a GRUB-compatible partition |
| **Firmware** | Legacy BIOS **or** UEFI with CSM (Compatibility Support Module) enabled — pure UEFI without CSM is not yet supported |
| **Display** | VGA-compatible text-mode adapter (the kernel writes directly to the frame buffer at physical address `0xB8000`); a graphical display is not required |
| **Serial** | Optional — kernel also outputs to COM1 (115 200 8N1) if present |

> **Note on UEFI / Secure Boot:** ComputeKERNEL is loaded by GRUB 2 via Multiboot2.
> Pure UEFI without a CSM, and Secure Boot, are not supported in the current release.
> Disable Secure Boot and enable CSM in your firmware settings before booting.

---

## GRUB version

GRUB 2 (any version that ships with Debian 10+, Ubuntu 20.04+, Fedora 34+, or Arch Linux).  
GRUB Legacy (0.9x) is **not** supported.

---

## VirtualBox

| Setting | Required value |
|---------|---------------|
| **Guest type** | `Other / Other (64-bit)` or `Linux / Other Linux (64-bit)` |
| **Base memory** | ≥ 256 MiB (512 MiB recommended) |
| **CPU** | ≥ 1 vCPU; enable **PAE/NX** in `Settings → System → Processor` |
| **Chipset** | `PIIX3` (default) |
| **EFI** | **Disabled** — uncheck `Enable EFI (special OSes only)` in `Settings → System → Motherboard` |
| **Video memory** | 16 MiB or more; **Graphics controller: VBoxVGA** (not VMSVGA or VBoxSVGA) |
| **Acceleration** | Hardware virtualisation (VT-x/AMD-V) should be enabled |
| **Boot order** | Optical first (to boot from ISO), or Hard Disk first (after writing to a VDI) |

### Common VirtualBox errors and fixes

| Error | Cause | Fix |
|-------|-------|-----|
| `VINF_EM_TRIPLE_FAULT` / `VERR_PAGE_TABLE_NOT_PRESENT` | Guest triple-faulted because paging was enabled before page tables were fully initialised, or the stack grew into the page-table region | Fixed in kernel ≥ v1.0.1: BSS reordered so stacks precede page tables; kernel stack grown to 64 KiB |
| `no suitable video mode found` + `no console will be available to OS` (GRUB) | GRUB defaulted to the graphical terminal (`gfxterm`) but the host video adapter did not expose a compatible VESA/GOP mode | Fixed in kernel ≥ v1.0.1: `grub.cfg` now explicitly sets `terminal_input console` + `terminal_output console` |
| VM shuts down immediately at boot | EFI is enabled in VirtualBox but the ISO is a legacy BIOS/Multiboot2 image | Disable EFI in `Settings → System → Motherboard` |

---

## VMware Workstation / Fusion

| Setting | Required value |
|---------|---------------|
| **Guest OS** | `Other → Other 64-bit` |
| **Memory** | ≥ 256 MiB |
| **Firmware** | BIOS (not UEFI) |
| **Video** | Default (SVGA II is fine; the kernel uses text mode only) |

---

## QEMU (development / testing)

```sh
qemu-system-x86_64 \
    -cdrom out/computekernel.iso \
    -m 256M \
    -serial stdio \
    -display curses          # or -display sdl / -nographic
```

Or simply:

```sh
make run    # uses scripts/run-qemu.sh
make kvm    # uses KVM acceleration (Linux host only)
```

QEMU ≥ 5.0 is recommended.  The kernel has been tested with QEMU 6.x and 7.x.

---

## Build host (to compile the kernel yourself)

| Tool | Minimum version |
|------|----------------|
| GCC | 10 (must support `-m64 -ffreestanding`) |
| GNU Binutils (`ld`, `as`) | 2.34 |
| GNU Make | 4.0 |
| Python 3 | 3.8 (for `tools/`) |
| `grub-mkrescue` + `xorriso` | any version in Debian/Ubuntu repos | (for `make iso`) |
