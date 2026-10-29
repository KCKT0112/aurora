# System Monitor

Samples kernel thread counters once per second and publishes private mapped
memory and sampled CPU usage. With inspect/task_control grants it can stop an
ordinary app after two Delete presses within three seconds. Core services are
protected by the kernel, not merely by the UI. Init restarts the stopped app.

Rows show real owner names. Non-root callers may stop only their own apps.
