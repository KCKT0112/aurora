/* Aurora v2 - boot protocol accessors (Limine). */
#ifndef AURORA_ARCH_BOOT_H
#define AURORA_ARCH_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

bool boot_protocol_supported(void);
const struct limine_framebuffer *boot_framebuffer(void);
const struct limine_memmap_response *boot_memmap(void);
uint64_t boot_hhdm_offset(void);
const char *boot_loader_name(void);
const char *boot_loader_version(void);
const struct limine_file *boot_module(const char *name);

#endif /* AURORA_ARCH_BOOT_H */
