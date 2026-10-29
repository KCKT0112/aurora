# Files

Owns navigation, VFS request IDs and preview state. Publishes Files/Preview
models; receives select/activate/back/path/refresh events from display.
Initial location: the signed-in account home. Grants: fs_read + fs_write (write/metadata UI remains
terminal-only). List limit: 12 entries; preview limit: 16 lines of 62 bytes.

WAV files open via an explicitly delegated read descriptor to audiod. Space
requests stop for playback owned by this app; the caption reports status/errors.
