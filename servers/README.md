# User services

- `auth/`: salted password verification and session authority.
- `init/`: sole boot-module spawner; bounded restart policy for ordinary apps.
- `display/`: framebuffer owner, window manager, validated model compositor.
- `input/`: kernel-origin PS/2/tablet events to semantic desktop input.
- `vfs/`: validated immutable image and bounded transactional RAM overlay.
- `audio/`: authorized tone requests to the PC-speaker HAL.

All are separate ring-3 ELF modules. Trusted grants are generated from
`config/system.json`. Services must authenticate message sources; no inclusion
of architecture/kernel headers is allowed. Full device isolation and stateful
service recovery remain future work.
