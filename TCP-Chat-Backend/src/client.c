#include "client.h"
#include "framer.h"
#include "message.h"
#include "network.h"
#include "utility.h"
#include "validate.h"

#include <poll.h>
#include <stdarg.h>
#include <time.h>

static int sockfd = -1;
static framer_t in;
static client_state_t state = CLIENT_NAMING;
static uint32_t tx_seq = 0;

/* stdin is read with read(), not fgets(): stdio buffering does not mix
 * with poll(). Complete lines are cut out of this buffer. */
static char line_buf[CLIENT_LINE_MAX];
static size_t line_len = 0;
static int line_overflow = 0;
static int stdin_eof = 0;

static char my_name[NAME_MAX_LEN + 1];
static int use_color = 0;

/* ANSI colors, only used when stdout is a terminal. */
#define C_RESET  (use_color ? "\033[0m"  : "")
#define C_DIM    (use_color ? "\033[2m"  : "")
#define C_SELF   (use_color ? "\033[1;32m" : "")
#define C_OTHER  (use_color ? "\033[1;36m" : "")
#define C_INFO   (use_color ? "\033[33m" : "")
#define C_ERROR  (use_color ? "\033[1;31m" : "")

/* ---------------------------------------------------------------- */
/* Output                                                            */
/* ---------------------------------------------------------------- */

static void prompt(void) {
    printToConsole(state == CLIENT_NAMING ? "Name: " : PROMPT, 0);
}

/* Prints a line above the prompt. */
static void show(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void show(const char* fmt, ...) {
    char buf[TEXT_MAX_LEN + 256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    size_t n = strlen(buf);
    if (n + 1 < sizeof(buf)) {
        buf[n] = '\n';
        buf[n + 1] = '\0';
    }
    printToConsole(buf, 0);
    prompt();
}

/* Never trust the server with raw terminal bytes. */
static void sanitize(char* s) {
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7F) *s = '?';
}

/* ---------------------------------------------------------------- */
/* Network                                                           */
/* ---------------------------------------------------------------- */

static int send_all(const uint8_t* p, size_t n) {
    while (n > 0) {
        ssize_t w = send(sockfd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int send_message(const message_t* m) {
    static uint8_t buf[RECORD_HEADER_SIZE + FRAME_MAX_SIZE];
    int n = message_encode(m, tx_seq++, buf + RECORD_HEADER_SIZE,
                           sizeof(buf) - RECORD_HEADER_SIZE);
    if (n < 0) return -1;
    record_put_header(buf, (uint32_t)n);
    return send_all(buf, RECORD_HEADER_SIZE + (size_t)n);
}

static void handle_server_message(message_t* m) {
    sanitize(m->name);
    sanitize(m->text);
    for (uint16_t i = 0; i < m->user_count; i++) sanitize(m->users[i]);

    switch (m->type) {
        case FT_WELCOME:
            state = CLIENT_CHATTING;
            show("%s* %s%s", C_INFO, m->text, C_RESET);
            break;
        case FT_CHAT: {
            char stamp[8];
            time_t t = (time_t)(m->timestamp / 1000);
            struct tm tm;
            localtime_r(&t, &tm);
            strftime(stamp, sizeof(stamp), "%H:%M", &tm);
            int self = strcmp(m->name, my_name) == 0;
            show("%s[%s]%s %s%s%s: %s", C_DIM, stamp, C_RESET,
                 self ? C_SELF : C_OTHER, m->name, C_RESET, m->text);
            break;
        }
        case FT_JOIN:
            show("%s* %s joined the chat%s", C_INFO, m->name, C_RESET);
            break;
        case FT_LEAVE:
            show("%s* %s left the chat%s", C_INFO, m->name, C_RESET);
            break;
        case FT_USERS: {
            char list[USERS_MAX * (NAME_MAX_LEN + 2)] = "";
            for (uint16_t i = 0; i < m->user_count; i++) {
                if (i) strcat(list, ", ");
                strcat(list, m->users[i]);
            }
            show("%s* online (%u): %s%s", C_INFO, m->user_count, list, C_RESET);
            break;
        }
        case FT_ERR:
            if (state == CLIENT_REGISTERING) state = CLIENT_NAMING;
            show("%s! %s%s", C_ERROR, m->text, C_RESET);
            break;
        default:
            break;
    }
}

/* Returns -1 when the connection is gone. */
static int handle_socket(void) {
    size_t avail;
    uint8_t* space = framer_space(&in, &avail);
    ssize_t n = recv(sockfd, space, avail, 0);
    if (n == 0) {
        if (state == CLIENT_QUITTING)
            printToConsole("* Bye!\n", 0);
        else
            show("! Server closed the connection.");
        return -1;
    }
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        show("! Read error: %s", strerror(errno));
        return -1;
    }
    framer_commit(&in, (size_t)n);

    const uint8_t* body;
    size_t len;
    int r;
    static message_t m;
    while ((r = framer_next(&in, &body, &len)) == 1) {
        if (message_decode(body, len, &m, NULL) < 0) {
            show("! Malformed frame from server.");
            return -1;
        }
        handle_server_message(&m);
    }
    if (r < 0) {
        show("! Oversized record from server.");
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* User input                                                        */
/* ---------------------------------------------------------------- */

static int handle_line(const char* line) {
    static message_t m;

    if (line[0] == '\0') {
        prompt();
        return 0;
    }

    if (state == CLIENT_NAMING) {
        if (strlen(line) > NAME_MAX_LEN) {
            show("! Name is too long (max %d bytes).", NAME_MAX_LEN);
            return 0;
        }
        message_init(&m, FT_NAME);
        strcpy(m.name, line);
        strcpy(my_name, line);
        state = CLIENT_REGISTERING;
        return send_message(&m);
    }

    if (line[0] == '/') {
        if (strcmp(line, "/quit") == 0 || strcmp(line, "/exit") == 0) {
            message_init(&m, FT_QUIT);
            state = CLIENT_QUITTING;
            return send_message(&m); /* server closes, recv() then sees EOF */
        }
        if (strcmp(line, "/users") == 0) {
            message_init(&m, FT_USERS_REQ);
            return send_message(&m);
        }
        if (strcmp(line, "/help") == 0) {
            show("* Commands: /users  list online users\n"
                 "*           /quit   leave the chat\n"
                 "*           /help   show this help");
            return 0;
        }
        show("! Unknown command '%s'. Type /help.", line);
        return 0;
    }

    if (strlen(line) > TEXT_MAX_LEN) {
        show("! Message is too long (max %d bytes).", TEXT_MAX_LEN);
        return 0;
    }
    message_init(&m, FT_MSG);
    strcpy(m.text, line);
    if (send_message(&m) < 0) return -1;
    prompt();
    return 0;
}

/* Handles buffered complete lines. While a NAME is in flight, lines wait
 * in the buffer: otherwise text typed (or pasted) quickly would be sent
 * as another NAME before the server's answer arrives. */
static int process_lines(void) {
    char* nl;
    while (state != CLIENT_REGISTERING && state != CLIENT_QUITTING &&
           (nl = memchr(line_buf, '\n', line_len)) != NULL) {
        *nl = '\0';
        if (nl > line_buf && nl[-1] == '\r') nl[-1] = '\0';

        if (line_overflow)
            line_overflow = 0; /* tail of an over-long line: drop it */
        else if (handle_line(line_buf) < 0)
            return -1;

        size_t used = (size_t)(nl - line_buf) + 1;
        memmove(line_buf, nl + 1, line_len - used);
        line_len -= used;
    }

    return 0;
}

/* Returns -1 to quit. */
static int handle_stdin(void) {
    ssize_t n = read(STDIN_FILENO, line_buf + line_len, sizeof(line_buf) - line_len);
    if (n <= 0) {
        stdin_eof = 1; /* Ctrl+D or closed pipe; finish pending lines first */
        return 0;
    }
    line_len += (size_t)n;
    if (process_lines() < 0) return -1;

    if (line_len == sizeof(line_buf) && state != CLIENT_REGISTERING) {
        show("! Line too long, ignored.");
        line_overflow = 1;
        line_len = 0;
    }
    return 0;
}

/* ---------------------------------------------------------------- */

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : CLIENT_DEFAULT_HOST;
    int port = argc > 2 ? atoi(argv[2]) : CLIENT_DEFAULT_PORT;

    use_color = isatty(STDOUT_FILENO);

    sockfd = connectToServer(host, port);
    if (sockfd < 0) return 1;
    if (framer_init(&in, RECORD_MAX_BODY) < 0) return 1;

    prompt();

    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sockfd, .events = POLLIN },
    };

    for (;;) {
        /* Negative fd: poll() ignores it. */
        fds[0].fd = stdin_eof ? -1 : STDIN_FILENO;
        fds[0].events = state == CLIENT_REGISTERING ? 0 : POLLIN;

        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (handle_socket() < 0) break;
            if (process_lines() < 0) break; /* state may have changed */
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            if (handle_stdin() < 0) break;
        }
        if (stdin_eof && state != CLIENT_REGISTERING && state != CLIENT_QUITTING &&
            !memchr(line_buf, '\n', line_len))
            break;
    }

    printf("\n");
    framer_free(&in);
    close(sockfd);
    return 0;
}
