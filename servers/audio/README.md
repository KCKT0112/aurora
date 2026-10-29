# Audio service

User-space WAV parser, ownership policy, PCM conversion and streaming. Supported
integer PCM:8/16/24/32-bit mono/stereo,8–96kHz; output48kHz stereo s16 through a
bounded AC97 HAL. Accepts only a VFS read handle explicitly delegated by the
requesting application; never opens an arbitrary path using its root identity.

Single stream,64KiB input cache,1024-frame output batches. Original owner exit,
logout, file mutation or permission rejection invalidates reads/stops playback.
PC-speaker notification requests remain separate. No float/compressed WAV,
multichannel mixer or production hardware support is claimed.

See [audio contract and usage](../../docs/AUDIO.md). QEMU CoreAudio is playback
only; unsupported recording warnings are summarized once and kept in qemu.log.
