#ifndef A429_COMMUNICATOR_H
#define A429_COMMUNICATOR_H

#include <iostream>
#include <chrono>
#include <functional>
#include <deque>
#include <vector>
#include "a429_export.h"
#include "a429_protocol.h"

class A429UdpDriver;

enum class ChannelState {
    IDLE,           // Channel not yet configured
    CONFIGURING,    // Configuration sent, waiting for response
    OPERATIONAL,    // Channel configured and operational
    ERROR_RECOVERY  // Connection lost, reconfiguration needed
};

enum class CommState {
    STARTUP,
    CONFIGURING,
    OPERATIONAL,
    ERROR_RECOVERY
};

struct ChannelInfo {
    ChannelState state;
    std::chrono::steady_clock::time_point lastConfigSent;
    std::chrono::steady_clock::time_point lastStatusReceived;
    bool configured;
    
    ChannelInfo() : state(ChannelState::IDLE), configured(false) {}
};

class A429_API A429Communicator {
public:
    using SendCallback = std::function<void(const A429Message&)>;
    using ReceiveCallback = std::function<void(const A429Message&)>;
    using ReportCallback = std::function<void()>;

private:
    CommState currentState;
    std::chrono::steady_clock::time_point lastOmdReport;

    uint32_t txCounter = 0;
    uint32_t rxCounter = 0;
    std::deque<A429Message> rxBuffer;

    // Separate state info for each channel
    ChannelInfo txChannels[MAX_TX_CHANNELS];
    ChannelInfo rxChannels[MAX_RX_CHANNELS];

    SendCallback sendCallback;
    ReceiveCallback receiveCallback;
    ReportCallback reportCallback;

    // Message Queues
    std::vector<A429Message> txQueue;
    std::vector<A429Message> rxQueue;

    // UDP driver as member
    A429UdpDriver* udpDriver = nullptr;

public:
    A429Communicator(SendCallback sendCb, ReceiveCallback receiveCb, ReportCallback reportCb, A429UdpDriver* udpDrv);

    void resetConfigurationStatus();

    // Update with external time provided by the caller/simulator
    void update(std::chrono::steady_clock::time_point now);

    // Send TX message via API
    // Send message from one or more TX channels
    void sendMessage(const A429Message& msg, const std::vector<int>& channelIndices = {});

    // Enable or disable TX channel
    void enableTxChannel(int channelIndex, bool enable);

    CommState getCurrentState() const;

private:
    void checkConfigurationComplete(std::chrono::steady_clock::time_point now);

    void sendConfiguration(std::chrono::steady_clock::time_point now);
    void sendChannelConfiguration(int channelIndex, bool isTx, std::chrono::steady_clock::time_point now);

    void sendToHardware(A429Message& msg);

    void processPacket(const A429Message& msg, std::chrono::steady_clock::time_point now);
    
    void updateChannelStates(std::chrono::steady_clock::time_point now);

    void defaultReceiveCallback(const A429Message& msg);

    // Internal helpers to buffer messages
    void bufferTx(const A429Message& msg);
    void bufferRx(const A429Message& msg);
};

#endif