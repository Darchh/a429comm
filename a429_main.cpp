#include <iostream>
#include <chrono>
#include <thread>
#include "a429_protocol.h"
#include "a429_communicator.h"
#include "a429_udp.h"

int main() {
    // UDP Driver Setup (Local Port: 8080, Target: 192.168.1.55:8080)
    A429UdpDriver udpDriver(8081, "127.0.0.1", 8080);

    // Initialize API with callbacks for hardware IO and Reporting
    A429Communicator comm(
        [&udpDriver](const A429Message& msg) {
            udpDriver.send(msg);
            std::cout << "Sending Message Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
        },
        []() {
            std::cout << "Reporting Status to OMD..." << std::endl;
        }
    );

    std::cout << "A429 Communicator Started" << std::endl;
    
    while (true) {
        // Check for incoming data
        A429Message rxMsg;
        std::string senderIp;
        
        if (udpDriver.receive(rxMsg, senderIp)) {
            comm.onPacketReceived(rxMsg, std::chrono::steady_clock::now());
            std::cout << "Received packet from " << senderIp << std::endl;
        }

        comm.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}