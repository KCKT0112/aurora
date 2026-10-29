#ifndef AURORA_VFS_HANDLES_H
#define AURORA_VFS_HANDLES_H
#include <aurora/abi.h>
#include <stdbool.h>
bool vfs_handle_request(const ipc_message_t *message);
void vfs_handles_session_changed(void);
int vfs_open_node(const char *path,const credentials_t *credentials,uint32_t *node,uint64_t *version,uint32_t *size);
int vfs_read_node(uint32_t node,uint64_t version,const credentials_t *credentials,uint64_t offset,uint32_t requested,const void **data);
#endif
