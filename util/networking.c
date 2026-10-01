#include "networking.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>

int createTCPIPv4Socket(void) {
    int socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) {
        perror("socket");
        return -1;
    }

    return socketFD;
}

int createIPv4Address(const char *ip, uint16_t port, struct sockaddr_in *address) {
    if (address == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    if (ip == NULL || ip[0] == '\0') {
        address->sin_addr.s_addr = INADDR_ANY;
        return 0;
    }

    if (inet_pton(AF_INET, ip, &address->sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}
