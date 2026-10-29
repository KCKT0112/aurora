# Terminal

Bounded command line and history; no POSIX shell, pipelines or arbitrary exec.
Commands: help, clear, pwd, ls, cd, cat, write, mkdir, rm, ps, info, caps, beep, id, whoami, logout, stat, chmod, play, audio, stop.
All file/audio actions use authenticated IPC. Writes are temporary RAM state.
US set-1 key mapping with Shift/CapsLock; 128-event typeahead survives command RPC waits.
Grants: fs_read, fs_write, inspect, audio. No raw device or task-control access.

`play` without a path selects the first WAV in the current user Music directory,
so non-ASCII filenames can be played without typing them using the US keymap.
