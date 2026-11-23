#include "message.h"
#include "bytebuf.h"

#include <string.h>

void message_init(message_t* m, uint8_t type) {
    memset(m, 0, sizeof(*m));
    m->type = type;
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
        .flags = 0,
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
        default:
            break; /* empty payloads */
    }
}

int message_decode(const uint8_t* frame, size_t len, message_t* m, uint32_t* seq) {
    frame_header_t h;
    if (frame_header_decode(frame, len, &h) < 0) return -1;
    if (len - FRAME_HEADER_SIZE != h.len) return -1;

    message_init(m, h.type);
    bytebuf_t b;
    bb_init_reader(&b, frame + FRAME_HEADER_SIZE, h.len);
    decode_payload(m, &b);

    if (b.error) return -1;
    if (bb_remaining(&b) != 0) return -1; /* trailing garbage */
    if (seq) *seq = h.seq;
    return 0;
}
