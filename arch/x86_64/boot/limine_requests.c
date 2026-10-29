/* Aurora v2 - Limine boot protocol requests.
 *
 * These objects must remain in the `.limine_requests` sections exactly as the
 * linker script lays them out. Limine rewrites their response pointers before
 * entering kmain.
 */
#include <arch/boot.h>
#include <string.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0,
};

const struct limine_file *boot_module(const char *name)
{
    const struct limine_module_response *response = module_request.response;
    if (response == NULL) return NULL;
    for (uint64_t i = 0; i < response->module_count; i++) {
        const struct limine_file *file = response->modules[i];
        if (file->string != NULL && strcmp(file->string, name) == 0)
            return file;
    }
    return NULL;
}

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

bool boot_protocol_supported(void)
{
    return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

const struct limine_framebuffer *boot_framebuffer(void)
{
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0) {
        return NULL;
    }
    return framebuffer_request.response->framebuffers[0];
}

const struct limine_memmap_response *boot_memmap(void)
{
    return memmap_request.response;
}

uint64_t boot_hhdm_offset(void)
{
    return hhdm_request.response != NULL ? hhdm_request.response->offset : 0;
}

const char *boot_loader_name(void)
{
    return bootloader_info_request.response != NULL
               ? bootloader_info_request.response->name
               : "unknown";
}

const char *boot_loader_version(void)
{
    return bootloader_info_request.response != NULL
               ? bootloader_info_request.response->version
               : "unknown";
}
