# ----- Toolchain -----
CC      = gcc
CFLAGS  = -g -Wall -Wextra -Werror -I./include -MMD -MP
LDLIBS  = -lpthread

# ----- Directories -----
SRC_DIR = src
OBJ_DIR = lib
BIN_DIR = bin

# ----- Binaries -----
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

# ----- Objects -----
COMMON_OBJ = $(OBJ_DIR)/utility.o $(OBJ_DIR)/network.o
SERVER_OBJ = $(OBJ_DIR)/server.o $(OBJ_DIR)/gui_bridge.o $(OBJ_DIR)/queue.o $(COMMON_OBJ)
CLIENT_OBJ = $(OBJ_DIR)/client.o $(COMMON_OBJ)
DEPS       = $(wildcard $(OBJ_DIR)/*.d)

# ----- Defaults for run targets -----
PORT      = 8080
SERVER_IP = 127.0.0.1

.PHONY: all build run-server run-client clean

all: build

build: $(SERVER_BIN) $(CLIENT_BIN)

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_BIN): $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS) -lwebsockets

$(CLIENT_BIN): $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LDLIBS)

run-server: $(SERVER_BIN)
	$(SERVER_BIN) $(PORT)

run-client: $(CLIENT_BIN)
	$(CLIENT_BIN) $(SERVER_IP) $(PORT)

clean:
	rm -rf $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(SERVER_BIN) $(CLIENT_BIN)

-include $(DEPS)
