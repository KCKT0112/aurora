# Third-party components and licenses

- **Limine Bootloader** — BSD-2-Clause. Used as the bootloader; its BIOS/UEFI
  assets are supplied by the host installation (`brew install limine`).
- **Limine protocol header** (`arch/x86_64/boot/limine.h`) — 0BSD, vendored from
  the official `limine-protocol` repository for reproducible builds.
- **Inter font** (`assets/fonts/InterVariable.ttf`) — SIL Open Font License 1.1,
  see `assets/fonts/Inter-OFL.txt`. The runtime only embeds generated bitmap
  atlases; the TTF is kept for reproducible asset generation.

No macOS system fonts or proprietary assets are embedded in Aurora.

Local media under `rootfs/home/*/Music/` is excluded from source control. Test
images generate deterministic synthetic tones using `tools/wav_fixture.py`
when no local Music WAV is present; personal recordings are not distributed.
