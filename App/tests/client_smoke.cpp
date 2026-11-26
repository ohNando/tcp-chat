// Headless smoke test for ChatClient against a running server.
//   client_smoke <host> <port>
// Uses a temporary known_servers file (TCPCHAT_KNOWN_SERVERS) so it never
// touches the user's real one.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

#include <unistd.h>

#include "chat_client.h"

static bool pump_until(ChatClient& c, const std::function<bool()>& done, double secs = 3.0) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
    while (std::chrono::steady_clock::now() < end) {
        c.poll();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

#define REQUIRE(cond, msg)                                                  \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::fprintf(stderr, "FAIL: %s (last error: %s)\n", msg,        \
                         c.lastError().c_str());                            \
            return 1;                                                       \
        }                                                                   \
        std::printf("  PASS %s\n", msg);                                    \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s host port\n", argv[0]);
        return 2;
    }
    if (sodium_init() < 0) return 1;
    char tmpl[] = "/tmp/tcpchat_smoke_XXXXXX";
    if (!mkdtemp(tmpl)) return 1;
    std::string known = std::string(tmpl) + "/known_servers";
    setenv("TCPCHAT_KNOWN_SERVERS", known.c_str(), 1);
    // Never touch the user's real key or pinned peers.
    std::string peers = std::string(tmpl) + "/known_peers";
    std::string key_a = std::string(tmpl) + "/a.key", key_b = std::string(tmpl) + "/b.key";
    setenv("TCPCHAT_KNOWN_PEERS", peers.c_str(), 1);
    setenv("TCPCHAT_USER_KEY", key_a.c_str(), 1);

    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);

    ChatClient c;
    c.connect(host, port, "desktop");
    REQUIRE(pump_until(c, [&] { return c.trustPending(); }), "unknown server key asks for trust");
    REQUIRE(c.state() == ConnState::Disconnected, "nothing is sent before the key is trusted");

    c.acceptPendingKey();
    REQUIRE(pump_until(c, [&] { return c.state() == ConnState::Chatting; }), "joins after trusting");
    REQUIRE(pump_until(c, [&] { return !c.users().empty(); }), "receives the user list");

    c.sendText("merhaba from the desktop app");
    REQUIRE(pump_until(c, [&] {
        for (auto& l : c.lines())
            if (l.kind == ChatLine::Chat && l.own && l.text == "merhaba from the desktop app") return true;
        return false;
    }), "own message comes back from the server");

    ChatClient c2;
    c2.connect(host, port, "DESKTOP");
    REQUIRE(pump_until(c2, [&] { return c2.state() == ConnState::Disconnected && !c2.lastError().empty(); }),
            "second client with a taken name is refused");
    std::printf("    (%s)\n", c2.lastError().c_str());

    ChatClient partner(key_b);
    partner.connect(host, port, "partner");
    REQUIRE(pump_until(partner, [&] { return partner.state() == ConnState::Chatting; }), "a second user joins");
    REQUIRE(pump_until(c, [&] { c.poll(); partner.poll(); return c.canDm("partner") && partner.canDm("desktop"); }),
            "both learn each other's private-message key");
    REQUIRE(c.safetyWith("partner") == partner.safetyWith("desktop") && !c.safetyWith("partner").empty(),
            "both see the same safety number");

    c.sendDm("partner", "sadece sana, sifreli");
    auto has_dm = [](ChatClient& who, bool own) {
        for (auto& l : who.lines())
            if (l.kind == ChatLine::Dm && l.own == own && l.text == "sadece sana, sifreli") return true;
        return false;
    };
    REQUIRE(pump_until(partner, [&] { c.poll(); return has_dm(partner, false); }), "the recipient decrypts the DM");
    REQUIRE(pump_until(c, [&] { partner.poll(); return has_dm(c, true); }), "the sender decrypts its own echo");

    // History: someone joining later gets the last 50 messages and can load
    // the older ones page by page.
    for (int i = 1; i <= 60; i++) partner.sendText("bulk " + std::to_string(i));
    REQUIRE(pump_until(c, [&] {
        partner.poll();
        return !c.lines().empty() && c.lines().back().text == "bulk 60";
    }), "sixty more messages arrive");
    auto chats = [](ChatClient& who) {
        int n = 0;
        for (auto& l : who.lines()) n += l.kind == ChatLine::Chat;
        return n;
    };
    ChatClient reader;
    reader.connect(host, port, "reader");
    REQUIRE(pump_until(reader, [&] { return reader.state() == ConnState::Chatting && reader.hasOlder() == 1; }),
            "a newcomer is told that older messages exist");
    REQUIRE(chats(reader) == 50, "and receives the last 50");
    REQUIRE(reader.loadOlder(), "asks for the page before them");
    REQUIRE(pump_until(reader, [&] { return !reader.loadingOlder() && reader.prependVersion() == 1; }),
            "the older page arrives");
    REQUIRE(chats(reader) == 61 && reader.lines().front().text == "merhaba from the desktop app",
            "it goes above the others, oldest first");
    REQUIRE(reader.hasOlder() == 0, "and that was the beginning of the chat");
    reader.quit();
    partner.quit();

    c.quit();
    REQUIRE(c.state() == ConnState::Disconnected, "quit disconnects");

    // Simulate a reinstalled server by pinning a different key for it.
    FILE* f = std::fopen(known.c_str(), "w");
    std::fprintf(f, "%s:%d %064d\n", host.c_str(), port, 0);
    std::fclose(f);
    ChatClient c3;
    c3.connect(host, port, "desktop");
    REQUIRE(pump_until(c3, [&] { return c3.identityChanged(); }), "changed server key is detected");
    REQUIRE(pump_until(c3, [&] { return c3.state() == ConnState::Disconnected; }), "and the connection is refused");

    for (const std::string& f : { known, peers, key_a, key_b }) std::remove(f.c_str());
    rmdir(tmpl);
    std::printf("  all passed\n");
    return 0;
}
