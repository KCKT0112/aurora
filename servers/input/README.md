# Input service

Receives only kernel-origin IRQ1/IRQ12/virtual-channel15 events. Parses PS/2
mouse packets and forwards raw set-1 keys, relative mouse and normalized
absolute virtio-tablet coordinates to the supplied display endpoint.

Only this process has the input grant. The kernel still owns port reads and
virtio PCI/queue access; this is a user-space protocol service, not a complete
user-space hardware driver. USB, keymaps, IME and hotplug remain unsupported.
