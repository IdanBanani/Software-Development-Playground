#include "socket_api.h"
// *I DONT THINK WE NEED THIS ONE*

int createListenerSocket()
{
    // tcp_socket
    // AF_INET because we need to give the other players an ip (v4)
    return socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
}

int setSocketOption(int sock_fd, int option, int optval)
{
    return setsockopt(sock_fd, SOL_SOCKET, option, &optval, sizeof(optval));
}

int bindServerSocket(int sock_fd, uint16_t port)
{
    // A  TCP local socket address that has been bound is unavailable for some time after closing, unless the SO_REUSEADDR flag has been set.  Care should be taken when using this flag as it makes TCP less reliable.

    // sin_addr is the IP host address.  The s_addr member of struct in_addr contains the host interface address in network byte order.  in_addr should be
    // assigned one of the INADDR_* values (e.g., INADDR_LOOPBACK) using htonl(3) or set using the inet_aton(3),  inet_addr(3),  inet_makeaddr(3)  library
    // functions or directly with the name resolver (see gethostbyname(3)).
    struct in_addr sin_addr;
    struct sockaddr_in listenSockAddr;
    memset(&listenSockAddr, 0, sizeof(listenSockAddr));
    listenSockAddr.sin_family = AF_INET;
    // sin_addr.s_addr = htonl(inet_addr(ip));
    sin_addr.s_addr = INADDR_ANY;
    // listenSockAddr.sin_addr = sin_addr;
    listenSockAddr.sin_addr = sin_addr; ///* Address to accept any incoming messages.  */???
    listenSockAddr.sin_port = htons(port);
    //  int inet_pton(int af, const char *src, void *dst);
    //  listenSockAddr.sin_addr = htonl(inet_pton("127.0.0.1"))
    if (-1 == bind(sock_fd, (const struct sockaddr*)&listenSockAddr, sizeof(listenSockAddr)))
    {
        handle_error("bind");
    }
    //TODO: need to either print the wlan ip address or manually check it
    // getsockopt()
    // getaddrinfo()
    //getifaddrs(3),
    printf("successfully bind to port %d\n",port);
    return 0;
}

int transformIntoAListener(int sock_fd)
{
    if (listen(sock_fd, LISTEN_BACKLOG) == -1)
    {
        handle_error("listen()");
    }
    return 0;
}


int acceptConnections(int sock_fd)
{
    struct sockaddr_in player_addr;
    socklen_t player_addr_size = sizeof(player_addr);
    int player_sockfd = -1;

    while (1)
    {
        player_sockfd = accept(sock_fd, (struct sockaddr*)&player_addr, &player_addr_size);
        if (-1 == player_sockfd) {
            handle_error("accept()");
        }
    }
}