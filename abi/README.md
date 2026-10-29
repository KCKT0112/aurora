# ABI 4

Public kernel/user wire definitions only. `abi.h` owns syscall/message/image
IDs and fixed-size structures, `security.h` authority bits and common errors,
`fs.h` the on-disk image format, `version.h` the release version.

Changing an existing wire structure requires an ABI decision and matching
ELF note in `libs/user/start.S`. Append syscall IDs; do not repurpose existing
semantics. See [application protocol](../docs/APPLICATIONS.md).
