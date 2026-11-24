#include "frame.h"
#include "bytebuf.h"

int frame_type_known(uint8_t type) {
    switch (type) {
        case FT_NAME: case FT_MSG: case FT_USERS_REQ: case FT_QUIT:
        case FT_PING: case FT_PONG: case FT_TYPING:
        case FT_USER_TYPING:
        case FT_WELCOME: case FT_CHAT: case FT_JOIN: case FT_LEAVE:
        case FT_USERS: case FT_ERR:
            return 1;
        default:
            return 0;
    }
}

const char* frame_type_name(uint8_t type) {
    switch (type) {
        case FT_NAME:      return "NAME";
        case FT_MSG:       return "MSG";
        case FT_USERS_REQ: return "USERS_REQ";
        case FT_QUIT:      return "QUIT";
        case FT_PING:      return "PING";
        case FT_PONG:      return "PONG";
        case FT_TYPING:    return "TYPING";
        case FT_USER_TYPING: return "USER_TYPING";
        case FT_WELCOME:   return "WELCOME";
        case FT_CHAT:      return "CHAT";
        case FT_JOIN:      return "JOIN";
        case FT_LEAVE:     return "LEAVE";
        case FT_USERS:     return "USERS";
        case FT_ERR:       return "ERR";
        default:           return "UNKNOWN";
    }
}

int frame_header_encode(const frame_header_t* h, uint8_t* out, size_t cap) {
    bytebuf_t b;
    bb_init_writer(&b, out, cap);
    bb_put_u8(&b, h->version);
    bb_put_u8(&b, h->type);
    bb_put_u8(&b, h->flags);
    bb_put_u8(&b, 0); /* reserved */
    bb_put_u32(&b, h->seq);
    bb_put_u32(&b, h->len);
    return b.error ? -1 : 0;
}

int frame_header_decode(const uint8_t* in, size_t len, frame_header_t* h) {
    bytebuf_t b;
    uint8_t reserved;

    bb_init_reader(&b, in, len);
    bb_get_u8(&b, &h->version);
    bb_get_u8(&b, &h->type);
    bb_get_u8(&b, &h->flags);
    bb_get_u8(&b, &reserved);
    bb_get_u32(&b, &h->seq);
    bb_get_u32(&b, &h->len);

    if (b.error) return -1;
    if (h->version != FRAME_VERSION) return -1;
    if (reserved != 0) return -1;
    if (!frame_type_known(h->type)) return -1;
    if (h->len > FRAME_MAX_PAYLOAD) return -1;
    return 0;
}
