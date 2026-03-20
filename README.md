# ComputeKERNEL Master Architecture + Implementation Specification

> **Status update (March 2026):** this document still contains long-form architecture/spec planning content.  
> For day-to-day development, treat the current repository code and scripts as authoritative.
>
> Quick current commands:
> - `make kernel` — build `out/computekernel.elf`
> - `make iso` — build bootable ISO via `scripts/build-iso.sh`
> - `make qemu` / `make kvm` — run in VM (QEMU user networking forwards `localhost:2222` to guest `:22`)
> - `make lint` — shell/python syntax checks
> - `make test` — scaffold checks (+ rust check when `rust/Cargo.toml` exists)
>
> Current shell highlights (`cksh`): `setup`, `setup-guide`, `setup-alpine` (alias), `arch-install` (deprecated, use `setup`),
> `kblayout`/`layout`, `netinfo`, `mouse`, and `credits`.
>
> Networking note: main ISO does **not** yet ship a full in-kernel TCP/IP stack or SSH daemon; the `localhost:2222` to guest `:22` forward is host-side VM plumbing.

## Section 1 — Executive technical definition

### What ComputeKERNEL is
ComputeKERNEL is a **from-scratch UNIX-like kernel and early OS core** targeting `x86_64` first, designed as a **monolithic kernel with loadable module support**, built for real boot on commodity hardware and major VMs.

### What ComputeKERNEL is not
- not a Linux/BSD/XNU/Minix/Redox fork
- not a distro or package set
- not just a bootloader demo
- not a tutorial toy with fake subsystems

### Why it exists
ComputeKERNEL exists to provide a modern, small, debuggable kernel base where architecture and subsystem boundaries are explicit, auditable, and buildable by a small engineering team.

### UNIX-like definition in this project
UNIX-like means:
- process/thread model with PID and per-process virtual address spaces
- file descriptor based I/O
- hierarchical VFS namespace with mount points and device nodes
- POSIX-inspired syscall behavior (subset initially)
- user/kernel privilege split and permission checks (UID/GID model)

### Linux-inspired, intentionally different
**Inspired by Linux spirit**:
- pragmatic monolithic design
- proven abstractions (VFS, block layer, driver model)
- fast syscall path and strong tooling

**Intentionally different**:
- smaller baseline surface area and stricter feature gates
- tighter Rust FFI boundary from day one
- deterministic boot/init sequencing and explicit memory budget rules
- “MVP first” subsystem contracts with compile-time enforcement

### Originality
Originality is delivered through:
- original codebase and API naming
- explicit split of C hot paths and Rust safety-oriented auxiliary modules
- image/tooling pipeline built for reproducible boot artifacts
- staged hardware matrix and stabilization-first roadmap

### Lightweight (technical interpretation)
- small default kernel text/data footprint
- low baseline resident memory after boot (target: < 64 MB kernel+core services on x86_64 MVP)
- optional components disabled by default via config profiles
- no heavyweight tracing/debug layers in release builds

### Stable (technical interpretation)
- fail-fast for kernel invariants (`BUG_ON`/panic path)
- defensive copy/validation on syscall boundary
- lock discipline + static/CI checks
- reproducible init order and deterministic fallback boot modes

### Real-hardware bootable (validation interpretation)
“Real hardware bootable” means boot succeeds from USB ISO on at least two physical x86_64 platforms (different vendors), with serial/framebuffer console, timer interrupts, keyboard input, block device discovery, and init process launch.

### Design values
- determinism over cleverness
- explicit contracts over implicit behavior
- observability before optimization
- conservative defaults, optional expansion

### Engineering values
- small, reviewable patches
- measurable milestones with done criteria
- zero undefined ownership of subsystems
- tool-assisted validation in CI and local loops

### Goals
- **Performance:** predictable scheduler latency, O(log n) or O(1) hot paths where possible
- **Stability:** panic-free common boot path on supported targets
- **Size:** bootable ISO around 500 MB–1 GB (debug symbols, test userland, docs, recovery tools included)
- **Bring-up:** QEMU/KVM first, then VirtualBox/VMware, then physical hardware

### Non-goals
- full POSIX compliance in MVP
- advanced SMP NUMA tuning in phase 1
- full desktop stack in kernel scope

---

## Section 2 — Kernel architecture

### Core decision
**Monolithic kernel with modular capabilities**.

Justification:
- lower IPC overhead for early performance/stability
- simpler bring-up on x86_64
- module support preserves extensibility

### Privilege/ring model
- Ring 0: kernel core + loaded modules
- Ring 3: userspace tasks
- `syscall/sysret` for fast transitions (x86_64)

### Syscall entry model
- MSR-configured `LSTAR` entry
- per-CPU kernel stack switch via TSS
- C dispatcher after assembly prologue register save

### Task/process/thread model
- process owns address space, fd table, credentials
- thread owns CPU context, scheduling state, kernel stack
- kernel threads have no userspace mm

### Userspace boundary
- strict copy helpers (`copy_from_user`, `copy_to_user`)
- address range and access permission checks

### Memory map (x86_64 high-half)
- `0x0000_0000_0000_0000`–`0x0000_7fff_ffff_ffff`: user
- `0xffff_8000_0000_0000`–...: kernel direct map
- higher region slices for text/rodata/data, vmalloc, per-cpu, fixmap

### Subsystem map
- `arch`: CPU/interrupt/syscall/paging arch specifics
- `kernel`: init, scheduler, task core, panic/logging
- `mm`: PMM/VMM/slab/kmalloc
- `fs`: VFS + filesystem drivers
- `drivers`: char/block/bus/device core
- `net`: sockets/net core (roadmap)
- `ipc`: signals, pipes, unix-like IPC

### Init stages
1. boot handoff
2. arch early init
3. memory bring-up
4. interrupt/timer setup
5. scheduler start
6. VFS/device init
7. userspace `init`

### Module strategy
- ELF relocatable kernel modules (`.kmod`)
- signed-module policy optional in hardened profile

### Configuration strategy
- Kconfig-like static profiles in `configs/*.mk` and generated headers
- build profiles: `debug`, `release`, `safe-mode`

### Locking/concurrency
- spinlocks for IRQ-sensitive short critical sections
- mutexes for sleepable paths
- rwlocks for read-heavy shared structures
- lock ordering documented in `docs/STABILITY.md`

### Fault handling philosophy
- recover when user fault; isolate and signal task
- panic on kernel integrity violation
- record fault context and symbolized backtrace

### Text diagram
```text
+--------------------------- Userspace (Ring 3) ----------------------------+
| init, shell, tests, user tools                                            |
+--------------------- syscall boundary (syscall/sysret) -------------------+
|                    Kernel Core (Ring 0, monolithic)                       |
|  scheduler | task mgmt | mm | vfs | block/char | net | ipc | drivers      |
|                    ^                ^                ^                    |
|                  arch x86_64 (idt/gdt/apic/timer/paging/syscall)         |
+--------------------------- boot + firmware handoff -----------------------+
```

---

## Section 3 — Boot chain and startup path

### Boot design decisions
- Firmware: support **UEFI first**, BIOS compatibility via GRUB2 multiboot path
- Bootloader: GRUB2 for MVP (faster reproducible bring-up)
- Handoff contract: Multiboot2 info + memory map + framebuffer/serial params

### Startup path details
- assembly entry in `boot/x86_64/entry64.S`
- establish temporary stack
- validate long mode and required CPU features
- setup early paging and high-half jump
- initialize GDT/TSS and IDT stub table
- parse memory map from bootloader handoff
- switch into `kmain()` in C
- bring up early console (serial first, framebuffer optional)
- initialize PMM->VMM->allocator->interrupts->timer->scheduler
- spawn `kthreadd`, mount rootfs, exec `/sbin/init`

### Numbered boot flow: power-on to `init`
1. CPU reset vector executes firmware.
2. Firmware loads boot medium and starts GRUB2.
3. GRUB2 loads kernel ELF + initrd + multiboot2 info.
4. Control enters `entry64.S` in long mode (or transitions if needed).
5. Early stack and minimal page tables become active.
6. CPU features checked (SSE2, NX, SYSENTER/SYSCALL, APIC as required).
7. GDT/TSS initialized, IDT with early exception handlers installed.
8. Multiboot memory map copied to safe kernel buffer.
9. `kmain(boot_info*)` starts.
10. Early serial logger online.
11. PMM initialized from usable regions.
12. Kernel VMM + direct map + heap/slab initialized.
13. Timer/APIC/PIT init and interrupt enable.
14. Scheduler initialized; idle thread + init kernel thread created.
15. VFS initialized and rootfs mounted (initrd/ext2).
16. Device manager probes mandatory devices.
17. ELF loader loads first userspace binary `/sbin/init`.
18. Context switch to userspace init process.

---

## Section 4 — Major subsystems with implementation strategy

Below format: **Purpose | Responsibilities | Structures/APIs | Files | Dependencies | MVP | Expansion | Lightweight | Stability**

1. **arch layer**
- CPU/arch specifics; paging, irq gates, syscall entry.
- `struct cpu_local`, `arch_init()`, `arch_switch_to()`.
- Files: `arch/x86_64/cpu.c`, `arch/x86_64/idt.c`, `arch/x86_64/syscall_entry.S`.
- Depends on boot info + kernel core.
- MVP: single-core robust path.
- Later: SMP AP startup.
- Lightweight: avoid generic abstraction overhead in hot paths.
- Stability: assert feature bits before enabling features.

2. **boot**
- image entry + early setup.
- APIs: `boot_parse_multiboot2()`, `boot_get_memmap()`.
- Files: `boot/x86_64/entry64.S`, `boot/common/bootinfo.c`.
- MVP: GRUB2 handoff parse.
- Later: native UEFI stub.

3. **interrupts/exceptions**
- IDT registration, exception decode, IRQ dispatch.
- `irq_register()`, `isr_common_handler()`.
- Files: `kernel/irq.c`, `arch/x86_64/idt.S`.
- MVP: CPU exceptions + timer + keyboard IRQ.
- Later: MSI/MSI-X.

4. **syscall layer**
- ABI entry/dispatch/validation.
- `sys_dispatch()`, `sys_read()`, `sys_write()`.
- Files: `kernel/syscall.c`, `include/ck/syscall.h`.

5. **process/task subsystem**
- PID, lifecycle, credentials/mm linkage.
- `struct process`, `task_create_user()`.
- Files: `kernel/task.c`, `kernel/process.c`.

6. **scheduler**
- preemptive round-robin MVP.
- runqueue + timeslice accounting.
- Files: `kernel/sched/core.c`, `kernel/sched/runqueue.c`.

7. **physical memory manager**
- page-frame ownership.
- bitmap/buddy allocator.
- Files: `mm/pmm.c`, `mm/buddy.c`.

8. **virtual memory manager**
- page tables, mapping APIs, faults.
- `vmm_map()`, `vmm_unmap()`, `handle_page_fault()`.
- Files: `mm/vmm.c`, `arch/x86_64/pagetable.c`.

9. **kernel allocator**
- `kmalloc/kfree`, slab caches.
- Files: `mm/slab.c`, `mm/kmalloc.c`.

10. **VFS**
- inode/dentry/file abstraction and path walk.
- Files: `fs/vfs_inode.c`, `fs/vfs_path.c`, `fs/vfs_file.c`.

11. **filesystem layer**
- ext2 + tmpfs MVP.
- Files: `fs/ext2/*.c`, `fs/tmpfs/*.c`.

12. **block layer**
- block request queue abstraction.
- Files: `drivers/block/block_core.c`.

13. **character device layer**
- major/minor registration, tty endpoints.
- Files: `drivers/char/chardev.c`, `drivers/tty/tty_core.c`.

14. **device manager**
- central device tree/registry.
- Files: `drivers/core/device.c`, `drivers/core/bus.c`.

15. **driver framework**
- probe/bind/remove, IRQ hooks.
- Files: `include/ck/driver.h`, `drivers/core/driver.c`.

16. **timers/timekeeping**
- tick source + monotonic clock.
- Files: `kernel/time/timer.c`, `arch/x86_64/apic_timer.c`.

17. **IPC/signals**
- pipes, signals delivery MVP.
- Files: `ipc/signal.c`, `ipc/pipe.c`.

18. **executable loader**
- ELF64 loader.
- Files: `kernel/exec/elf64.c`.

19. **TTY/console**
- serial console and virtual tty core.
- Files: `drivers/tty/serial8250.c`, `drivers/tty/vt.c`.

20. **security/credentials**
- uid/gid/cap-like minimal checks.
- Files: `kernel/cred.c`, `include/ck/cred.h`.

21. **networking core (planned)**
- current state in main ISO: boot-time Multiboot2 network tag visibility only.
- planned: packet buffers, NIC abstraction, IPv4 stack.
- Files: planned under `net/*` (not implemented yet).

22. **module loader**
- relocations, symbol lookup, dependency checks.
- Files: `kernel/module/loader.c`.

23. **logging/tracing**
- ring buffer logger + serial sink.
- Files: `kernel/printk.c`, `kernel/trace.c`.

24. **panic/crash handling**
- panic path, stack unwind, crash dump hooks.
- Files: `kernel/panic.c`.

25. **power basics**
- halt/reboot/acpi poweroff baseline.
- Files: `kernel/power.c`, `drivers/acpi/acpi_core.c`.

26. **testing hooks**
- kernel selftests + syscall tests.
- Files: `tests/kselftest/*.c`, `tests/user/*.c`.

---

## Section 5 — Repository tree

### Directory purposes and interfaces
- `arch/`: architecture-specific code; exposes arch APIs consumed by `kernel/` and `mm/`.
- `boot/`: boot protocol, entry stubs, linker scripts; hands off to `kernel/init`.
- `kernel/`: core init/sched/syscall/task/panic/logging.
- `mm/`: allocators and VMM; used by nearly all subsystems.
- `fs/`: VFS + concrete filesystems; consumes block/char interfaces.
- `drivers/`: hardware drivers + device model.
- `net/`: reserved for future protocol stack and socket layer (not implemented yet).
- `ipc/`: signals/pipes/queues.
- `lib/`: freestanding libc-like helpers.
- `include/`: kernel headers and subsystem contracts.
- `rust/`: `no_std` Rust crate(s) for safety-targeted components.
- `tools/`: Python tools for generation/analysis.
- `scripts/`: shell wrappers for build/run/debug.
- `configs/`: build profiles and feature sets.
- `docs/`: architecture and subsystem docs.
- `tests/`: kernel/user test suites.
- `build/`: generated intermediates.
- `out/`: final artifacts (`kernel.elf`, ISO, symbols, reports).

### Representative tree
```text
ComputeKERNEL/
├── Makefile
├── README.md
├── arch/
│   ├── x86_64/
│   │   ├── cpu.c
│   │   ├── gdt.c
│   │   ├── idt.c
│   │   ├── idt.S
│   │   ├── syscall_entry.S
│   │   ├── switch.S
│   │   ├── pagetable.c
│   │   └── linker.ld
│   └── aarch64/
│       └── README.port.md
├── boot/
│   ├── x86_64/entry64.S
│   ├── grub/grub.cfg
│   └── common/bootinfo.c
├── kernel/
│   ├── init/main.c
│   ├── printk.c
│   ├── panic.c
│   ├── syscall.c
│   ├── task.c
│   ├── process.c
│   ├── cred.c
│   ├── sched/core.c
│   ├── sched/runqueue.c
│   ├── time/timer.c
│   ├── exec/elf64.c
│   └── module/loader.c
├── mm/
│   ├── pmm.c
│   ├── buddy.c
│   ├── vmm.c
│   ├── slab.c
│   └── kmalloc.c
├── fs/
│   ├── vfs_inode.c
│   ├── vfs_dentry.c
│   ├── vfs_path.c
│   ├── vfs_file.c
│   ├── ext2/super.c
│   └── tmpfs/tmpfs.c
├── drivers/
│   ├── core/device.c
│   ├── core/driver.c
│   ├── core/bus.c
│   ├── char/chardev.c
│   ├── tty/serial8250.c
│   ├── tty/vt.c
│   ├── block/ata_pio.c
│   ├── block/virtio_blk.c
│   ├── input/ps2kbd.c
│   └── timer/pit.c
├── ipc/
│   ├── signal.c
│   └── pipe.c
├── lib/
│   ├── string.c
│   ├── bitmap.c
│   └── printf.c
├── include/ck/
│   ├── kernel.h
│   ├── types.h
│   ├── errno.h
│   ├── syscall.h
│   ├── task.h
│   ├── mm.h
│   ├── vfs.h
│   ├── device.h
│   └── spinlock.h
├── rust/
│   ├── Cargo.toml
│   ├── rust-toolchain.toml
│   └── src/lib.rs
├── tools/
│   ├── gen_syscalls.py
│   ├── gen_config.py
│   ├── symbol_map.py
│   ├── mkfs_image.py
│   └── test_harness.py
├── scripts/
│   ├── check-env.sh
│   ├── build-iso.sh
│   ├── run-qemu.sh
│   ├── run-kvm.sh
│   ├── run-vbox.sh
│   ├── run-vmware.sh
│   └── debug-gdb.sh
├── configs/
│   ├── x86_64_debug.mk
│   ├── x86_64_release.mk
│   └── x86_64_safe.mk
├── docs/
│   ├── ARCHITECTURE.md
│   ├── BOOTFLOW.md
│   ├── MM.md
│   ├── SCHEDULER.md
│   ├── SYSCALLS.md
│   ├── VFS.md
│   ├── DRIVERS.md
│   ├── BUILDING.md
│   ├── DEBUGGING.md
│   ├── HARDWARE.md
│   ├── STABILITY.md
│   ├── LIGHTWEIGHT.md
│   ├── CONTRIBUTING.md
│   ├── SECURITY.md
│   └── ROADMAP.md
├── tests/
│   ├── kselftest/test_mm.c
│   ├── kselftest/test_sched.c
│   └── user/test_syscalls.c
├── build/
└── out/
```

---

## Section 6 — Build system

### Makefile architecture
- top-level `Makefile` orchestrates config, compile, link, iso, run, test, lint.
- per-subdir `*.mk` provides source lists and local flags.
- build outputs in `build/`, final artifacts in `out/`.

### Toolchain and flags
- `CC=x86_64-elf-gcc`, `LD=x86_64-elf-ld`, `AS`, `AR`, `OBJCOPY`, `NM`
- common flags: `-ffreestanding -fno-omit-frame-pointer -fno-stack-protector -nostdlib`
- debug: `-O0 -g3 -DCK_DEBUG`
- release: `-O2 -DNDEBUG`

### Cross compile and arch selection
- `make ARCH=x86_64 PROFILE=debug`
- future: `ARCH=aarch64` uses dedicated linker/entry files.

### Rust integration
- build `rust/` as staticlib: `cargo build -Zbuild-std=core --target x86_64-unknown-none`
- link archive into kernel ELF

### Generated content
- `tools/gen_syscalls.py` -> `include/generated/syscall_nr.h`, `kernel/generated/syscall_table.c`
- `tools/gen_config.py` -> `include/generated/config.h`

### Core targets
- `make all`
- `make kernel`
- `make iso`
- `make run`
- `make qemu`
- `make vmware`
- `make vbox`
- `make gdb`
- `make test`
- `make lint`
- `make rust-check`
- `make size-report`

### ISO size control (500 MB–1 GB)
- include debug symbol pack and diagnostics bundle in debug ISO
- include test rootfs payload and fallback binaries
- optional compression profile flags
- hard size checks in `size-report`: warn <500 MB or >1 GB for release-candidate test media target

---

## Section 7 — Real boot and hardware validation strategy

### VM targets
- **QEMU**: reference platform, serial-first logging, `-d int` optional debug
- **KVM**: acceleration validation with same disk/iso artifacts
- **VirtualBox**: BIOS/UEFI mode matrix
- **VMware**: EFI boot path and storage controller compatibility

### Real hardware target
- USB bootable ISO written by `dd`/Rufus/Etcher
- require serial-capable board for first-tier validation when possible

### Boot medium and logging
- boot from ISO on optical/virtual optical and from USB image variant
- serial (`COM1`) always enabled; framebuffer console fallback if no serial

### Storage/media assumptions
- initial support: ATAPI/ATA PIO + virtio-blk
- fallback read-only initrd rootfs if no writable block device

### Failure debug strategy
1. force `safe-mode` profile (single CPU, minimal drivers)
2. serial-only log mode
3. panic dump + symbolized stack via `tools/symbol_map.py`
4. binary search disable of driver probes via boot args

### Hardware support staging
- Stage A: QEMU/KVM virtio baseline
- Stage B: VirtualBox/VMware controller variants
- Stage C: two physical platforms

### Compatibility matrix concept
Document CPU, firmware mode, chipset/storage/NIC, result, regression status in `docs/HARDWARE.md`.

### Minimum CPU features
- x86_64 long mode
- NX bit
- APIC
- `syscall/sysret`
- TSC (invariant preferred)

### Safe mode boot
`ck.safe=1` disables modules and non-essential drivers; networking stack is not available in main ISO yet.

---

## Section 8 — Process, thread, and scheduler implementation

```c
/* include/ck/task.h */
enum task_state {
    TASK_NEW,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_SLEEP_INT,
    TASK_SLEEP_UNINT,
    TASK_ZOMBIE,
    TASK_DEAD,
};

struct cpu_context {
    uint64_t r15, r14, r13, r12, rbx, rbp, rip, rsp;
};

struct process {
    int32_t pid;
    int32_t ppid;
    struct mm_space *mm;
    struct cred cred;
    struct fd_table *fds;
    char name[32];
};

struct thread {
    int32_t tid;
    enum task_state state;
    struct process *proc;
    struct cpu_context ctx;
    void *kstack_base;
    uint64_t vruntime;
    uint32_t timeslice_ticks;
    struct wait_queue *wq;
};

struct runqueue {
    struct thread *current;
    struct list_head runnable;
    uint32_t nr_running;
    spinlock_t lock;
};

void sched_init(void);
void sched_tick(void);
void sched_enqueue(struct thread *t);
void sched_block_current(enum task_state state);
void sched_wake(struct thread *t);
void context_switch(struct thread *prev, struct thread *next);
```

- Preemption: timer tick decrements `timeslice_ticks`; 0 triggers reschedule.
- Wait queues: intrusive linked lists with lock-protected sleep/wake transitions.
- SMP roadmap: per-CPU runqueues + work stealing phase.

---

## Section 9 — Memory management implementation

```c
/* include/ck/mm.h */
#define PAGE_SIZE 4096

enum zone_type { ZONE_DMA, ZONE_NORMAL };

struct page {
    uint32_t flags;
    uint16_t order;
    uint16_t refcnt;
    struct list_head node;
};

struct pmm_state {
    struct page *page_array;
    uint64_t total_pages;
    spinlock_t lock;
};

void pmm_init(const struct boot_memmap *map);
struct page *alloc_pages(uint32_t order, enum zone_type zone);
void free_pages(struct page *pg, uint32_t order);

void *kmalloc(size_t sz, uint32_t gfp);
void kfree(void *ptr);

int vmm_map(struct mm_space *mm, uintptr_t va, uintptr_t pa, uint64_t flags);
int vmm_unmap(struct mm_space *mm, uintptr_t va);
int handle_page_fault(uintptr_t addr, uint64_t err);
```

- Early boot: bump allocator in reserved bootstrap region.
- PMM: buddy allocator for page frames.
- Kernel heap: slab caches for small objs + page-backed large allocs.
- VA layout: high-half kernel, user low-half with guard pages.
- OOM: invoke reclaim hooks then kill selectable user task, never silently corrupt.
- COW/mmap roadmap explicitly scheduled in phase expansion.

---

## Section 10 — Syscall layer

### ABI
- x86_64 `syscall` instruction
- args: `rdi,rsi,rdx,r10,r8,r9`; syscall nr in `rax`
- return: `rax` (`-errno` on failure)

```c
/* kernel/syscall.c */
typedef long (*sys_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

extern long sys_write(uint64_t fd, uint64_t buf, uint64_t len, uint64_t, uint64_t, uint64_t);
extern long sys_exit(uint64_t code, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static sys_fn_t sys_table[] = {
    [0] = sys_write,
    [1] = sys_exit,
};

long sys_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                  uint64_t a3, uint64_t a4, uint64_t a5) {
    if (nr >= (sizeof(sys_table)/sizeof(sys_table[0])) || !sys_table[nr])
        return -38; /* -ENOSYS */
    return sys_table[nr](a0,a1,a2,a3,a4,a5);
}
```

MVP syscalls: `read, write, openat, close, exit, fork, execve, wait4, mmap, munmap, brk, nanosleep, getpid, kill`.

Validation rules: user pointers validated; copy helpers fault-safe; size caps; fd bounds checks.
Versioning: append-only syscall number policy with generated headers.

---

## Section 11 — VFS and filesystems

```c
/* include/ck/vfs.h */
struct inode_ops;
struct file_ops;

struct inode {
    uint64_t ino;
    uint32_t mode;
    uint32_t uid, gid;
    uint64_t size;
    const struct inode_ops *iops;
    const struct file_ops *fops;
    void *fs_private;
};

struct dentry {
    char name[64];
    struct inode *inode;
    struct dentry *parent;
    struct rb_node children;
};

struct file {
    struct inode *inode;
    uint64_t pos;
    uint32_t flags;
    const struct file_ops *ops;
};

int vfs_mount(const char *dev, const char *target, const char *fstype, uint64_t flags);
int vfs_openat(int dfd, const char *path, int flags, int mode);
ssize_t vfs_read(struct file *f, void *buf, size_t len);
ssize_t vfs_write(struct file *f, const void *buf, size_t len);
```

- mount tree rooted at `/`
- device nodes under `/dev`
- `proc`-like virtual FS roadmap under `/proc`
- initial FS: `initramfs` + `ext2`; native FS later (`ckfs` planned)
- buffer cache: block-indexed cache with LRU write-back policy

---

## Section 12 — Device and driver model

### Model
- device registry keyed by bus+address+class
- drivers provide `probe/init/remove/suspend/resume`

```c
struct ck_device {
    const char *name;
    uint32_t class_id;
    void *bus_data;
    int irq;
};

struct ck_driver {
    const char *name;
    uint32_t class_id;
    int (*probe)(struct ck_device *);
    void (*remove)(struct ck_device *);
    irqreturn_t (*irq)(int, void *);
};
```

### Initial drivers
- serial: 16550/8250
- timer: PIT + APIC timer
- keyboard: PS/2
- display: framebuffer console
- storage: ATA PIO + virtio-blk
- NIC roadmap: virtio-net first, then e1000

### Rust-safe boundary
Rust drivers can only call C-exported stable FFI wrappers; direct MMIO in unsafe blocks with audited wrappers.

---

## Section 13 — Security and hardening

- privilege model: ring0/ring3 strict split
- identity model: UID/GID + supplemental groups roadmap
- permission checks in VFS/open/exec/signal
- syscall validation: strict bounds, pointer checks, copy helpers
- memory protections: NX, user/supervisor bits, readonly text
- module policy: unsigned modules disallowed in hardened profile
- W^X roadmap: prohibit RWX mappings by default
- stack canaries roadmap for kernel C
- KASLR roadmap once early boot fixed-address assumptions removed
- audit logging: security-relevant events to ring buffer
- secure boot compatibility: shim/GRUB signed chain supported later

Incremental policy: enable safeguards in debug+hardened first, then default once stable.

---

## Section 14 — Rust integration

### Why Rust
Used for memory-safety sensitive leaf modules (parsers, selected drivers, helper subsystems) without rewriting the C core.

### Allowed/forbidden
- Allowed: new optional drivers, parsers, small utility subsystems
- Forbidden: initial scheduler core, low-level context-switch, early boot/paging bring-up

### FFI and runtime
- `no_std`, `panic = abort`
- allocator via C kernel allocator shim once heap ready
- all `unsafe` blocks documented and minimized

```rust
// rust/src/lib.rs
#![no_std]

#[repr(C)]
pub struct CkLogMsg {
    pub level: u32,
    pub ptr: *const u8,
    pub len: usize,
}

unsafe extern "C" {
    fn ck_log(msg: *const CkLogMsg);
}

#[unsafe(no_mangle)]
pub extern "C" fn ck_rust_hello() {
    let txt = b"rust module online\n";
    let msg = CkLogMsg { level: 1, ptr: txt.as_ptr(), len: txt.len() };
    unsafe { ck_log(&msg as *const CkLogMsg) };
}
```

Linting: `cargo clippy -D warnings` for rust components; unsafe policy enforced in `docs/SECURITY.md`.

---

## Section 15 — Tooling

### Shell roles (`scripts/`)
- `check-env.sh`: verify cross toolchain and required binaries
- `build-iso.sh`: package kernel+initrd into ISO
- `run-qemu.sh`, `run-kvm.sh`, `run-vbox.sh`, `run-vmware.sh`
- `debug-gdb.sh`: launch gdb with symbol file and remote target
- `test.sh`: wrapper for kernel/user tests

### Python roles (`tools/`)
- `gen_syscalls.py`: generate syscall ids/table stubs
- `gen_config.py`: convert profiles to generated config header
- `symbol_map.py`: symbolize panic addresses using `nm` map
- `test_harness.py`: orchestrate VM boot tests and log checks
- `mkfs_image.py`: build rootfs/initrd images
- `scaffold_subsys.py`: create subsystem skeletons with headers/tests

Example usage:
```bash
python3 tools/gen_syscalls.py --in configs/syscalls.yml --out include/generated
python3 tools/mkfs_image.py --rootfs rootfs/ --out out/initrd.img --size-mb 512
```

---

## Section 16 — Documentation layout

- `README.md`: project master specification and quick-start commands
- `ARCHITECTURE.md`: subsystem boundaries and contracts
- `BOOTFLOW.md`: firmware-to-init boot path with diagrams
- `MM.md`: PMM/VMM allocator design and page fault handling
- `SCHEDULER.md`: runqueue, preemption, wait queues
- `SYSCALLS.md`: ABI, numbering policy, validation
- `VFS.md`: path resolution, mounts, fd layer
- `DRIVERS.md`: device model and driver lifecycle
- `BUILDING.md`: toolchain setup and build targets
- `DEBUGGING.md`: qemu+gdb and panic triage
- `HARDWARE.md`: compatibility matrix and board notes
- `STABILITY.md`: invariants, lock ordering, release gates
- `LIGHTWEIGHT.md`: size/memory budgeting and feature policy
- `CONTRIBUTING.md`: workflow, coding standards, review expectations
- `SECURITY.md`: threat model and hardening roadmap
- `ROADMAP.md`: phased plan and milestone status

---

## Section 17 — Phased implementation roadmap

1. **Toolchain/bootstrap**
- goals: cross-toolchain, Make targets, env checks
- files: `Makefile`, `scripts/check-env.sh`, `configs/*`
- tests: env script + dry build
- boot validation: N/A
- blockers: toolchain mismatch
- done: `make all` produces kernel ELF skeleton

2. **Boot entry**
- files: `boot/x86_64/entry64.S`, `arch/x86_64/linker.ld`
- test: QEMU reaches `kmain`
- done: serial prints boot banner

3. **Early console**
- files: `kernel/printk.c`, `drivers/tty/serial8250.c`
- done: stable serial output

4. **Interrupts + IDT**
- files: `arch/x86_64/idt.*`, `kernel/irq.c`
- done: timer IRQ observed

5. **Paging + allocators**
- files: `mm/*`, `arch/x86_64/pagetable.c`
- done: dynamic allocations reliable in stress test

6. **Scheduler + tasks**
- files: `kernel/task.c`, `kernel/sched/*`
- done: idle + worker threads preempt correctly

7. **Syscall layer**
- files: `kernel/syscall.c`, `arch/x86_64/syscall_entry.S`
- done: userspace `write/exit` works

8. **Userspace launch**
- files: `kernel/exec/elf64.c`, `kernel/init/main.c`
- done: `/sbin/init` executes

9. **VFS + FS**
- files: `fs/vfs_*`, `fs/ext2/*`, `fs/tmpfs/*`
- done: open/read/write on rootfs

10. **Storage**
- files: `drivers/block/*`
- done: root mount from block device

11. **Drivers**
- files: serial/timer/keyboard/framebuffer
- done: basic interactive console

12. **Networking**
- files: planned under `net/*`, `drivers/net/*`
- status: not implemented in main ISO yet (roadmap item)

13. **Modules**
- files: `kernel/module/*`
- done: load/unload demo module

14. **Hardening**
- files: syscall checks, W^X, module policy
- done: hardened profile boots and passes tests

15. **Real hardware stabilization**
- files: broad touch across arch/drivers/docs
- done: two physical machines pass boot matrix

### Advanced subsystem checklist (not yet implemented)
- [ ] TCP/IP network stack                         - In-kernel TCP/IP (lwIP or custom)
- [ ] Network device driver model                  - NIC driver framework
- [ ] SSH daemon                                   - In-kernel SSH service
- [ ] SMP (multi-core) support                     - Per-CPU scheduler, IPI, spinlocks
- [ ] ACPI power management                        - Full ACPI AML interpreter
- [ ] USB stack                                    - xHCI host controller + USB drivers
- [ ] NVMe driver                                  - PCIe NVMe block device driver
- [ ] ext4 filesystem                              - Full ext4 with journaling
- [ ] procfs / sysfs                               - Runtime kernel info filesystems
- [ ] Memory-mapped files                          - mmap(MAP_FILE) with page cache
- [ ] Copy-on-write fork                           - Real COW fork() implementation
- [ ] POSIX threads (pthreads)                     - Full pthread support
- [ ] ASLR                                         - Address space layout randomization
- [ ] KVM hypervisor support                       - Paravirtualization
- [ ] DRM/KMS graphics                             - Framebuffer + GPU driver model
- [ ] Audio subsystem                              - ALSA-style audio model
- [ ] Bluetooth stack                              - HCI/L2CAP model
- [ ] SELinux/AppArmor model                       - Mandatory access control
- [ ] eBPF subsystem                               - In-kernel safe programs
- [ ] io_uring                                     - Async I/O interface

---

## Section 18 — Starter code

### 1) Boot assembly entry (`boot/x86_64/entry64.S`)
```asm
.section .text
.global _start
.extern kmain

_start:
    cli
    lea boot_stack_top(%rip), %rsp
    xor %rbp, %rbp

    /* bootloader provides pointer in rdi (multiboot2 info) */
    call kmain
.hang:
    hlt
    jmp .hang

.section .bss
.align 16
boot_stack:
    .skip 16384
boot_stack_top:
```

### 2) Linker script (`arch/x86_64/linker.ld`)
```ld
ENTRY(_start)
SECTIONS
{
  . = 1M;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}
```

### 3) C kernel entry (`kernel/init/main.c`)
```c
#include <ck/kernel.h>

void kmain(void *boot_info) {
    ck_early_console_init();
    ck_printk("ComputeKERNEL: boot start\n");
    mm_early_init(boot_info);
    arch_init();
    sched_init();
    vfs_init();
    init_spawn_first_user("/sbin/init");
    for (;;) arch_halt();
}
```

### 4) Early printk/logger (`kernel/printk.c`)
```c
#include <stdarg.h>
#include <ck/kernel.h>

void ck_printk(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_write(buf);
}
```

### 5) IDT/GDT skeleton (`arch/x86_64/idt.c`)
```c
#include <ck/irq.h>

void idt_init(void) {
    idt_set_gate(0, isr_div0_stub, 0x8E);
    idt_set_gate(14, isr_pf_stub, 0x8E);
    idt_set_gate(32, irq0_timer_stub, 0x8E);
    idt_load();
}
```

### 6) Page allocator skeleton (`mm/pmm.c`)
```c
#include <ck/mm.h>

static struct pmm_state pmm;

void pmm_init(const struct boot_memmap *map) {
    /* initialize page metadata from usable regions */
}

struct page *alloc_pages(uint32_t order, enum zone_type zone) {
    (void)zone;
    return buddy_alloc(&pmm, order);
}
```

### 7) Scheduler skeleton (`kernel/sched/core.c`)
```c
#include <ck/task.h>

static struct runqueue rq;

void sched_init(void) {
    runqueue_init(&rq);
}

void sched_tick(void) {
    struct thread *cur = rq.current;
    if (--cur->timeslice_ticks == 0)
        schedule();
}
```

### 8) Syscall table skeleton (`kernel/syscall.c`)
```c
long sys_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                  uint64_t a3, uint64_t a4, uint64_t a5);
```

### 9) Minimal VFS skeleton (`fs/vfs_file.c`)
```c
#include <ck/vfs.h>

int vfs_openat(int dfd, const char *path, int flags, int mode) {
    struct path p;
    int rc = vfs_resolve_path(dfd, path, &p);
    if (rc < 0) return rc;
    return fd_install(vfs_do_open(&p, flags, mode));
}
```

### 10) Rust module example (`rust/src/lib.rs`)
(see Section 14 skeleton)

### 11) Top-level Makefile
```make
ARCH ?= x86_64
PROFILE ?= debug
OUT := out
BUILD := build

all: kernel

kernel:
	@echo "[CK] build kernel ($(ARCH),$(PROFILE))"

iso: kernel
	./scripts/build-iso.sh

qemu run:
	./scripts/run-qemu.sh

vmware:
	./scripts/run-vmware.sh

vbox:
	./scripts/run-vbox.sh

gdb:
	./scripts/debug-gdb.sh

test:
	python3 tools/test_harness.py

lint:
	@echo "lint placeholder: clang-format/clippy/checkpatch"

rust-check:
	cd rust && cargo check --target x86_64-unknown-none

size-report:
	@du -h $(OUT) || true
```

### 12) Shell run script (`scripts/run-qemu.sh`)
```sh
#!/usr/bin/env sh
set -eu
qemu-system-x86_64 \
  -m 1024 \
  -cdrom out/computekernel.iso \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device e1000,netdev=net0 \
  -serial stdio \
  -no-reboot
```

Note: the QEMU forward (`localhost:2222 -> guest:22`) is host-side plumbing. Main ISO currently has no in-kernel TCP/IP or SSH daemon yet.

### 13) Python generation helper (`tools/gen_syscalls.py`)
```python
#!/usr/bin/env python3
import argparse

SYSCALLS = ["write", "exit", "read", "openat", "close"]

p = argparse.ArgumentParser()
p.add_argument("--out", required=True)
args = p.parse_args()

with open(f"{args.out}/syscall_nr.h", "w", encoding="utf-8") as f:
    for i, name in enumerate(SYSCALLS):
        f.write(f"#define __NR_{name} {i}\n")
```

---

## Section 19 — Coding standards

- **C:** C11 freestanding subset, explicit-width integer types, no VLAs
- **Assembly:** AT&T syntax for x86_64; one entry symbol per file; ABI-preserving comments required
- **Rust:** `#![no_std]`, deny warnings in CI, document every `unsafe`
- **Headers:** include guards, minimal includes, forward declarations where possible
- **Macros:** uppercase, side-effect-safe, parenthesized
- **Logging:** severity-tagged (`TRACE/DEBUG/INFO/WARN/ERR/PANIC`)
- **Unsafe code:** justified in comments; smallest scope only
- **Comments:** explain invariants/why, not obvious syntax
- **Assertions:** `CK_ASSERT` in debug, hardened checks in release for critical paths
- **Tests:** deterministic, isolated, no hidden state assumptions
- **Naming:** `ck_` prefix for global kernel symbols

---

## Section 20 — Stability and lightweight engineering rules

- stable by default through strict init ordering and invariant checks
- debuggable via always-on serial logs in debug/safe profiles
- small by default via feature-gated modules and minimal baseline drivers
- efficient by limiting abstraction in hot paths and preallocating critical pools

### Rules
1. Kernel memory budget tracked per subsystem at boot.
2. Default build enables only mandatory drivers.
3. Optional features compile as modules where possible.
4. Trace points are static and compile-time gated.
5. Boot diagnostics include feature bits, memmap summary, init timings.
6. Panic path always emits symbolizable backtrace.
7. Conservative driver defaults: DMA off until validated path exists.
8. Safe fallback: drop to read-only rootfs and safe-mode profile if init fails.
9. Release gates: boot matrix pass + no critical selftest failures.
10. ISO size budget enforced in CI (`size-report` threshold 500 MB–1 GB target image profile).

---

## Section 21 — Final implementation summary

### 1) Repository tree
See Section 5 tree (authoritative initial layout).

### 2) Subsystem summary
Core subsystems: arch/boot, task+scheduler, mm, syscall, vfs/fs, driver/device, ipc, net, logging/panic, module loader, tooling/docs.

### 3) First 30 implementation tasks
1. Add top-level Makefile scaffold.
2. Add `scripts/check-env.sh`.
3. Add linker script.
4. Add `entry64.S`.
5. Add `kmain()`.
6. Add serial 8250 init/write.
7. Add printk ring buffer.
8. Parse multiboot memory map.
9. Add early bump allocator.
10. Add PMM metadata init.
11. Add buddy allocator alloc/free.
12. Add page table map helpers.
13. Add IDT table + loader.
14. Add exception stubs.
15. Add timer IRQ handler.
16. Add runqueue struct + init.
17. Add context switch stub.
18. Add kernel thread create API.
19. Add syscall entry asm.
20. Add syscall dispatcher.
21. Add `write` syscall to serial/tty.
22. Add ELF loader skeleton.
23. Add first user task launch.
24. Add basic VFS inode/dentry/file structs.
25. Add initramfs mount.
26. Add fd table and open/close.
27. Add PS/2 keyboard driver.
28. Add block request core.
29. Add ext2 read-only mount path.
30. Add test harness boot smoke test.

### 4) First successful boot milestone
QEMU boots ISO, prints boot log, initializes timer and scheduler, launches `/sbin/init`, accepts keyboard input on console.

### 5) First real hardware milestone
Two physical x86_64 systems boot from USB ISO in safe mode and normal mode; serial/framebuffer logs confirm init launch and stable idle for 10 minutes.

### 6) Commands to build and run
```bash
make kernel
make iso
make qemu
make run      # alias to qemu
make kvm
make gdb
make lint
make test
make rust-check
make size-report
```

### 7) Exact next coding steps
1. Commit repository skeleton and build scripts.
2. Implement boot entry + linker + `kmain` path.
3. Implement serial logger and verify first boot text in QEMU.
4. Implement memmap ingestion + PMM bring-up.
5. Implement IDT/timer interrupt and scheduler tick.
6. Implement syscall entry/dispatch and first userspace init.
