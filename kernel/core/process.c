/* Boot-module loader: no application bytes are linked into the kernel. */
#include <arch/boot.h>
#include <arch/pmm.h>
#include <arch/vmm.h>
#include <aurora/elf.h>
#include <kernel/log.h>
#include <kernel/ipc.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <string.h>

typedef struct {
    const char *name;
    uint64_t caps;
    bool session_identity;
} image_policy_t;
/* This is the trusted boot policy, not a capability request from an ELF. */
static const image_policy_t g_images[IMAGE_COUNT] = {
#include <boot_policy.h>
};
static process_t g_processes[CONFIG_MAX_PROCESSES];
static credentials_t g_session = {AURORA_NO_USER, AURORA_NO_USER, 0, 0};
static uint32_t g_session_generation;

credentials_t process_session(void)
{
    return g_session;
}

int process_session_set(uint32_t uid)
{
    account_info_t account = aurora_account(uid);
    if (uid != AURORA_NO_USER && account.uid == AURORA_NO_USER)
        return -AURORA_EINVAL;
    if (g_session_generation == UINT32_MAX)
        return -AURORA_ENOMEM;
    /* Credentials never change in place: terminate every old-session process. */
    for (unsigned i = 0; i < CONFIG_MAX_PROCESSES; i++) {
        process_t *p = &g_processes[i];
        if (p->used && p->credentials.session)
            (void)sched_kill(p->main_tid);
    }
    g_session = (credentials_t){uid, account.gid, ++g_session_generation, 0};
    /* Purge pending input and UI data. New models must come from new live TIDs. */
    const int endpoints[] = {IMAGE_DISPLAY, IMAGE_INPUT};
    for (unsigned i = 0; i < 2; i++) {
        thread_t *t = sched_thread_get(process_service_tid(endpoints[i]));
        if (t)
            mailbox_init(&t->mailbox);
    }
    ipc_message_t event = {.type = MSG_SESSION_CHANGED, .arg0 = g_session.session};
    const int observers[] = {IMAGE_DISPLAY, IMAGE_INIT, IMAGE_VFS, IMAGE_INPUT, IMAGE_AUDIO};
    for (unsigned i = 0; i < 5; i++)
        (void)ipc_send(process_service_tid(observers[i]), &event, KERNEL_ENDPOINT);
    kprintf("[session] uid=%u generation=%u\n", uid, g_session.session);
    return 0;
}

bool process_has_cap(const thread_t *thread, uint64_t cap)
{
    return thread && thread->process && (thread->process->capabilities & cap) == cap;
}

int process_service_tid(int image_id)
{
    if (image_id < 0 || image_id >= IMAGE_COUNT)
        return -AURORA_EINVAL;
    for (unsigned i = 0; i < CONFIG_MAX_PROCESSES; i++) {
        process_t *p = &g_processes[i];
        thread_t *t = p->used ? sched_thread_get(p->main_tid) : NULL;
        if (t && t->state != THREAD_DEAD && p->image_id == image_id)
            return p->main_tid;
    }
    return -AURORA_ENOENT;
}

int process_display_tid(void)
{
    return process_service_tid(IMAGE_DISPLAY);
}

void process_release(process_t *process)
{
    if (!process)
        return;
    vmm_destroy_space(&process->space);
    memset(process, 0, sizeof(*process));
}

static bool load_segment(process_t *process, const uint8_t *bytes, const aurora_elf_segment_t *p)
{
    uint64_t start = p->vaddr & ~4095ull;
    uint64_t end = (p->vaddr + p->memsz + 4095) & ~4095ull;
    uint64_t flags = VMM_PRESENT | VMM_USER | VMM_OWNED | ((p->flags & 2) ? VMM_WRITE : 0) |
                     ((p->flags & 1) ? 0 : VMM_NX);
    for (uint64_t page = start; page < end; page += 4096) {
        uint64_t frame = pmm_alloc_frame();
        if (!frame)
            return false;
        uint64_t lo = page > p->vaddr ? page : p->vaddr;
        uint64_t hi = p->vaddr + p->filesz;
        if (hi > page + 4096)
            hi = page + 4096;
        if (lo < hi)
            memcpy((uint8_t *)vmm_phys_to_virt(frame) + lo - page,
                   bytes + p->offset + lo - p->vaddr, hi - lo);
        if (!vmm_map_range(&process->space, page, frame, 4096, flags)) {
            pmm_free_frame(frame);
            return false;
        }
        process->memory_bytes += 4096;
    }
    return true;
}

int process_spawn_image(int image_id, uint64_t argument, int parent_tid)
{
    if (image_id < 0 || image_id >= IMAGE_COUNT)
        return -AURORA_EINVAL;
    if (process_service_tid(image_id) > 0)
        return -AURORA_EBUSY;
    const image_policy_t *policy = &g_images[image_id];
    if (policy->session_identity && g_session.uid == AURORA_NO_USER)
        return -AURORA_EPERM;
    const struct limine_file *module = boot_module(policy->name);
    if (!module)
        return -AURORA_ENOENT;
    if (!aurora_elf_valid(module->address, module->size)) {
        kprintf("[elf] invalid or incompatible module '%s'\n", policy->name);
        return -AURORA_EINVAL;
    }
    process_t *process = NULL;
    for (unsigned i = 0; i < CONFIG_MAX_PROCESSES; i++) {
        if (!g_processes[i].used) {
            process = &g_processes[i];
            break;
        }
    }
    if (!process)
        return -AURORA_ENOMEM;
    memset(process, 0, sizeof(*process));
    if (!vmm_create_space(&process->space))
        return -AURORA_ENOMEM;
    const aurora_elf_header_t *h = module->address;
    const aurora_elf_segment_t *segments =
        (const void *)((const uint8_t *)module->address + h->phoff);
    for (unsigned i = 0; i < h->phnum; i++) {
        if (segments[i].type == 1 && segments[i].memsz &&
            !load_segment(process, module->address, &segments[i]))
            goto fail;
    }
    for (uint64_t page = USER_STACK_TOP - USER_STACK_SIZE; page < USER_STACK_TOP; page += 4096) {
        uint64_t frame = pmm_alloc_frame();
        if (!frame)
            goto fail;
        if (!vmm_map_range(&process->space, page, frame, 4096,
                           VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NX | VMM_OWNED)) {
            pmm_free_frame(frame);
            goto fail;
        }
        process->memory_bytes += 4096;
    }
    process->capabilities = policy->caps;
    process->credentials = policy->session_identity ? g_session : (credentials_t){0, 0, 0, 0};
    process->image_id = image_id;
    process->parent_tid = parent_tid;
    int tid =
        sched_create_user_thread(process, h->entry, USER_STACK_TOP - 16, argument, policy->name);
    if (tid < 0)
        goto fail;
    process->used = true;
    kprintf("[proc] module '%s' tid=%d parent=%d caps=%x\n", policy->name, tid, parent_tid,
            (unsigned)policy->caps);
    return tid;
fail:
    process_release(process);
    return -AURORA_ENOMEM;
}

bool process_start_init(void)
{
    return process_spawn_image(IMAGE_INIT, AURORA_ABI_VERSION, KERNEL_ENDPOINT) > 0;
}
