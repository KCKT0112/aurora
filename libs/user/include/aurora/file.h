#ifndef AURORA_FILE_H
#define AURORA_FILE_H
#include <aurora/abi.h>
#include <stddef.h>
typedef void (*file_event_fn)(const ipc_message_t *message,void *context);
typedef struct { int64_t endpoint;uint64_t sequence;file_event_fn event;void *context; } file_client_t;
void file_client_init(file_client_t *client,file_event_fn event,void *context);
int64_t file_open(file_client_t *client,const char *path,uint32_t *size);
int64_t file_pread(file_client_t *client,uint64_t fd,void *buffer,uint32_t count,uint64_t offset);
int64_t file_close(file_client_t *client,uint64_t fd);
int64_t file_revoke(file_client_t *client,uint64_t fd);
int64_t file_claim(file_client_t *client,uint64_t fd,int64_t source,uint32_t *size);
int64_t file_delegate(file_client_t *client,uint64_t fd,int64_t endpoint);
#endif
