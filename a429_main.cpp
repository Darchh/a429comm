#include <iostream>
#include <chrono>
#include <thread>
#include "a429_protocol.h"
#include "a429_communicator.h"
#include "a429_udp.h"

void hardwareIOCallback(const A429Message& msg, A429UdpDriver* udpDriver) {
    if (udpDriver) {
        udpDriver->send(msg);
        std::cout << "[Main] Sent Message - Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
    }
}

void appReceiveCallback(const A429Message& msg) {
    std::cout << "[Main] Received Message - Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
}

void reportingCallback() {
    std::cout << "[Main] Reporting Status to OMD..." << std::endl;
}

int main() {
    // UDP Driver Setup (Local Port: 8081, Target: 127.0.0.1:8080)
    A429UdpDriver udpDriver(8081, "127.0.0.1", 8080);

    A429Communicator comm(
        [&](const A429Message& msg) { hardwareIOCallback(msg, &udpDriver); }, // Send Callback
        appReceiveCallback,                                                   // Receive Callback (NEW)
        reportingCallback,                                                    // Report Callback
        &udpDriver                                                            // Driver Pointer
    );

    std::cout << "A429 Communicator Started" << std::endl;

    while (true) {
        // Pass time externally: simulator program can call at any frequency
        auto now = std::chrono::steady_clock::now();
        comm.update(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}