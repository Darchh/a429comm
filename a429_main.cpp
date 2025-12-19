#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "a429_protocol.h"

using namespace std::chrono;

#include "a429_communicator.h"

int main() {
    // UDP Socket Setup
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080); // Target Port
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Target IP

    // Initialize API with callbacks for hardware IO and Reporting
    A429Communicator comm(
        [sock, &servaddr](const A429Message& msg) {
            sendto(sock, &msg, sizeof(msg), 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
            std::cout << "Sending Message Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
        },
        []() {
            std::cout << "Reporting Status to OMD..." << std::endl;
        }
    );

    std::cout << "A429 Communicator Started" << std::endl;
    
    while (true) {
        comm.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(sock);
    return 0;
}