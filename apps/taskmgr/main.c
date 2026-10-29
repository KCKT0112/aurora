#include <aurora/model.h>
#include <string.h>
#define MAX_THREADS 12
static thread_info_t previous[MAX_THREADS], current[MAX_THREADS];
static unsigned previous_count;
static uint64_t last_tick;
static char status[32] = "Live processes";
static int armed_tid;
static uint64_t armed_until;

static void publish(void)
{
    int64_t count = sys_thread_list(current, MAX_THREADS);
    if (count < 0)
        return;
    table_row_t rows[MAX_THREADS] = {0};
    uint64_t now = (uint64_t)sys_get_ticks();
    uint64_t elapsed = now - last_tick;
    for (int i = 0; i < count; i++) {
        text_copy(rows[i].label, sizeof(rows[i].label), current[i].name);
        uint64_t old = current[i].cpu_ticks;
        for (unsigned j = 0; j < previous_count; j++)
            if (previous[j].tid == current[i].tid)
                old = previous[j].cpu_ticks;
        uint64_t cpu = elapsed ? (current[i].cpu_ticks - old) * 100 / elapsed : 0;
        if (cpu > 100)
            cpu = 100;
        char *v = rows[i].value;
        text_u64(v, 40, cpu);
        text_append(v, 40, "%  ");
        text_u64(v, 40, current[i].memory_bytes / 1024);
        text_append(v, 40, "K  ");
        text_append(v,40,aurora_account(current[i].uid).name);
    }
    model_publish(VIEW_TASK, "System Monitor", status, rows, (uint32_t)count);
    memcpy(previous, current, sizeof(previous));
    previous_count = (unsigned)count;
    last_tick = now;
}
int umain(uint64_t arg)
{
    (void)arg;
    user_log("taskmgr: online");
    unsigned selected = 0;
    for (;;) {
        publish();
        uint64_t next = (uint64_t)sys_get_ticks() + 100;
        do {
            ipc_message_t msg;
            while (sys_recv(&msg) == 0) {
                if (msg.from != (uint64_t)sys_service(IMAGE_DISPLAY) || msg.arg0 != VIEW_TASK)
                    continue;
                if (msg.type == MSG_VIEW_SELECT)
                    selected = (unsigned)msg.arg1;
                if (msg.type == MSG_VIEW_KEY && msg.arg1 == 0x153 && selected < previous_count) {
                    thread_info_t *t = &previous[selected];
                    if (t->capabilities & CAP_SERVICE)
                        text_copy(status, sizeof(status), "System service is protected");
                    else if (armed_tid == (int)t->tid && (uint64_t)sys_get_ticks() < armed_until) {
                        int64_t result = sys_kill((int)t->tid);
                        text_copy(status, sizeof(status),
                                  result == 0 ? "App stopped; init will restart" : "Stop failed");
                        armed_tid = 0;
                    } else {
                        armed_tid = (int)t->tid;
                        armed_until = (uint64_t)sys_get_ticks() + 300;
                        text_copy(status, sizeof(status), "Delete again to restart app");
                    }
                }
            }
            uint64_t now = (uint64_t)sys_get_ticks();
            if (now >= next)
                break;
            sys_wait(next - now);
        } while (1);
    }
}
