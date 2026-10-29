/* Pure ELF validation, shared by the loader and host adversarial tests. */
#ifndef AURORA_ELF_H
#define AURORA_ELF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} aurora_elf_header_t;
typedef struct __attribute__((packed)) {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} aurora_elf_segment_t;
bool aurora_elf_valid(const void *data, size_t size);
#endif
