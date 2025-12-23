#include "a429_udp.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

A429UdpDriver::A429UdpDriver(int localPort, const std::string& targetIp, int targetPort) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
    }

    struct sockaddr_in myaddr;
    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(localPort);

    if (bind(sock, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
    }

    // Set socket to non-blocking mode
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    memset(&targetAddr, 0, sizeof(targetAddr));
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(targetPort);
    targetAddr.sin_addr.s_addr = inet_addr(targetIp.c_str());
}

A429UdpDriver::~A429UdpDriver() {
    if (sock >= 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
    }
}

bool A429UdpDriver::send(const A429Message& msg) {
    if (sock < 0) return false;
    int sent = sendto(sock, (const char*)&msg, sizeof(msg), 0, (const struct sockaddr *)&targetAddr, sizeof(targetAddr));
    return sent == sizeof(msg);
}

bool A429UdpDriver::receive(A429Message& msg, std::string& senderIp) {
    if (sock < 0) return false;
    struct sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);
    int len = recvfrom(sock, (char*)&msg, sizeof(msg), 0, (struct sockaddr*)&senderAddr, &senderLen);
    
    if (len == sizeof(A429Message)) {
        senderIp = inet_ntoa(senderAddr.sin_addr);
        return true;
    }
    return false;
}