#include "message.h"
#include "bytebuf.h"

#include <string.h>

void message_init(message_t* m, uint8_t type) {
    memset(m, 0, sizeof(*m));
    m->type = type;
}

/* nonce[24] | u16 len | box[len] */
static void put_box(bytebuf_t* b, const message_t* m) {
    if (m->box_len > DM_BOX_MAX) {
        b->error = 1;
        return;
    }
    bb_put_bytes(b, m->nonce, DM_NONCE_BYTES);
    bb_put_u16(b, m->box_len);
    bb_put_bytes(b, m->box, m->box_len);
}

static void get_box(bytebuf_t* b, message_t* m) {
    bb_get_bytes(b, m->nonce, DM_NONCE_BYTES);
    if (bb_get_u16(b, &m->box_len) < 0) return;
    if (m->box_len > DM_BOX_MAX) {
        b->error = 1;
        return;
    }
    bb_get_bytes(b, m->box, m->box_len);
}

static void encode_payload(const message_t* m, bytebuf_t* b) {
    switch (m->type) {
        case FT_NAME:
        case FT_JOIN:
        case FT_LEAVE:
            bb_put_str(b, m->name);
            break;
        case FT_MSG:
        case FT_WELCOME:
            bb_put_str(b, m->text);
            break;
        case FT_CHAT:
            bb_put_u64(b, m->timestamp);
            bb_put_str(b, m->name);
            bb_put_str(b, m->text);
            break;
        case FT_USERS:
            if (m->user_count > USERS_MAX) {
                b->error = 1;
                break;
            }
            bb_put_u16(b, m->user_count);
            for (uint16_t i = 0; i < m->user_count; i++)
                bb_put_str(b, m->users[i]);
            break;
        case FT_ERR:
            bb_put_u16(b, m->code);
            bb_put_str(b, m->text);
            break;
        case FT_TYPING:
            bb_put_u8(b, m->active ? 1 : 0);
            break;
        case FT_KEY:
            bb_put_bytes(b, m->key, DM_KEY_BYTES);
            break;
        case FT_PEER_KEY:
            bb_put_str(b, m->name);
            bb_put_bytes(b, m->key, DM_KEY_BYTES);
            break;
        case FT_DM:
            bb_put_str(b, m->to);
            put_box(b, m);
            break;
        case FT_DM_RECV:
            bb_put_u64(b, m->timestamp);
            bb_put_str(b, m->name);
            bb_put_str(b, m->to);
            put_box(b, m);
            break;
        case FT_USER_TYPING:
            bb_put_str(b, m->name);
            bb_put_u8(b, m->active ? 1 : 0);
            break;
        case FT_USERS_REQ:
        case FT_QUIT:
        case FT_PING:
        case FT_PONG:
            break;
        default:
            b->error = 1;
            break;
    }
}

int message_encode(const message_t* m, uint32_t seq, uint8_t* out, size_t cap) {
    if (cap < FRAME_HEADER_SIZE) return -1;

    /* Payload first, so we know its length for the header. */
    bytebuf_t b;
    bb_init_writer(&b, out + FRAME_HEADER_SIZE, cap - FRAME_HEADER_SIZE);
    encode_payload(m, &b);
    if (b.error || b.pos > FRAME_MAX_PAYLOAD) return -1;

    frame_header_t h = {
        .version = FRAME_VERSION,
        .type = m->type,
        .flags = m->flags,
        .seq = seq,
        .len = (uint32_t)b.pos,
    };
    if (frame_header_encode(&h, out, FRAME_HEADER_SIZE) < 0) return -1;
    return (int)(FRAME_HEADER_SIZE + b.pos);
}

static void decode_payload(message_t* m, bytebuf_t* b) {
    switch (m->type) {
        case FT_NAME:
        case FT_JOIN:
        case FT_LEAVE:
            bb_get_str(b, m->name, sizeof(m->name));
            break;
        case FT_MSG:
        case FT_WELCOME:
            bb_get_str(b, m->text, sizeof(m->text));
            break;
        case FT_CHAT:
            bb_get_u64(b, &m->timestamp);
            bb_get_str(b, m->name, sizeof(m->name));
            bb_get_str(b, m->text, sizeof(m->text));
            break;
        case FT_USERS:
            bb_get_u16(b, &m->user_count);
            if (m->user_count > USERS_MAX) {
                b->error = 1;
                break;
            }
            for (uint16_t i = 0; i < m->user_count && !b->error; i++)
                bb_get_str(b, m->users[i], sizeof(m->users[i]));
            break;
        case FT_ERR:
            bb_get_u16(b, &m->code);
            bb_get_str(b, m->text, sizeof(m->text));
            break;
        case FT_TYPING:
            bb_get_u8(b, &m->active);
            break;
        case FT_KEY:
            bb_get_bytes(b, m->key, DM_KEY_BYTES);
            break;
        case FT_PEER_KEY:
            bb_get_str(b, m->name, sizeof(m->name));
            bb_get_bytes(b, m->key, DM_KEY_BYTES);
            break;
        case FT_DM:
            bb_get_str(b, m->to, sizeof(m->to));
            get_box(b, m);
            break;
        case FT_DM_RECV:
            bb_get_u64(b, &m->timestamp);
            bb_get_str(b, m->name, sizeof(m->name));
            bb_get_str(b, m->to, sizeof(m->to));
            get_box(b, m);
            break;
        case FT_USER_TYPING:
            bb_get_str(b, m->name, sizeof(m->name));
            bb_get_u8(b, &m->active);
            break;
        default:
            break; /* empty payloads */
    }
}

int message_decode(const uint8_t* frame, size_t len, message_t* m, uint32_t* seq) {
    frame_header_t h;
    if (frame_header_decode(frame, len, &h) < 0) return -1;
    if (len - FRAME_HEADER_SIZE != h.len) return -1;

    message_init(m, h.type);
    m->flags = h.flags;
    bytebuf_t b;
    bb_init_reader(&b, frame + FRAME_HEADER_SIZE, h.len);
    decode_payload(m, &b);

    if (b.error) return -1;
    if (bb_remaining(&b) != 0) return -1; /* trailing garbage */
    if (seq) *seq = h.seq;
    return 0;
}
