#include <iostream>
#include <chrono>
#include <thread>
#include "a429_protocol.h"
#include "a429_communicator.h"
#include "a429_udp.h"

void hardwareIOCallback(const A429Message& msg) {
    // Only log here, actual sending is done by A429Communicator
    std::cout << "[Main] Sent Message - Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
}

void appReceiveCallback(const A429Message& msg) {
    std::cout << "[Main] Received Message - Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
    
    // Example: Pass data to Simulator variables
    // Simulator::instance().setArincData(msg.label, msg.data);
}

void reportingCallback() {
    std::cout << "[Main] Reporting Status to OMD..." << std::endl;
}

int main() {
    // UDP Driver Setup (Local Port: 8081, Target: 127.0.0.1:8080)
    A429UdpDriver udpDriver(8081, "127.0.0.1", 8080);

    A429Communicator comm(
        hardwareIOCallback,                                                   // Send Callback (Logging only)
        appReceiveCallback,                                                   // Receive Callback (NEW)
        reportingCallback,                                                    // Report Callback
        &udpDriver                                                            // Driver Pointer
    );

    std::cout << "A429 Communicator Started" << std::endl;

    // Simulation frequency (e.g., 100Hz -> 10ms period)
    const int CYCLE_MS = 10; 
    const auto period = std::chrono::milliseconds(CYCLE_MS);
    
    auto nextCycle = std::chrono::steady_clock::now();

    while (true) {
        // Use the time provided (or calculated) by the simulator
        comm.update(nextCycle);

        // Advance time by the period (Fixed Time Step)
        nextCycle += period;
        std::this_thread::sleep_until(nextCycle);
    }

    return 0;
}