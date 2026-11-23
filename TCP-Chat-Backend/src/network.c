#include "network.h"

#include <fcntl.h>

int connectToServer(const char* serverIP, int port){
    int new_sockfd;
    struct sockaddr_in server_addr;

    new_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(new_sockfd < 0){
        error("(-)| Error opening socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, serverIP, &server_addr.sin_addr) <= 0){
        error("(-)| Invalid address | Address not supported.\n");
        close(new_sockfd);
        return -1;
    }

    if(connect(new_sockfd, (SA*)&server_addr, sizeof(server_addr)) < 0){
        error("(-)| Error connecting to server");
        close(new_sockfd);
        return -1;
    }
    printf("(+)| Connected to server %s:%d\n",serverIP,port);

    return new_sockfd;
}

int start_listening(int iPort){
    int listen_sockfd;
    struct sockaddr_in server_addr;

    listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sockfd == -1){ error("(-)| Socket creating failed!");}
    printf("(+)| Socket created successfully!\n");

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(iPort);

    int optval = 1;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if(bind(listen_sockfd, (SA*)&server_addr, sizeof(server_addr)) != 0){
        close(listen_sockfd);
        error("(-)| Socket bind failed...");
    }
    printf("(+)| Socket successfully binded to port :%d...\n",iPort);

    if(listen(listen_sockfd, MAX_CLIENT) != 0){
        close(listen_sockfd);
        error("(-)| Listen failed...");
    }

    printf("(+)| Listening on port %d\n", iPort);
    return listen_sockfd;
}

int net_set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int net_listen(int port, int backlog){
    struct sockaddr_in addr;
    int optval = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return -1;

    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
        goto fail;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if(bind(fd, (SA*)&addr, sizeof(addr)) < 0) goto fail;
    if(listen(fd, backlog) < 0) goto fail;
    if(net_set_nonblocking(fd) < 0) goto fail;
    return fd;

fail:
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    return -1;
}

void net_format_addr(const struct sockaddr_in* addr, char* out, size_t cap){
    char ip[INET_ADDRSTRLEN];
    if(!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)))
        snprintf(ip, sizeof(ip), "?");
    snprintf(out, cap, "%s:%u", ip, (unsigned)ntohs(addr->sin_port));
}
