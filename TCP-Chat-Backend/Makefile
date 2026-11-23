# ----- Toolchain -----
CC      = gcc
SODIUM_CFLAGS := $(shell pkg-config --cflags libsodium)
SODIUM_LIBS   := $(shell pkg-config --libs libsodium)

CFLAGS  = -g -Wall -Wextra -Werror -I./include -MMD -MP $(SODIUM_CFLAGS)
LDLIBS  = $(SODIUM_LIBS)

# ----- Directories -----
SRC_DIR = src
OBJ_DIR = lib
BIN_DIR = bin

# ----- Binaries -----
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

# ----- Objects -----
COMMON_OBJ = $(OBJ_DIR)/utility.o $(OBJ_DIR)/network.o
PROTO_OBJ  = $(OBJ_DIR)/bytebuf.o $(OBJ_DIR)/frame.o $(OBJ_DIR)/framer.o $(OBJ_DIR)/message.o $(OBJ_DIR)/validate.o
CORE_OBJ   = $(OBJ_DIR)/conn.o $(OBJ_DIR)/room.o
WEB_OBJ    = $(OBJ_DIR)/sha1.o
CRYPTO_OBJ = $(OBJ_DIR)/identity.o $(OBJ_DIR)/handshake.o $(OBJ_DIR)/secure.o $(OBJ_DIR)/known_servers.o
SERVER_OBJ = $(OBJ_DIR)/server.o $(COMMON_OBJ) $(PROTO_OBJ) $(CORE_OBJ) $(CRYPTO_OBJ) $(WEB_OBJ)
CLIENT_OBJ = $(OBJ_DIR)/client.o $(COMMON_OBJ) $(PROTO_OBJ) $(CRYPTO_OBJ)
DEPS       = $(wildcard $(OBJ_DIR)/*.d $(TEST_BIN_DIR)/*.d)

# ----- Tests -----
# Modules without a main(), linked into every test binary.
TEST_DIR     = tests
TEST_BIN_DIR = $(TEST_DIR)/bin
MODULE_OBJ   = $(COMMON_OBJ) $(PROTO_OBJ) $(CORE_OBJ) $(CRYPTO_OBJ) $(WEB_OBJ)
TEST_SRC     = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS    = $(patsubst $(TEST_DIR)/%.c,$(TEST_BIN_DIR)/%,$(TEST_SRC))

# ----- Defaults for run targets -----
PORT      = 8080
SERVER_IP = 127.0.0.1

.PHONY: all build test memcheck run-server run-client clean

all: build

build: $(SERVER_BIN) $(CLIENT_BIN)

$(OBJ_DIR) $(BIN_DIR) $(TEST_BIN_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_BIN): $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

$(CLIENT_BIN): $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

$(TEST_BIN_DIR)/%: $(TEST_DIR)/%.c $(MODULE_OBJ) | $(TEST_BIN_DIR)
	$(CC) $(CFLAGS) -I./$(TEST_DIR) -o $@ $< $(MODULE_OBJ) $(LDLIBS)

test: $(TEST_BINS)
	@status=0; for t in $(TEST_BINS); do echo "$$t"; ./$$t || status=1; done; exit $$status

# Runs every test under valgrind; fails on any memory error or leak.
memcheck: $(TEST_BINS)
	@status=0; for t in $(TEST_BINS); do echo "$$t"; \
		DEBUGINFOD_URLS= valgrind -q --leak-check=full --error-exitcode=9 ./$$t >/dev/null || status=1; \
	done; exit $$status

run-server: $(SERVER_BIN)
	$(SERVER_BIN) -p $(PORT)

run-client: $(CLIENT_BIN)
	$(CLIENT_BIN) $(SERVER_IP) $(PORT)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN_DIR)

-include $(DEPS)
