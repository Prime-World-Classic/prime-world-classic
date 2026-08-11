#pragma once
#include_next <sys/socket.h>
#include_next <netinet/in.h>
#include_next <arpa/inet.h>
#include_next <fcntl.h>
#include_next <sys/poll.h>
#include_next <unistd.h>

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

static inline void closesocket(int handle) { close(handle); }
