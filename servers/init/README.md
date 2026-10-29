# Init

Starts display/input/VFS/audio and the three standard apps using trusted image
IDs. Ordinary apps are restarted with a minimum two-second start interval;
core services are not blindly restarted because their state cannot yet be
recovered safely. Test images additionally run unprivileged denial tests,
16 reclamation cycles and transactional service tests.

Init alone holds `CAP_SPAWN`. There is no general exec or package installer.

Boots authd and waits for an authenticated session before starting apps. Logout
terminates old-session apps; new apps inherit the kernel session UID/GID.
