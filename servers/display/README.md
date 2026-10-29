# Display service

`main.c` maps the framebuffer and dispatches authenticated input and app models.
`desktop.c` maintains window geometry/focus, per-view transactional staging,
scroll/selection, damage composition and save-under cursor rendering.

The service never starts applications or interprets shell/filesystem policy.
Only the assigned live app can update each view. Missing rows do not commit;
periodic refreshes do not undo window minimization. F1 cycles focus; F2–F4 open
Terminal/Files/Monitor. See [UI specification](../../docs/UI.md).

Limits: 1440x900 cache, five predefined windows, no generic surface allocation,
no GPU acceleration, no vblank synchronization.

`login.c` owns the trusted sign-in form; authd verifies passwords. Window edges
resize with directional cursors and clipping. Session changes clear models,
geometry, focus and key state. SVG sources live under assets/icons.
