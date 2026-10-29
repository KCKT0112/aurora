/* ABI 3 syscall dispatch: user ranges and explicit authority checked here. */
#include <arch/fb.h>
#include <arch/boot.h>
#include <arch/audio.h>
#include <arch/pmm.h>
#include <arch/io.h>
#include <arch/isr.h>
#include <arch/pit.h>
#include <arch/vmm.h>
#include <aurora/abi.h>
#include <kernel/ipc.h>
#include <kernel/log.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/thread.h>
#include <string.h>

#define MAX_LOG_LENGTH 240
static uint64_t next_io_token = 1;

static void erase_message(ipc_message_t *message)
{
    volatile uint8_t *bytes = (volatile uint8_t *)message;
    for (unsigned i = 0; i < sizeof(*message); i++)
        bytes[i] = 0;
}

static bool user_range(thread_t *thread, uint64_t address, uint64_t size, bool write)
{
    return thread != NULL && thread->process != NULL &&
           vmm_user_range(&thread->process->space, address, size, write);
}

static int64_t sys_fb_map(thread_t *thread, uint64_t info_address)
{
    if (!process_has_cap(thread, CAP_FRAMEBUFFER) ||
        !user_range(thread, info_address, sizeof(framebuffer_info_t), true)) {
        return -1;
    }
    uint64_t physical = fb_physical_address();
    uint64_t size = (fb_mapping_bytes() + 4095ull) & ~4095ull;
    if ((physical & 4095ull) != 0) {
        return -1;
    }
    if (!vmm_user_range(&thread->process->space, USER_FB_BASE, size, true) &&
        !vmm_map_range(&thread->process->space, USER_FB_BASE, physical, size,
                       VMM_PRESENT | VMM_WRITE | VMM_USER | VMM_NX | VMM_PCD)) {
        return -1;
    }

    const framebuffer_t *framebuffer = fb_get();
    framebuffer_info_t info = {
        .width = framebuffer->width,
        .height = framebuffer->height,
        .pitch = framebuffer->pitch_pixels * sizeof(uint32_t),
        .bpp = 32,
        .vaddr = USER_FB_BASE,
        .red_shift = framebuffer->red_shift,
        .green_shift = framebuffer->green_shift,
        .blue_shift = framebuffer->blue_shift,
    };
    memcpy((void *)(uintptr_t)info_address, &info, sizeof(info));
    return (int64_t)USER_FB_BASE;
}

static int64_t sys_log(thread_t *thread, uint64_t address, uint64_t length)
{
    char buffer[MAX_LOG_LENGTH + 1];
    if (length > MAX_LOG_LENGTH || !user_range(thread, address, length, false)) {
        return -1;
    }
    memcpy(buffer, (const void *)(uintptr_t)address, (size_t)length);
    buffer[length] = '\0';
    kprintf("[%s:%d] %s\n", thread->name, thread->tid, buffer);
    return (int64_t)length;
}

void syscall_handle(struct interrupt_frame *frame)
{
    thread_t *thread = sched_current();
    int64_t result = -1;

    if (thread == NULL || !thread->is_user) {
        frame->rax = (uint64_t)-1;
        return;
    }

    switch (frame->rax) {
    case SYS_EXIT:
        kprintf("[user] %s tid=%d exited\n", thread->name, thread->tid);
        sched_terminate_current();
        result = 0;
        break;
    case SYS_YIELD:
        sched_request();
        result = 0;
        break;
    case SYS_PAUSE:
        sched_wait(1);
        result = 0;
        break;
    case SYS_GETTID:
        result = thread->tid;
        break;
    case SYS_IRQ_SUBSCRIBE:
        result = ipc_subscribe_irqs(thread->tid, frame->rdi) ? 0 : -AURORA_EPERM;
        break;
    case SYS_RECV: {
        ipc_message_t message;
        if (!user_range(thread, frame->rdi, sizeof(message), true)) {
            result = -1;
            break;
        }
        if (!mailbox_pop(&thread->mailbox, &message)) {
            result = -AURORA_EAGAIN;
            break;
        }
        memcpy((void *)(uintptr_t)frame->rdi, &message, sizeof(message));
        erase_message(&message);
        result = 0;
        break;
    }
    case SYS_SEND: {
        ipc_message_t message;
        if (!user_range(thread, frame->rsi, sizeof(message), false)) {
            result = -1;
            break;
        }
        memcpy(&message, (const void *)(uintptr_t)frame->rsi, sizeof(message));
        if (frame->rdi > 0x7fffffffu) {
            result = -AURORA_EINVAL;
            break;
        }
        thread_t *peer = sched_thread_get((int)frame->rdi);
        if (message.type < 0x200) {
            result = -AURORA_EPERM;
            break;
        }
        if (peer && peer->process &&
            ((peer->process->image_id == IMAGE_VFS && !process_has_cap(thread, CAP_FS_READ)) ||
             (peer->process->image_id == IMAGE_AUDIO && !process_has_cap(thread, CAP_AUDIO) &&
              !(thread->process->image_id == IMAGE_VFS && message.type == MSG_FS_FD_RESULT)))) {
            result = -AURORA_EPERM;
            break;
        }
        if (peer && peer->process && peer->process->image_id == IMAGE_AUTH &&
            thread->process->image_id != IMAGE_DISPLAY &&
            !(thread->process->image_id == IMAGE_TERMINAL && message.type == MSG_AUTH_LOGOUT)) {
            result = -AURORA_EPERM;
            break;
        }
        if (peer && peer->process && thread->process->credentials.session &&
            peer->process->credentials.session &&
            peer->process->credentials.session != thread->process->credentials.session) {
            result = -AURORA_EPERM;
            break;
        }
        result = ipc_send((int)frame->rdi, &message, (uint64_t)thread->tid);
        erase_message(&message);
        break;
    }
    case SYS_IO_BUFFER: {
        process_t *p = thread->process;
        while (p->io_pages < USER_IO_SIZE / 4096) {
            uint64_t page = pmm_alloc_frame();
            if (!page) {
                result = -AURORA_ENOMEM;
                break;
            }
            if (!vmm_map_range(&p->space, USER_IO_BASE + (uint64_t)p->io_pages * 4096, page, 4096,
                               VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NX | VMM_OWNED)) {
                pmm_free_frame(page);
                result = -AURORA_ENOMEM;
                break;
            }
            p->io_pages++;
            p->memory_bytes += 4096;
        }
        if (p->io_pages == USER_IO_SIZE / 4096)
            result = USER_IO_BASE;
        break;
    }
    case SYS_IO_GRANT: {
        process_t *p = thread->process;
        p->io_token = 0; /* also provides an explicit revoke operation */
        if (frame->rsi == 0) {
            result = 0;
            break;
        }
        thread_t *peer = frame->rdi <= INT32_MAX ? sched_thread_get((int)frame->rdi) : NULL;
        if (!peer || peer->state == THREAD_DEAD || peer == thread)
            result = -AURORA_EINVAL;
        else if (frame->rsi > USER_IO_SIZE || p->io_pages != USER_IO_SIZE / 4096)
            result = -AURORA_EINVAL;
        else if (next_io_token >= INT64_MAX)
            result = -AURORA_ENOMEM;
        else {
            p->io_peer = peer->tid;
            p->io_limit = (uint32_t)frame->rsi;
            p->io_token = next_io_token++;
            result = (int64_t)p->io_token;
        }
        break;
    }
    case SYS_IO_COPY: {
        /* rdi=target TID, rsi=one-use token, rdx=descriptor {source,size}. */
        uint64_t descriptor[2];
        if (!user_range(thread, frame->rdx, sizeof(descriptor), false)) {
            result = -AURORA_EFAULT;
            break;
        }
        memcpy(descriptor, (const void *)(uintptr_t)frame->rdx, sizeof(descriptor));
        thread_t *target = frame->rdi <= INT32_MAX ? sched_thread_get((int)frame->rdi) : NULL;
        process_t *p = target ? target->process : NULL;
        if (!p || target->state == THREAD_DEAD || !p->io_token || p->io_token != frame->rsi ||
            p->io_peer != thread->tid)
            result = -AURORA_EPERM;
        else if (descriptor[1] > p->io_limit ||
                 (descriptor[1] && !user_range(thread, descriptor[0], descriptor[1], false)))
            result = -AURORA_EFAULT;
        else {
            p->io_token = 0;
            result = vmm_copy_to_space(&p->space, USER_IO_BASE,
                                       (const void *)(uintptr_t)descriptor[0], descriptor[1])
                         ? (int64_t)descriptor[1]
                         : -AURORA_EFAULT;
        }
        break;
    }
    case SYS_DISPLAY_MODE:
        if (!process_has_cap(thread, CAP_FRAMEBUFFER))
            result = -AURORA_EPERM;
        else if (!user_range(thread, frame->rdx, sizeof(framebuffer_info_t), true))
            result = -AURORA_EFAULT;
        else if (frame->rdi > 3840 || frame->rsi > 2160 ||
                 !fb_set_mode((uint32_t)frame->rdi, (uint32_t)frame->rsi))
            result = -AURORA_EINVAL;
        else
            result = sys_fb_map(thread, frame->rdx);
        break;
    case SYS_FB_MAP:
        result = sys_fb_map(thread, frame->rdi);
        break;
    case SYS_GET_TICKS:
        result = (int64_t)pit_ticks();
        break;
    case SYS_LOG:
        result = sys_log(thread, frame->rdi, frame->rsi);
        break;
    case SYS_SPAWN:
        if (!process_has_cap(thread, CAP_SPAWN)) {
            result = -AURORA_EPERM;
        } else {
            if (frame->rdi >= IMAGE_COUNT || frame->rdi == IMAGE_INIT) {
                result = -AURORA_EINVAL;
                break;
            }
            result = process_spawn_image((int)frame->rdi, frame->rsi, thread->tid);
        }
        break;
    case SYS_GETPPID:
        result = thread->process != NULL ? thread->process->parent_tid : -1;
        break;
    case SYS_THREAD_LIST: {
        if (!process_has_cap(thread, CAP_INSPECT)) {
            result = -AURORA_EPERM;
            break;
        }
        uint32_t capacity = (uint32_t)frame->rsi;
        if (capacity > CONFIG_MAX_THREADS) {
            capacity = CONFIG_MAX_THREADS;
        }
        if (capacity == 0 ||
            !user_range(thread, frame->rdi, (uint64_t)capacity * sizeof(thread_info_t), true)) {
            result = -1;
            break;
        }
        result = sched_snapshot((thread_info_t *)(uintptr_t)frame->rdi, capacity);
        break;
    }
    case SYS_INFO: {
        if (!user_range(thread, frame->rdi, sizeof(system_info_t), true)) {
            result = -AURORA_EFAULT;
            break;
        }
        system_info_t info = {
            .abi_version = AURORA_ABI_VERSION,
            .size = sizeof(info),
            .capabilities = thread->process->capabilities,
            .uptime_ticks = pit_ticks(),
            .free_memory_bytes = pmm_free_bytes(),
            .tick_hz = AURORA_TICK_HZ,
            .uid = thread->process->credentials.uid,
            .gid = thread->process->credentials.gid,
            .session = thread->process->credentials.session,
        };
        memcpy((void *)(uintptr_t)frame->rdi, &info, sizeof(info));
        result = 0;
        break;
    }
    case SYS_SERVICE:
        result = frame->rdi < IMAGE_COUNT ? process_service_tid((int)frame->rdi) : -AURORA_EINVAL;
        break;
    case SYS_KILL: {
        thread_t *target = frame->rdi <= INT32_MAX ? sched_thread_get((int)frame->rdi) : NULL;
        if (!process_has_cap(thread, CAP_TASK_CONTROL))
            result = -AURORA_EPERM;
        else if (!target || !target->process)
            result = -AURORA_ENOENT;
        else if (thread->process->credentials.uid != 0 &&
                 target->process->credentials.uid != thread->process->credentials.uid)
            result = -AURORA_EPERM;
        else
            result = sched_kill((int)frame->rdi);
        break;
    }
    case SYS_WAIT:
        sched_wait(frame->rdi);
        result = 0;
        break;
    case SYS_PCM_WRITE:
        if (!process_has_cap(thread, CAP_AUDIO_DEVICE))
            result = -AURORA_EPERM;
        else if (!frame->rsi || frame->rsi > PCM_MAX_FRAMES)
            result = -AURORA_EINVAL;
        else if (!user_range(thread, frame->rdi, frame->rsi * 4, false))
            result = -AURORA_EFAULT;
        else
            result = pcm_write((const int16_t *)(uintptr_t)frame->rdi, (uint32_t)frame->rsi);
        break;
    case SYS_PCM_CONTROL: {
        pcm_status_t status;
        if (!process_has_cap(thread, CAP_AUDIO_DEVICE))
            result = -AURORA_EPERM;
        else if (!user_range(thread, frame->rsi, sizeof(status), true))
            result = -AURORA_EFAULT;
        else if (frame->rdi > PCM_START)
            result = -AURORA_EINVAL;
        else {
            result = pcm_control((uint32_t)frame->rdi, &status);
            if (result >= 0)
                memcpy((void *)(uintptr_t)frame->rsi, &status, sizeof(status));
        }
        break;
    }
    case SYS_AUDIO_TONE:
        result = !process_has_cap(thread, CAP_AUDIO_DEVICE) ? -AURORA_EPERM
                 : frame->rdi > 8000 || frame->rsi > 200
                     ? -AURORA_EINVAL
                     : audio_tone((uint32_t)frame->rdi, (uint32_t)frame->rsi);
        break;
    case SYS_PEER_CAPS: {
        thread_t *peer = frame->rdi <= 0x7fffffffu ? sched_thread_get((int)frame->rdi) : NULL;
        result = !process_has_cap(thread, CAP_SERVICE) ? -AURORA_EPERM
                 : !peer || !peer->process || peer->state == THREAD_DEAD
                     ? -AURORA_ENOENT
                     : (int64_t)peer->process->capabilities;
        break;
    }
    case SYS_BOOT_RESOURCE: {
        if (thread->process->image_id != IMAGE_VFS && thread->process->image_id != IMAGE_AUTH) {
            result = -AURORA_EPERM;
            break;
        }
        const struct limine_file *module =
            boot_module(thread->process->image_id == IMAGE_AUTH ? "accounts" : "initramfs");
        if (!module || !module->size || module->size > 64 * 1024 * 1024) {
            result = -AURORA_ENOENT;
            break;
        }
        uint64_t physical = (uint64_t)(uintptr_t)module->address - vmm_hhdm();
        uint64_t size = (module->size + 4095) & ~4095ull;
        if ((physical & 4095) ||
            (!vmm_user_range(&thread->process->space, USER_RESOURCE_BASE, size, false) &&
             !vmm_map_range(&thread->process->space, USER_RESOURCE_BASE, physical, size,
                            VMM_PRESENT | VMM_USER | VMM_NX))) {
            result = -AURORA_ENOMEM;
            break;
        }
        result = (int64_t)module->size;
        break;
    }
    case SYS_PEER_CREDENTIALS: {
        thread_t *peer = frame->rdi <= INT32_MAX ? sched_thread_get((int)frame->rdi) : NULL;
        if (!process_has_cap(thread, CAP_SERVICE))
            result = -AURORA_EPERM;
        else if (!peer || !peer->process || peer->state == THREAD_DEAD)
            result = -AURORA_ENOENT;
        else if (!user_range(thread, frame->rsi, sizeof(credentials_t), true))
            result = -AURORA_EFAULT;
        else {
            memcpy((void *)(uintptr_t)frame->rsi, &peer->process->credentials,
                   sizeof(credentials_t));
            result = 0;
        }
        break;
    }
    case SYS_SESSION_GET: {
        if (!user_range(thread, frame->rdi, sizeof(credentials_t), true))
            result = -AURORA_EFAULT;
        else {
            credentials_t session = process_session();
            memcpy((void *)(uintptr_t)frame->rdi, &session, sizeof(session));
            result = 0;
        }
        break;
    }
    case SYS_SESSION_SET:
        if (!process_has_cap(thread, CAP_SESSION) || thread->process->image_id != IMAGE_AUTH)
            result = -AURORA_EPERM;
        else if (frame->rdi > UINT32_MAX)
            result = -AURORA_EINVAL;
        else
            result = process_session_set((uint32_t)frame->rdi);
        break;
    default:
        kprintf("[user] invalid syscall %u from tid %d\n", (unsigned)frame->rax, thread->tid);
        result = -AURORA_ENOSYS;
        break;
    }

    frame->rax = (uint64_t)result;
}
