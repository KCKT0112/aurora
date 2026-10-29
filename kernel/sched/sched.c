/* Aurora v2 - x86_64 preemptive round-robin scheduler. */
#include <arch/gdt.h>
#include <arch/pit.h>
#include <kernel/process.h>
#include <arch/isr.h>
#include <arch/vmm.h>
#include <kernel/ipc.h>
#include <kernel/log.h>
#include <kernel/sched.h>
#include <string.h>

#define IDLE_SLOT 0

_Static_assert(sizeof(struct interrupt_frame) == 26 * sizeof(uint64_t),
               "trap frame layout must match cpu/isr.S");

static thread_t g_threads[CONFIG_MAX_THREADS];
static uint8_t g_kernel_stacks[CONFIG_MAX_THREADS][CONFIG_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));
static thread_t *g_current;
static uint32_t g_round_robin;
static uint64_t g_switches;
static int g_next_tid = 1;
static bool g_need_reschedule;

static thread_t *pick_ready(void)
{
    for (uint32_t offset = 1; offset <= CONFIG_MAX_THREADS; offset++) {
        uint32_t slot = (g_round_robin + offset) % CONFIG_MAX_THREADS;
        if (slot == IDLE_SLOT || g_threads[slot].is_idle) {
            continue;
        }
        if (g_threads[slot].state == THREAD_READY) {
            g_round_robin = slot;
            return &g_threads[slot];
        }
    }
    return NULL;
}

void sched_init(void)
{
    memset(g_threads, 0, sizeof(g_threads));
    g_current = &g_threads[IDLE_SLOT];
    g_current->tid = -1;
    g_current->state = THREAD_RUNNING;
    g_current->is_idle = true;
    strcpy(g_current->name, "boot-idle");
    mailbox_init(&g_current->mailbox);
    g_round_robin = 0;
    g_switches = 0;
    g_need_reschedule = false;
    kprintf("[sched] round-robin ready, %u threads, %u KiB stacks\n", CONFIG_MAX_THREADS,
            CONFIG_KERNEL_STACK_SIZE / 1024);
}

void sched_request(void)
{
    g_need_reschedule = true;
}

thread_t *sched_thread_get(int tid)
{
    if (tid <= 0) {
        return NULL;
    }
    for (uint32_t slot = 0; slot < CONFIG_MAX_THREADS; slot++) {
        if (g_threads[slot].state != THREAD_FREE && g_threads[slot].tid == tid) {
            return &g_threads[slot];
        }
    }
    return NULL;
}

thread_t *sched_current(void)
{
    return g_current;
}

int sched_create_user_thread(process_t *process, uint64_t entry, uint64_t user_stack,
                             uint64_t argument, const char *name)
{
    if (g_next_tid == INT32_MAX)
        return -AURORA_ENOMEM;
    for (uint32_t slot = 1; slot < CONFIG_MAX_THREADS; slot++) {
        thread_t *thread = &g_threads[slot];
        if (thread->state != THREAD_FREE) {
            continue;
        }

        uint64_t top =
            ((uint64_t)(uintptr_t)g_kernel_stacks[slot] + CONFIG_KERNEL_STACK_SIZE) & ~0xFull;
        uint64_t frame_address = (top - sizeof(struct interrupt_frame)) & ~0xFull;
        struct interrupt_frame *frame = (struct interrupt_frame *)(uintptr_t)frame_address;
        memset(frame, 0, sizeof(*frame));

        frame->gs = 0;
        frame->fs = 0;
        frame->es = GDT_USER_DATA;
        frame->ds = GDT_USER_DATA;
        frame->rdi = argument;
        frame->rip = entry;
        frame->cs = GDT_USER_CODE;
        frame->rflags = 0x202;
        frame->rsp = user_stack;
        frame->ss = GDT_USER_DATA;

        memset(thread, 0, sizeof(*thread));
        mailbox_init(&thread->mailbox);
        thread->tid = g_next_tid++;
        thread->state = THREAD_READY;
        thread->is_user = true;
        thread->kernel_stack = g_kernel_stacks[slot];
        thread->kernel_stack_top = top;
        thread->saved_frame = frame;
        thread->process = process;
        if (name != NULL) {
            strncpy(thread->name, name, sizeof(thread->name) - 1);
        }
        if (process != NULL) {
            process->main_tid = thread->tid;
        }

        kprintf("[sched] thread %d '%s' ready, entry=%p sp=%p\n", thread->tid, thread->name,
                (void *)(uintptr_t)entry, (void *)(uintptr_t)user_stack);
        return thread->tid;
    }
    kprintf("[sched] no free thread slots\n");
    return -1;
}

static void reap_dead(void)
{
    for (unsigned i = 1; i < CONFIG_MAX_THREADS; i++) {
        thread_t *t = &g_threads[i];
        if (t != g_current && t->state == THREAD_DEAD) {
            ipc_unsubscribe(t->tid);
            process_release(t->process);
            memset(t, 0, sizeof(*t));
        }
    }
}

struct interrupt_frame *sched_on_interrupt(struct interrupt_frame *frame)
{
    reap_dead();
    thread_t *current = g_current;
    if (!g_need_reschedule && current->state == THREAD_RUNNING)
        return frame;
    g_need_reschedule = false;
    current->saved_frame = frame;
    if (current->state == THREAD_RUNNING)
        current->state = THREAD_READY;
    thread_t *next = pick_ready();
    if (!next)
        next = &g_threads[IDLE_SLOT];
    next->state = THREAD_RUNNING;
    if (next == current)
        return frame;
    g_current = next;
    g_round_robin = (uint32_t)(next - g_threads);
    g_switches++;
    if (next->is_user) {
        tss_set_rsp0(next->kernel_stack_top);
        vmm_switch(&next->process->space);
    } else
        vmm_switch_kernel();
    return next->saved_frame;
}

void sched_wait(uint64_t timeout)
{
    if (g_current->mailbox.count)
        return;
    if (timeout == 0 || timeout > 6000)
        timeout = 6000;
    g_current->wake_tick = pit_ticks() + timeout;
    g_current->state = THREAD_WAITING;
    sched_request();
}

void sched_tick(uint64_t ticks)
{
    if (g_current && !g_current->is_idle)
        g_current->cpu_ticks++;
    for (unsigned i = 1; i < CONFIG_MAX_THREADS; i++) {
        thread_t *t = &g_threads[i];
        if (t->state == THREAD_WAITING && ticks >= t->wake_tick)
            t->state = THREAD_READY;
    }
    sched_request();
}

int sched_kill(int tid)
{
    thread_t *t = sched_thread_get(tid);
    if (!t || t->state == THREAD_DEAD)
        return -AURORA_ENOENT;
    if (process_has_cap(t, CAP_SERVICE))
        return -AURORA_EPERM;
    t->state = THREAD_DEAD;
    ipc_unsubscribe(tid);
    sched_request();
    return 0;
}

void sched_terminate_current(void)
{
    if (g_current == NULL || g_current->is_idle) {
        kpanic("attempted to terminate the idle thread");
    }
    ipc_unsubscribe(g_current->tid);
    g_current->state = THREAD_DEAD;
    sched_request();
}

uint64_t sched_switch_count(void)
{
    return g_switches;
}

uint32_t sched_snapshot(thread_info_t *threads, uint32_t capacity)
{
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < CONFIG_MAX_THREADS; slot++) {
        thread_t *thread = &g_threads[slot];
        if (thread->state == THREAD_FREE || thread->is_idle) {
            continue;
        }
        if (thread->state == THREAD_DEAD)
            continue;
        process_t *caller = g_current->process;
        if (caller && caller->credentials.uid != 0 && thread->process &&
            thread->process->credentials.session &&
            caller->credentials.uid != thread->process->credentials.uid)
            continue;
        if (count == capacity) {
            break;
        }
        threads[count].tid = (uint32_t)thread->tid;
        threads[count].state = (uint32_t)thread->state;
        threads[count].flags = thread->is_user ? THREAD_FLAG_USER : 0;
        threads[count].uid = thread->process ? thread->process->credentials.uid : 0;
        threads[count].cpu_ticks = thread->cpu_ticks;
        threads[count].memory_bytes = thread->process ? thread->process->memory_bytes : 0;
        threads[count].capabilities = thread->process ? thread->process->capabilities : 0;
        memset(threads[count].name, 0, sizeof(threads[count].name));
        strncpy(threads[count].name, thread->name, sizeof(threads[count].name) - 1);
        count++;
    }
    return count;
}
