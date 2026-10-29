# Shared libraries

- `core/`: allocation-free ELF/FS validators, canonical paths, DAC, authentication, WAV parsing and resize geometry; host tested.
- `libc/`: freestanding memory/string subset.
- `user/`: CRT, syscall wrappers, bounded send retries, model transactions, read-only fd client and text helpers.
- `ui/`: software drawing, clipping, SVG vectors, soft shadows, presentation and Inter atlas.

Only UI drawing is shared with the transitional kernel boot splash. Core and
user libraries may not depend on architecture or kernel headers.
