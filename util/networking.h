#ifndef NETWORKING_H
#define NETWORKING_H

#include <netinet/in.h>
#include <stdint.h>

int createTCPIPv4Socket(void);
int createIPv4Address(const char *ip, uint16_t port, struct sockaddr_in *address);

#endif
