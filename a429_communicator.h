#ifndef A429_COMMUNICATOR_H
#define A429_COMMUNICATOR_H

#include <iostream>
#include <chrono>
#include <functional>
#include <deque>
#include "a429_export.h"
#include "a429_protocol.h"

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
    using ReportCallback = std::function<void()>;

private:
    CommState currentState;
    std::chrono::steady_clock::time_point lastOmdReport;
    
    uint32_t txCounter = 0;
    uint32_t rxCounter = 0;
    std::deque<A429Message> rxBuffer;
    
    // Her kanal için ayrı state bilgisi
    ChannelInfo txChannels[MAX_TX_CHANNELS];
    ChannelInfo rxChannels[MAX_RX_CHANNELS];

    SendCallback sendCallback;
    ReportCallback reportCallback;

public:
    A429Communicator(SendCallback sendCb, ReportCallback reportCb);

    void resetConfigurationStatus();

    void update(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // Simulates receiving a UDP packet
    void onPacketReceived(const A429Message& msg, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // Getters for testing purposes
    CommState getCurrentState() const;
    const ChannelInfo& getTxChannelInfo(int index) const { return txChannels[index]; }
    const ChannelInfo& getRxChannelInfo(int index) const { return rxChannels[index]; }

private:
    void checkConfigurationComplete(std::chrono::steady_clock::time_point now);

    void sendConfiguration();
    void sendChannelConfiguration(int channelIndex, bool isTx);

    void sendToHardware(A429Message& msg);

    void processPacket(const A429Message& msg, std::chrono::steady_clock::time_point now);
    
    void updateChannelStates(std::chrono::steady_clock::time_point now);
};

#endif