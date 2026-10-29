#ifndef AURORA_PERMISSIONS_H
#define AURORA_PERMISSIONS_H
#include <aurora/fs.h>
#include <aurora/security.h>
#include <stdbool.h>
#define ACCESS_EXECUTE 1u
#define ACCESS_WRITE 2u
#define ACCESS_READ 4u
bool aurora_dac_allowed(const credentials_t *credentials, const aurora_fs_node_t *node,
                        uint32_t access);
bool aurora_unlink_allowed(const credentials_t *credentials, const aurora_fs_node_t *parent,
                           const aurora_fs_node_t *node);
#endif
