#include <aurora/permissions.h>
bool aurora_dac_allowed(const credentials_t *c, const aurora_fs_node_t *n, uint32_t access)
{
    if (!c || !n || c->uid == AURORA_NO_USER || (access & ~7u))
        return false;
    if (c->uid == 0)
        return true; /* DAC override only; never bypass immutable mounts/capabilities. */
    unsigned shift = c->uid == n->uid ? 6 : c->gid == n->gid ? 3 : 0;
    return ((n->mode >> shift) & access) == access;
}
bool aurora_unlink_allowed(const credentials_t *c, const aurora_fs_node_t *parent,
                           const aurora_fs_node_t *node)
{
    if (!aurora_dac_allowed(c, parent, ACCESS_WRITE | ACCESS_EXECUTE))
        return false;
    return !(parent->mode & 01000) || c->uid == 0 || c->uid == parent->uid || c->uid == node->uid;
}
