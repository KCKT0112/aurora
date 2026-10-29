#ifndef AURORA_AUTH_H
#define AURORA_AUTH_H
#include <aurora/security.h>
#include <stdbool.h>
#include <stddef.h>
void aurora_sha256(const void *data, size_t size, uint8_t digest[32]);
void aurora_pbkdf2(const char *password, const uint8_t salt[16], uint32_t iterations,
                   uint8_t out[32]);
bool aurora_auth_database_valid(const void *data, size_t size);
bool aurora_auth_verify(const auth_database_t *db, const login_request_t *request, uint32_t *uid);
void aurora_erase(void *data, size_t size);
#endif
