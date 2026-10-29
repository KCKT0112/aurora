# Applications

`files/`, `taskmgr/` and `terminal/` run as independent ring-3 processes and
publish model views. They do not touch the framebuffer or hardware. Init is
the parent; service discovery uses SYS_SERVICE, never the parent TID.

`security-test/` and `service-test.c` are test-image-only clients. Ordinary
images do not package or execute them. See [application guide](../docs/APPLICATIONS.md).
