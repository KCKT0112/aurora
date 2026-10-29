# Kernel

`core/` loads validated ELF boot modules and creates init; `sched/` implements
single-CPU preemption, blocking waits and safe deferred reclamation; `ipc/`
owns bounded mailboxes and IRQ subscriptions; `syscall/` validates pointers and
authority. No application ELF is linked into the kernel.

Boot splash and architecture-aware thread contexts are transitional dependencies.
Device mechanisms remain under `arch/x86_64/`; the current kernel is not yet a
pure microkernel. See [architecture](../docs/ARCHITECTURE.md) and
[security](../docs/SECURITY.md) before extending privileged operations.
