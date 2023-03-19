#ifndef __SOCKET_API_H
#define __SOCKET_API_h

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <sys/un.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>

#include "../common/common.h"

#define SOCKET_ERROR -1

// TODO: how to choose/pick this value?
#define LISTEN_BACKLOG 5

int createListenerSocket();
int setSocketOption(int sock_fd, int option , int option_val);
int bindServerSocket(int sock_fd, uint16_t port);
int transformIntoAListener(int sock_fd);

#endif // __SOCKET_API

