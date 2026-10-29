/* Host ASan/UBSan harness exercises the exact parsers linked into kernel/vfsd. */
#include <aurora/abi.h>
#include <aurora/elf.h>
#include <aurora/fs_validate.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void checksum(void *data, size_t size)
{
    aurora_fs_header_t *h = data;
    uint8_t *bytes = data;
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = sizeof(*h); i < size; i++)
        hash = (hash ^ bytes[i]) * 0x01000193u;
    h->checksum = hash;
}
static void test_fs(void)
{
    struct {
        aurora_fs_header_t h;
        aurora_fs_node_t nodes[3];
        char bytes[4];
    } image = {.h = {AURORA_FS_MAGIC, 2, 3, 0, 32 + 3 * 72, 4, 32 + 3 * 72 + 4, 0},
               .nodes = {{"/", 0, 1, 0, 0, 0555, 0, 0, 0},
                         {"home", 0, 1, 0, 0, 0555, 0, 0, 0},
                         {"hello", 1, 2, 4, 0, 0444, 0, 0, 0}},
               .bytes = "hey"};
    checksum(&image, sizeof(image));
    assert(aurora_fs_valid(&image, sizeof(image)));
    image.nodes[1].parent = 2;
    checksum(&image, sizeof(image));
    assert(!aurora_fs_valid(&image, sizeof(image))); /* cycle/future parent */
    image.nodes[1].parent = 0;
    image.nodes[2].data_offset = 0xffffffff;
    checksum(&image, sizeof(image));
    assert(!aurora_fs_valid(&image, sizeof(image)));
    image.nodes[2].data_offset = 0;
    strcpy(image.nodes[2].name, "../evil");
    checksum(&image, sizeof(image));
    assert(!aurora_fs_valid(&image, sizeof(image)));
    strcpy(image.nodes[2].name, "home");
    image.nodes[2].parent = 0;
    checksum(&image, sizeof(image));
    assert(!aurora_fs_valid(&image, sizeof(image)));
    char path[80];
    assert(aurora_path_normalize("/home/user", "../user/./Documents//a", path, sizeof(path)));
    assert(!strcmp(path, "/home/user/Documents/a"));
    assert(aurora_path_normalize("/", "/tmp/../system", path, sizeof(path)));
    assert(!strcmp(path, "/system"));
    assert(aurora_path_normalize("/", "../../..", path, sizeof(path)) && !strcmp(path, "/"));
    assert(!aurora_path_normalize("/", "a\nb", path, sizeof(path)));
    assert(!aurora_path_normalize("/", "too-long", path, 4));
}
static void test_elf(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    size_t size = (size_t)length;
    uint8_t *data = malloc(size), *copy = malloc(size);
    assert(data && copy);
    assert(fread(data, 1, size, file) == size);
    fclose(file);
    assert(aurora_elf_valid(data, size));
    memcpy(copy, data, size);
    aurora_elf_header_t *h = (void *)copy;
    h->phoff = UINT64_MAX - 16;
    assert(!aurora_elf_valid(copy, size));
    memcpy(copy, data, size);
    h->entry = USER_STACK_TOP;
    assert(!aurora_elf_valid(copy, size));
    memcpy(copy, data, size);
    aurora_elf_segment_t *p = (void *)(copy + h->phoff);
    p[1].flags = 7;
    assert(!aurora_elf_valid(copy, size));
    memcpy(copy, data, size);
    p[2].vaddr = p[1].vaddr;
    assert(!aurora_elf_valid(copy, size));
    memcpy(copy, data, size);
    copy[p[0].offset + 20] = 99;
    assert(!aurora_elf_valid(copy, size));
    memcpy(copy, data, size);
    p[1].memsz = UINT64_MAX;
    assert(!aurora_elf_valid(copy, size));
    for (size_t n = 0; n < sizeof(aurora_elf_header_t); n++)
        assert(!aurora_elf_valid(data, n));
    /* Mutations may be valid; they must never crash or walk outside the input. */
    uint32_t random = 1234567;
    for (unsigned i = 0; i < 4000; i++) {
        memcpy(copy, data, size);
        random = random * 1664525u + 1013904223u;
        size_t at = random % (size < 320 ? size : 320);
        copy[at] ^= (uint8_t)(1u << (random % 8));
        (void)aurora_elf_valid(copy, size);
    }
    free(copy);
    free(data);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    test_fs();
    test_elf(argv[1]);
    puts("core parsers: adversarial cases passed");
    return 0;
}
