#ifndef A429_UDP_H
#define A429_UDP_H

#include <string>
#include <netinet/in.h>
#include "a429_export.h"
#include "a429_protocol.h"

class A429_API A429UdpDriver {
public:
    A429UdpDriver(int localPort, const std::string& targetIp, int targetPort);
    ~A429UdpDriver();

    bool send(const A429Message& msg);
    bool receive(A429Message& msg, std::string& senderIp);

private:
    int sock;
    struct sockaddr_in targetAddr;
};

#endif