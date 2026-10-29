/* SHA-256 / HMAC / PBKDF2, bounded to the local account-verifier format.
 * Tested against hashlib/RFC vectors; not a general cryptographic API. */
#include <aurora/auth.h>
#include <string.h>
typedef struct {
    uint32_t h[8];
    uint64_t count;
    uint8_t block[64];
    size_t used;
} sha_t;
static const uint32_t constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
static uint32_t rotr(uint32_t v, unsigned n)
{
    return (v >> n) | (v << (32 - n));
}
static void transform(sha_t *s, const uint8_t *data)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++)
        w[i] = (uint32_t)data[i * 4] << 24 | (uint32_t)data[i * 4 + 1] << 16 |
               (uint32_t)data[i * 4 + 2] << 8 | data[i * 4 + 3];
    for (unsigned i = 16; i < 64; i++) {
        uint32_t a = w[i - 15], b = w[i - 2];
        w[i] = w[i - 16] + (rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
               (rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10));
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5],
             g = s->h[6], h = s->h[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                      constants[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}
static sha_t begin(void)
{
    return (sha_t){.h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
                         0x1f83d9ab, 0x5be0cd19}};
}
static void update(sha_t *s, const void *input, size_t size)
{
    const uint8_t *p = input;
    s->count += size;
    while (size) {
        size_t n = 64 - s->used;
        if (n > size)
            n = size;
        memcpy(s->block + s->used, p, n);
        s->used += n;
        p += n;
        size -= n;
        if (s->used == 64) {
            transform(s, s->block);
            s->used = 0;
        }
    }
}
static void finish(sha_t *s, uint8_t out[32])
{
    uint64_t bits = s->count * 8;
    uint8_t pad = 0x80;
    update(s, &pad, 1);
    pad = 0;
    while (s->used != 56)
        update(s, &pad, 1);
    uint8_t length[8];
    for (unsigned i = 0; i < 8; i++)
        length[7 - i] = (uint8_t)(bits >> (i * 8));
    update(s, length, 8);
    for (unsigned i = 0; i < 32; i++)
        out[i] = (uint8_t)(s->h[i / 4] >> (24 - (i % 4) * 8));
}
void aurora_erase(void *data, size_t size)
{
    volatile uint8_t *p = data;
    while (size--)
        *p++ = 0;
}
void aurora_sha256(const void *data, size_t size, uint8_t digest[32])
{
    sha_t s = begin();
    update(&s, data, size);
    finish(&s, digest);
    aurora_erase(&s, sizeof(s));
}
static void hmac(const sha_t *inner, const sha_t *outer, const void *data, size_t size,
                 uint8_t out[32])
{
    sha_t s = *inner;
    uint8_t digest[32];
    update(&s, data, size);
    finish(&s, digest);
    s = *outer;
    update(&s, digest, 32);
    finish(&s, out);
    aurora_erase(&s, sizeof(s));
    aurora_erase(digest, sizeof(digest));
}
void aurora_pbkdf2(const char *password, const uint8_t salt[16], uint32_t iterations,
                   uint8_t out[32])
{
    uint8_t key[64] = {0}, block[64], u[32], salt_block[20];
    size_t n = strlen(password);
    if (n > 64)
        aurora_sha256(password, n, key);
    else
        memcpy(key, password, n);
    for (unsigned i = 0; i < 64; i++)
        block[i] = key[i] ^ 0x36;
    sha_t inner = begin();
    update(&inner, block, 64);
    for (unsigned i = 0; i < 64; i++)
        block[i] = key[i] ^ 0x5c;
    sha_t outer = begin();
    update(&outer, block, 64);
    memcpy(salt_block, salt, 16);
    memset(salt_block + 16, 0, 4);
    salt_block[19] = 1;
    hmac(&inner, &outer, salt_block, 20, u);
    memcpy(out, u, 32);
    for (uint32_t round = 1; round < iterations; round++) {
        hmac(&inner, &outer, u, 32, u);
        for (unsigned i = 0; i < 32; i++)
            out[i] ^= u[i];
    }
    aurora_erase(key, sizeof(key));
    aurora_erase(block, sizeof(block));
    aurora_erase(u, sizeof(u));
    aurora_erase(&inner, sizeof(inner));
    aurora_erase(&outer, sizeof(outer));
}
bool aurora_auth_database_valid(const void *data, size_t size)
{
    if (size != sizeof(auth_database_t))
        return false;
    const auth_database_t *db = data;
    if (db->magic != AURORA_AUTH_MAGIC || db->version != AURORA_AUTH_VERSION ||
        db->count != AURORA_ACCOUNT_COUNT || db->iterations < AURORA_AUTH_ITERATIONS ||
        db->iterations > 1000000)
        return false;
    for (unsigned i = 0; i < db->count; i++) {
        const account_info_t *a = &db->records[i].account;
        account_info_t expected = aurora_account(a->uid);
        if (expected.uid == AURORA_NO_USER || a->gid != expected.gid || a->name[15] ||
            a->home[31] || strcmp(a->name, expected.name) || strcmp(a->home, expected.home))
            return false;
        for (unsigned j = 0; j < i; j++)
            if (db->records[j].account.uid == a->uid)
                return false;
    }
    return true;
}
bool aurora_auth_verify(const auth_database_t *db, const login_request_t *request, uint32_t *uid)
{
    if (request->username[15] || request->password[63])
        return false;
    const auth_record_t *record = &db->records[0];
    bool found = false;
    for (unsigned i = 0; i < db->count; i++)
        if (!strcmp(request->username, db->records[i].account.name)) {
            record = &db->records[i];
            found = true;
        }
    uint8_t digest[32];
    aurora_pbkdf2(request->password, record->salt, db->iterations, digest);
    uint8_t difference = 0;
    for (unsigned i = 0; i < 32; i++)
        difference |= digest[i] ^ record->verifier[i];
    aurora_erase(digest, sizeof(digest));
    if (difference || !found)
        return false;
    *uid = record->account.uid;
    return true;
}
