#include <kernel/ipc.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
thread_t *sched_thread_get(int tid){(void)tid;return NULL;}
bool process_has_cap(const thread_t *thread,uint64_t cap){(void)thread;(void)cap;return false;}
void kprintf(const char *format,...){(void)format;}
int main(void)
{
    mailbox_t box;mailbox_init(&box);
    ipc_message_t m={.type=MSG_TABLET,.arg0=10,.arg2=0,.from=0},out;
    assert(mailbox_push(&box,&m));m.arg0=20;assert(mailbox_push(&box,&m));assert(box.count==1);
    m.arg2=1;assert(mailbox_push(&box,&m));m.arg0=30;assert(mailbox_push(&box,&m));assert(box.count==2);
    m.arg2=0;assert(mailbox_push(&box,&m));assert(box.count==3);
    assert(mailbox_pop(&box,&out)&&out.arg0==20&&out.arg2==0);
    assert(mailbox_pop(&box,&out)&&out.arg0==30&&out.arg2==1);
    assert(mailbox_pop(&box,&out)&&out.arg2==0);
    assert(box.messages[0].type==0&&box.messages[1].arg0==0);
    m.type=MSG_INPUT_MOUSE_ABS;m.from=3;m.arg0=0;assert(mailbox_push(&box,&m));
    ipc_message_t key={.type=MSG_INPUT_KEY,.from=3,.arg0=28};assert(mailbox_push(&box,&key));
    m.arg1=40;assert(mailbox_push(&box,&m));assert(box.count==3); /* cannot reorder over key */
    m.from=4;assert(mailbox_push(&box,&m));assert(box.count==4);
    puts("mailbox: adjacent motion coalesces; click edges, source and key ordering preserved");
}
