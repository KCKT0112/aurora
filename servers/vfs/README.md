# VFS service

`main.c`: validated immutable image, bounded atomic RAM overlay and UID/GID/mode
checks. `handles.c`: owner-bound read descriptors, pread, close, source-lifetime
reclamation and explicit audio delegation. Directory search, sticky deletion,
metadata changes and read permissions are enforced using kernel credentials.

Bulk reads use a recipient-authorized one-use64KiB buffer grant. No user-provided
physical address or arbitrary writable target pointer crosses the boundary.
No raw disk, persistence or full POSIX fd/unlink semantics exist yet.

See [layout](../../docs/FILESYSTEM.md), [protocol](../../docs/APPLICATIONS.md),
[audio](../../docs/AUDIO.md) and `apps/service-test.c`.
