/* Aurora v2 - kernel diagnostics over the serial console. */
#ifndef AURORA_KERNEL_LOG_H
#define AURORA_KERNEL_LOG_H

void kprintf(const char *format, ...);
void kpanic(const char *message) __attribute__((noreturn));

#endif /* AURORA_KERNEL_LOG_H */
