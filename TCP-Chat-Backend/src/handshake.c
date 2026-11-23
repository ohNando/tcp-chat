#include "handshake.h"
#include "bytebuf.h"

#include <string.h>

static void put_prefix(bytebuf_t* b) {
    bb_put_bytes(b, HS_MAGIC, HS_MAGIC_LEN);
    bb_put_u8(b, HS_VERSION);
}

static int get_prefix(bytebuf_t* b, uint8_t* version) {
    char magic[HS_MAGIC_LEN];
    if (bb_get_bytes(b, magic, HS_MAGIC_LEN) < 0) return -1;
    if (memcmp(magic, HS_MAGIC, HS_MAGIC_LEN) != 0) return -1;
    if (bb_get_u8(b, version) < 0) return -1;
    return *version == HS_VERSION ? 0 : -1;
}

int client_hello_encode(const client_hello_t* h, uint8_t* out, size_t cap) {
    bytebuf_t b;
    bb_init_writer(&b, out, cap);
    put_prefix(&b);
    bb_put_bytes(&b, h->eph_pk, sizeof(h->eph_pk));
    return b.error ? -1 : (int)b.pos;
}

int server_hello_encode(const server_hello_t* h, uint8_t* out, size_t cap) {
    bytebuf_t b;
    bb_init_writer(&b, out, cap);
    put_prefix(&b);
    bb_put_bytes(&b, h->eph_pk, sizeof(h->eph_pk));
    bb_put_bytes(&b, h->id_pk, sizeof(h->id_pk));
    bb_put_bytes(&b, h->sig, sizeof(h->sig));
    return b.error ? -1 : (int)b.pos;
}

int client_hello_decode(const uint8_t* in, size_t len, client_hello_t* h) {
    if (len != CLIENT_HELLO_LEN) return -1;
    bytebuf_t b;
    bb_init_reader(&b, in, len);
    if (get_prefix(&b, &h->version) < 0) return -1;
    return bb_get_bytes(&b, h->eph_pk, sizeof(h->eph_pk));
}

int server_hello_decode(const uint8_t* in, size_t len, server_hello_t* h) {
    if (len != SERVER_HELLO_LEN) return -1;
    bytebuf_t b;
    bb_init_reader(&b, in, len);
    if (get_prefix(&b, &h->version) < 0) return -1;
    bb_get_bytes(&b, h->eph_pk, sizeof(h->eph_pk));
    bb_get_bytes(&b, h->id_pk, sizeof(h->id_pk));
    bb_get_bytes(&b, h->sig, sizeof(h->sig));
    return b.error ? -1 : 0;
}

void handshake_transcript(uint8_t out[HS_TRANSCRIPT_LEN],
                          const unsigned char client_eph_pk[HS_KX_PK_BYTES],
                          const unsigned char server_eph_pk[HS_KX_PK_BYTES],
                          const unsigned char server_id_pk[HS_ID_PK_BYTES]) {
    uint8_t* p = out;
    memcpy(p, HS_CONTEXT, HS_CONTEXT_LEN);  p += HS_CONTEXT_LEN;
    memcpy(p, client_eph_pk, HS_KX_PK_BYTES); p += HS_KX_PK_BYTES;
    memcpy(p, server_eph_pk, HS_KX_PK_BYTES); p += HS_KX_PK_BYTES;
    memcpy(p, server_id_pk, HS_ID_PK_BYTES);
}
