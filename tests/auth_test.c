#include <aurora/auth.h>
#include <aurora/permissions.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void expect(const uint8_t *digest, const char *hex)
{
    char actual[65];
    for (unsigned i = 0; i < 32; i++)
        snprintf(actual + i * 2, 3, "%02x", digest[i]);
    assert(!strcmp(actual, hex));
}
int main(int argc, char **argv)
{
    uint8_t digest[32], salt[16] = {0};
    aurora_sha256("abc", 3, digest);
    expect(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    aurora_sha256("", 0, digest);
    expect(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    aurora_pbkdf2("password", salt, 2, digest);
    expect(digest, "9e41dea5935137cf748669cbf242b3ea049d20e0add3db231dabc9840bc58a6a");
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    auth_database_t db;
    assert(fread(&db, 1, sizeof(db), file) == sizeof(db));
    fclose(file);
    assert(aurora_auth_database_valid(&db, sizeof(db)));
    login_request_t request = {"root", "root-test"};
    uint32_t uid = 99;
    assert(aurora_auth_verify(&db, &request, &uid) && uid == 0);
    strcpy(request.password, "wrong");
    assert(!aurora_auth_verify(&db, &request, &uid));
    strcpy(request.username, "nobody");
    strcpy(request.password, "root-test");
    assert(!aurora_auth_verify(&db, &request, &uid));
    db.records[1].account.uid = 0;
    assert(!aurora_auth_database_valid(&db, sizeof(db)));
    credentials_t root = {0}, user = {1000, 1000, 1, 0}, guest = {1001, 1001, 2, 0};
    aurora_fs_node_t n = {.uid = 1000, .gid = 1000, .mode = 0600},
                     tmp = {.uid = 0, .gid = 0, .mode = 01777};
    assert(aurora_dac_allowed(&user, &n, 6));
    assert(!aurora_dac_allowed(&guest, &n, 4));
    assert(aurora_dac_allowed(&root, &n, 6));
    assert(aurora_unlink_allowed(&user, &tmp, &n));
    assert(!aurora_unlink_allowed(&guest, &tmp, &n));
    assert(aurora_unlink_allowed(&root, &tmp, &n));
    n.mode = 0640;
    guest.gid = 1000;
    assert(aurora_dac_allowed(&guest, &n, 4));
    assert(!aurora_dac_allowed(&guest, &n, 2));
    puts("authentication: SHA/PBKDF2, account verification and DAC passed");
}
