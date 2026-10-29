#include <aurora/abi.h>
#include <aurora/elf.h>
#include <string.h>

static bool bounded(uint64_t offset, uint64_t length, uint64_t size)
{
    return offset <= size && length <= size - offset;
}

bool aurora_elf_valid(const void *data, size_t size)
{
    if (data == NULL || size < sizeof(aurora_elf_header_t))
        return false;
    const uint8_t *bytes = data;
    const aurora_elf_header_t *h = data;
    if (memcmp(h->ident, "\177ELF\002\001\001", 7) != 0 || h->type != 2 || h->machine != 62 ||
        h->version != 1 || h->ehsize != sizeof(*h) ||
        h->phentsize != sizeof(aurora_elf_segment_t) || h->phnum == 0 || h->phnum > 32 ||
        !bounded(h->phoff, (uint64_t)h->phnum * h->phentsize, size))
        return false;
    const aurora_elf_segment_t *segments = (const void *)(bytes + h->phoff);
    bool executable_entry = false, compatible = false;
    for (uint16_t i = 0; i < h->phnum; i++) {
        const aurora_elf_segment_t *p = &segments[i];
        if (!bounded(p->offset, p->filesz, size))
            return false;
        if (p->type == 4) {
            /* One canonical Aurora ABI note; reject malformed/unknown notes. */
            static const uint32_t prefix[] = {8, 4, 1};
            if (p->filesz == 24 && memcmp(bytes + p->offset, prefix, 12) == 0 &&
                memcmp(bytes + p->offset + 12, "AURORA\0\0", 8) == 0) {
                uint32_t version;
                memcpy(&version, bytes + p->offset + 20, 4);
                compatible = version == AURORA_ABI_VERSION;
            }
        }
        if (p->type == 2 || p->type == 3)
            return false; /* no dynamic/interp */
        if (p->type != 1)
            continue;
        if (p->filesz > p->memsz || (p->flags & ~7u) || (p->flags & 3) == 3)
            return false;
        if (!p->memsz)
            continue;
        if (p->vaddr < USER_BASE || p->vaddr >= USER_IMAGE_LIMIT ||
            p->memsz > USER_IMAGE_LIMIT - p->vaddr)
            return false;
        if (p->align > 1 && ((p->align & (p->align - 1)) ||
                             (p->offset & (p->align - 1)) != (p->vaddr & (p->align - 1))))
            return false;
        uint64_t start = p->vaddr & ~4095ull;
        uint64_t end = (p->vaddr + p->memsz + 4095) & ~4095ull;
        for (uint16_t j = 0; j < i; j++) {
            const aurora_elf_segment_t *q = &segments[j];
            if (q->type != 1 || !q->memsz)
                continue;
            uint64_t qs = q->vaddr & ~4095ull;
            uint64_t qe = (q->vaddr + q->memsz + 4095) & ~4095ull;
            if (start < qe && qs < end)
                return false;
        }
        if ((p->flags & 1) && h->entry >= p->vaddr && h->entry - p->vaddr < p->filesz)
            executable_entry = true;
    }
    return compatible && executable_entry;
}
